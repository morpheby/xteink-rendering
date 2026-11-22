#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "rendering.h"
namespace x4render {
enum class Step : uint8_t { BlackCondition, BwPrepare, GraySettle };
template <size_t N>
struct Pipeline {
  std::array<Step, N> steps;
  constexpr bool valid() const {
    bool prepared = false, settled = false;
    for (size_t i = 0; i < N; ++i) {
      if (steps[i] == Step::BlackCondition && i != 0) return false;
      if (steps[i] == Step::BwPrepare) {
        if (prepared || settled) return false;
        prepared = true;
      }
      if (steps[i] == Step::GraySettle) {
        if (!prepared || settled) return false;
        settled = true;
      }
    }
    return prepared;
  }
};
constexpr Pipeline<1> Bw{{Step::BwPrepare}};
constexpr Pipeline<2> Single{{Step::BwPrepare, Step::GraySettle}};
constexpr Pipeline<2> Relative{{Step::BwPrepare, Step::GraySettle}};
constexpr Pipeline<3> FullGray{
    {Step::BlackCondition, Step::BwPrepare, Step::GraySettle}};
static_assert(Bw.valid() && Single.valid() && Relative.valid() &&
                  FullGray.valid(),
              "invalid refresh pipeline");
static_assert(!Pipeline<2>{{Step::GraySettle, Step::BwPrepare}}.valid(),
              "settle requires preparation");
// FullGray's first drive and all production gray recipes stay gated on physical
// calibration. This models composition without promoting unmeasured pulses.
}  // namespace x4render
