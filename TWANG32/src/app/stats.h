// TWANG Next - play statistics, high scores and per-level death maps (LittleFS /stats.json)
#pragma once
#include <Arduino.h>
#include "../core/tw_game.h"

namespace stats {
void begin();                     // load from flash
void onEvent(const tw::Event& e, const tw::Game& g, const tw::Campaign& c);
bool setPendingName(const char* name);  // name the most recent high score
int32_t highScore();
String toJson();
bool fromJson(const char* json, size_t len);
void reset();
void flushIfDirty(bool force);    // write to flash (called from the main loop)
}  // namespace stats
