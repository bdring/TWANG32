// Minimal JSON writer for the firmware's API responses.
#pragma once
#include <Arduino.h>

class JsonOut {
 public:
  String s;
  explicit JsonOut(size_t reserve = 256) { s.reserve(reserve); }
  JsonOut& obj() { comma(); s += '{'; need_ = false; return *this; }
  JsonOut& end() { s += '}'; need_ = true; return *this; }
  JsonOut& arr() { comma(); s += '['; need_ = false; return *this; }
  JsonOut& endArr() { s += ']'; need_ = true; return *this; }
  JsonOut& key(const char* k) { comma(); str(k); s += ':'; need_ = false; return *this; }
  JsonOut& val(const char* v) { comma(); str(v); need_ = true; return *this; }
  JsonOut& val(long v) { comma(); s += String(v); need_ = true; return *this; }
  JsonOut& val(unsigned long v) { comma(); s += String(v); need_ = true; return *this; }
  JsonOut& val(int v) { return val((long)v); }
  JsonOut& val(unsigned v) { return val((unsigned long)v); }
  JsonOut& val(float v) { comma(); s += String(v, 3); need_ = true; return *this; }
  JsonOut& val(bool v) { comma(); s += v ? "true" : "false"; need_ = true; return *this; }
  template <typename T>
  JsonOut& kv(const char* k, T v) { key(k); return val(v); }

 private:
  void comma() { if (need_) s += ','; }
  void str(const char* v) {
    s += '"';
    for (const char* p = v ? v : ""; *p; p++) {
      char c = *p;
      if (c == '"' || c == '\\') { s += '\\'; s += c; }
      else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); s += b; }
      else s += c;
    }
    s += '"';
  }
  bool need_ = false;
};
