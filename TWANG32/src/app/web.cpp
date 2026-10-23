#include "web.h"
#include <ArduinoOTA.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include "../core/tw_json.h"
#include "../hal/audio_out.h"
#include "app.h"
#include "default_campaign.h"
#include "json_out.h"
#include "stats.h"
#include "web_ui.h"

using namespace twapp;

namespace web {

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static DNSServer dns;

struct Sub {
  uint32_t id;
  bool mirror, joy;
};
static const int MAX_SUBS = 8;
static Sub subs[MAX_SUBS];
static portMUX_TYPE subsMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t lastState, lastMirror, lastJoy, lastCleanup;

String apIp() { return WiFi.softAPIP().toString(); }

// ------------------------------------------------------------------ helpers
static void sendJson(AsyncWebServerRequest* r, int code, const String& body) {
  AsyncWebServerResponse* res = r->beginResponse(code, "application/json", body);
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}
static void sendOk(AsyncWebServerRequest* r) { sendJson(r, 200, "{\"ok\":true}"); }
static void sendErr(AsyncWebServerRequest* r, int code, const char* msg) {
  JsonOut j;
  j.obj().kv("ok", false).kv("message", msg).end();
  sendJson(r, code, j.s);
}

static const size_t MAX_BODY = 96 * 1024;
static void collectBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
  if (total > MAX_BODY) return;
  if (index == 0) {
    req->_tempObject = malloc(total + 1);
    if (!req->_tempObject) return;
  }
  if (!req->_tempObject) return;
  memcpy((uint8_t*)req->_tempObject + index, data, len);
  if (index + len == total) ((char*)req->_tempObject)[total] = 0;
}
static const char* body(AsyncWebServerRequest* r) { return r->_tempObject ? (const char*)r->_tempObject : "{}"; }
static size_t bodyLen(AsyncWebServerRequest* r) { return strlen(body(r)); }

// read a few common fields out of a small JSON body
struct Fields {
  int mode = 0, level = -1, id = -1, music = -999;
  float intensity = 0.6f;
  char action[16] = "";
  char name[16] = "";
  bool spectate = false;
};
static Fields fields(const char* json) {
  Fields f;
  tw::JsonReader r(json, strlen(json));
  char k[16];
  if (!r.beginObject()) return f;
  while (r.nextKey(k, sizeof k)) {
    if (!strcmp(k, "mode")) f.mode = r.asInt(0);
    else if (!strcmp(k, "level")) f.level = r.asInt(-1);
    else if (!strcmp(k, "id")) f.id = r.asInt(-1);
    else if (!strcmp(k, "music")) f.music = r.asInt(-1);
    else if (!strcmp(k, "intensity")) f.intensity = r.asFloat(0.6f);
    else if (!strcmp(k, "spectate")) f.spectate = r.asBool(false);
    else if (!strcmp(k, "action") && r.peek() == tw::JsonReader::T_STRING) r.readString(f.action, sizeof f.action);
    else if (!strcmp(k, "name") && r.peek() == tw::JsonReader::T_STRING) r.readString(f.name, sizeof f.name);
    else r.skip();
  }
  return f;
}

static void serveUi(AsyncWebServerRequest* r) {
  AsyncWebServerResponse* res = r->beginResponse(200, "text/html", WEB_UI_GZ, WEB_UI_GZ_LEN);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("Cache-Control", "no-cache");
  r->send(res);
}
// Relative, so it works on the TWANG network (where DNS points everything here) and on a home network
static void toPortal(AsyncWebServerRequest* r) { r->redirect("/"); }

static bool writeFile(const char* path, const char* data, size_t len) {
  String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  size_t w = f.write((const uint8_t*)data, len);
  f.close();
  if (w != len) { LittleFS.remove(tmp); return false; }
  LittleFS.remove(path);
  return LittleFS.rename(tmp, path);
}

// ------------------------------------------------------------------ websocket
static void wsEvent(AsyncWebSocket*, AsyncWebSocketClient* c, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    portENTER_CRITICAL(&subsMux);
    for (auto& s : subs) if (!s.id) { s.id = c->id(); s.mirror = false; s.joy = false; break; }
    portEXIT_CRITICAL(&subsMux);
  } else if (type == WS_EVT_DISCONNECT) {
    portENTER_CRITICAL(&subsMux);
    for (auto& s : subs) if (s.id == c->id()) s.id = 0;
    portEXIT_CRITICAL(&subsMux);
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT || len > 200) return;
    char buf[201];
    memcpy(buf, data, len);
    buf[len] = 0;
    tw::JsonReader r(buf, len);
    char k[8], t[12] = "";
    bool on = false;
    if (!r.beginObject()) return;
    while (r.nextKey(k, sizeof k)) {
      if (!strcmp(k, "t") && r.peek() == tw::JsonReader::T_STRING) r.readString(t, sizeof t);
      else if (!strcmp(k, "on")) on = r.asBool(false);
      else r.skip();
    }
    portENTER_CRITICAL(&subsMux);
    for (auto& s : subs) if (s.id == c->id()) {
      if (!strcmp(t, "mirror")) s.mirror = on;
      if (!strcmp(t, "joy")) s.joy = on;
    }
    portEXIT_CRITICAL(&subsMux);
  }
}

