/*
  TWANG Next - portable core
  Base types, math and utilities shared by the ESP32 firmware, the native
  test harness and the WebAssembly simulator.

  Rules for everything in src/core:
    - no Arduino, no STL, no libc beyond what the compiler provides as builtins
    - no heap allocation
    - all timing comes from the game clock, never from millis()
*/
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace tw {

// The playfield is always 1000 units wide regardless of strip length.
constexpr int WORLD = 1000;
constexpr int TICK_HZ = 60;

struct RGB {
  uint8_t r, g, b;
};

inline RGB rgb(uint8_t r, uint8_t g, uint8_t b) { return RGB{r, g, b}; }

// ---------------------------------------------------------------- math
constexpr float PI_F = 3.14159265358979f;
constexpr float TWO_PI_F = 6.28318530717959f;

inline int absi(int v) { return v < 0 ? -v : v; }
inline int mini(int a, int b) { return a < b ? a : b; }
inline int maxi(int a, int b) { return a > b ? a : b; }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float absf(float v) { return __builtin_fabsf(v); }
inline float minf(float a, float b) { return a < b ? a : b; }
inline float maxf(float a, float b) { return a > b ? a : b; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float floorf_(float v) { return __builtin_floorf(v); }
inline float sqrtf_(float v) { return __builtin_sqrtf(v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Arduino-compatible integer map (keeps level geometry identical to the original)
inline long mapl(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
inline long map_constrain(long x, long in_min, long in_max, long out_min, long out_max) {
  if (in_max > in_min) x = x < in_min ? in_min : (x > in_max ? in_max : x);
  else x = x < in_max ? in_max : (x > in_min ? in_min : x);
  return mapl(x, in_min, in_max, out_min, out_max);
}

// sin(2*pi*p) for any p. Range-reduced odd polynomial, max error ~2e-6.
inline float sin2pi(float p) {
  p -= floorf_(p);            // [0,1)
  float x = p * TWO_PI_F;     // [0, 2pi)
  if (x > PI_F) x -= TWO_PI_F; // [-pi, pi]
  if (x > PI_F * 0.5f) x = PI_F - x;
  else if (x < -PI_F * 0.5f) x = -PI_F - x;
  float x2 = x * x;
  return x * (1.0f + x2 * (-0.16666667f + x2 * (0.0083333310f + x2 * (-0.00019840874f + x2 * 2.7525562e-6f))));
}
inline float sinf_(float x) { return sin2pi(x * (1.0f / TWO_PI_F)); }

// 2^x, accurate to ~1e-5 relative over the range we use
inline float exp2f_(float x) {
  if (x < -126.0f) return 0.0f;
  if (x > 127.0f) x = 127.0f;
  float fi = floorf_(x);
  float f = x - fi;
  float p = 1.0f + f * (0.69314718f + f * (0.24022650f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
  union { float f; int32_t i; } u;
  u.f = p;
  u.i += (int32_t)fi * (1 << 23);  // add to the exponent (multiply avoids UB on negative shifts)
  return u.f;
}
inline float midiToHz(float note) { return 440.0f * exp2f_((note - 69.0f) * (1.0f / 12.0f)); }

// Cheap soft clipper (rational tanh approximation)
inline float softclip(float x) {
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// ---------------------------------------------------------------- rng
struct Rng {
  uint32_t s = 0x9E3779B9u;
  void seed(uint32_t v) { s = v ? v : 0x9E3779B9u; }
  uint32_t next() {
    uint32_t x = s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return s = x;
  }
  // [0, n)
  uint32_t below(uint32_t n) { return n ? next() % n : 0; }
  // [lo, hi]
  int range(int lo, int hi) { return hi <= lo ? lo : lo + (int)below((uint32_t)(hi - lo + 1)); }
  uint8_t u8() { return (uint8_t)(next() >> 24); }
  float unit() { return (next() >> 8) * (1.0f / 16777216.0f); }
  float bipolar() { return unit() * 2.0f - 1.0f; }
};

// ---------------------------------------------------------------- memory/string
inline void memzero(void* p, size_t n) { __builtin_memset(p, 0, n); }
inline void memcopy(void* d, const void* s, size_t n) { __builtin_memcpy(d, s, n); }
inline size_t strlen_(const char* s) { size_t n = 0; while (s && s[n]) n++; return n; }
inline void strcopy(char* dst, const char* src, size_t cap) {
  if (!cap) return;
  size_t i = 0;
  if (src) for (; i + 1 < cap && src[i]; i++) dst[i] = src[i];
  dst[i] = 0;
}
inline bool streq(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *a == *b) { a++; b++; }
  return *a == *b;
}

// ---------------------------------------------------------------- color helpers
inline uint8_t qadd8(uint8_t a, uint8_t b) { int v = a + b; return v > 255 ? 255 : (uint8_t)v; }
inline uint8_t qsub8(uint8_t a, uint8_t b) { int v = a - b; return v < 0 ? 0 : (uint8_t)v; }
inline uint8_t scale8(uint8_t v, uint8_t s) { return (uint8_t)(((uint16_t)v * (uint16_t)(s + 1)) >> 8); }
inline RGB scale(RGB c, uint8_t s) { return RGB{scale8(c.r, s), scale8(c.g, s), scale8(c.b, s)}; }
inline RGB addc(RGB a, RGB b) { return RGB{qadd8(a.r, b.r), qadd8(a.g, b.g), qadd8(a.b, b.b)}; }
inline RGB blend(RGB a, RGB b, uint8_t t) {
  return RGB{(uint8_t)(a.r + (((int)b.r - a.r) * t >> 8)), (uint8_t)(a.g + (((int)b.g - a.g) * t >> 8)),
             (uint8_t)(a.b + (((int)b.b - a.b) * t >> 8))};
}
// FastLED-compatible HSV rainbow (hue 0-255)
RGB hsv(uint8_t h, uint8_t s, uint8_t v);
// FastLED HeatColor (black-body palette used by the fire screensaver)
RGB heatColor(uint8_t temperature);

// beatsin16-style helper driven by a millisecond clock
inline int beatsin(uint32_t ms, float bpm, int lo, int hi) {
  float s = sin2pi((float)ms * bpm / 60000.0f);
  return lo + (int)((s * 0.5f + 0.5f) * (float)(hi - lo));
}

}  // namespace tw
