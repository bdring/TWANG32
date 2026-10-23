/*
  TWANG Next - hardware configuration.

  This is the only file you should need to touch to match your build.
  Everything else (LED count, brightness, joystick tuning, volume, Wi-Fi)
  is set from the web page at http://192.168.4.1 and saved on the device.

  Defaults match the stock TWANG32 board by Bart Dring:
    APA102 strip on 16/17, MPU6050 on I2C 21/22, PAM8403 amp on DAC GPIO25.
*/
#pragma once

// ------------------------------------------------------------------ LED strip
// Pick exactly one. APA102 (DotStar) is strongly recommended: it has a clock line,
// so it runs fast with long strips and has far better low-brightness range.
#define USE_APA102
// #define USE_NEOPIXEL   // WS2812B / SK6812

#define LED_DATA_PIN 16
#define LED_CLOCK_PIN 17  // APA102 only

// If the player is not green, the exit not blue and enemies not red, change this.
// APA102 strips are usually BGR, WS2812B strips usually GRB.
#ifdef USE_APA102
#define LED_COLOR_ORDER BGR
#define MAX_LEDS 1000
#define DEFAULT_LED_COUNT 144
#define DEFAULT_CONVEYOR_BRIGHTNESS 8
#define DEFAULT_LAVA_OFF_BRIGHTNESS 4
#endif
#ifdef USE_NEOPIXEL
#define LED_COLOR_ORDER GRB
#define MAX_LEDS 300  // WS2812 can't refresh more than this at 60 fps
#define DEFAULT_LED_COUNT 144
#define DEFAULT_CONVEYOR_BRIGHTNESS 40  // low NeoPixel values are nearly invisible
#define DEFAULT_LAVA_OFF_BRIGHTNESS 15
#endif

// Current limit for the whole strip (mA at 5 V). FastLED dims frames that would
// exceed it, which protects your power supply during the boss-kill rainbow.
// Change it from the web page; this is just the default.
#define DEFAULT_POWER_LIMIT_MA 2500

// ------------------------------------------------------------------ joystick (MPU6050)
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// ------------------------------------------------------------------ audio
// Pick exactly one.
// AUDIO_OUT_DAC: the ESP32's 8-bit DAC on GPIO25 into a PAM8403 (stock board).
// AUDIO_OUT_I2S: an I2S amplifier such as the MAX98357A. 16-bit, louder, cleaner:
//                the single best upgrade for the new music. See docs/HARDWARE.md.
#define AUDIO_OUT_DAC
// #define AUDIO_OUT_I2S

#define I2S_BCLK_PIN 26
#define I2S_LRC_PIN 27
#define I2S_DOUT_PIN 25

#define AUDIO_SAMPLE_RATE 22050

// ------------------------------------------------------------------ Wi-Fi defaults
// Change these from the web page; they are only used until you do.
#define DEFAULT_AP_NAME "TWANG"
#define DEFAULT_AP_PASS "12345678"  // at least 8 characters, or "" for an open network
#define DEFAULT_HOSTNAME "twang"    // also reachable as http://twang.local on your home network

// ------------------------------------------------------------------ checks
#if defined(USE_APA102) == defined(USE_NEOPIXEL)
#error "config.h: define exactly one of USE_APA102 or USE_NEOPIXEL"
#endif
#if defined(AUDIO_OUT_DAC) == defined(AUDIO_OUT_I2S)
#error "config.h: define exactly one of AUDIO_OUT_DAC or AUDIO_OUT_I2S"
#endif
