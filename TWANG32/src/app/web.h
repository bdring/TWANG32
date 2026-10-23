// TWANG Next - Wi-Fi, captive portal, web UI, REST API, WebSocket live view, OTA
#pragma once
#include <Arduino.h>
#include "../core/tw_game.h"

namespace web {
void begin();
void loop(uint32_t nowMs);
void event(const tw::Event& e);
String apIp();
}  // namespace web