void event(const tw::Event& e) {
  using namespace tw;
  switch (e.type) {
    case EV_LEVEL_START: case EV_KILL: case EV_ARMOR_HIT: case EV_SPLIT: case EV_DIE: case EV_LEVEL_CLEAR:
    case EV_BOSS_HIT: case EV_BOSS_KILLED: case EV_PICKUP_GEM: case EV_PICKUP_LIFE: case EV_CHECKPOINT:
    case EV_GAME_OVER: case EV_NEW_HIGHSCORE: case EV_CAMPAIGN_DONE:
      break;
    default:
      return;
  }
  if (!ws.count()) return;
  JsonOut j(128);
  j.obj().kv("t", "e").kv("type", e.type).kv("kind", e.kind).kv("pos", e.pos).kv("value", (long)e.value)
      .kv("mode", (int)game.mode()).kv("lvl", game.levelIndex()).end();
  ws.textAll(j.s);
}

static void sendState() {
  JsonOut j(220);
  j.obj().kv("t", "s").kv("stage", (int)game.stage()).kv("mode", (int)game.mode()).kv("level", game.levelIndex())
      .kv("name", game.level().name).kv("lives", game.lives()).kv("score", (long)game.score()).kv("combo", game.combo())
      .kv("boss", game.bossHp()).kv("paused", game.paused()).kv("fps", (unsigned long)fps()).end();
  ws.textAll(j.s);
}

static void sendMirror() {
  static uint8_t frame[3 + 300 * 3];
  int N = settings.ledCount;
  int n = min(N, 300);
  frame[0] = 1;
  frame[1] = n & 0xFF;
  frame[2] = n >> 8;
  for (int i = 0; i < n; i++) {
    // when downsampling keep the brightest pixel of each group so single-LED enemies never vanish
    int a = (int)((long)i * N / n), b = max(a + 1, (int)((long)(i + 1) * N / n));
    int best = a, bestSum = -1;
    for (int k = a; k < b && k < N; k++) {
      int s = leds[k].r + leds[k].g + leds[k].b;
      if (s > bestSum) { bestSum = s; best = k; }
    }
    frame[3 + i * 3] = leds[best].r;
    frame[4 + i * 3] = leds[best].g;
    frame[5 + i * 3] = leds[best].b;
  }
  Sub local[MAX_SUBS];
  portENTER_CRITICAL(&subsMux);
  memcpy(local, subs, sizeof local);
  portEXIT_CRITICAL(&subsMux);
  for (auto& s : local) {
    if (!s.id || !s.mirror) continue;
    if (!ws.availableForWrite(s.id)) continue;  // slow phone: drop this frame rather than queue it
    ws.binary(s.id, frame, 3 + n * 3);
  }
}

static void sendJoy() {
  Sub local[MAX_SUBS];
  portENTER_CRITICAL(&subsMux);
  memcpy(local, subs, sizeof local);
  portEXIT_CRITICAL(&subsMux);
  String msg;
  for (auto& s : local) {
    if (!s.id || !s.joy) continue;
    if (!msg.length()) {
      JsonOut j(64);
      j.obj().kv("t", "j").kv("tilt", joystick.lastTilt()).kv("wobble", joystick.lastWobble()).end();
      msg = j.s;
    }
    if (ws.availableForWrite(s.id)) ws.text(s.id, msg);
  }
}

