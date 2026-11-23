#pragma once
#include "rendering.h"

namespace x4render {

struct BwStripPlan {
  std::array<uint16_t, Width / 8> ends{};
  uint8_t count{0};
};

inline BwStripPlan planBwStrips(const Buffer& target, uint16_t railBudget) {
  BwStripPlan plan;
  if (target.size() != PlaneBytes || railBudget < 8 || railBudget > Width)
    return plan;

  constexpr uint16_t rowBytes = Width / 8;
  // Entering an active rail from the modeled midpoint costs half a full
  // swing. Limit width for the all-one-polarity parts of the waveform too.
  const uint16_t maxBytes = railBudget / 4;
  std::array<uint16_t, Height - 1> toWhite, toBlack;
  for (uint16_t start = 0; start < rowBytes;) {
    toWhite.fill(0);
    toBlack.fill(0);
    uint16_t end = start;
    for (; end < rowBytes && end - start < maxBytes; ++end) {
      bool fits = true;
      for (uint16_t y = 0; y < Height - 1; ++y) {
        const uint8_t before = target[size_t(y) * rowBytes + end];
        const uint8_t after = target[size_t(y + 1) * rowBytes + end];
        const uint8_t changed = before ^ after;
        toWhite[y] += __builtin_popcount(changed & after);
        toBlack[y] += __builtin_popcount(changed & before);
        if (toWhite[y] > railBudget || toBlack[y] > railBudget) {
          fits = false;
          break;
        }
      }
      if (!fits) break;
    }
    plan.ends[plan.count++] = end * 8;
    start = end;
  }
  return plan;
}

}  // namespace x4render
