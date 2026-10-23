// TWANG Next - persisted device settings (LittleFS /settings.json)
#pragma once
#include <Arduino.h>
#include "../../config.h"
#include "../hal/joystick.h"

struct Settings {
  // LEDs
  uint16_t ledCount = DEFAULT_LED_COUNT;
  uint8_t brightness = 150;
  uint16_t powerLimit = DEFAULT_POWER_LIMIT_MA;
  uint8_t conveyorBrightness = DEFAULT_CONVEYOR_BRIGHTNESS;
  uint8_t lavaOffBrightness = DEFAULT_LAVA_OFF_BRIGHTNESS;
  bool gravity = false;
  int16_t bendPoint = 550;
  // game
  uint8_t difficulty = 1;
  uint8_t livesPerLevel = 3;
  bool livesPerRun = false;
  uint16_t idleTimeout = 30;
  uint8_t defaultMode = 0;
  bool juice = true, telegraph = true, smooth = true, attract = true;
  uint8_t palette = 0;
  // sound
  uint8_t masterVol = 80, musicVol = 60, sfxVol = 90;
  bool music = true, moveSound = true, attractAudio = false;
  // joystick
  JoystickConfig joy;
  // wifi
  char apName[33] = DEFAULT_AP_NAME;
  char apPass[65] = DEFAULT_AP_PASS;
  char staSsid[33] = "";
  char staPass[65] = "";
};

// Apply a (possibly partial) JSON object to settings. Returns the number of keys applied.
int settingsApplyJson(Settings& s, const char* json, size_t len);
// Serialise. Passwords are only included when includeSecrets is true (for the file on flash).
String settingsToJson(const Settings& s, bool includeSecrets);
