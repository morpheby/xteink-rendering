#pragma once
#include "bw_strips.h"
#include "waveform.h"

namespace x4render {

// Absolute endpoints and the three gray selectors share the same update mask.
// For stable gray, the combined LUT supplies its own black start.
inline uint32_t encodeGrayPass(Frames& frames, bool settle, const BwZonePlan& zones = {},
                               uint8_t zone = 0) {
  frames.dtm1.clear();
  frames.dtm2.clear();
  const auto& settings = frames.current;
  std::array<ShadeMix, 256> tones;
  for (size_t i = 0; i < tones.size(); ++i) tones[i] = quantizeMix(i, settings);
  const auto& bounds = frames.expanded;
  uint32_t count = 0;
  for (uint16_t y = bounds.y; y < bounds.y + bounds.height; ++y)
    for (uint16_t x = bounds.x; x < bounds.x + bounds.width; ++x) {
      if (zones.count > 1 && zones.owners[x / 8] != zone) continue;
      const size_t i = size_t(y) * Width + x;
      if (!packedBit(frames.mask, i)) continue;
      const auto shade = tones[frames.target[i]].sample(x, y, settings.columnDither);
      uint8_t selector = shade.selector;
      if (!settle && (settings.mode == Mode::DitherSettle || !selector))
        selector = shade.base ? 1 : 2;
      else if (!settle)
        selector = 0;
      packedBit(frames.dtm1, i, selector & 2);
      packedBit(frames.dtm2, i, selector & 1);
      count += selector != 0;
    }
  return count;
}

inline BwZonePlan grayPassZones(Frames& frames, bool settle, unsigned railBudget, unsigned localBudget) {
  const auto& bounds = frames.expanded;
  const uint16_t left = bounds.x & ~7, right = (bounds.x + bounds.width + 7) & ~7;
  return planBwZones([&](BwZonePlan& plan, const BwColumnMask* active) {
    auto measure = [&](auto read) {
      plan.include(measureRailLoad(read, left, right, active, bounds.y, bounds.y + bounds.height));
    };
    const auto& first = frames.dtm1;
    const auto& second = frames.dtm2;
    if (!settle) {
      measure([&](size_t i) { return std::array<uint8_t, 2>{second[i], first[i]}; });
      return;
    }
    // At first all candidate selectors share the same rail. When G1 and G2
    // finish, successively smaller subsets are still drawing current.
    measure([&](size_t i) { return std::array<uint8_t, 2>{uint8_t(first[i] | second[i]), 0}; });
    measure([&](size_t i) { return std::array<uint8_t, 2>{first[i], 0}; });
    measure([&](size_t i) { return std::array<uint8_t, 2>{uint8_t(first[i] & second[i]), 0}; });
  }, railBudget, localBudget);
}

// Runs with the caller's exclusive transport lease. The caller confirms
// power-off before committing the new framebuffer.
class GrayRefresh {
 public:
  enum class Stage : uint8_t { Idle, PowerOn, Prepare, Hold, Settle, Done, Failed };
  explicit GrayRefresh(Frames& frames) : frames_(frames) {}

  bool plan(const LutBank& bw, const LutBank& settle, unsigned railBudget, unsigned localBudget) {
    bw_ = bw;
    bw_[Row[0]] = bw_[Row[3]] = bw_[0];
    settle_ = settle;
    zones_ = {};
    pass_ = 0;
    stage = Stage::Idle;
    for (size_t i = 0; i < zones_.size(); ++i) {
      if (!encodeGrayPass(frames_, i != 0)) continue;
      zones_[i] = grayPassZones(frames_, i != 0, railBudget, localBudget);
      if (!zones_[i].count) return false;
    }
    // Preserve the measured relative preparation/settle sequence when
    // settling fits in one pass. Masking lanes can only lower these scores.
    if (frames_.current.mode == Mode::DitherSettle && zones_[0].count > 1 && zones_[1].count == 1)
      zones_[1] = zones_[0];
    return true;
  }
  uint8_t preparationPasses() const { return zones_[0].count; }
  uint8_t settlePasses() const { return zones_[1].count; }
  bool finishesPreparationWithHold() const { return preparationPasses() > 1; }

  template <class Transport>
  bool start(Transport& transport, uint32_t now) {
    if (stage != Stage::Idle || frames_.phase != Phase::Planned || !transport.powerOn(now)) return false;
    stage = Stage::PowerOn;
    return true;
  }
  template <class Transport>
  bool poll(Transport& transport, uint32_t now) {
    if (stage == Stage::Failed || stage == Stage::Done) return true;
    const auto result = transport.poll(now);
    if (result == Transport::Result::Pending) return false;
    if (result != Transport::Result::Complete) return fail();
    if (stage == Stage::PowerOn) stage = Stage::Prepare;
    if (stage == Stage::Prepare) {
      if (runPass(transport, now, false)) return stage == Stage::Failed;
      if (finishesPreparationWithHold()) {
        frames_.dtm1.clear();
        frames_.dtm2.clear();
        stage = Stage::Hold;
        if (!activate(transport, now, false)) return fail();
        return false;
      }
    }
    if (stage != Stage::Settle) {
      pass_ = 0;
      stage = Stage::Settle;
      frames_.phase = Phase::Settling;
    }
    if (runPass(transport, now, true)) return stage == Stage::Failed;
    stage = Stage::Done;
    return true;
  }

  Stage stage{Stage::Idle};

 private:
  bool fail() {
    frames_.fail();
    stage = Stage::Failed;
    return true;
  }
  template <class Transport>
  bool runPass(Transport& transport, uint32_t now, bool settle) {
    const auto& zones = zones_[settle];
    while (pass_ < zones.count) {
      if (!encodeGrayPass(frames_, settle, zones, pass_++)) continue;
      if (!activate(transport, now, settle)) fail();
      return true;
    }
    return false;
  }
  template <class Transport>
  bool activate(Transport& transport, uint32_t now, bool settle) {
    constexpr typename Transport::Window full{0, 0, Width, Height};
    // NEW first. The controller may modify OLD when NEW is written.
    return transport.upload(0x13, {frames_.dtm2.data(), PlaneBytes}, full, 0) &&
           transport.upload(0x10, {frames_.dtm1.data(), PlaneBytes}, full, 0) &&
           transport.activate(settle ? settle_ : bw_, full, false, now, !settle);
  }

  Frames& frames_;
  LutBank bw_{}, settle_{};
  std::array<BwZonePlan, 2> zones_{};
  uint8_t pass_{0};
};
}  // namespace x4render
