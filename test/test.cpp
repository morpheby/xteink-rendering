#include <xteink/pipeline.h>
#include <xteink/rendering.h>
#include <xteink/waveform.h>

#include <cassert>
#include <iostream>
using namespace x4render;
Settings gray(Mode m) {
  Settings s;
  s.mode = m;
  s.palette.count = 5;
  s.palette.calibrated = true;
  s.palette.shades = {
      {{0, 0, 0}, {60, 1, 1}, {120, 1, 2}, {180, 1, 3}, {255, 1, 0}}};
  return s;
}
void finish(Frames& f) {
  f.preparation();
  assert(f.prepared(true));
  if (f.current.mode != Mode::BwDither) assert(f.settle());
  assert(f.commit());
}
int main() {
  Frames f;
  assert(f.allocate());
  Settings bw;
  assert(f.plan(bw));
  assert(f.driven == Pixels);
  finish(f);
  assert(f.plan(bw));
  assert(f.driven == 0);
  f.draw(10 * Width + 10, 0);
  f.draw(10 * Width + 700, 0);
  assert(f.plan(bw));
  assert(f.driven == 2);
  f.preparation();
  assert(!packedBit(f.dtm1, 10 * Width + 300) &&
         !packedBit(f.dtm2, 10 * Width + 300));
  assert(!f.draw(0, 0));
  assert(f.displayed[10 * Width + 10] == 255);
  assert(f.prepared(true));
  assert(f.commit());
  f.draw(0, 0);
  assert(f.plan(bw));
  f.preparation();
  f.prepared(false);
  assert(f.knowledge == Knowledge::Interrupted && f.displayed[0] == 255);
  assert(f.plan(bw) && f.driven == Pixels);
  finish(f);
  assert(f.draw(12, 171) && f.beginAbsoluteBw());
  assert(!f.draw(12, 5) && !f.beginAbsoluteBw());
  assert(!f.prepared(false) && f.displayed[12] == 255);
  assert(f.beginAbsoluteBw() && f.prepared(true) && f.commit());
  assert(f.displayed[12] == 171);
  for (const auto mode : {Mode::SingleSettle, Mode::DitherSettle}) {
    Frames padded;
    auto settings = gray(mode);
    assert(padded.allocate() && padded.plan(settings));
    finish(padded);
    for (const uint8_t radius : {1, 2}) {
      settings.margin = radius;
      assert(padded.plan(settings) && !padded.driven);
      assert(padded.commit());
      const auto value = radius == 1 ? 0 : 255;
      assert(padded.draw(0, value) && padded.draw(10 * Width + 20, value));
      assert(padded.plan(settings));
      // One corner kernel clips to the panel, and the other stays separate.
      const unsigned side = 2 * radius + 1;
      assert(padded.driven == (radius + 1u) * (radius + 1u) + side * side);
      assert(padded.requested.width == 21 && padded.requested.height == 11);
      assert(padded.expanded.width == 21 + radius && padded.expanded.height == 11 + radius);
      assert(packedBit(padded.mask, size_t(radius) * Width + radius));
      assert(packedBit(padded.mask, size_t(10 + radius) * Width + 20 + radius));
      assert(!packedBit(padded.mask, 5 * Width + 10));
      assert(!packedBit(padded.mask, Width - 1));
      finish(padded);
    }
  }
  auto s = gray(Mode::DitherSettle);
  assert(s.palette.valid());
  assert(f.plan(s));
  finish(f);
  // Connected old gray survives removal/movement, including a component more
  // than one raster traversal long. White gap is never driven by its bbox.
  for (size_t x = 100; x < 180; ++x) f.draw(Width * 100 + x, 120);
  assert(f.plan(s));
  finish(f);
  f.draw(Width * 100 + 100, 255);
  f.draw(Width * 100 + 300, 60);
  assert(f.plan(s));
  assert(packedBit(f.mask, Width * 100 + 179));
  assert(!packedBit(f.mask, Width * 100 + 250));
  assert(f.driven == 81);
  finish(f);
  // Same palette revision with changed measured levels still invalidates.
  s.palette.shades[1].luma = 61;
  assert(f.plan(s));
  assert(f.driven == Pixels);
  finish(f);
  for (uint16_t x = 0; x < Width; ++x)
    for (uint8_t v = 0; v < 255; ++v)
      assert(same(quantize(v, x, 9, s), quantize(v, x, 9, s)));
  // A measured flat response drops redundant duty steps and changes the
  // quantization on the very same neighboring shade pair.
  auto curved = s;
  curved.palette.coverage[0] = {0, 0, 0, 0, 0, 0, 0, 0, 128, 255, 255,
                                255, 255, 255, 255, 255, 255};
  assert(curved.palette.valid() && !curved.equivalent(s));
  auto lighterCount = [](const Settings& settings, uint8_t luma) {
    unsigned lit = 0;
    for (uint16_t y = 0; y < 4; ++y)
      for (uint16_t x = 0; x < 4; ++x)
        lit += same(quantize(luma, x, y, settings), settings.palette.shades[1]);
    return lit;
  };
  assert(lighterCount(s, 46) == 12);
  assert(lighterCount(curved, 46) == 9);
  assert(lighterCount(curved, 61) == 16);
  auto columns = s;
  columns.columnDither = true;
  assert(!columns.equivalent(s));
  for (uint8_t count = 0; count <= 16; ++count) {
    unsigned selected = 0;
    for (uint16_t x = 0; x < 16; ++x) {
      const auto next = mixNeighbors(s.palette.shades[0], s.palette.shades[1], count, x, 4, true);
      assert(same(next, mixNeighbors(s.palette.shades[0], s.palette.shades[1], count, x, 5, true)));
      selected += same(next, s.palette.shades[1]);
    }
    assert(selected == count);
  }
  curved.palette.coverage[0][3] = 1;
  assert(!curved.palette.valid());
  s.palette.calibrated = false;
  assert(!f.plan(s));
  const auto bank = candidates(
      {Pulse{1, 2, 0, 1, 1}, Pulse{1, 4, 0, 1, 1}, Pulse{1, 6, 0, 1, 1}});
  for (auto row : bank) {
    int frames = 0;
    for (int i = 1; i <= 4; ++i) frames += row[i] & 63;
    assert(frames == 7);
  }
  assert((bank[4][1] >> 6) ==
         0);  // hold is a raw code under test, not a zero-field assertion
  std::cout << "rendering: history, failures, masks, old/new connectivity, "
               "palette, waveform timing passed\n";
}
