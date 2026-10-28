// oRings - just enough JSON reading for the state blob. MIT.
//
// Runs on the audio thread (set_param("state") is served there), so: no
// allocation, bounded work, every scan limited to the span it was given.
#ifndef ORINGS_JSON_MINI_H_
#define ORINGS_JSON_MINI_H_

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace orings {
namespace json {

inline const char *SkipWs(const char *p, const char *end) {
  while (p < end && isspace(static_cast<unsigned char>(*p))) ++p;
  return p;
}

// End of the string starting at p (p points at the opening quote): returns
// the pointer just past the closing quote, or NULL.
inline const char *StringEnd(const char *p, const char *end) {
  ++p;
  while (p < end) {
    if (*p == '\\') {
      p += 2;
      continue;
    }
    if (*p == '"') return p + 1;
    ++p;
  }
  return NULL;
}

// p points at '{' or '['; returns the pointer just past the matching close.
inline const char *ContainerEnd(const char *p, const char *end) {
  int depth = 0;
  while (p < end) {
    const char c = *p;
    if (c == '"') {
      p = StringEnd(p, end);
      if (!p) return NULL;
      continue;
    }
    if (c == '{' || c == '[') ++depth;
    if (c == '}' || c == ']') {
      if (--depth == 0) return p + 1;
    }
    ++p;
  }
  return NULL;
}

// Value of `key` among the DIRECT members of the object spanning
// [obj, obj_end) -- nested objects are skipped, so a key of the same name
// inside a child can never answer for the parent. NULL when absent.
inline const char *FindMember(const char *obj, const char *obj_end, const char *key) {
  const size_t n = strlen(key);
  const char *p = SkipWs(obj, obj_end);
  if (p >= obj_end || *p != '{') return NULL;
  ++p;
  while (p < obj_end) {
    p = SkipWs(p, obj_end);
    if (p >= obj_end || *p == '}') return NULL;
    if (*p == ',') { ++p; continue; }
    if (*p != '"') return NULL;
    const char *kend = StringEnd(p, obj_end);
    if (!kend) return NULL;
    const bool match = static_cast<size_t>(kend - p - 2) == n && !strncmp(p + 1, key, n);
    p = SkipWs(kend, obj_end);
    if (p >= obj_end || *p != ':') return NULL;
    p = SkipWs(p + 1, obj_end);
    if (match) return p;
    // Skip the value.
    if (*p == '{' || *p == '[') {
      p = ContainerEnd(p, obj_end);
    } else if (*p == '"') {
      p = StringEnd(p, obj_end);
    } else {
      while (p < obj_end && *p != ',' && *p != '}') ++p;
    }
    if (!p) return NULL;
  }
  return NULL;
}

// A JSON number (strict grammar: no NaN, hex or leading '+').
inline bool ReadNumber(const char *p, const char *end, float *out) {
  const char *s = p;
  if (p < end && *p == '-') ++p;
  if (p >= end || !isdigit(static_cast<unsigned char>(*p))) return false;
  while (p < end && isdigit(static_cast<unsigned char>(*p))) ++p;
  if (p < end && *p == '.') {
    ++p;
    if (p >= end || !isdigit(static_cast<unsigned char>(*p))) return false;
    while (p < end && isdigit(static_cast<unsigned char>(*p))) ++p;
  }
  if (p < end && (*p == 'e' || *p == 'E')) {
    ++p;
    if (p < end && (*p == '+' || *p == '-')) ++p;
    if (p >= end || !isdigit(static_cast<unsigned char>(*p))) return false;
    while (p < end && isdigit(static_cast<unsigned char>(*p))) ++p;
  }
  char tmp[32];
  const size_t len = static_cast<size_t>(p - s);
  if (len == 0 || len >= sizeof(tmp)) return false;
  memcpy(tmp, s, len);
  tmp[len] = 0;
  const float v = strtof(tmp, NULL);
  if (!isfinite(v)) return false;
  *out = v;
  return true;
}

// A JSON string without escapes (all this blob ever stores): copies the
// characters between the quotes into buf. False if absent, escaped or long.
inline bool ReadString(const char *p, const char *end, char *buf, size_t size) {
  if (p >= end || *p != '"' || size == 0) return false;
  ++p;
  size_t n = 0;
  while (p < end && *p != '"') {
    if (*p == '\\' || n + 1 >= size) return false;
    buf[n++] = *p++;
  }
  if (p >= end) return false;
  buf[n] = 0;
  return true;
}

// An array of numbers, truncated to `max` entries; returns how many were
// read, or -1 when it is not a well-formed array of numbers.
inline int ReadNumberArray(const char *p, const char *end, float *out, int max) {
  if (p >= end || *p != '[') return -1;
  const char *close = ContainerEnd(p, end);
  if (!close) return -1;
  p = SkipWs(p + 1, close);
  int n = 0;
  while (p < close - 1) {
    float v;
    if (!ReadNumber(p, close, &v)) return -1;
    if (n < max) out[n] = v;
    ++n;
    while (p < close && *p != ',' && *p != ']') ++p;
    if (*p == ',') p = SkipWs(p + 1, close);
    else break;
  }
  return n < max ? n : max;
}

}  // namespace json
}  // namespace orings

#endif  // ORINGS_JSON_MINI_H_