// ------------------------------------------------------------------ routes
static void routes() {
  server.on("/", HTTP_GET, serveUi);
  server.on("/index.html", HTTP_GET, serveUi);
  // phones probe these to detect a captive portal; answering with a redirect pops the UI open
  const char* probes[] = {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html", "/connecttest.txt",
                          "/ncsi.txt", "/fwlink", "/redirect", "/canonical.html", "/success.txt"};
  for (const char* p : probes) server.on(p, HTTP_GET, toPortal);

  server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonOut j(512);
    j.obj().kv("device", "twang").kv("version", TWANG_VERSION).kv("name", settings.apName).kv("ip", apIp().c_str());
    if (WiFi.status() == WL_CONNECTED) j.kv("staIp", WiFi.localIP().toString().c_str()).kv("staSsid", settings.staSsid);
    char joy[40];
    if (joystick.present()) snprintf(joy, sizeof joy, "MPU6050 (id 0x%02X)", joystick.whoAmI());
    else snprintf(joy, sizeof joy, "not found, check SDA %d / SCL %d", I2C_SDA_PIN, I2C_SCL_PIN);
    j.kv("joystick", joy).kv("heap", (unsigned long)ESP.getFreeHeap()).kv("minHeap", (unsigned long)ESP.getMinFreeHeap())
        .kv("uptime", (unsigned long)(millis() / 1000)).kv("fps", (unsigned long)fps()).kv("ledCount", settings.ledCount)
        .kv("ledType", ledType()).kv("maxLeds", MAX_LEDS).kv("audio", audio_out::description())
        .kv("audioUnderruns", (unsigned long)audio_out::underruns()).kv("levels", campaign->count).end();
    sendJson(r, 200, j.s);
  });

  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* r) {
    String js;
    xSemaphoreTake(settingsLock, portMAX_DELAY);
    js = settingsToJson(settings, false);
    xSemaphoreGive(settingsLock);
    sendJson(r, 200, js);
  });
  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest* r) {
    xSemaphoreTake(settingsLock, portMAX_DELAY);
    int n = settingsApplyJson(settings, body(r), bodyLen(r));
    xSemaphoreGive(settingsLock);
    if (n < 0) return sendErr(r, 400, "settings must be a JSON object");
    post({CMD_SETTINGS, 0, 0, 0});
    sendOk(r);
  }, nullptr, collectBody);

  server.on("/api/campaign", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (LittleFS.exists("/campaign.json")) {
      AsyncWebServerResponse* res = r->beginResponse(LittleFS, "/campaign.json", "application/json");
      res->addHeader("Cache-Control", "no-store");
      r->send(res);
    } else {
      r->send(r->beginResponse(200, "application/json", DEFAULT_CAMPAIGN, DEFAULT_CAMPAIGN_LEN - 1));
    }
  });
  server.on("/api/campaign", HTTP_PUT | HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!r->_tempObject) return sendErr(r, 413, "campaign too large or missing (96 KB max)");
    tw::Campaign* tmp = (tw::Campaign*)malloc(sizeof(tw::Campaign));
    if (!tmp) return sendErr(r, 503, "not enough memory, try again");
    tw::ParseResult pr = tw::parseCampaign(body(r), bodyLen(r), *tmp);
    free(tmp);
    if (!pr.ok) return sendErr(r, 400, pr.message);
    if (!writeFile("/campaign.json", body(r), bodyLen(r))) return sendErr(r, 500, "could not write to flash");
    post({CMD_CAMPAIGN, 0, 0, 0});
    JsonOut j;
    j.obj().kv("ok", true).kv("levels", pr.levels).kv("warnings", pr.warnings).end();
    sendJson(r, 200, j.s);
  }, nullptr, collectBody);
  server.on("/api/campaign/reset", HTTP_POST, [](AsyncWebServerRequest* r) {
    LittleFS.remove("/campaign.json");
    post({CMD_CAMPAIGN, 0, 0, 0});
    sendOk(r);
  });

  server.on("/api/play", HTTP_POST, [](AsyncWebServerRequest* r) {
    Fields f = fields(body(r));
    post({CMD_PLAY, constrain(f.mode, 0, 2), f.level, 0});
    sendOk(r);
  }, nullptr, collectBody);
  server.on("/api/control", HTTP_POST, [](AsyncWebServerRequest* r) {
    Fields f = fields(body(r));
    int a = !strcmp(f.action, "pause") ? 0 : !strcmp(f.action, "skip") ? 1 : !strcmp(f.action, "boot") ? 2 : !strcmp(f.action, "attract") ? 3 : -1;
    if (a < 0) return sendErr(r, 400, "action must be pause, skip, boot or attract");
    post({CMD_CONTROL, a, 0, 0});
    sendOk(r);
  }, nullptr, collectBody);
  server.on("/api/test", HTTP_POST, [](AsyncWebServerRequest* r) {
    const char* b = body(r);
    size_t n = bodyLen(r);
    tw::JsonReader rd(b, n);
    char k[12];
    size_t start = 0, end = 0;
    bool spectate = false;
    if (rd.beginObject()) {
      while (rd.nextKey(k, sizeof k)) {
        if (!strcmp(k, "level")) { rd.peek(); start = rd.pos(); rd.skip(); end = rd.pos(); }
        else if (!strcmp(k, "spectate")) spectate = rd.asBool(false);
        else rd.skip();
      }
    }
    if (!rd.ok() || end <= start) return sendErr(r, 400, "expected {\"level\": {...}}");
    if (pendingTestBusy) return sendErr(r, 503, "busy, try again");
    tw::ParseResult pr = tw::parseLevel(b + start, end - start, pendingTest);
    if (!pr.ok) return sendErr(r, 400, pr.message);
    pendingTestBusy = true;
    post({CMD_TEST, spectate ? 1 : 0, 0, 0});
    sendOk(r);
  }, nullptr, collectBody);
  server.on("/api/sound", HTTP_POST, [](AsyncWebServerRequest* r) {
    Fields f = fields(body(r));
    if (f.music != -999) post({CMD_MUSIC, f.music, 0, f.intensity});
    else post({CMD_SOUND, f.id, 0, 0});
    sendOk(r);
  }, nullptr, collectBody);
  server.on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest* r) {
    post({CMD_CALIBRATE, 0, 0, 0});
    sendOk(r);
  });

  server.on("/api/stats", HTTP_GET, [](AsyncWebServerRequest* r) { sendJson(r, 200, stats::toJson()); });
  server.on("/api/stats", HTTP_PUT, [](AsyncWebServerRequest* r) {
    if (!stats::fromJson(body(r), bodyLen(r))) return sendErr(r, 400, "not a stats file");
    sendOk(r);
  }, nullptr, collectBody);
  server.on("/api/stats/reset", HTTP_POST, [](AsyncWebServerRequest* r) { stats::reset(); sendOk(r); });
  server.on("/api/scores/name", HTTP_POST, [](AsyncWebServerRequest* r) {
    Fields f = fields(body(r));
    if (!stats::setPendingName(f.name)) return sendErr(r, 409, "no recent high score to name");
    sendOk(r);
  }, nullptr, collectBody);

  server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest* r) {
    xSemaphoreTake(settingsLock, portMAX_DELAY);
    int n = settingsApplyJson(settings, body(r), bodyLen(r));
    xSemaphoreGive(settingsLock);
    if (n < 0) return sendErr(r, 400, "expected a JSON object");
    post({CMD_SETTINGS, 1, 0, 0});
    post({CMD_RESTART, 1500, 0, 0});
    sendOk(r);
  }, nullptr, collectBody);
  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* r) { post({CMD_RESTART, 600, 0, 0}); sendOk(r); });
  server.on("/api/factory", HTTP_POST, [](AsyncWebServerRequest* r) { post({CMD_FACTORY, 0, 0, 0}); sendOk(r); });

  // firmware update from the browser (multipart upload of the .bin)
  server.on("/api/update", HTTP_POST,
    [](AsyncWebServerRequest* r) {
      bool ok = !Update.hasError();
      AsyncWebServerResponse* res = r->beginResponse(ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
      res->addHeader("Connection", "close");
      r->send(res);
      otaProgress = -1;
      if (ok) post({CMD_RESTART, 800, 0, 0});
    },
    [](AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
      if (index == 0) {
        size_t total = r->contentLength();
        otaProgress = 0;
        if (!Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
      }
      if (len && Update.write(data, len) != len) Update.printError(Serial);
      if (r->contentLength()) otaProgress = (int)((index + len) * 100 / r->contentLength());
      if (final && !Update.end(true)) Update.printError(Serial);
    });

  server.onNotFound([](AsyncWebServerRequest* r) {
    if (r->url().startsWith("/api/")) return sendErr(r, 404, "no such endpoint");
    toPortal(r);
  });
}

// ------------------------------------------------------------------ lifecycle
void begin() {
  WiFi.persistent(false);
  bool sta = settings.staSsid[0] != 0;
  WiFi.mode(sta ? WIFI_AP_STA : WIFI_AP);
  WiFi.setHostname(DEFAULT_HOSTNAME);
  const char* pass = strlen(settings.apPass) >= 8 ? settings.apPass : nullptr;
  WiFi.softAP(settings.apName, pass);
  if (sta) {
    WiFi.setAutoReconnect(true);
    WiFi.begin(settings.staSsid, settings.staPass);
  }
  WiFi.setSleep(false);  // snappier live view
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  if (MDNS.begin(DEFAULT_HOSTNAME)) MDNS.addService("http", "tcp", 80);
  ArduinoOTA.setHostname(DEFAULT_HOSTNAME);
  ArduinoOTA.onStart([]() { otaProgress = 0; });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) { otaProgress = t ? (int)(p * 100 / t) : 0; });
  ArduinoOTA.onError([](ota_error_t) { otaProgress = -1; });
  ArduinoOTA.begin();
  ws.onEvent(wsEvent);
  server.addHandler(&ws);
  routes();
  server.begin();
}

void loop(uint32_t now) {
  dns.processNextRequest();
  ArduinoOTA.handle();
  if (!ws.count()) return;
  if (now - lastState >= 200) { lastState = now; sendState(); }
  if (now - lastMirror >= 66) { lastMirror = now; sendMirror(); }
  if (now - lastJoy >= 66) { lastJoy = now; sendJoy(); }
  if (now - lastCleanup >= 1000) { lastCleanup = now; ws.cleanupClients(4); }
}

}  // namespace web
