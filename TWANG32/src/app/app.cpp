#include "app.h"
#include <LittleFS.h>
#include "../hal/audio_out.h"
#include "default_campaign.h"
#include "stats.h"
#include "web.h"

#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "TWANG Next needs the ESP32 Arduino core 3.x (Boards Manager: esp32 by Espressif, 3.0 or newer)"
#endif

namespace twapp {

tw::Game game;
tw::Campaign* campaign = nullptr;
Settings settings;
SemaphoreHandle_t settingsLock;
Joystick joystick;
CRGB leds[MAX_LEDS];
volatile int otaProgress = -1;
tw::LevelDef pendingTest;
volatile bool pendingTestBusy = false;

static tw::Synth* synthPtr = nullptr;  // on the heap (its echo buffer is large)
#define synth (*synthPtr)
static tw::SoundDirector sound;
static QueueHandle_t cmdQ;
static CLEDController* ledCtl = nullptr;
static uint16_t activeLeds = 0;
static uint32_t frameCount = 0, fpsValue = 0, fpsAt = 0;
static uint32_t nextTickUs = 0;
static uint32_t restartAt = 0;
static uint32_t settingsSaveAt = 0;

bool post(const Cmd& c) { return cmdQ && xQueueSend(cmdQ, &c, 0) == pdTRUE; }
uint32_t fps() { return fpsValue; }
const char* ledType() {
#ifdef USE_APA102
  return "APA102";
#else
  return "WS2812";
#endif
}

// ------------------------------------------------------------------ storage
static char* readFile(const char* path, size_t* len) {
  File f = LittleFS.open(path, "r");
  if (!f) return nullptr;
  size_t n = f.size();
  char* buf = (char*)malloc(n + 1);
  if (buf) {
    *len = f.readBytes(buf, n);
    buf[*len] = 0;
  }
  f.close();
  return buf;
}

static bool loadCampaign() {
  size_t n = 0;
  char* text = readFile("/campaign.json", &n);
  bool ok = false;
  if (text) {
    tw::Campaign* tmp = (tw::Campaign*)malloc(sizeof(tw::Campaign));
    if (tmp) {
      tw::ParseResult pr = tw::parseCampaign(text, n, *tmp);
      if (pr.ok) { memcpy(campaign, tmp, sizeof(tw::Campaign)); ok = true; }
      else Serial.printf("campaign.json rejected: %s\n", pr.message);
      free(tmp);
    }
    free(text);
  }
  if (!ok) {
    tw::ParseResult pr = tw::parseCampaign((const char*)DEFAULT_CAMPAIGN, DEFAULT_CAMPAIGN_LEN - 1, *campaign);
    Serial.printf("Using the built-in campaign (%d levels)\n", pr.levels);
  }
  return ok;
}

static void loadSettings() {
  size_t n = 0;
  char* text = readFile("/settings.json", &n);
  if (!text) return;
  settingsApplyJson(settings, text, n);
  free(text);
}

static void saveSettings() {
  xSemaphoreTake(settingsLock, portMAX_DELAY);
  String js = settingsToJson(settings, true);
  xSemaphoreGive(settingsLock);
  File f = LittleFS.open("/settings.json.tmp", "w");
  if (!f) return;
  f.print(js);
  f.close();
  LittleFS.remove("/settings.json");
  LittleFS.rename("/settings.json.tmp", "/settings.json");
}

// ------------------------------------------------------------------ settings -> engine and hardware
static void applySettings() {
  xSemaphoreTake(settingsLock, portMAX_DELAY);
  Settings s = settings;
  xSemaphoreGive(settingsLock);
  tw::GameOptions& o = game.opt;
  o.livesPerLevel = s.livesPerLevel;
  o.livesPerRun = s.livesPerRun;
  o.idleTimeoutSec = s.idleTimeout;
  o.juice = s.juice;
  o.smooth = s.smooth;
  o.palette = s.palette;
  o.difficulty = s.difficulty;
  o.gravity = s.gravity;
  o.bendPoint = s.bendPoint;
  o.conveyorBrightness = s.conveyorBrightness;
  o.lavaOffBrightness = s.lavaOffBrightness;
  o.attract = s.attract;
  o.telegraph = s.telegraph;
  game.setDefaultMode((tw::Mode)s.defaultMode);
  sound.moveSound = s.moveSound;
  sound.musicOn = s.music;
  sound.attractAudio = s.attractAudio;
  synth.volumes(s.masterVol / 100.0f, s.musicVol / 100.0f, s.sfxVol / 100.0f);
  FastLED.setBrightness(s.brightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, s.powerLimit ? s.powerLimit : 100000);
  uint16_t n = constrain(s.ledCount, 1, MAX_LEDS);
  if (ledCtl && n != activeLeds) {
    if (n < activeLeds) {  // blank the LEDs we are about to stop driving
      fill_solid(leds, activeLeds, CRGB::Black);
      FastLED.show();
    }
    ledCtl->setLeds(leds, n);
    activeLeds = n;
  }
}

// ------------------------------------------------------------------ commands from the web server
static void handle(const Cmd& c) {
  switch (c.type) {
    case CMD_PLAY: game.startRun((tw::Mode)c.a, c.b); break;
    case CMD_CONTROL:
      if (c.a == 0) game.setPaused(!game.paused());
      else if (c.a == 1) game.skipLevel();
      else if (c.a == 2) game.boot();
      else if (c.a == 3) game.goAttract();
      break;
    case CMD_TEST:
      if (pendingTestBusy) { game.testLevel(pendingTest, c.a != 0); pendingTestBusy = false; }
      break;
    case CMD_SOUND: sound.testSound(c.a); break;
    case CMD_MUSIC:
      if (c.a < 0) synth.musicStop();
      else { synth.music((uint8_t)c.a); synth.intensity(c.f); }
      break;
    case CMD_CAMPAIGN:
      loadCampaign();
      game.setCampaign(campaign);
      break;
    case CMD_SETTINGS:
      applySettings();
      settingsSaveAt = millis() + (c.a ? 10 : 1500);
      break;
    case CMD_CALIBRATE:
      xSemaphoreTake(settingsLock, portMAX_DELAY);
      joystick.calibrate(settings.joy);
      xSemaphoreGive(settingsLock);
      settingsSaveAt = millis() + 10;
      break;
    case CMD_RESTART: restartAt = millis() + (uint32_t)max<int32_t>(c.a, 50); break;
    case CMD_FACTORY:
      LittleFS.remove("/settings.json");
      LittleFS.remove("/campaign.json");
      LittleFS.remove("/stats.json");
      restartAt = millis() + 500;
      break;
    default: break;
  }
}

static void renderOta() {
  // firmware update in progress: a blue bar fills the strip
  int n = activeLeds;
  int lit = n * constrain((int)otaProgress, 0, 100) / 100;
  for (int i = 0; i < n; i++) leds[i] = i < lit ? CRGB(0, 0, 90) : CRGB(0, 0, 0);
  if (lit < n) leds[lit] = CRGB(120, 120, 255);
}

static void serialConsole() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '?') {
      Serial.printf("\nTWANG %s  Wi-Fi \"%s\"  http://%s  fps %u  heap %u\n", TWANG_VERSION, settings.apName, web::apIp().c_str(),
                    (unsigned)fpsValue, (unsigned)ESP.getFreeHeap());
      Serial.println(settingsToJson(settings, false));
    } else if (c == '!') {
      ESP.restart();
    }
  }
}

