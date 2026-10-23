# Hardware

The firmware runs on the stock TWANG32 board unchanged. This page covers wiring and the upgrades that are worth it, ranked.

## Stock wiring (`TWANG32/config.h` defaults)

| Part | ESP32 pin | Notes |
|---|---|---|
| APA102 data | GPIO16 | |
| APA102 clock | GPIO17 | |
| MPU6050 SDA / SCL | GPIO21 / GPIO22 | 3.3 V, at the tip of the spring |
| Audio (DAC) | GPIO25 | into a PAM8403 amp |
| I2S amp BCLK / LRC / DIN | GPIO26 / 27 / 25 | only with `AUDIO_OUT_I2S` |

The original code declared "life LEDs" on GPIO 5, 6 and 7. They were never used, and GPIO 6/7 are wired to the flash chip on ESP32 modules, so they're gone.

## Upgrades, in order of payoff

1. **An I2S amplifier: MAX98357A (about $6).** The new music is the biggest change in this build, and the ESP32's DAC is 8-bit. The firmware shapes the quantisation noise as well as it can, but a MAX98357A gives you 16-bit, about 3 W into 4 Ω, and no separate PAM8403. Wire BCLK→26, LRC→27, DIN→25, VIN→5 V, GND→GND, speaker to its outputs, then in `config.h` comment out `AUDIO_OUT_DAC` and enable `AUDIO_OUT_I2S`. Leave the GAIN pin floating (9 dB) to start.
2. **A real speaker.** A 3 W, 4 Ω full-range driver (40-57 mm) in a small closed box. The drone and heartbeat live below 150 Hz; a bare 28 mm speaker will lose them.
3. **Power sized for the strip.** Budget 20 mA per LED for typical play and 60 mA per LED at full white. The firmware's power limit (Setup → LED strip, default 2500 mA) dims any frame that would exceed it, so set it to your supply's rating minus about 20%. A 5 V 4 A supply is a good fit for 144 LEDs. Put a 1000 µF capacitor across the strip's power input.
4. **Power injection on long strips.** Feed 5 V and ground into both ends (and every 2 m) of anything longer than about 2 m, or the far end goes brown and pink.
5. **A level shifter for long cable runs.** The ESP32 drives the strip at 3.3 V. APA102 usually accepts that over short wires; past about 30 cm of cable use a 74AHCT125 (or 74HCT245) on data and clock.
6. **Strain relief on the spring.** The MPU6050 wires flex every twang. Run them along the spring with heat-shrink and leave a loop at the base. Most "joystick stopped working" problems are a broken wire here.

## Strip choices

APA102 (DotStar) is strongly preferred: it has a clock line, refreshes fast at any length, and its low-brightness range is what makes cool lava and conveyors visible. WS2812B works (set `USE_NEOPIXEL`) up to 300 LEDs; raise *Conveyor brightness* to about 40 and *Cool lava brightness* to about 15 in Setup, because NeoPixels barely light at the low values.

If the player is not green, the exit not blue and enemies not red, change `LED_COLOR_ORDER` in `config.h`.

## Bring-up checklist

Work through these with the serial monitor open (115200 baud; type `?` for status).

1. **Boots:** the serial log prints the Wi-Fi name and address. The strip plays the green startup wipe and sparkle.
2. **Joystick:** Setup → Joystick shows tilt and wobble moving as you move the spring. If it says "not found", check SDA/SCL and 3.3 V. If tilt reads backwards, turn on *Reverse direction*; if it moves on the wrong axis, change *Tilt axis*.
3. **Calibrate:** let the spring hang still, press *Calibrate level*. Then lower the dead zone until the player creeps, and raise it by 2.
4. **Twang threshold:** flick the spring and watch the wobble bar turn blue. Move *Twang sensitivity* until a deliberate flick twangs and walking past the table doesn't.
5. **LEDs:** set *Number of LEDs* to your strip. The exit (blue) should be the last pixel.
6. **Sound:** Sound → Sound effects → *Twang*. If you hear nothing, check the amp's power and the `AUDIO_OUT_*` choice. Then try a music track and drag intensity.
7. **Frame rate:** Setup → System should show about 60 fps. If it's much lower with a long strip, that strip is the bottleneck (see "Strip choices").
