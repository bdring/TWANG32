/*
  TWANG Next for TWANG32
  A one-dimensional dungeon crawler on an LED strip, played with a doorstop spring.

  Original TWANG by Critters (https://github.com/Critters/TWANG), inspired by
  Robin Baumgarten's Line Wobbler. ESP32 port (TWANG32) by Bart Dring.
  License: Creative Commons 4.0 Attribution - Share Alike.

  Board:     ESP32 Dev Module (or NodeMCU-32S), ESP32 Arduino core 3.x
  Partition: Minimal SPIFFS (1.9MB APP with OTA / 190KB SPIFFS)
  Libraries: FastLED, ESPAsyncWebServer + AsyncTCP (both by ESP32Async)

  Hardware choices live in config.h. Everything else is configured from the
  web page: join the "TWANG" Wi-Fi (password 12345678) and open http://192.168.4.1

  The game itself is in src/core and is plain portable C++: the same code runs
  in the browser simulator (WebAssembly) and in the native test suite.
*/

#include "src/app/app.h"

void setup() { twapp::setup(); }
void loop() { twapp::loop(); }