// ------------------------------------------------------------------ lifecycle
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.printf("\n\nTWANG Next %s\n", TWANG_VERSION);
  settingsLock = xSemaphoreCreateMutex();
  cmdQ = xQueueCreate(24, sizeof(Cmd));
  campaign = new tw::Campaign;
  synthPtr = new tw::Synth;
  if (!LittleFS.begin(true)) Serial.println("LittleFS failed to mount; settings will not be saved");
  loadSettings();
  loadCampaign();
  stats::begin();

  if (!joystick.begin(I2C_SDA_PIN, I2C_SCL_PIN)) Serial.println("No MPU6050 found: check the joystick wiring");

  activeLeds = constrain(settings.ledCount, 1, MAX_LEDS);
#ifdef USE_APA102
  ledCtl = &FastLED.addLeds<APA102, LED_DATA_PIN, LED_CLOCK_PIN, LED_COLOR_ORDER, DATA_RATE_MHZ(12)>(leds, activeLeds);
#else
  ledCtl = &FastLED.addLeds<WS2812B, LED_DATA_PIN, LED_COLOR_ORDER>(leds, activeLeds);
#endif
  FastLED.setDither(1);

  synth.init(AUDIO_SAMPLE_RATE);
  sound.attach(&synth);
  game.init(campaign, esp_random());
  game.highScore = stats::highScore();
  applySettings();
  if (!audio_out::begin(&synth, AUDIO_SAMPLE_RATE)) Serial.println("Audio output failed to start");

  web::begin();
  Serial.printf("Join Wi-Fi \"%s\" (password \"%s\") and open http://%s\n", settings.apName, settings.apPass, web::apIp().c_str());
  nextTickUs = micros();
  fpsAt = millis();
}

void loop() {
  Cmd c;
  for (int i = 0; i < 8 && xQueueReceive(cmdQ, &c, 0) == pdTRUE; i++) handle(c);

  uint32_t nowUs = micros();
  int steps = 0;
  while ((int32_t)(nowUs - nextTickUs) >= 0 && steps < 4) {
    tw::Input in = joystick.read(settings.joy);
    game.tick(in);
    tw::Event e;
    while (game.pollEvent(e)) {
      sound.onEvent(e);
      stats::onEvent(e, game, *campaign);
      web::event(e);
      if (e.type == tw::EV_GAME_OVER) game.highScore = stats::highScore();
    }
    sound.update(game.audio());
    nextTickUs += 16667;
    steps++;
  }
  if ((int32_t)(nowUs - nextTickUs) > 200000) nextTickUs = nowUs;  // fell far behind (flash write): resync

  if (steps) {
    if (otaProgress >= 0) renderOta();
    else game.render(reinterpret_cast<tw::RGB*>(leds), activeLeds);
    FastLED.show();
    frameCount++;
  }

  uint32_t now = millis();
  if (now - fpsAt >= 1000) {
    fpsValue = frameCount * 1000 / (now - fpsAt);
    frameCount = 0;
    fpsAt = now;
  }
  web::loop(now);
  stats::flushIfDirty(false);
  if (settingsSaveAt && (int32_t)(now - settingsSaveAt) >= 0) { settingsSaveAt = 0; saveSettings(); }
  if (restartAt && (int32_t)(now - restartAt) >= 0) {
    stats::flushIfDirty(true);
    ESP.restart();
  }
  serialConsole();
}

}  // namespace twapp

static_assert(sizeof(CRGB) == sizeof(tw::RGB), "CRGB and tw::RGB must have the same layout");
