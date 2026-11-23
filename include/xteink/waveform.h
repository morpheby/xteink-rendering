#pragma once
#include <array>
#include <cstddef>
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
constexpr uint8_t phaseCode(uint8_t rail, uint8_t frames) {
  return frames ? uint8_t((rail << 6) | frames) : 0;
}
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
  uint8_t frames{20}, repeats{1}, inversionFrames{20};
  bool conditioned{false};
  uint8_t activationFrames{2}, activationCycles{2}, referenceFrames{0},
      restFrames{3}, tailFrames{3}, whiteFrames{0}, restRail{0}, vcomRail{0};
  uint16_t pauseMs{0};
  bool factoryPrime{false};
  constexpr bool valid() const {
    return frames >= 1 && frames <= 63 && repeats >= 1 && repeats <= 16 &&
           inversionFrames <= 63 && activationFrames <= 8 &&
           activationCycles <= 4 && referenceFrames <= 63 && restFrames <= 16 &&
           tailFrames <= 16 && whiteFrames <= 63 && restRail <= 3 &&
           vcomRail <= 3 && pauseMs <= 1000;
  }
};
constexpr LutBank preparation(BwPreparation p, uint8_t holdRail = 0) {
  if (!p.valid() || holdRail > 3) return {};
  LutBank bank{};
  if (p.conditioned) {
    // Codes are experimental actions, NOT certified physical voltages.
    // All five rows have exactly aligned group/state durations. Preparation
    // drives every visible pixel; no previous-image RAM assumptions are used.
    const uint8_t white = p.whiteFrames ? p.whiteFrames : p.frames;
    const uint8_t longest = white > p.frames ? white : p.frames;
    for (uint8_t row = 0; row < 5; ++row) {
      auto& lut = bank[row];
      const uint8_t rest = row ? p.restRail : p.vcomRail;
      uint8_t base = 0;
      if (p.activationFrames && p.activationCycles) {
        lut[0] = p.activationCycles;
        lut[1] = uint8_t(((row ? 1 : p.vcomRail) << 6) | p.activationFrames);
        lut[2] = uint8_t(((row ? 2 : p.vcomRail) << 6) | p.activationFrames);
        lut[5] = lut[6] = 1;
        base += 7;
      }
      // A common white reference precedes target-dependent inversion/writing.
      if (p.referenceFrames || p.restFrames) {
        lut[base] = 1;
        lut[base + 1] = phaseCode(row ? 2 : p.vcomRail, p.referenceFrames);
        lut[base + 2] = phaseCode(rest, p.restFrames);
        lut[base + 5] = lut[base + 6] = 1;
        base += 7;
      }
      // The inversion/write pair alone is repeated, like the simple profile.
      lut[base] = p.referenceFrames ? 1 : p.repeats;
      const bool black = row == Row[0] || row == Row[2];
      const bool target = row != 0;
      const uint8_t rail = target ? (black ? 1 : 2) : rest;
      const uint8_t length = target ? (black ? p.frames : white) : longest;
      // After a white reference, black-target pixels are ALREADY at their
      // inverted endpoint. Hold them instead of another full white drive.
      // Without the reference, both targets need the full inversion pulse.
      const uint8_t inverse = black && p.referenceFrames ? rest :
                                  (target ? uint8_t(3 - rail) : rest);
      lut[base + 1] = phaseCode(inverse, p.inversionFrames);
      lut[base + 2] = phaseCode(rail, length);
      lut[base + 3] = phaseCode(rest, longest - length);
      lut[base + 4] = phaseCode(rest, p.tailFrames);
      lut[base + 5] = lut[base + 6] = 1;
      if (p.referenceFrames && p.repeats > 1) {
        // Later cycles begin at the requested image, so they need genuine
        // inversion for BOTH endpoints. Keep this group aligned in all rows.
        for (uint8_t i = 0; i < 7; ++i) lut[base + 7 + i] = lut[base + i];
        lut[base + 7] = p.repeats - 1;
        lut[base + 8] = phaseCode(target ? uint8_t(3 - rail) : rest,
                                  p.inversionFrames);
      }
    }
    return bank;
  }
  // Absolute B/W depends on NEW only: 00/10 -> black, 01/11 -> white.
  // Never let an unexpected OLD bit choose an idle row during preparation.
  // Drive every pixel toward the COMPLEMENT of its requested B/W value,
  // then drive the requested endpoint, then hold. This is an experimental
  // conditioning sequence, not a reconstruction of the factory OTP bank.
  for (auto& row : bank)
    row = held(p.inversionFrames + p.frames + 1, p.repeats, holdRail);
  bank[0] = held(p.inversionFrames + p.frames + 1, p.repeats, 0);
  for (uint8_t selector = 0; selector < 4; ++selector) {
    auto& row = bank[Row[selector]];
    row = {};
    row[0] = p.repeats;
    const uint8_t targetRail = (selector & 1) ? 2 : 1;
    row[1] = p.inversionFrames
                 ? uint8_t(((3 - targetRail) << 6) | p.inversionFrames)
                 : 0;
    row[2] = uint8_t((targetRail << 6) | p.frames);
    row[3] = uint8_t((holdRail << 6) | 1);
    row[5] = row[6] = 1;
  }
  return bank;
}
struct FactoryBwTuning {
  uint8_t shakeFrames = 0;
  uint8_t shakeCycles = 2;
  uint8_t whiteReference = 0;
  int8_t driveTrim = 0;
  int8_t pauseTrim = 0;
};

