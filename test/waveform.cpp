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
  const auto prep = preparation({true, 63, 16}, 0);
  assert(prep[Row[2]][1] == 0x7F && prep[Row[3]][1] == 0xBF);
  for (auto row : prep) assert(counts(row) == 1024);
  assert(preparation({true, 0, 1}) == LutBank{});
  assert(preparation({true, 16, 0}) == LutBank{});
  std::cout << "waveform: opposite-first bytes, binary/hex inspection, aligned "
               "repeats and programmable B/W passed\n";
}
