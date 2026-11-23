#pragma once
#include "bw_approximation.h"

namespace x4render {

struct BwStripPlan {
  std::array<uint16_t, Width / 8> ends{};
  uint8_t count{0};
};

struct BwZonePlan {
  BwColumnMask owners{};
  uint8_t count{0}, laneWidth{0};
  uint16_t wholeRecent{0}, localRecent{0}, localSustained{0};

  void include(const BwLoad& load) {
    wholeRecent = std::max(wholeRecent, load.wholeRecentPeak);
    localRecent = std::max(localRecent, load.recentPeak);
    localSustained = std::max(localSustained, load.sustainedPeak);
  }
  bool fits(unsigned railBudget, unsigned localBudget) const {
    return wholeRecent <= railBudget && localRecent <= localBudget &&
           localSustained <= 4 * localBudget / 5;
  }
};

template <class Measure>
BwZonePlan planBwZones(Measure measure, unsigned railBudget, unsigned localBudget) {
  constexpr uint8_t maxZones = 3;
  BwZonePlan plan;
  measure(plan, nullptr);
  if (plan.fits(railBudget, localBudget)) {
    plan.count = 1;
    return plan;
  }
  for (uint8_t zones = 2; zones <= maxZones; ++zones) {
    // Try 16-pixel lanes first to use fewer boundaries. Eight-pixel lanes
    // spread activity more evenly across the overlapping 128-column windows.
    for (unsigned laneBytes : {2u, 1u}) {
      plan = {};
      plan.count = zones;
      plan.laneWidth = 8 * laneBytes;
      for (size_t x = 0; x < plan.owners.size(); ++x) plan.owners[x] = (x / laneBytes) % zones;
      for (uint8_t zone = 0; zone < zones; ++zone) {
        BwColumnMask active;
        for (size_t x = 0; x < active.size(); ++x) active[x] = plan.owners[x] == zone;
        measure(plan, &active);
        if (!plan.fits(railBudget, localBudget)) break;
      }
      if (plan.fits(railBudget, localBudget)) return plan;
    }
  }
  return {};
}

inline BwZonePlan planBwZones(const Buffer& target, const BwLoad& full, unsigned railBudget,
                             unsigned localBudget) {
  return planBwZones([&](BwZonePlan& plan, const BwColumnMask* active) {
    plan.include(active ? measureBwLoad(target, 0, Width, active) : full);
  }, railBudget, localBudget);
}

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