// The verified full B/W table family uses two groups, pair repeats of one,
// and two identical transition rows for each endpoint. Limit edits to that
// shape. Copy the original byte-for-byte when all adjustments are zero.
constexpr bool tuneFactoryBw(const LutBank& factory, FactoryBwTuning tuning, LutBank& result) {
  if (tuning.shakeFrames > 8 || tuning.shakeCycles > 4 ||
      tuning.whiteReference > 32 || tuning.driveTrim < -8 || tuning.driveTrim > 8 ||
      tuning.pauseTrim < -4 || tuning.pauseTrim > 8)
    return false;
  const auto& vcom = factory[0];
  const int inverse = vcom[1];
  const int pause = vcom[2];
  const int first = vcom[3];
  const int last = vcom[4];
  if (inverse != first + last || first == 0 || last == 0 || last != vcom[8] ||
      pause != 4 || inverse > 63 || factory[1] != factory[2] || factory[3] != factory[4])
    return false;
  const int adjustedInverse = inverse + tuning.driveTrim;
  const int adjustedFirst = first + tuning.driveTrim;
  const int adjustedPause = pause + tuning.pauseTrim;
  if (adjustedFirst < 1 || adjustedInverse > 63) return false;
  result = {};
  for (size_t row = 0; row < factory.size(); ++row) {
    const bool white = row == 1 || row == 2;
    const uint8_t toward = row == 0 ? 0 : white ? 2 : 1;
    const uint8_t away = row == 0 ? 0 : uint8_t(3 - toward);
    const uint8_t mid = white ? toward : 0;
    const uint8_t finish = white ? 0 : toward;
    const Lut expected{1, phaseCode(away, inverse), phaseCode(0, pause),
                       phaseCode(toward, first), phaseCode(mid, last), 1, 1,
                       1, phaseCode(finish, last), 0, 0, 0, 1, 1};
    if (factory[row] != expected) return false;
    auto& dest = result[row];
    size_t offset = 0;
    if (tuning.shakeFrames && tuning.shakeCycles) {
      dest[0] = tuning.shakeCycles;
      dest[1] = phaseCode(row ? 1 : 0, tuning.shakeFrames);
      dest[2] = phaseCode(row ? 2 : 0, tuning.shakeFrames);
      dest[5] = dest[6] = 1;
      offset += 7;
    }
    if (tuning.whiteReference) {
      dest[offset] = dest[offset + 5] = dest[offset + 6] = 1;
      // Balance the added white reference with an equal common dark pulse.
      dest[offset + 1] = phaseCode(row ? 1 : 0, tuning.whiteReference);
      dest[offset + 2] = phaseCode(row ? 2 : 0, tuning.whiteReference);
      offset += 7;
    }
    for (size_t byte = 0; byte < 14; ++byte) dest[offset + byte] = factory[row][byte];
    dest[offset + 1] = phaseCode(away, adjustedInverse);
    dest[offset + 2] = phaseCode(0, adjustedPause);
    dest[offset + 3] = phaseCode(toward, adjustedFirst);
  }
  return true;
}

static_assert(pulse(Pulse{})[2] == 0x84, "candidate must follow prefix");
static_assert(pulse(Pulse{2, 4, 6, 1, 1})[1] == 0x46, "darkening prefix first");
static_assert(!Pulse{1, 64, 0, 1, 0}.valid(), "phase overflow must fail");
}  // namespace x4render
