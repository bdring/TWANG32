#include "settings.h"
#include "json_out.h"
#include "../core/tw_json.h"

using tw::JsonReader;

int settingsApplyJson(Settings& s, const char* json, size_t len) {
  JsonReader r(json, len);
  int n = 0;
  char k[24];
  if (!r.beginObject()) return 0;
  auto num = [&](long lo, long hi, long cur) -> long { return constrain((long)r.asInt((int)cur), lo, hi); };
  auto str = [&](char* dst, size_t cap) {
    if (r.peek() == JsonReader::T_STRING) r.readString(dst, (int)cap);
    else r.skip();
  };
  while (r.nextKey(k, sizeof k)) {
    n++;
    if (!strcmp(k, "ledCount")) s.ledCount = num(1, MAX_LEDS, s.ledCount);
    else if (!strcmp(k, "brightness")) s.brightness = num(5, 255, s.brightness);
    else if (!strcmp(k, "powerLimit")) s.powerLimit = num(0, 20000, s.powerLimit);
    else if (!strcmp(k, "conveyorBrightness")) s.conveyorBrightness = num(1, 255, s.conveyorBrightness);
    else if (!strcmp(k, "lavaOffBrightness")) s.lavaOffBrightness = num(1, 255, s.lavaOffBrightness);
    else if (!strcmp(k, "gravity")) s.gravity = r.asBool(s.gravity);
    else if (!strcmp(k, "bendPoint")) s.bendPoint = num(0, 1000, s.bendPoint);
    else if (!strcmp(k, "difficulty")) s.difficulty = num(0, 2, s.difficulty);
    else if (!strcmp(k, "livesPerLevel")) s.livesPerLevel = num(1, 9, s.livesPerLevel);
    else if (!strcmp(k, "livesPerRun")) s.livesPerRun = r.asBool(s.livesPerRun);
    else if (!strcmp(k, "idleTimeout")) s.idleTimeout = num(5, 3600, s.idleTimeout);
    else if (!strcmp(k, "defaultMode")) s.defaultMode = num(0, 1, s.defaultMode);
    else if (!strcmp(k, "juice")) s.juice = r.asBool(s.juice);
    else if (!strcmp(k, "telegraph")) s.telegraph = r.asBool(s.telegraph);
    else if (!strcmp(k, "smooth")) s.smooth = r.asBool(s.smooth);
    else if (!strcmp(k, "attract")) s.attract = r.asBool(s.attract);
    else if (!strcmp(k, "palette")) s.palette = num(0, 1, s.palette);
    else if (!strcmp(k, "masterVol")) s.masterVol = num(0, 100, s.masterVol);
    else if (!strcmp(k, "musicVol")) s.musicVol = num(0, 100, s.musicVol);
    else if (!strcmp(k, "sfxVol")) s.sfxVol = num(0, 100, s.sfxVol);
    else if (!strcmp(k, "music")) s.music = r.asBool(s.music);
    else if (!strcmp(k, "moveSound")) s.moveSound = r.asBool(s.moveSound);
    else if (!strcmp(k, "attractAudio")) s.attractAudio = r.asBool(s.attractAudio);
    else if (!strcmp(k, "deadzone")) s.joy.deadzone = num(0, 40, s.joy.deadzone);
    else if (!strcmp(k, "attackThreshold")) s.joy.attackThreshold = num(2000, 32767, s.joy.attackThreshold);
    else if (!strcmp(k, "joyAxis")) s.joy.axis = num(0, 2, s.joy.axis);
    else if (!strcmp(k, "joyInvert")) s.joy.invert = r.asBool(s.joy.invert);
    else if (!strcmp(k, "joyOffset")) {
      if (r.beginArray()) {
        int i = 0;
        while (r.nextItem()) { int v = r.asInt(0); if (i < 3) s.joy.offset[i++] = (int16_t)constrain(v, -32768, 32767); }
      }
    }
    else if (!strcmp(k, "apName")) { char b[33] = ""; str(b, sizeof b); if (b[0]) strlcpy(s.apName, b, sizeof s.apName); }
    else if (!strcmp(k, "apPass")) { char b[65] = ""; str(b, sizeof b); if (b[0] && strlen(b) >= 8) strlcpy(s.apPass, b, sizeof s.apPass); }
    else if (!strcmp(k, "staSsid")) str(s.staSsid, sizeof s.staSsid);
    else if (!strcmp(k, "staPass")) { char b[65] = ""; str(b, sizeof b); if (b[0]) strlcpy(s.staPass, b, sizeof s.staPass); }
    else { r.skip(); n--; }
  }
  return r.ok() ? n : -1;
}

String settingsToJson(const Settings& s, bool secrets) {
  JsonOut j(900);
  j.obj();
  j.kv("ledCount", s.ledCount).kv("brightness", s.brightness).kv("powerLimit", s.powerLimit);
  j.kv("conveyorBrightness", s.conveyorBrightness).kv("lavaOffBrightness", s.lavaOffBrightness);
  j.kv("gravity", s.gravity).kv("bendPoint", s.bendPoint);
  j.kv("difficulty", s.difficulty).kv("livesPerLevel", s.livesPerLevel).kv("livesPerRun", s.livesPerRun);
  j.kv("idleTimeout", s.idleTimeout).kv("defaultMode", s.defaultMode);
  j.kv("juice", s.juice).kv("telegraph", s.telegraph).kv("smooth", s.smooth).kv("attract", s.attract).kv("palette", s.palette);
  j.kv("masterVol", s.masterVol).kv("musicVol", s.musicVol).kv("sfxVol", s.sfxVol);
  j.kv("music", s.music).kv("moveSound", s.moveSound).kv("attractAudio", s.attractAudio);
  j.kv("deadzone", s.joy.deadzone).kv("attackThreshold", s.joy.attackThreshold).kv("joyAxis", s.joy.axis).kv("joyInvert", s.joy.invert);
  j.key("joyOffset").arr().val(s.joy.offset[0]).val(s.joy.offset[1]).val(s.joy.offset[2]).endArr();
  j.kv("apName", s.apName).kv("staSsid", s.staSsid);
  if (secrets) j.kv("apPass", s.apPass).kv("staPass", s.staPass);
  else j.kv("apPassSet", s.apPass[0] != 0).kv("staPassSet", s.staPass[0] != 0);
  j.end();
  return j.s;
}
