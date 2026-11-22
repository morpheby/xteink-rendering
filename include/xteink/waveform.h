#pragma once
#include <array>
#include <cstdint>
namespace x4render {
using Lut = std::array<uint8_t, 49>;
using LutBank = std::array<Lut, 5>;
// Experimental UC8279 seven-byte groups. These are the exact programmed bytes;
// interpreted timelines do not certify measured voltages or matching-silicon
// repeat/end semantics. Panel observations: rail 1 darkens, 2 brightens, 0
// holds.
struct Pulse {
  uint8_t rail{2}, frames{4}, opposite{0}, repeats{1}, neutral{1};
  constexpr bool valid() const {
    return rail >= 1 && rail <= 2 && frames >= 1 && frames <= 63 &&
           opposite <= 63 && repeats >= 1 && repeats <= 16 && neutral <= 63;
  }
};
// Common opposite prefix, THEN the candidate pulse. With rail=2 this means
// darkening prefix followed by A/B/C brightening doses.
constexpr Lut pulse(Pulse p, uint8_t holdRail = 0) {
  Lut out{};
  if (!p.valid() || holdRail > 3) return out;
  out[0] = p.repeats;
  out[1] = p.opposite ? uint8_t(((3 - p.rail) << 6) | p.opposite) : 0;
  out[2] = uint8_t((p.rail << 6) | p.frames);
  out[3] = uint8_t((holdRail << 6) | p.neutral);
  out[5] = out[6] = 1;
  return out;
}
constexpr Lut held(uint16_t duration, uint8_t repeats, uint8_t rail = 0) {
  Lut out{};
  if (duration > 252 || rail > 3 || !repeats) return out;
  out[0] = repeats;
  out[5] = out[6] = 1;
  for (uint8_t phase = 1; phase <= 4; ++phase) {
    const uint8_t n = duration > 63 ? 63 : uint8_t(duration);
    out[phase] = uint8_t((rail << 6) | n);
    duration -= n;
  }
  return out;
}
// Direct wire selectors, no inversion: 00->R24, 01->R22, 10->R23, 11->R21.
constexpr uint8_t Row[4] = {4, 2, 3, 1};
constexpr LutBank candidates(std::array<Pulse, 3> p, uint8_t holdRail = 0) {
  LutBank out{};
  uint16_t longest = 1;
  for (auto v : p) {
    if (!v.valid() || v.rail != p[0].rail || v.opposite != p[0].opposite ||
        v.repeats != p[0].repeats || v.neutral != p[0].neutral || holdRail > 3)
      return {};
    if (v.frames + v.opposite + v.neutral > longest)
      longest = v.frames + v.opposite + v.neutral;
  }
  for (uint8_t s = 0; s < 3; ++s) {
    auto& lut = out[Row[s + 1]];
    lut = pulse(p[s], holdRail);
    uint16_t remaining = longest - p[s].frames - p[s].opposite;
    for (uint8_t phase = 3; phase <= 4; ++phase) {
      const uint8_t n = remaining > 63 ? 63 : uint8_t(remaining);
      lut[phase] = uint8_t((holdRail << 6) | n);
      remaining -= n;
    }
  }
  out[0] = held(longest, p[0].repeats, 0);  // VCOM stays at code 00
  out[4] = held(longest, p[0].repeats, holdRail);
  return out;
}
struct BwPreparation {
  bool programmable{false};
  uint8_t frames{16}, repeats{1}, inversionFrames{16};
  constexpr bool valid() const {
    return frames >= 1 && frames <= 63 && repeats >= 1 && repeats <= 16 &&
           inversionFrames <= 63;
  }
};
constexpr LutBank preparation(BwPreparation p, uint8_t holdRail = 0) {
  if (!p.valid() || holdRail > 3) return {};
  LutBank bank{};
  // B/W screen uses DTM1=1 and DTM2=image: black=10, white=11.
  // Drive every pixel toward the COMPLEMENT of its requested B/W value,
  // then drive the requested endpoint, then hold. This is an experimental
  // conditioning sequence, not a reconstruction of the factory OTP bank.
  for (auto& row : bank)
    row = held(p.inversionFrames + p.frames + 1, p.repeats, holdRail);
  bank[0] = held(p.inversionFrames + p.frames + 1, p.repeats, 0);
  for (uint8_t selector : {uint8_t(2), uint8_t(3)}) {
    auto& row = bank[Row[selector]];
    row = {};
    row[0] = p.repeats;
    const uint8_t targetRail = selector == 2 ? 1 : 2;
    row[1] = p.inversionFrames
                 ? uint8_t(((3 - targetRail) << 6) | p.inversionFrames)
                 : 0;
    row[2] = uint8_t((targetRail << 6) | p.frames);
    row[3] = uint8_t((holdRail << 6) | 1);
    row[5] = row[6] = 1;
  }
  return bank;
}
static_assert(pulse(Pulse{})[2] == 0x84, "candidate must follow prefix");
static_assert(pulse(Pulse{2, 4, 6, 1, 1})[1] == 0x46, "darkening prefix first");
static_assert(!Pulse{1, 64, 0, 1, 0}.valid(), "phase overflow must fail");
}  // namespace x4render
