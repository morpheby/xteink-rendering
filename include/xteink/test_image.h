#pragma once
#include "rendering.h"
#include "landscape.h"

namespace x4render {
enum class TestImage : uint8_t { Calibration, White, Black, Reference, Random,
                                LandscapeDither, LandscapeGray, NeighborSweep, LinearRamp,
                                RampBase, RampInverse, LoadStripes, LoadCheckerboard,
                                LoadStrip, SplitCheckerboard, BwPhoto, BwChecker, BwBoiler };

// Leave space for a 96-column reference triplet and a 16-column margin on
// each side. Steps of 16 keep both edges of the centered strip byte aligned.
constexpr uint16_t LoadSwatchWidth = 32;
constexpr uint16_t LoadReferenceInset = 16;
constexpr uint16_t LoadReferenceWidth = 3 * LoadSwatchWidth;
constexpr uint16_t LoadStripMaxWidth = Width - 2 * (LoadReferenceWidth + LoadReferenceInset);
constexpr uint16_t LoadPatchX[][2] = {{16, 416}, {144, 544}, {280, 680}};
constexpr uint16_t LoadPatchY[] = {72, 208, 344};
inline bool isLoadPatch(uint16_t x, uint16_t y) {
  for (size_t band = 0; band < 3; ++band) {
    if (y < LoadPatchY[band] || y >= LoadPatchY[band] + 64) continue;
    for (auto left : LoadPatchX[band])
      if (x >= left && x < left + LoadReferenceWidth) return true;
  }
  return false;
}
constexpr bool usesBwTexture(TestImage image) {
  return image == TestImage::BwPhoto || image == TestImage::BwChecker || image == TestImage::BwBoiler;
}
constexpr uint16_t loadStripWidth(uint32_t requested) {
  return uint16_t((requested < LoadStripMaxWidth ? requested : LoadStripMaxWidth) / 16 * 16);
}
enum class StripPattern : uint8_t { Centered, Distributed, BurstOne, BurstEight, Jittered, Staggered };
constexpr StripPattern stripPattern(uint32_t argument) {
  return (argument >> 16) <= uint32_t(StripPattern::Staggered)
      ? StripPattern(argument >> 16) : StripPattern::Centered;
}
// A fixed seed makes these stimuli comparable across refreshes and drive modes.
constexpr uint32_t stripNoise(uint32_t index) {
  uint32_t x = index + 0x9e3779b9u;
  x = (x ^ (x >> 16)) * 0x7feb352du;
  x = (x ^ (x >> 15)) * 0x846ca68bu;
  return x ^ (x >> 16);
}
constexpr bool stripPhase(StripPattern pattern, uint16_t y) {
  if (pattern == StripPattern::BurstOne) return ((y + 1) / 2) & 1;
  if (pattern == StripPattern::BurstEight) return std::min(unsigned(y % 16), 8u) & 1;
  if (pattern == StripPattern::Jittered) {
    const bool early = (y & 1) && (y + 1 == Height || (stripNoise(y / 2) & 1));
    return (y / 2 + early) & 1;
  }
  return y & 1;
}

inline void staggeredStripRow(uint8_t* row, const uint8_t* previous, unsigned left,
                              unsigned bytes, uint16_t y, bool extra) {
  constexpr uint8_t pairChoices[] = {0xF0, 0xCC, 0xC3, 0x3C, 0x33, 0x0F};
  // Four B/W pairs per byte. Randomize which two switch early, then switch
  // the other two on the next row. Each pair always keeps one white pixel.
  const auto extraSeed = stripNoise(0x10000u + y);
  const unsigned extraByte = left + extraSeed % bytes;
  for (unsigned x = left; x < left + bytes; ++x) {
    const auto seed = stripNoise(unsigned((y - 1) / 2) * (Width / 8) + x);
    uint8_t switching = pairChoices[seed % 6];
    if (!(y & 1)) switching = uint8_t(~switching);
    if (extra && x == extraByte) {
      for (unsigned offset = 0; offset < 4; ++offset) {
        const uint8_t pair = 0xC0u >> (2 * (((extraSeed >> 16) + offset) % 4));
        if (switching & pair) continue;
        switching |= pair;
        break;
      }
    }
    row[x] = previous[x] ^ switching;
  }
}

// Original binary test artwork. Chunky shapes expose retained outlines without
// confusing intentional pixel noise with drive noise. The seed replays exactly.
constexpr bool isRampImage(TestImage image) {
  return image == TestImage::LinearRamp || image == TestImage::RampBase ||
         image == TestImage::RampInverse;
}

constexpr bool usesRelativeGray(TestImage image) {
  return image == TestImage::Reference || image == TestImage::Random ||
         image == TestImage::LandscapeGray || image == TestImage::NeighborSweep ||
         image == TestImage::LinearRamp;
}
constexpr bool grayPreparationImage(TestImage image) {
  return usesRelativeGray(image) || isRampImage(image);
}
constexpr bool measuresBwImage(TestImage image) {
  return usesBwTexture(image) || grayPreparationImage(image) || image == TestImage::LoadStrip;
}

inline void paintTestImage(Buffer& plane, TestImage image, uint32_t argument) {
  if (image == TestImage::SplitCheckerboard || image == TestImage::BwChecker)
    image = TestImage::LoadCheckerboard;
  if (image == TestImage::LoadStripes || image == TestImage::LoadCheckerboard ||
      image == TestImage::LoadStrip) {
    // Identical, row-balanced reference patches embedded in the load. Fixed
    // two-pixel stripes give a middle tone using only the B/W waveform.
    auto reference = [](uint8_t* row, uint16_t x) {
      constexpr size_t swatchBytes = LoadSwatchWidth / 8;
      auto* swatches = row + x / 8;
      std::memset(swatches, 0x00, swatchBytes);
      std::memset(swatches + swatchBytes, 0xCC, swatchBytes);
      std::memset(swatches + 2 * swatchBytes, 0xFF, swatchBytes);
    };
    const bool strip = image == TestImage::LoadStrip;
    const auto switchingWidth = strip ? loadStripWidth(argument & 0xFFFFu)
        : image == TestImage::LoadCheckerboard ? Width : 0;
    const size_t leftByte = (Width - switchingWidth) / 16;
    const auto pattern = strip ? stripPattern(argument) : StripPattern::Centered;
    std::array<uint8_t, Width / 8> spread{};
    if (pattern == StripPattern::Distributed) {
      constexpr unsigned pairs = (Width - 2 * LoadReferenceWidth) / 2;
      unsigned error = 0;
      for (uint16_t x = 0; x < Width; x += 2) {
        if ((x >= LoadReferenceInset && x < LoadReferenceInset + LoadReferenceWidth) ||
            (x >= Width - LoadReferenceInset - LoadReferenceWidth && x < Width - LoadReferenceInset)) continue;
        error += switchingWidth / 2;
        if (error < pairs) continue;
        error -= pairs;
        spread[x / 8] |= 0xC0u >> (x % 8);
      }
    }
    unsigned extra = 0;
    for (uint16_t y = 0; y < Height; ++y) {
      auto* row = plane.data() + size_t(y) * (Width / 8);
      std::memset(row, 0xAA, Width / 8);
      if (pattern == StripPattern::Staggered) {
        if (y && switchingWidth) {
          // There are 479 visible transitions. Spread the half-row that
          // rounds up in the burst probes so the total dose matches exactly.
          extra += switchingWidth / 4;
          staggeredStripRow(row, row - Width / 8, leftByte, switchingWidth / 8,
                            y, extra >= Height - 1);
          if (extra >= Height - 1) extra -= Height - 1;
        }
      } else if (stripPhase(pattern, y)) {
        if (pattern == StripPattern::Distributed)
          for (size_t x = 0; x < spread.size(); ++x) row[x] ^= spread[x];
        else
          std::memset(row + leftByte, 0x55, switchingWidth / 8);
      }
      if (strip) {
        // Full-height triplets avoid adding transitions at patch boundaries.
        reference(row, LoadReferenceInset);
        reference(row, Width - LoadReferenceInset - LoadReferenceWidth);
      }
    }
    if (strip) return;

    for (size_t band = 0; band < 3; ++band)
      for (uint16_t y = LoadPatchY[band]; y < LoadPatchY[band] + 64; ++y)
        for (uint16_t x : LoadPatchX[band])
          reference(plane.data() + size_t(y) * (Width / 8), x);
    return;
  }

  plane.clear(image == TestImage::Black ? 0 : 255);
  if (image == TestImage::White || image == TestImage::Black ||
      image == TestImage::LandscapeGray || image == TestImage::NeighborSweep ||
      image == TestImage::BwBoiler || isRampImage(image)) return;
  if (image == TestImage::LandscapeDither || image == TestImage::BwPhoto) {
    Settings bw;
    const uint16_t inset = image == TestImage::BwPhoto ? 0 : 20;
    for (uint16_t y = inset; y < Height - inset; ++y)
      for (uint16_t x = 0; x < Width; ++x) {
        const uint32_t i = uint32_t(y) * Width + x;
        const bool white = quantize(landscapeLuma(i), x, y, bw).base;
        packedBit(plane, i, white);
      }
    return;
  }
  auto rect = [&](int x, int y, int w, int h, bool white) {
    for (int py = y; py < y + h && py < Height; ++py)
      for (int px = x; px < x + w && px < Width; ++px)
        if (px >= 0 && py >= 0)
          packedBit(plane, size_t(py) * Width + px, white);
  };
  if (image == TestImage::Reference) {
    rect(20, 60, 230, 150, false);
    rect(270, 60, 230, 150, false);
    rect(280, 70, 210, 130, true);
    // Fine-to-coarse stripes and hard vertical/horizontal boundaries.
    for (int y = 60; y < 210; ++y)
      for (int x = 520; x < 780; ++x) {
        int period = 1 << ((x - 520) / 65);
        packedBit(plane, size_t(y) * Width + x, ((y / period) & 1) != 0);
      }
    for (int y = 240; y < 420; ++y)
      for (int x = 20; x < 310; ++x)
        packedBit(plane, size_t(y) * Width + x, ((x / 16 + y / 16) & 1) != 0);
    // Diagonal staircase / triangular edges, plus an isolated white stripe.
    for (int y = 240; y < 420; ++y)
      rect(340, y, (y - 240) * 220 / 180 + 1, 1, false);
    rect(415, 240, 7, 180, true);
  } else {
    uint32_t state = argument ? argument : 0x584434u;
    auto next = [&]() {
      state ^= state << 13;
      state ^= state >> 17;
      state ^= state << 5;
      return state;
    };
    for (int y = 55; y < 435; y += 38)
      for (int x = 20; x < 780; x += 40)
        rect(x, y, 40, 38, (next() & 1) != 0);
    // Irregular offset blocks add long and short non-grid edges.
    for (int i = 0; i < 18; ++i) {
      const int x = 20 + next() % 650, y = 65 + next() % 270;
      const int w = 25 + next() % 90, h = 15 + next() % 70;
      const bool white = (next() & 1) != 0;
      rect(x, y, w, h, white);
    }
    rect(600, 225, 180, 200, true);
  }
  // A recognizable house provides sloped roof, solid walls and window edges.
  for (int y = 245; y < 305; ++y) {
    const int half = (y - 245) * 75 / 60;
    rect(690 - half, y, 2 * half + 1, 1, false);
  }
  rect(630, 305, 120, 110, false);
  rect(650, 325, 25, 25, true);
  rect(705, 325, 25, 25, true);
  rect(680, 370, 25, 45, true);
  // A continuous outer border makes cropping and orientation visible.
  rect(10, 50, 780, 3, false);
  rect(10, 435, 780, 3, false);
  rect(10, 50, 3, 388, false);
  rect(787, 50, 3, 388, false);
}
// IDs 1..3 are A/B/C from black. IDs 4..6 are A/B/C from white.
constexpr Shade relativeShade(uint8_t id, uint8_t luma) {
  return {luma, uint8_t(id >= 4), uint8_t(id == 0 || id == 7 ? 0 : 1 + (id - 1) % 3)};
}
constexpr uint8_t relativeShadeId(Shade shade) {
  return shade.selector ? uint8_t(shade.selector + 3 * shade.base) : uint8_t(shade.base ? 7 : 0);
}
constexpr uint8_t NeighborTop = 78;
constexpr uint8_t NeighborRowHeight = 56;
constexpr uint8_t NeighborSwatchHeight = 43;
constexpr uint16_t neighborColumn(uint8_t step) {
  return uint16_t(unsigned(step) * Width / 17);
}
constexpr const char* shadeLabel(uint8_t id) {
  constexpr const char* labels[] = {"BLACK", "A BLACK", "B BLACK", "C BLACK",
                                    "A WHITE", "B WHITE", "C WHITE", "WHITE"};
  return labels[id];
}
inline Palette provisionalRelativePalette() {
  Palette palette;
  palette.count = 8;
  for (uint8_t i = 0; i < 8; ++i)
    palette.shades[i] = relativeShade(i, uint8_t(unsigned(i) * 255 / 7));
  return palette;
}

inline Palette measuredRelativePalette(const std::array<uint8_t, 6>& luminance,
                                       const std::array<CoverageCurve, 7>& coverage) {
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
  palette.calibrated = palette.count > 2 && palette.valid();
  return palette;
}

inline std::array<Shade, 2> relativeRampEndpoints(const Palette& palette) {
  std::array<Shade, 2> ends{{palette.shades[0], palette.shades[palette.count - 1]}};
  for (uint8_t i = 1; i + 1 < palette.count; ++i) {
    const auto shade = palette.shades[i];
    if (relativeShadeId(shade) == 1) ends[0] = shade;
    if (relativeShadeId(shade) == 6) ends[1] = shade;
  }
  if (ends[0].luma >= ends[1].luma)
    return {palette.shades[0], palette.shades[palette.count - 1]};
  return ends;
}

// Identical coordinates and quantizer for both passes. Each emitted shade
// carries its B/W starting pixel AND the matching settle selector.
template <class Emit>
void paintRelativeMixes(TestImage image, uint32_t seed, const Settings& settings, Emit emit) {
  auto patch = [&](uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t luma) {
    const auto mix = quantizeMix(luma, settings);
    for (uint16_t py = y; py < y + h; ++py)
      for (uint16_t px = x; px < x + w; ++px)
        emit(px, py, mix);
  };
  switch (image) {
    case TestImage::LandscapeGray:
      for (uint16_t y = 20; y < 460; ++y)
        for (uint16_t x = 0; x < Width; ++x)
          emit(x, y, quantizeMix(landscapeLuma(uint32_t(y) * Width + x), settings));
      break;
    case TestImage::Reference:
      for (uint8_t i = 0; i < 12; ++i)
        patch(20 + 20 * i, 222, 20, 16, uint8_t(255u * i / 11));
      patch(285, 81, 92, 105, 72);
      patch(385, 81, 98, 105, 182);
      patch(638, 313, 10, 92, 104);
      break;
    case TestImage::Random:
      for (uint8_t i = 0; i < 12; ++i) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        patch(25 + (i % 6) * 90, 62 + (i / 6) * 191, 41 + seed % 25,
              47 + (seed >> 8) % 50, uint8_t(42 + (seed >> 16) % 172));
      }
      break;
    case TestImage::LinearRamp:
    case TestImage::RampBase:
    case TestImage::RampInverse: {
      const auto ends = relativeRampEndpoints(settings.palette);
      const auto left = ends[0].luma, right = ends[1].luma;
      Settings bw;
      for (uint16_t x = 0; x < Width; ++x) {
        const uint8_t luma = uint8_t((unsigned(left) * (Width - 1 - x) +
                                      unsigned(right) * x + (Width - 1) / 2) / (Width - 1));
        const auto grayMix = quantizeMix(luma, settings), bwMix = quantizeMix(luma, bw);
        for (uint16_t y = 76; y < 258; ++y) emit(x, y, grayMix);
        for (uint16_t y = 278; y < 460; ++y) emit(x, y, bwMix);
      }
      break;
    }
    case TestImage::NeighborSweep:
      // One raw sample of every endpoint, in fixed identity order for sorting.
      for (uint8_t raw = 0; raw < 8; ++raw) {
        const auto shade = relativeShade(raw, 0);
        for (uint16_t y = 28; y < 56; ++y)
          for (uint16_t x = raw * 100; x < (raw + 1) * 100; ++x)
            emit(x, y, ShadeMix{shade, shade, 0});
      }
      for (uint8_t pair = 0; pair + 1 < settings.palette.count; ++pair) {
        const auto dark = settings.palette.shades[pair], light = settings.palette.shades[pair + 1];
        const uint16_t top = NeighborTop + pair * NeighborRowHeight;
        for (uint8_t step = 0; step <= 16; ++step)
          for (uint16_t y = top; y < top + NeighborSwatchHeight; ++y)
            for (uint16_t x = neighborColumn(step); x < neighborColumn(step + 1); ++x)
              emit(x, y, ShadeMix{dark, light, step});
      }
      break;
    default:
      break;
  }
}
template <class Emit>
void paintRelativeShades(TestImage image, uint32_t seed, const Settings& settings, Emit emit) {
  paintRelativeMixes(image, seed, settings, [&](uint16_t x, uint16_t y, ShadeMix mix) {
    emit(x, y, mix.sample(x, y, settings.columnDither));
  });
}
}  // namespace x4render
