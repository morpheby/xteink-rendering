#include <xteink/waveform_inspection.h>

#include <cassert>
#include <iostream>
using namespace x4render;
uint32_t counts(const Lut& row) {
  uint32_t total = 0;
  for (size_t b = 0; b < 49; b += 7)
    total += row[b] * (((row[b + 1] & 63) + (row[b + 2] & 63)) * row[b + 5] +
                       ((row[b + 3] & 63) + (row[b + 4] & 63)) * row[b + 6]);
  return total;
}
int main() {
  const auto bank = candidates(
      {Pulse{2, 2, 6, 1, 1}, Pulse{2, 4, 6, 1, 1}, Pulse{2, 6, 6, 1, 1}});
  const Lut a{1, 0x46, 0x82, 5, 0, 1, 1};
  const Lut vcom{1, 13, 0, 0, 0, 1, 1};
  assert(bank[Row[1]] == a && bank[0] == vcom && bank[4] == vcom);
  assert(lutTrace(a) == "g0 x1 s0 x1[01*6 10*2] s1 x1[00*5] ");
  assert(lutHex(a).size() == 98 && lutHex(a).substr(0, 14) == "01468205000101");
  assert(lutBinary(a).size() == 440 &&
         lutBinary(a).substr(0, 26) == "00000001 01000110 10000010");
  for (auto row : bank) assert(counts(row) == 13);
  // The common prefix and both repeat fields are included in every timeline.
  for (uint8_t rail : {uint8_t(1), uint8_t(2)})
    for (uint8_t opposite : {uint8_t(0), uint8_t(63)}) {
      auto b = candidates({Pulse{rail, 1, opposite, 16, 63},
                           Pulse{rail, 63, opposite, 16, 63},
                           Pulse{rail, 7, opposite, 16, 63}});
      for (auto row : b)
        assert(counts(row) == uint32_t(63 + opposite + 63) * 16);
      assert((b[Row[1]][2] >> 6) == rail);
      if (opposite)
        assert((b[Row[1]][1] >> 6) == 3 - rail);
      else
        assert(b[Row[1]][1] == 0);
    }
  assert(candidates({Pulse{2, 2, 6, 1, 1}, Pulse{2, 4, 7, 1, 1},
                     Pulse{2, 6, 6, 1, 1}}) == LutBank{});
  const auto prep = preparation({true, 63, 16, 63}, 0);
  assert(prep[Row[2]][1] == 0xBF && prep[Row[2]][2] == 0x7F);
  assert(prep[Row[3]][1] == 0x7F && prep[Row[3]][2] == 0xBF);
  assert(prep[Row[2]][3] == 1 && prep[Row[3]][3] == 1);
  for (auto row : prep) assert(counts(row) == 2032);
  const auto asymmetric = preparation({true, 37, 2, 5}, 0);
  assert(asymmetric[Row[2]][1] == 0x85 && asymmetric[Row[2]][2] == 0x65);
  assert(asymmetric[Row[3]][1] == 0x45 && asymmetric[Row[3]][2] == 0xA5);
  for (auto row : asymmetric) assert(counts(row) == 86);
  const auto legacy = preparation({true, 63, 16, 0}, 0);
  assert(legacy[Row[2]][1] == 0 && legacy[Row[2]][2] == 0x7F);
  for (auto row : legacy) assert(counts(row) == 1024);
  assert(preparation({true, 0, 1, 16}) == LutBank{});
  assert(preparation({true, 16, 0, 16}) == LutBank{});
  assert(preparation({true, 16, 1, 64}) == LutBank{});
  BwPreparation c;
  c.programmable = c.conditioned = true;
  auto cb = preparation(c);
  assert(cb[Row[2]][1] == 0x42 && cb[Row[2]][2] == 0x82);
  assert(cb[Row[2]][8] == 0x94 && cb[Row[2]][9] == 3);
  assert(cb[Row[2]][15] == 20 && cb[Row[2]][16] == 0x54);
  assert(cb[Row[3]][15] == 0x54 && cb[Row[3]][16] == 0x94);
  for (auto row : cb) assert(counts(row) == 74);
  // No holes/empty leading groups when disabling activation and reference.
  c.activationFrames = c.referenceFrames = c.restFrames = 0;
  cb = preparation(c);
  assert(cb[0][0] == 1 && cb[0][7] == 0);
  c.whiteFrames = 30;
  cb = preparation(c);
  assert(cb[Row[2]][3] == 10 && cb[Row[3]][3] == 0);
  for (auto row : cb) assert(counts(row) == 53);
  // Exercise aligned VCOM/source timings at parameter boundaries, asymmetric
  // endpoints and disabled stages. No row can end before another row.
  for (uint8_t activation : {uint8_t(0),uint8_t(8)})
    for (uint8_t cycles : {uint8_t(0),uint8_t(4)})
      for (uint8_t reference : {uint8_t(0),uint8_t(63)})
        for (uint8_t frames : {uint8_t(1),uint8_t(63)})
          for (uint8_t white : {uint8_t(0),uint8_t(1),uint8_t(63)}) {
            c.activationFrames = activation; c.activationCycles = cycles;
            c.referenceFrames = reference; c.frames = frames; c.whiteFrames = white;
            c.restFrames = c.tailFrames = 16;
            c.repeats = 16; c.inversionFrames = 63;
            c.vcomRail = 1; c.restRail = 3;
            const uint32_t target = white > frames ? white : frames;
            const uint32_t expected = 2*activation*cycles + reference + 16 + (63+target+16)*16;
            for (auto row : preparation(c)) assert(counts(row) == expected);
          }
  c.activationFrames = 9;
  assert(preparation(c) == LutBank{});
  std::cout << "waveform: opposite-first bytes, binary/hex inspection, aligned "
               "repeats and programmable B/W passed\n";
}
