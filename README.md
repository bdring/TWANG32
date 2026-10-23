# TWANG Next

A one-dimensional dungeon crawler on an LED strip, played by flicking a doorstop spring. This is a ground-up rebuild of [TWANG32](https://github.com/bdring/TWANG32) (Bart Dring's ESP32 port of [Critters' TWANG](https://github.com/Critters/TWANG), itself inspired by Robin Baumgarten's *Line Wobbler*).

The original feel is preserved exactly: movement speed, the crossing-based collision, lava timing and the 20 stock levels. Everything around it is new.

## What changed

**Game**
- 33-level campaign: the original 20 in order, with 13 new levels that introduce each new mechanic, then a second act ending in a five-hit final boss.
- New enemies: armored (two twangs, knocked back and stunned by the first), chaser (homes in), splitter (bursts into fragments that come back at you), phantom (fades in and out; untouchable while faded).
- New terrain: ice (keeps momentum, you can twang mid-slide) and darkness (only what's near you is lit).
- Gems, extra lives, checkpoints, data-driven bosses with a telegraphed "rage" eruption.
- Endless mode: procedurally generated levels that unlock mechanics as you go deeper, with fairness rules (no overlapping lava, no spawning on the player, ice never runs into lava). A boss every seventh depth.
- Practice mode, difficulty levels, lives that refill per level (original) or carry over.
- Juice: hit-stop on kills, kill bursts, combo multiplier, sub-pixel player motion, lava flickers before it ignites, spawners glow before they spawn, score bar against the high score after every game.

**Sound**
- A 20-voice synthesizer (oscillators with envelopes, pitch sweeps, vibrato and resonant filters), plucked-string voices for the twang, tempo-synced echo, ducking and soft clipping.
- Adaptive music in four tracks written in dark modes. Layers (drone, heartbeat, bells, bass, drums, lead, a dissonant cluster) fade in as danger rises and the tempo leans forward under pressure.
- Event-driven effects that layer instead of overwriting each other. Kill sounds climb in pitch with your combo.

**Device**
- Web app served from the ESP32 (offline, fonts included): live mirror of the strip on your phone, start runs, pause, skip; full settings; high-score names; per-level stats with a death heatmap.
- Level editor: reorder by drag, add and drag entities on a track, inspector for every value, fairness warnings, undo/redo, presets, import/export. Changes preview live on the strip in your browser as you edit; "Test on strip" plays it on the real thing.
- Joystick calibration with a live meter, power limiting, captive portal, home Wi-Fi + `twang.local`, firmware updates from the browser, backups.

## Play it right now

`build/web/index.html` (or the published simulator) runs the exact same engine in your browser as WebAssembly. Arrow keys or A/D to move, Space to twang. On a phone: the on-screen spring, or tilt the phone and flick it.

## Flash it

Fastest path, no toolchain: open [esptool-js](https://espressif.github.io/esptool-js/) in Chrome, connect the ESP32 over USB, and write `firmware/twang-next-stock-merged.bin` at address `0x0`. That image is built for the stock TWANG32 board (APA102 on 16/17, MPU6050 on 21/22, DAC on 25).

Then join the Wi-Fi **TWANG** (password `12345678`); the page opens by itself, or go to http://192.168.4.1. Later updates: **Setup → Update firmware** with `twang-next-stock-ota.bin`.

### Building from source

Arduino IDE 2 or `arduino-cli`:
- Boards Manager: **esp32 by Espressif, 3.x** (tested with 3.3.12)
- Libraries: **FastLED**, **ESP Async WebServer** and **Async TCP** (both by ESP32Async)
- Board **ESP32 Dev Module**, Partition Scheme **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**
- Open `TWANG32/TWANG32.ino`. Hardware choices (strip type, audio output, pins) are in `TWANG32/config.h`.

```
arduino-cli compile -b esp32:esp32:esp32:PartitionScheme=min_spiffs TWANG32
```

PlatformIO works too: `pio run -t upload` (see `platformio.ini`).

The web UI is compiled into the firmware. If you change anything under `web/` or the stock campaign, rebuild it first:

```
bash wasm/build.sh      # engine -> build/twang.wasm (needs clang with the wasm32 target)
python3 web/build.py    # -> build/web/index.html and TWANG32/src/app/web_ui.h
```

## Develop without hardware

```
npm install
node tools/virtual-device.mjs     # http://localhost:8080
```

The virtual device runs the real engine behind the same API as the firmware, so the phone UI works against it in device mode. Handy on a home server too.

Tests:

```
bash tests/run.sh           # engine + every level played by a bot + audio checks, under ASan/UBSan
python3 tests/ui_smoke.py   # simulator UI in headless Chrome
python3 tests/ui_device.py  # device-mode UI against the virtual device
```

## Layout

```
TWANG32/                 Arduino sketch (open TWANG32.ino)
  config.h               your hardware
  src/core/              the game, synth and music: portable C++, no Arduino
  src/hal/               joystick, audio output
  src/app/               settings, stats, web server, main loop
campaign/                next.json (stock, 33 levels), classic.json (original 20)
web/                     the web app and its bundler
wasm/                    WebAssembly bindings
tools/virtual-device.mjs fake TWANG for UI work
tests/                   native and browser tests
docs/                    HARDWARE.md, LEVELS.md, ROADMAP.md
parts/                   PCB, enclosure (from the original project)
```

## Docs

- [docs/ROADMAP.md](docs/ROADMAP.md) - what to do, in what order, to get this built
- [docs/HARDWARE.md](docs/HARDWARE.md) - wiring, recommended upgrades, power
- [docs/LEVELS.md](docs/LEVELS.md) - the level format, for hand-editing or sharing

## Credits and license

TWANG by Critters, TWANG32 and hardware by Bart Dring ([buildlog.net](http://www.buildlog.net/blog?s=twang)), inspired by Robin Baumgarten's Line Wobbler. Fire2012 screensaver by Mark Kriegsman. Fonts: Grenze Gotisch and Archivo (SIL Open Font License, see `web/fonts`). Creative Commons 4.0 Attribution - Share Alike, as the original.
