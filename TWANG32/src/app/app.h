// TWANG Next - firmware application: wires the portable engine to the hardware and the web UI.
#pragma once
#include <Arduino.h>
#include <FastLED.h>
#include "../../config.h"
#include "../core/tw_game.h"
#include "../core/tw_synth.h"
#include "../hal/joystick.h"
#include "settings.h"

#define TWANG_VERSION "next-2026.10"

namespace twapp {

// Commands from the web server (another task) to the main loop, so the game is only touched in one place.
enum CmdType : uint8_t {
  CMD_PLAY = 1,       // a = mode, b = level
  CMD_CONTROL,        // a = 0 pause/resume, 1 skip, 2 restart, 3 screensaver
  CMD_TEST,           // pending test level is ready; a = spectate
  CMD_SOUND,          // a = sound id
  CMD_MUSIC,          // a = track (-1 stops), f = intensity
  CMD_CAMPAIGN,       // /campaign.json changed: reload it
  CMD_SETTINGS,       // settings changed: apply and save
  CMD_CALIBRATE,
  CMD_RESTART,        // a = delay ms
  CMD_FACTORY,
};

struct Cmd {
  uint8_t type;
  int32_t a, b;
  float f;
};

bool post(const Cmd& c);

extern tw::Game game;
extern tw::Campaign* campaign;  // on the heap: too big for the static DRAM segment
extern Settings settings;
extern SemaphoreHandle_t settingsLock;
extern Joystick joystick;
extern CRGB leds[MAX_LEDS];
extern volatile int otaProgress;  // -1 when no update is running
extern tw::LevelDef pendingTest;
extern volatile bool pendingTestBusy;

uint32_t fps();
const char* ledType();

void setup();
void loop();

}  // namespace twapp
