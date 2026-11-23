#pragma once
#include "rendering.h"

#include <cmath>
#include <optional>

namespace x4render {
// IDs 1..3 are A/B/C from black. IDs 4..6 are A/B/C from white.
constexpr Shade relativeShade(uint8_t id, uint8_t luma) {
  return {luma, uint8_t(id >= 4), uint8_t(id == 0 || id == 7 ? 0 : 1 + (id - 1) % 3)};
}
constexpr uint8_t relativeShadeId(Shade shade) {
  return shade.selector ? uint8_t(shade.selector + 3 * shade.base) : uint8_t(shade.base ? 7 : 0);
}

// A/B/C from black, then A/B/C from white. Zero omits an interior
// shade. Curves are stored in ascending optical order between active shades.
struct RelativeGrayProfile {
  std::array<uint8_t, 6> luminance{};
  std::array<CoverageCurve, 7> coverage{};
  Palette palette() const;
};

inline Palette RelativeGrayProfile::palette() const {
  std::array<uint8_t, 6> order{{1, 2, 3, 4, 5, 6}};
  std::sort(order.begin(), order.end(), [&](uint8_t a, uint8_t b) {
    return luminance[a - 1] < luminance[b - 1];
  });
  Palette palette;
  palette.count = 1;
  for (const auto id : order) {
    const auto luma = luminance[id - 1];
    if (luma) palette.shades[palette.count++] = relativeShade(id, luma);
  }
  palette.shades[palette.count++] = relativeShade(7, 255);
  palette.coverage = coverage;
  const auto used = palette.count - 1;
  const bool unusedEmpty = std::all_of(coverage.begin() + used, coverage.end(),
                                      [](const auto& curve) { return curve == CoverageCurve{}; });
  palette.calibrated = palette.count > 2 && palette.valid() && unusedEmpty;
  return palette;
}

// Fit the brightness averaged over each swatch, in counts 0..16 of the
// lighter shade. Use a common linear-light scale for all 17 measurements.
inline std::optional<CoverageCurve> fitCoverage(const std::array<float, 17>& samples) {
  if (!std::all_of(samples.begin(), samples.end(), [](float v) { return std::isfinite(v); }) ||
      samples.back() <= samples.front())
    return std::nullopt;
  const double offset = samples.front(), span = double(samples.back()) - offset;
  struct Block { double sum; uint8_t count; };
  std::array<Block, 17> blocks{};
  size_t count = 0;
  for (const auto sample : samples) {
    blocks[count++] = {std::clamp((sample - offset) / span, 0.0, 1.0), 1};
    while (count > 1) {
      const auto& next = blocks[count - 1];
      auto& prev = blocks[count - 2];
      if (prev.sum * next.count <= next.sum * prev.count) break;
      prev.sum += next.sum;
      prev.count += next.count;
      --count;
    }
  }
  CoverageCurve curve{};
  size_t step = 0;
  for (size_t i = 0; i < count; ++i) {
    const auto value = uint8_t(std::lround(255 * blocks[i].sum / blocks[i].count));
    std::fill_n(curve.begin() + step, blocks[i].count, value);
    step += blocks[i].count;
  }
  return curve;
}

inline Palette measuredRelativePalette(const std::array<uint8_t, 6>& luminance,
                                       const std::array<CoverageCurve, 7>& coverage) {
  return RelativeGrayProfile{luminance, coverage}.palette();
}

inline Palette measuredStablePalette(const std::array<uint8_t, 3>& luminance) {
  Palette palette;
  palette.count = 5;
  for (uint8_t i = 0; i < 3; ++i) palette.shades[i+1] = {luminance[i], 0, uint8_t(i + 1)};
  std::sort(palette.shades.begin()+1, palette.shades.begin()+4,
            [](const auto& a, const auto& b) { return a.luma < b.luma; });
  palette.shades[4] = {255, 1, 0};
  palette.calibrated = palette.valid();
  return palette;
}
}  // namespace x4render
