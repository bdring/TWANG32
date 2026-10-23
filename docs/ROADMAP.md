# Roadmap: from here to a finished TWANG

The software is done enough to play. What's left is mostly hardware and taste. Each phase ends with something working, so you can stop at any point and still have a better TWANG than before.

## What is verified, and what isn't

Verified in this repo:
- The engine plays every one of the 53 levels (20 classic, 33 next) to completion with a scripted bot, under AddressSanitizer and UBSan, at strip lengths from 1 to 1000 LEDs. It is deterministic and never gets stuck.
- Endless mode: 250 generated depths pass the fairness checks; the bot clears 26 of the first 30.
- Audio renders without clipping or dropped commands; spectrograms of the key sounds look right.
- The firmware compiles cleanly against ESP32 Arduino core 3.3.12 for both APA102+DAC and WS2812+I2S (1.40 MB of a 1.97 MB app slot, 79 KB static RAM).
- The web UI works in a headless browser in both simulator and device mode; device mode was tested against the virtual device, which speaks the firmware's API.

Not verified, because it needs the board:
- Real-world timing: frame rate with your strip, audio task scheduling alongside Wi-Fi, joystick feel.
- The DAC continuous driver and MAX98357A output on real hardware.
- The captive portal on your phone.

Expect one evening of bring-up fixes. The serial console (`?`) and Setup → System show what's going on.

## Phase 0 - Tonight, 20 minutes, no soldering

1. Open the simulator. Play the campaign to *Thick Skin* (level 10). Notice what's fun and what isn't; that's your design brief for later.
2. Go to **Sound**, play *Descent* and drag intensity from 0 to 100. Then *Reckoning*. This is what the boss sounds like.
3. Go to **Levels**, pick *Swing Time*, add a *Chaser*, and watch it on the strip as you drag it.

You will know by the end of this whether the game is worth the build. (It is.)

## Phase 1 - First light on the real hardware (1 evening)

Goal: the stock board running TWANG Next with no code changes.

1. Flash `firmware/twang-next-stock-merged.bin` at `0x0` with [esptool-js](https://espressif.github.io/esptool-js/) in Chrome. No IDE needed.
2. Join the **TWANG** Wi-Fi (password `12345678`) and open http://192.168.4.1.
3. Work through the bring-up checklist in [HARDWARE.md](HARDWARE.md): boot, joystick, calibrate, twang threshold, LED count, sound, frame rate.
4. Play the first five levels on the spring. If movement feels off compared to the original, it's the dead zone and calibration, not the engine (the movement maths is the original's).

If the strip shows wrong colours, change `LED_COLOR_ORDER` in `config.h` and build from source (Phase 2).

## Phase 2 - Own the build (1 evening)

Goal: you can change code and ship it to the device wirelessly.

1. Install Arduino IDE 2 (or `arduino-cli`) with ESP32 core 3.x and the three libraries in the README. Compile once over USB.
2. After that, update from the browser (**Setup → Update firmware**) or with `pio run -e twang32-ota -t upload`. The USB cable can stay in the drawer.
3. Add your home Wi-Fi in Setup so TWANG is also at http://twang.local.
4. Optional: run `node tools/virtual-device.mjs` on the Unraid box (or anywhere) for level design without the strip powered.

## Phase 3 - Sound upgrade (1 hour + parts)

Goal: the music sounds like it does in the browser.

1. Order a MAX98357A board and a 3 W 4 Ω speaker (see HARDWARE.md, about $12 total).
2. Wire it (four wires), switch `config.h` to `AUDIO_OUT_I2S`, flash.
3. Balance in **Sound**: master around 80, music 50-60, effects 90. Turn off *Movement thrum* if it gets on your nerves in a quiet room.

## Phase 4 - Make it a fixture (a weekend)

Goal: something people walk up to and play.

1. Mount the strip. A run along a hallway baseboard or a shelf edge reads best; up a wall works too (turn on *Death sparks fall* and set the bend point).
2. Power: a 5 V supply sized for your strip, injection at both ends if it's over 2 m, and the power limit set to match.
3. Print or adapt the enclosure in `parts/stl`. Add strain relief to the spring wires.
4. Set **Start up in** to *Campaign*, the screensaver timeout to taste, and difficulty to *Normal*. Let the screensaver run while it's idle.

## Phase 5 - Design your levels (ongoing, the fun part)

1. Play the campaign on the strip with friends. Then look at **Stats**: the clear rate column and the death heatmap under each level in the editor show where it's too mean or too easy.
2. Tune those levels in the editor: *Watch* while you edit, *Play it* to try it, *Test on strip* to feel it on the spring, *Save to TWANG*.
3. Build a campaign of your own. Ideas that fit this engine well:
   - a darkness level where the only light is burning lava, with a phantom hiding in it;
   - an ice slope into a conveyor with a splitter at the bottom;
   - a boss with rage whose positions sit between two lava pools.
4. Export your campaign and keep it in `campaign/` in this repo.

## Phase 6 - Stretch goals, if you want more

In rough order of fun per hour:

1. **Two-player versus.** Two springs (the second MPU6050 at I2C address 0x69 with AD0 high), one strip, players start at opposite ends. The engine already handles enemies from both directions.
2. **Leaderboard screen.** A phone or old tablet on the wall showing Stats at http://twang.local.
3. **Daily seed.** Endless mode with a seed from the date, so everyone plays the same run that day.
4. **Pass-the-spring party mode.** One life each, the score bar after each death, highest depth wins.
5. **A second strip** as a scoreboard or "health bar" display.

## If you get stuck

- **Player creeps on its own:** calibrate, then raise the dead zone by 2.
- **Twang fires on its own, or never:** watch the wobble meter in Setup and move *Twang sensitivity*.
- **Choppy frame rate:** check Setup → System. Fewer LEDs, or APA102 instead of WS2812.
- **Crackling audio:** *Audio underruns* in `/api/info` should stay at 0. If it climbs, close the live view on phones you aren't using.
- **Can't reach the page:** the AP is always at 192.168.4.1 on the TWANG network. Serial `?` prints the address.
