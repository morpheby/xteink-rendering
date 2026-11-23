#pragma once
#include "bw_approximation.h"
#include "waveform.h"

namespace x4render {

// Counts of C_source * one midpoint-to-rail step. Treat code 0 as the
// midpoint, 1/2 as equal opposite rails. This scores source-line charging;
// it is not a measurement of VCOM, the TFT load, or the converter output.
struct BwSourceCharge {
  uint16_t both{0}, one{0}, boundary{0}, recent{0}, sustained{0};
  uint32_t frames{0};
  std::array<uint64_t, 2> total{};

  void include(const BwSourceCharge& pass) {
    both = std::max(both, pass.both);
    one = std::max(one, pass.one);
    boundary = std::max(boundary, pass.boundary);
    recent = std::max(recent, pass.recent);
    sustained = std::max(sustained, pass.sustained);
    frames += pass.frames;
    for (size_t rail = 0; rail < total.size(); ++rail) total[rail] += pass.total[rail];
  }
};

constexpr unsigned sourceStep(uint8_t before, uint8_t after) {
  return !after || before == after ? 0 : before ? 2 : 1;
}

inline BwSourceCharge estimateBwSourceCharge(const BwLoad& load, uint16_t width,
                                             const LutBank& bank, bool masked) {
  BwSourceCharge out;
  const auto& white = bank[Row[1]];
  const auto& black = bank[Row[2]];
  const auto& padding = bank[masked ? Row[0] : Row[3]];
  std::array<uint8_t, 2> previous{{0,0}};
  auto run = [&](size_t phase) {
    const unsigned n = white[phase] & 63;
    const uint8_t pad = padding[phase] >> 6;
    const std::array<uint8_t, 2> rail{{uint8_t(white[phase] >> 6), uint8_t(black[phase] >> 6)}};
    if ((black[phase] & 63) != n || (padding[phase] & 63) != n ||
        pad > 2 || rail[0] > 2 || rail[1] > 2) return false;
    if (!n) return true;
    auto& peak = rail[0] && rail[1] ? out.both : out.one;
    unsigned firstPeak[2]{};
    std::array<unsigned, 2> lastPixels{{load.lastWhite, unsigned(width - load.lastWhite)}};
    for (size_t color = 0; color < 2; ++color) {
      if (!rail[color]) continue;
      const unsigned weight = sourceStep(rail[1 - color], rail[color]);
      const auto& stats = load.direction[color];
      peak = std::max<uint16_t>(peak, weight * stats.peak);
      out.recent = std::max<uint16_t>(out.recent, weight * stats.recentPeak);
      out.sustained = std::max<uint16_t>(out.sustained, weight * stats.sustainedPeak);
      const unsigned firstPixels = color ? width - load.firstWhite : load.firstWhite;
      const auto index = rail[color] - 1;
      firstPeak[index] += firstPixels * sourceStep(pad, rail[color]);
      out.total[index] += uint64_t(n) * weight * stats.total;
    }
    for (size_t r = 0; r < out.total.size(); ++r) {
      out.boundary = std::max<uint16_t>(out.boundary, firstPeak[r]);
      out.total[r] += uint64_t(n) * firstPeak[r];
    }
    if (pad) {
      unsigned first = 0, steady = 0;
      for (size_t color = 0; color < 2; ++color) {
        first += lastPixels[color] * sourceStep(previous[color], pad);
        steady += lastPixels[color] * sourceStep(rail[color], pad);
      }
      out.boundary = std::max<uint16_t>(out.boundary, first);
      if (n > 1) out.boundary = std::max<uint16_t>(out.boundary, steady);
      out.total[pad - 1] += first + uint64_t(n - 1) * steady;
    }
    previous = rail;
    out.frames += n;
    return true;
  };
  for (size_t base = 0; base < white.size(); base += 7) {
    for (auto i : {0, 5, 6})
      if (white[base + i] != black[base + i] || white[base + i] != padding[base + i]) return {};
    for (unsigned group = 0; group < white[base]; ++group)
      for (unsigned state = 0; state < 2; ++state)
        for (unsigned repeat = 0; repeat < white[base + 5 + state]; ++repeat) {
          const size_t phase = base + 1 + state * 2;
          if (!run(phase) || !run(phase + 1)) return {};
        }
  }
  return out;
}

}  // namespace x4render
