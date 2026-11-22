#pragma once
#include "rendering.h"

namespace x4render {
enum class TestImage : uint8_t { Calibration, White, Black, Reference, Random };

// Original binary test artwork. Chunky shapes expose retained outlines without
// confusing intentional pixel noise with drive noise. The seed replays exactly.
inline void paintTestImage(Buffer& plane, TestImage image, uint32_t seed) {
  plane.clear(image == TestImage::Black ? 0 : 255);
  if (image == TestImage::White || image == TestImage::Black) return;
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
    uint32_t state = seed ? seed : 0x584434u;
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
}  // namespace x4render
