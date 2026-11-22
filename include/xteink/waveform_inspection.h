#pragma once
#include <cstdio>
#include <string>

#include "waveform.h"
namespace x4render {
// Generated from the saved register payload that was passed to activate(),
// never from live controls that may have changed since that refresh.
inline std::string lutHex(const Lut& lut) {
  constexpr char digits[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(98);
  for (auto b : lut) {
    result += digits[b >> 4];
    result += digits[b & 15];
  }
  return result;
}
inline std::string lutBinary(const Lut& lut) {
  std::string result;
  result.reserve(440);
  for (size_t i = 0; i < lut.size(); ++i) {
    if (i) result += ' ';
    for (int bit = 7; bit >= 0; --bit)
      result += (lut[i] & (1u << bit)) ? '1' : '0';
  }
  return result;
}
inline std::string lutTrace(const Lut& lut) {
  std::string result;
  for (size_t group = 0; group < 7; ++group) {
    const size_t b = group * 7;
    if (!lut[b]) continue;
    char text[40];
    std::snprintf(text, sizeof(text), "g%u x%u ", unsigned(group), lut[b]);
    result += text;
    for (size_t state = 0; state < 2; ++state) {
      std::snprintf(text, sizeof(text), "s%u x%u[", unsigned(state),
                    lut[b + 5 + state]);
      result += text;
      bool first = true;
      for (size_t phase = 1 + state * 2; phase <= 2 + state * 2; ++phase) {
        const uint8_t value = lut[b + phase], count = value & 63;
        if (!count) continue;
        if (!first) result += ' ';
        first = false;
        std::snprintf(text, sizeof(text), "%u%u*%u", (value >> 7) & 1,
                      (value >> 6) & 1, count);
        result += text;
      }
      result += "] ";
    }
  }
  return result.empty() ? "zero groups" : result;
}
}  // namespace x4render
