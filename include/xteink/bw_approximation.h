#pragma once
#include "rendering.h"

#include <type_traits>

namespace x4render {

enum class BwTexture : uint8_t { Bayer, Columns, Repair, OriginalZoned };
using BwColumnMask = std::array<uint8_t, Width / 8>;

constexpr bool columnDither(uint8_t luma, uint16_t x) {
  return unsigned(luma) * 128 > unsigned(2 * columnRank(x, 6) + 1) * 255;
}

struct BwRailLoad {
  uint16_t peak{0}, localPeak{0}, recentPeak{0}, sustainedPeak{0};
  uint32_t total{0};
};
struct BwLoad {
  uint16_t peak{0}, localPeak{0}, recentPeak{0}, sustainedPeak{0};
  uint16_t wholeRecentPeak{0};
  uint16_t firstWhite{0}, lastWhite{0};
  std::array<BwRailLoad, 2> direction{};  // White arrivals, then black arrivals.
};
inline unsigned bwPopcount(uint8_t bits) { return __builtin_popcount(bits); }
// Q8 leaky average with approximately eight scan lines of half-life.
constexpr uint16_t bwRecovery(uint16_t previous, unsigned switches) {
  return uint16_t((235u * previous >> 8) + 21u * switches);
}
template <class Read>
BwLoad measureRailLoad(Read read, uint16_t left, uint16_t right, const BwColumnMask* lanes,
                       uint16_t top = 0, uint16_t bottom = Height) {
  constexpr size_t stride = Width / 8;
  if (left >= right || right > Width || (left & 7) || (right & 7)) return {};
  const size_t start = left / 8, stop = right / 8;
  const size_t span = std::min(size_t(128 / 8), stop - start);
  std::array<std::array<uint16_t, stride>, 2> recent{}, sustained{};
  std::array<uint32_t, 2> wholeRecent{};
  BwLoad load;
  for (size_t x = start; x < stop; ++x) {
    if (lanes && !(*lanes)[x]) continue;
    load.firstWhite += bwPopcount(read(x)[0]);
    load.lastWhite += bwPopcount(read((Height - 1) * stride + x)[0]);
  }
  // Include the transitions from and to hold on either side of the area.
  for (size_t y = std::max<uint16_t>(1, top); y < Height && y <= bottom; ++y) {
    std::array<std::array<uint8_t, stride>, 2> directed;
    unsigned total[2]{};
    for (size_t x = start; x < stop; ++x) {
      if (lanes && !(*lanes)[x]) {
        directed[0][x] = directed[1][x] = 0;
        continue;
      }
      const auto before = read((y - 1) * stride + x), after = read(y * stride + x);
      for (size_t rail = 0; rail < 2; ++rail) {
        const auto other = 1 - rail;
        // A change to or from hold is half a full rail-to-rail swing.
        const unsigned enter = bwPopcount(after[rail] & ~before[rail]);
        const unsigned leave = bwPopcount(before[other] & ~after[other]);
        directed[rail][x] = enter + leave;
      }
      total[0] += directed[0][x];
      total[1] += directed[1][x];
    }
    for (size_t rail = 0; rail < 2; ++rail) {
      total[rail] = (total[rail] + 1) / 2;
      auto& output = load.direction[rail];
      wholeRecent[rail] = (235u * wholeRecent[rail] >> 8) + 21u * total[rail];
      load.wholeRecentPeak = std::max<uint16_t>(load.wholeRecentPeak, (wholeRecent[rail] + 128u) >> 8);
      output.peak = std::max<uint16_t>(output.peak, total[rail]);
      output.total += total[rail];
      unsigned sum = 0;
      for (size_t x = start; x < stop; ++x) {
        sum += directed[rail][x];
        if (x >= start + span) sum -= directed[rail][x - span];
        if (x + 1 < start + span) continue;
        const size_t cell = x + 1 - span;
        const unsigned switches = (sum + 1) / 2;
        auto& state = recent[rail][cell];
        state = bwRecovery(state, switches);
        auto& slow = sustained[rail][cell];
        constexpr uint32_t alpha32 = 64132;  // Q16 decay over one row.
        slow = (alpha32 * slow + (65536u - alpha32) * switches * 256u) >> 16;
        output.localPeak = std::max<uint16_t>(output.localPeak, switches);
        output.recentPeak = std::max<uint16_t>(output.recentPeak, (state + 128u) >> 8);
        output.sustainedPeak = std::max<uint16_t>(output.sustainedPeak, (slow + 128u) >> 8);
      }
    }
  }
  load.peak = std::max(load.direction[0].peak, load.direction[1].peak);
  load.localPeak = std::max(load.direction[0].localPeak, load.direction[1].localPeak);
  load.recentPeak = std::max(load.direction[0].recentPeak, load.direction[1].recentPeak);
  load.sustainedPeak = std::max(load.direction[0].sustainedPeak, load.direction[1].sustainedPeak);
  return load;
}

inline BwLoad measureBwLoad(const Buffer& image, uint16_t left = 0, uint16_t right = Width,
                           const BwColumnMask* lanes = nullptr) {
  if (image.size() != PlaneBytes) return {};
  return measureRailLoad([&](size_t i) {
    return std::array<uint8_t, 2>{image[i], uint8_t(~image[i])};
  }, left, right, lanes);
}

namespace bw_detail {
constexpr unsigned TileBytes = 4, TileCount = Width / (8 * TileBytes);
template <size_t N> using Counts = std::array<uint8_t, N>;
constexpr uint32_t phase(unsigned y, unsigned block = 0) {
  const uint32_t value = (y + 1) * 0x9e3779b1u + (block + 1) * 0x85ebca77u;
  return ((value ^ (value >> 16)) * 0x7feb352du) & 0x7fffffffu;
}
template <size_t N>
unsigned total(const Counts<N>& counts) {
  unsigned sum = 0;
  for (unsigned count : counts) sum += count;
  return sum;
}
template <size_t N>
void removeQuota(Counts<N>& room, const Counts<N>& capacity, unsigned count, uint32_t offset) {
  if (!count) return;
  const unsigned sum = total(capacity);
  unsigned error = offset % sum;
  for (size_t i = 0; i < N; ++i) {
    error += capacity[i] * count;
    room[i] -= error / sum;
    error %= sum;
  }
}
// Spread paired removals first within the local groups, then allocate the
// remaining removals in each direction. Paired edits keep the white area.
template <size_t N>
void suppress(Counts<N> (&room)[2], unsigned removeWhite, unsigned removeBlack, uint32_t offset) {
  Counts<N> pairs;
  for (size_t i = 0; i < N; ++i) pairs[i] = std::min(room[0][i], room[1][i]);
  const unsigned paired = std::min({removeWhite, removeBlack, total(pairs)});
  for (size_t rail = 0; rail < 2; ++rail)
    removeQuota(room[rail], pairs, paired, offset);
  removeQuota(room[0], room[0], removeWhite - paired, offset);
  removeQuota(room[1], room[1], removeBlack - paired, offset);
}
template <class Priority>
uint8_t retain(uint8_t wanted, unsigned quota, uint32_t offset, Priority priority) {
  if (!quota) return 0;
  if (quota >= bwPopcount(wanted)) return wanted;
  Counts<12> ranks{};
  for (unsigned bit = 0; bit < 8; ++bit) {
    const uint8_t mask = 0x80u >> bit;
    if (wanted & mask) ranks[priority(bit)] |= mask;
  }
  uint8_t result = 0;
  for (auto rank = ranks.rbegin(); rank != ranks.rend() && quota; ++rank) {
    const unsigned available = bwPopcount(*rank);
    if (available <= quota) {
      result |= *rank;
      quota -= available;
      continue;
    }
    unsigned error = offset % available;
    for (uint8_t mask = 0x80u; mask; mask >>= 1) {
      if (!(*rank & mask)) continue;
      error += quota;
      if (error >= available) {
        error -= available;
        result |= mask;
      }
    }
    break;
  }
  return result;
}
}  // namespace bw_detail

inline bool useColumnDither(uint16_t x, uint16_t y, unsigned strength) {
  // Whole 4x4 dither cells use one pattern. Coordinate hashing spreads the
  // remaining Bayer cells instead of phasing every column on the same row.
  return strength >= 100 || (strength && bw_detail::phase(y / 4, x / 4) % 100 < strength);
}

inline uint16_t grainColumn(uint16_t x, uint16_t y, unsigned grain) {
  if (!grain) return x;
  const unsigned pair = x / 2;
  const uint32_t seed = bw_detail::phase(0x91, pair);
  if (seed % 100 >= grain) return x;
  // Swap neighboring thresholds in pairs. Stagger the eight-row opportunities
  // so a texture phase change does not land across the entire row at once.
  const unsigned epoch = (y + ((seed >> 8) & 7)) / 8;
  return x ^ ((bw_detail::phase(epoch, pair) >> 16) & 1);
}

struct BwBurstBudget {
  unsigned budget, headroom;
  std::array<unsigned, 2> credit;

