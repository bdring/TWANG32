/*
  TWANG Next - tiny pull-style JSON reader.
  Zero allocation; walks a text buffer in place. Used for levels, settings
  and anything else the firmware and simulator need to read.
*/
#pragma once
#include "tw_base.h"

namespace tw {

class JsonReader {
 public:
  enum Type { T_NONE, T_OBJECT, T_ARRAY, T_STRING, T_NUMBER, T_BOOL, T_NULL };

  JsonReader(const char* text, size_t len) : s_(text), n_(len) {}

  bool ok() const { return err_ == nullptr; }
  const char* error() const { return err_ ? err_ : ""; }
  size_t errorPos() const { return errPos_; }
  size_t pos() const { return i_; }

  Type peek();
  bool beginObject();
  // Reads the next key of the current object. Returns false (and consumes '}') at the end.
  bool nextKey(char* key, int cap);
  bool beginArray();
  // Advances to the next array element. Returns false (and consumes ']') at the end.
  bool nextItem();

  bool readNumber(float& out);
  bool readInt(int& out);
  bool readBool(bool& out);
  bool readString(char* out, int cap);
  bool skip();

  // Convenience: read a number/bool/string if present, otherwise skip whatever is there.
  int asInt(int fallback);
  float asFloat(float fallback);
  bool asBool(bool fallback);

  void fail(const char* msg) {
    if (!err_) { err_ = msg; errPos_ = i_; }
  }

 private:
  void ws();
  bool expect(char c);
  bool atEnd() const { return i_ >= n_ || s_[i_] == 0; }
  char cur() const { return atEnd() ? 0 : s_[i_]; }

  static const int MAX_DEPTH = 24;
  const char* s_;
  size_t n_;
  size_t i_ = 0;
  const char* err_ = nullptr;
  size_t errPos_ = 0;
  int depth_ = 0;
  bool first_[MAX_DEPTH];
};

}  // namespace tw
