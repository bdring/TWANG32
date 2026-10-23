#include "tw_json.h"

namespace tw {

void JsonReader::ws() {
  while (!atEnd()) {
    char c = s_[i_];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i_++; continue; }
    // allow // line comments so hand-written level files can be annotated
    if (c == '/' && i_ + 1 < n_ && s_[i_ + 1] == '/') {
      while (!atEnd() && s_[i_] != '\n') i_++;
      continue;
    }
    break;
  }
}

bool JsonReader::expect(char c) {
  ws();
  if (cur() != c) {
    fail(c == ':' ? "expected ':'" : c == ',' ? "expected ','" : "unexpected character");
    return false;
  }
  i_++;
  return true;
}

JsonReader::Type JsonReader::peek() {
  if (!ok()) return T_NONE;
  ws();
  char c = cur();
  if (c == '{') return T_OBJECT;
  if (c == '[') return T_ARRAY;
  if (c == '"') return T_STRING;
  if (c == '-' || (c >= '0' && c <= '9')) return T_NUMBER;
  if (c == 't' || c == 'f') return T_BOOL;
  if (c == 'n') return T_NULL;
  return T_NONE;
}

bool JsonReader::beginObject() {
  if (!ok()) return false;
  if (!expect('{')) return false;
  if (depth_ >= MAX_DEPTH) { fail("nesting too deep"); return false; }
  first_[depth_++] = true;
  return true;
}

bool JsonReader::beginArray() {
  if (!ok()) return false;
  if (!expect('[')) return false;
  if (depth_ >= MAX_DEPTH) { fail("nesting too deep"); return false; }
  first_[depth_++] = true;
  return true;
}

bool JsonReader::nextKey(char* key, int cap) {
  if (!ok() || depth_ <= 0) return false;
  ws();
  if (cur() == '}') { i_++; depth_--; return false; }
  if (!first_[depth_ - 1]) {
    if (!expect(',')) return false;
    ws();
    if (cur() == '}') { i_++; depth_--; return false; }  // tolerate trailing comma
  }
  first_[depth_ - 1] = false;
  if (peek() != T_STRING) { fail("expected key"); return false; }
  if (!readString(key, cap)) return false;
  return expect(':');
}

bool JsonReader::nextItem() {
  if (!ok() || depth_ <= 0) return false;
  ws();
  if (cur() == ']') { i_++; depth_--; return false; }
  if (!first_[depth_ - 1]) {
    if (!expect(',')) return false;
    ws();
    if (cur() == ']') { i_++; depth_--; return false; }
  }
  first_[depth_ - 1] = false;
  return true;
}

bool JsonReader::readNumber(float& out) {
  if (!ok()) return false;
  ws();
  bool neg = false;
  if (cur() == '-') { neg = true; i_++; }
  if (!(cur() >= '0' && cur() <= '9') && cur() != '.') { fail("expected number"); return false; }
  double v = 0;
  while (cur() >= '0' && cur() <= '9') { v = v * 10 + (cur() - '0'); i_++; }
  if (cur() == '.') {
    i_++;
    double scale = 0.1;
    while (cur() >= '0' && cur() <= '9') { v += (cur() - '0') * scale; scale *= 0.1; i_++; }
  }
  if (cur() == 'e' || cur() == 'E') {
    i_++;
    bool eneg = false;
    if (cur() == '-') { eneg = true; i_++; } else if (cur() == '+') i_++;
    int e = 0;
    while (cur() >= '0' && cur() <= '9') { e = e * 10 + (cur() - '0'); i_++; }
    while (e-- > 0) v = eneg ? v / 10.0 : v * 10.0;
  }
  out = (float)(neg ? -v : v);
  return true;
}

bool JsonReader::readInt(int& out) {
  float f;
  if (!readNumber(f)) return false;
  out = (int)(f < 0 ? f - 0.5f : f + 0.5f);
  return true;
}

bool JsonReader::readBool(bool& out) {
  if (!ok()) return false;
  ws();
  if (i_ + 4 <= n_ && s_[i_] == 't' && s_[i_ + 1] == 'r' && s_[i_ + 2] == 'u' && s_[i_ + 3] == 'e') {
    i_ += 4; out = true; return true;
  }
  if (i_ + 5 <= n_ && s_[i_] == 'f' && s_[i_ + 1] == 'a' && s_[i_ + 2] == 'l' && s_[i_ + 3] == 's' && s_[i_ + 4] == 'e') {
    i_ += 5; out = false; return true;
  }
  fail("expected true/false");
  return false;
}

bool JsonReader::readString(char* out, int cap) {
  if (!ok()) return false;
  ws();
  if (cur() != '"') { fail("expected string"); return false; }
  i_++;
  int o = 0;
  while (!atEnd() && s_[i_] != '"') {
    char c = s_[i_++];
    if (c == '\\') {
      char e = cur(); i_++;
      switch (e) {
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'u': c = '?'; i_ += 4; break;  // no unicode on an LED strip
        default: c = e; break;
      }
    }
    if (out && o + 1 < cap) out[o++] = c;
  }
  if (atEnd()) { fail("unterminated string"); return false; }
  i_++;  // closing quote
  if (out && cap > 0) out[o] = 0;
  return true;
}

bool JsonReader::skip() {
  switch (peek()) {
    case T_OBJECT: {
      if (!beginObject()) return false;
      char k[2];
      while (nextKey(k, sizeof(k))) if (!skip()) return false;
      return ok();
    }
    case T_ARRAY:
      if (!beginArray()) return false;
      while (nextItem()) if (!skip()) return false;
      return ok();
    case T_STRING: return readString(nullptr, 0);
    case T_NUMBER: { float f; return readNumber(f); }
    case T_BOOL: { bool b; return readBool(b); }
    case T_NULL:
      if (i_ + 4 <= n_) { i_ += 4; return true; }
      fail("bad null");
      return false;
    default:
      fail("unexpected value");
      return false;
  }
}

int JsonReader::asInt(int fallback) {
  Type t = peek();
  if (t == T_NUMBER) { int v; return readInt(v) ? v : fallback; }
  if (t == T_BOOL) { bool b; return readBool(b) ? (b ? 1 : 0) : fallback; }
  skip();
  return fallback;
}

float JsonReader::asFloat(float fallback) {
  if (peek() == T_NUMBER) { float v; return readNumber(v) ? v : fallback; }
  skip();
  return fallback;
}

bool JsonReader::asBool(bool fallback) {
  Type t = peek();
  if (t == T_BOOL) { bool b; return readBool(b) ? b : fallback; }
  if (t == T_NUMBER) { int v; return readInt(v) ? v != 0 : fallback; }
  skip();
  return fallback;
}

}  // namespace tw