  BwBurstBudget(unsigned budget, unsigned headroom)
      : budget(budget), headroom(headroom), credit{{headroom, headroom}} {}
  unsigned available(size_t rail) const { return budget + credit[rail]; }
  void consume(size_t rail, unsigned count) {
    credit[rail] = count < available(rail) ? std::min(headroom, available(rail) - count) : 0;
  }
};

inline bool fitsBwBurst(const Buffer& image, unsigned budget, unsigned headroom) {
  if (image.size() != PlaneBytes) return false;
  constexpr size_t stride = Width / 8;
  BwBurstBudget burst(budget, headroom);
  for (size_t y = 1; y < Height; ++y) {
    unsigned toWhite = 0, toBlack = 0;
    for (size_t x = 0; x < stride; ++x) {
      const uint8_t before = image[(y - 1) * stride + x], after = image[y * stride + x];
      const uint8_t changed = before ^ after;
      toWhite += bwPopcount(changed & after);
      toBlack += bwPopcount(changed & before);
    }
    if (toWhite > burst.available(0) || toBlack > burst.available(1)) return false;
    burst.consume(0, toWhite);
    burst.consume(1, toBlack);
  }
  return true;
}

// Repair in panel scan order. Only a desired transition can be suppressed.
// The callback grades a pixel 0..11; grades 8..11 are protected marks.
// An optional tile constraint can discard proposed edits before accounting.
// Return the number of protected marks the budget forced us to change.
template <class Priority, class Constrain = std::nullptr_t>
uint32_t repairBwRows(Buffer& image, unsigned budget, Priority priority,
                      unsigned strength = 100, unsigned headroom = 0, Constrain applyTile = nullptr) {
  using namespace bw_detail;
  constexpr size_t stride = Width / 8;
  if (!strength) return 0;
  strength = std::min(strength, 100u);
  const unsigned tilePeak = 3 * budget / 20, tileSustained = budget / 10;
  BwBurstBudget burst(budget, headroom);
  std::array<std::array<uint16_t, TileCount>, 2> recent{};
  uint32_t protectedEdits = 0;
  for (unsigned y = 1; y < Height; ++y) {
    auto* row = image.data() + size_t(y) * stride;
    const auto* before = row - stride;
    Counts<TileCount> room[2]{}, requested[2]{};
    for (size_t tile = 0; tile < TileCount; ++tile) {
      for (size_t x = tile * TileBytes; x < (tile + 1) * TileBytes; ++x) {
        const uint8_t changed = before[x] ^ row[x];
        requested[0][tile] += bwPopcount(changed & row[x]);
        requested[1][tile] += bwPopcount(changed & before[x]);
      }
      int drop = 0;
      for (size_t rail = 0; rail < 2; ++rail) {
        const unsigned carry = bwRecovery(recent[rail][tile], 0);
        const unsigned open = carry < 256u * tileSustained ? (256u * tileSustained - carry) / 21u : 0;
        drop = std::max(drop, int(requested[rail][tile]) - int(std::min(tilePeak, open)));
      }
      drop = (drop * int(strength) + 99) / 100;
      for (size_t rail = 0; rail < 2; ++rail)
        room[rail][tile] = std::max(0, int(requested[rail][tile]) - drop);
    }
    const unsigned whites = total(room[0]), blacks = total(room[1]);
    const int excess = std::max(int(whites) - int(burst.available(0)),
                                int(blacks) - int(burst.available(1)));
    if (excess > 0) {
      const unsigned drop = ((unsigned(excess) * strength) + 99) / 100;
      suppress(room, std::min(whites, drop), std::min(blacks, drop), phase(y));
    }
    for (size_t tile = 0; tile < TileCount; ++tile) {
      if (room[0][tile] == requested[0][tile] && room[1][tile] == requested[1][tile]) {
        for (size_t rail = 0; rail < 2; ++rail)
          recent[rail][tile] = bwRecovery(recent[rail][tile], room[rail][tile]);
        continue;
      }
      const size_t start = tile * TileBytes;
      Counts<TileBytes> wanted[2], sub[2];
      for (size_t i = 0; i < TileBytes; ++i) {
        const uint8_t changed = before[start + i] ^ row[start + i];
        wanted[0][i] = changed & row[start + i];
        wanted[1][i] = changed & before[start + i];
        for (size_t rail = 0; rail < 2; ++rail) sub[rail][i] = bwPopcount(wanted[rail][i]);
      }
      suppress(sub, requested[0][tile] - room[0][tile], requested[1][tile] - room[1][tile], phase(y, tile));
      Counts<TileBytes> next;
      for (size_t i = 0; i < TileBytes; ++i) {
        const size_t x = start + i;
        auto rank = [&](unsigned bit) { return priority(uint16_t(x * 8 + bit), uint16_t(y)); };
        const auto offset = phase(y, x);
        const uint8_t white = retain(wanted[0][i], sub[0][i], offset, rank);
        const uint8_t black = retain(wanted[1][i], sub[1][i], offset, rank);
        next[i] = (before[x] | white) & ~black;
      }
      if constexpr (!std::is_same_v<Constrain, std::nullptr_t>) {
        applyTile(y, start, row + start, next);
        // Budget the transitions actually left after unmatched edits were discarded.
        room[0][tile] = room[1][tile] = 0;
        for (size_t i = 0; i < TileBytes; ++i) {
          const uint8_t changed = before[start + i] ^ next[i];
          room[0][tile] += bwPopcount(changed & next[i]);
          room[1][tile] += bwPopcount(changed & before[start + i]);
        }
      }
      for (size_t rail = 0; rail < 2; ++rail)
        recent[rail][tile] = bwRecovery(recent[rail][tile], room[rail][tile]);
      for (size_t i = 0; i < TileBytes; ++i) {
        const size_t x = start + i;
        const uint8_t edited = row[x] ^ next[i];
        for (unsigned bit = 0; bit < 8; ++bit)
          if ((edited & (0x80u >> bit)) && priority(uint16_t(x * 8 + bit), uint16_t(y)) >= 8) ++protectedEdits;
        row[x] = next[i];
      }
    }
    if (headroom) {
      burst.consume(0, total(room[0]));
      burst.consume(1, total(room[1]));
    }
  }
  return protectedEdits;
}

}  // namespace x4render
