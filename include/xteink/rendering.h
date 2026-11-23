#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace x4render {
constexpr uint16_t Width = 800, Height = 480;
constexpr size_t Pixels = size_t(Width) * Height, PlaneBytes = Pixels / 8;
struct Free {
  void operator()(uint8_t* p) const { std::free(p); }
};
class Buffer {
 public:
  bool allocate(size_t size) {
    if (storage_) return size == size_;
#ifdef ESP_PLATFORM
    storage_.reset(static_cast<uint8_t*>(
        heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
#else
    storage_.reset(static_cast<uint8_t*>(std::malloc(size)));
#endif
    if (!storage_) return false;
    size_ = size;
    clear();
    return true;
  }
  uint8_t* data() { return storage_.get(); }
  const uint8_t* data() const { return storage_.get(); }
  size_t size() const { return size_; }
  void clear(uint8_t value = 0) {
    if (storage_) std::memset(data(), value, size_);
  }
  uint8_t& operator[](size_t i) { return data()[i]; }
  uint8_t operator[](size_t i) const { return data()[i]; }

 private:
  std::unique_ptr<uint8_t, Free> storage_;
  size_t size_{0};
};
struct Rect {
  uint16_t x{0}, y{0}, width{0}, height{0};
  bool empty() const { return !width || !height; }
  bool contains(uint16_t px, uint16_t py) const {
    return px >= x && py >= y && px < x + width && py < y + height;
  }
  void include(uint16_t px, uint16_t py) {
    if (empty()) {
      x = px;
      y = py;
      width = height = 1;
      return;
    }
    uint16_t x1 = std::max<uint16_t>(x + width, px + 1),
             y1 = std::max<uint16_t>(y + height, py + 1);
    x = std::min(x, px);
    y = std::min(y, py);
    width = x1 - x;
    height = y1 - y;
  }
  Rect fullLines() const {
    return empty() ? Rect{} : Rect{0, y, Width, height};
  }
};
enum class Mode : uint8_t { BwDither, SingleSettle, DitherSettle };
enum class Knowledge : uint8_t { Unknown, Expected, Known, Interrupted };
enum class Phase : uint8_t {
  Idle,
  Planned,
  Preparing,
  Prepared,
  Settling,
  Complete,
  Failed
};
struct Shade {
  uint8_t luma, base, selector;
};
// Optical progress from the darker endpoint (0) to the lighter one (255)
// at each of the 17 Bayer coverage counts. All zero means a linear pair.
// Equal interior values allow visually redundant coverage counts to collapse.
using CoverageCurve = std::array<uint8_t, 17>;
struct Palette {
  std::array<Shade, 8> shades{{{0, 0, 0}, {255, 1, 0}}};
  uint8_t count{2};
  uint32_t revision{0};
  bool calibrated{false};
  std::array<CoverageCurve, 7> coverage{};
  bool valid() const {
    if (count < 2 || count > 8 || shades[0].luma != 0 ||
        shades[count - 1].luma != 255)
      return false;
    for (uint8_t i = 0; i < count; ++i) {
      if (shades[i].base > 1 || shades[i].selector > 3 ||
          (i && shades[i - 1].luma >= shades[i].luma))
        return false;
      if (i) {
        const auto& curve = coverage[i - 1];
        if (curve.back() == 0) {
          if (std::any_of(curve.begin(), curve.end(), [](uint8_t v) { return v != 0; }))
            return false;
        } else if (curve.front() != 0 || curve.back() != 255 ||
                   !std::is_sorted(curve.begin(), curve.end())) {
          return false;
        }
      }
    }
    return shades[0].base == 0 && shades[0].selector == 0 &&
           shades[count - 1].base == 1 && shades[count - 1].selector == 0;
  }
};
struct Settings {
  Mode mode{Mode::BwDither};
  Palette palette{};
  uint8_t margin{0};
  bool columnDither{false};
  bool equivalent(const Settings& b) const {
    if (mode != b.mode || columnDither != b.columnDither || palette.revision != b.palette.revision ||
        palette.count != b.palette.count || palette.coverage != b.palette.coverage)
      return false;
    for (uint8_t i = 0; i < palette.count; ++i)
      if (palette.shades[i].luma != b.palette.shades[i].luma ||
          palette.shades[i].base != b.palette.shades[i].base ||
          palette.shades[i].selector != b.palette.shades[i].selector)
        return false;
    return true;
  }
};
constexpr uint8_t Bayer[16] = {0, 8,  2, 10, 12, 4, 14, 6,
                               3, 11, 1, 9,  15, 7, 13, 5};
constexpr uint8_t bayerRank(uint16_t x, uint16_t y) {
  return Bayer[(y % 4) * 4 + x % 4];
}
constexpr uint8_t columnRank(uint16_t x, uint8_t bits) {
  uint8_t rank = 0;
  for (uint8_t i = 0; i < bits; ++i) rank = uint8_t((rank << 1) | ((x >> i) & 1));
  return rank;
}
constexpr uint8_t ditherRank(uint16_t x, uint16_t y, bool columns) {
  return columns ? columnRank(x, 4) : bayerRank(x, y);
}
constexpr Shade mixNeighbors(Shade dark, Shade light, uint8_t lightCount,
                             uint16_t x, uint16_t y, bool columns = false) {
  return ditherRank(x, y, columns) < lightCount ? light : dark;
}
inline uint8_t coverageCount(unsigned distance, unsigned interval, const CoverageCurve& curve) {
  if (!distance) return 0;
  if (distance >= interval) return 16;
  if (!curve.back())
    return uint8_t((32 * distance + interval - 1) / (2 * interval));
  // Compare in the original interval's units. Avoid rounding the normalized
  // target before finding the closest optically measured coverage step.
  const int desired = int(255 * distance);
  uint8_t nearest = 0;
  int error = desired;
  for (uint8_t i = 1; i <= 16; ++i) {
    const int next = std::abs(int(curve[i] * interval) - desired);
    if (next < error) {
      nearest = i;
      error = next;
    }
  }
  return nearest;
}
inline Shade quantize(uint8_t luma, uint16_t x, uint16_t y, const Settings& s) {
  if (s.mode == Mode::BwDither) {
    const bool white = unsigned(luma) * 32 > unsigned(2 * ditherRank(x, y, s.columnDither) + 1) * 255;
    return {uint8_t(white ? 255 : 0), uint8_t(white), 0};
  }
  for (uint8_t i = 1; i < s.palette.count; ++i) {
    const auto a = s.palette.shades[i - 1], b = s.palette.shades[i];
    if (luma <= b.luma)
      return mixNeighbors(a, b, coverageCount(luma - a.luma, b.luma - a.luma,
                                               s.palette.coverage[i - 1]), x, y, s.columnDither);
  }
  return s.palette.shades[s.palette.count - 1];
}
inline bool same(Shade a, Shade b) {
  return a.base == b.base && a.selector == b.selector;
}
inline void packedBit(Buffer& b, size_t i, bool value) {
  const uint8_t mask = uint8_t(0x80u >> (i % 8));
  if (value)
    b[i / 8] |= mask;
  else
    b[i / 8] &= uint8_t(~mask);
}
inline bool packedBit(const Buffer& b, size_t i) {
  return b[i / 8] & (0x80u >> (i % 8));
}
// Two 8-bit frames; mask and two reusable wire planes. No quantized history.
// Draws are rejected while a transaction borrows the target, so async lifetimes
// cannot commit a newer caller frame. Both successful passes precede commit.
class Frames {
 public:
  bool allocate() {
    bool ok = displayed.allocate(Pixels) && target.allocate(Pixels) &&
              mask.allocate(PlaneBytes) && dtm1.allocate(PlaneBytes) &&
              dtm2.allocate(PlaneBytes);
    if (ok) {
      target.clear(255);
      displayed.clear(255);
    }
    return ok;
  }
  bool draw(size_t i, uint8_t v) {
    if (i >= Pixels || busy()) return false;
    target[i] = v;
    return true;
  }
  bool busy() const {
    return phase == Phase::Planned || phase == Phase::Preparing ||
           phase == Phase::Prepared || phase == Phase::Settling;
  }
  bool plan(Settings settings) {
    if (busy() || !target.data() || !settings.palette.valid() ||
        (settings.mode != Mode::BwDither && !settings.palette.calibrated))
      return false;
    current = settings;
    mask.clear();
    requested = {};
    expanded = {};
    driven = 0;
    const bool resync =
        knowledge != Knowledge::Known || !current.equivalent(previous);
    for (uint16_t y = 0; y < Height; ++y)
      for (uint16_t x = 0; x < Width; ++x) {
        const size_t i = size_t(y) * Width + x;
        if (resync || !same(quantize(displayed[i], x, y, previous),
                            quantize(target[i], x, y, current))) {
          packedBit(mask, i, true);
          requested.include(x, y);
        }
      }
    // Margin includes only a small user-selected neighborhood; full-line
    // padding never broadens this drive mask.
    if (!requested.empty() && settings.margin) {
      std::memcpy(dtm1.data(), mask.data(), PlaneBytes);
      for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x)
          if (packedBit(dtm1, size_t(y) * Width + x))
            for (int dy = -settings.margin; dy <= settings.margin; ++dy)
              for (int dx = -settings.margin; dx <= settings.margin; ++dx)
                if (x + dx >= 0 && x + dx < Width && y + dy >= 0 &&
                    y + dy < Height)
                  packedBit(mask, size_t(y + dy) * Width + x + dx, true);
    }
    if (settings.mode == Mode::DitherSettle) {
      // Fixed-point closure on the union of OLD and NEW gray connectivity.
      // A bounded scan avoids a full-screen queue or queue overflow fallback.
      bool grew;
      do {
        grew = false;
        for (uint16_t y = 0; y < Height; ++y)
          for (uint16_t x = 0; x < Width; ++x) {
            size_t i = size_t(y) * Width + x;
            if (packedBit(mask, i)) continue;
            const auto old = quantize(displayed[i], x, y, previous),
                       next = quantize(target[i], x, y, current);
            if (!old.selector && !next.selector) continue;
            if ((x && packedBit(mask, i - 1)) ||
                (x + 1 < Width && packedBit(mask, i + 1)) ||
                (y && packedBit(mask, i - Width)) ||
                (y + 1 < Height && packedBit(mask, i + Width)) ||
                (x && y && packedBit(mask, i - Width - 1)) ||
                (x + 1 < Width && y && packedBit(mask, i - Width + 1)) ||
                (x && y + 1 < Height && packedBit(mask, i + Width - 1)) ||
                (x + 1 < Width && y + 1 < Height &&
                 packedBit(mask, i + Width + 1))) {
              packedBit(mask, i, true);
              grew = true;
            }
          }
      } while (grew);
    }
    for (uint16_t y = 0; y < Height; ++y)
      for (uint16_t x = 0; x < Width; ++x)
        if (packedBit(mask, size_t(y) * Width + x)) {
          expanded.include(x, y);
          ++driven;
        }
    phase = driven ? Phase::Planned : Phase::Complete;
    return true;
  }
  void preparation() {
    dtm1.clear();
    dtm2.clear();
    for (uint16_t y = 0; y < Height; ++y)
      for (uint16_t x = 0; x < Width; ++x) {
        const size_t i = size_t(y) * Width + x;
        const auto q = quantize(target[i], x, y, current);
        // Raw selector 00=hold, 01=black, 10=white for absolute preparation.
        uint8_t selector =
            packedBit(mask, i)
                ? uint8_t(q.selector && current.mode == Mode::SingleSettle ? 2
                          : q.base                                         ? 2
                                                                           : 1)
                : 0;
        packedBit(dtm1, i, selector & 2);
        packedBit(dtm2, i, selector & 1);
      }
    phase = Phase::Preparing;
  }
  bool prepared(bool success) {
    if (phase != Phase::Preparing) return false;
    if (!success) {
      fail();
      return false;
    }
    phase = Phase::Prepared;
    return true;
  }
  bool beginAbsoluteBw() {
    if (busy() || !target.data()) return false;
    current = {};
    phase = Phase::Preparing;
    return true;
  }
  bool settle() {
    if (phase != Phase::Prepared) return false;
    dtm1.clear();
    dtm2.clear();
    for (uint16_t y = 0; y < Height; ++y)
      for (uint16_t x = 0; x < Width; ++x) {
        size_t i = size_t(y) * Width + x;
        uint8_t s = packedBit(mask, i)
                        ? quantize(target[i], x, y, current).selector
                        : 0;
        packedBit(dtm1, i, s & 2);
        packedBit(dtm2, i, s & 1);
      }
    phase = Phase::Settling;
    return true;
  }
  bool commit() {
    if (phase != Phase::Settling &&
        !(phase == Phase::Prepared && current.mode == Mode::BwDither))
      return false;
    std::memcpy(displayed.data(), target.data(), Pixels);
    previous = current;
    phase = Phase::Complete;
    knowledge = Knowledge::Known;
    return true;
  }
  void fail() {
    phase = Phase::Failed;
    knowledge = Knowledge::Interrupted;
  }
  void resetController() {
    phase = Phase::Idle;
    knowledge = Knowledge::Unknown;
  }
  Buffer displayed, target, mask, dtm1, dtm2;
  Settings previous{}, current{};
  Knowledge knowledge{Knowledge::Unknown};
  Phase phase{Phase::Idle};
  Rect requested{}, expanded{};
  uint32_t driven{0};
};
}  // namespace x4render
