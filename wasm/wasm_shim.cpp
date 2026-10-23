// Minimal libc pieces the compiler may call in a freestanding WebAssembly build.
#include <stddef.h>

extern "C" {

void* memset(void* d, int c, size_t n) {
  unsigned char* p = (unsigned char*)d;
  while (n--) *p++ = (unsigned char)c;
  return d;
}

void* memcpy(void* d, const void* s, size_t n) {
  unsigned char* a = (unsigned char*)d;
  const unsigned char* b = (const unsigned char*)s;
  while (n--) *a++ = *b++;
  return d;
}

void* memmove(void* d, const void* s, size_t n) {
  unsigned char* a = (unsigned char*)d;
  const unsigned char* b = (const unsigned char*)s;
  if (a < b) { while (n--) *a++ = *b++; }
  else { a += n; b += n; while (n--) *--a = *--b; }
  return d;
}

int memcmp(const void* x, const void* y, size_t n) {
  const unsigned char* a = (const unsigned char*)x;
  const unsigned char* b = (const unsigned char*)y;
  for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
  return 0;
}

}
