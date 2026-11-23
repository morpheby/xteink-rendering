#pragma once
#include "rendering.h"

namespace x4render {

enum class BwTexture : uint8_t { Bayer, Columns, Repair };

constexpr bool columnDither(uint8_t luma, uint16_t x) {
  uint8_t rank = 0;
  for (uint8_t i = 0; i < 6; ++i) rank = uint8_t((rank << 1) | ((x >> i) & 1));
  return unsigned(luma) * 128 > unsigned(2 * rank + 1) * 255;
}

struct BwLoad {
  uint16_t peak{0}, localPeak{0}, recentPeak{0};
};
inline unsigned bwPopcount(uint8_t bits) { return __builtin_popcount(bits); }
// Q8 leaky average with approximately eight scan lines of half-life.
constexpr uint16_t bwRecovery(uint16_t previous, unsigned switches) {
  return uint16_t((235u * previous >> 8) + 21u * switches);
}
inline BwLoad measureBwLoad(const Buffer& image) {
  constexpr unsigned stride = Width / 8, span = 128 / 8;
  std::array<std::array<uint16_t, stride - span + 1>, 2> recent{};
  BwLoad load;
  for (size_t y = 1; y < Height; ++y) {
    std::array<std::array<uint8_t, stride>, 2> directed;
    unsigned total[2]{};
    for (size_t x = 0; x < stride; ++x) {
      const uint8_t before = image[(y - 1) * stride + x], after = image[y * stride + x];
      const uint8_t changed = before ^ after;
      directed[0][x] = bwPopcount(changed & after);
      directed[1][x] = bwPopcount(changed & before);
      total[0] += directed[0][x];
      total[1] += directed[1][x];
    }
    for (size_t rail = 0; rail < 2; ++rail) {
      load.peak = std::max<uint16_t>(load.peak, total[rail]);
      unsigned sum = 0;
      for (size_t x = 0; x < stride; ++x) {
        sum += directed[rail][x];
        if (x >= span) sum -= directed[rail][x - span];
        if (x + 1 < span) continue;
        auto& state = recent[rail][x + 1 - span];
        state = bwRecovery(state, sum);
        load.localPeak = std::max<uint16_t>(load.localPeak, sum);
        load.recentPeak = std::max<uint16_t>(load.recentPeak, (state + 128u) >> 8);
      }
    }
  }
  return load;
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

// Repair in panel scan order. Only a desired transition can be suppressed.
// The callback grades a pixel 0..11; grades 8..11 are protected marks.
// Return the number of protected marks the budget forced us to change.
template <class Priority>
uint32_t repairBwRows(Buffer& image, unsigned budget, Priority priority) {
  using namespace bw_detail;
  constexpr size_t stride = Width / 8;
  const unsigned tilePeak = 3 * budget / 20, tileSustained = budget / 10;
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
        const unsigned open = (256u * tileSustained - bwRecovery(recent[rail][tile], 0)) / 21u;
        drop = std::max(drop, int(requested[rail][tile]) - int(std::min(tilePeak, open)));
      }
      for (size_t rail = 0; rail < 2; ++rail)
        room[rail][tile] = std::max(0, int(requested[rail][tile]) - drop);
    }
    const unsigned whites = total(room[0]), blacks = total(room[1]);
    const unsigned most = std::max(whites, blacks);
    if (most > budget)
      suppress(room, std::min(whites, most - budget), std::min(blacks, most - budget), phase(y));
    for (size_t tile = 0; tile < TileCount; ++tile) {
      for (size_t rail = 0; rail < 2; ++rail)
        recent[rail][tile] = bwRecovery(recent[rail][tile], room[rail][tile]);
      if (room[0][tile] == requested[0][tile] && room[1][tile] == requested[1][tile]) continue;
      const size_t start = tile * TileBytes;
      Counts<TileBytes> wanted[2], sub[2];
      for (size_t i = 0; i < TileBytes; ++i) {
        const uint8_t changed = before[start + i] ^ row[start + i];
        wanted[0][i] = changed & row[start + i];
        wanted[1][i] = changed & before[start + i];
        for (size_t rail = 0; rail < 2; ++rail) sub[rail][i] = bwPopcount(wanted[rail][i]);
      }
      suppress(sub, requested[0][tile] - room[0][tile], requested[1][tile] - room[1][tile], phase(y, tile));
      for (size_t i = 0; i < TileBytes; ++i) {
        const size_t x = start + i;
        auto rank = [&](unsigned bit) { return priority(uint16_t(x * 8 + bit), uint16_t(y)); };
        const auto offset = phase(y, x);
        const uint8_t white = retain(wanted[0][i], sub[0][i], offset, rank);
        const uint8_t black = retain(wanted[1][i], sub[1][i], offset, rank);
        const uint8_t next = (before[x] | white) & ~black;
        const uint8_t edited = row[x] ^ next;
        for (unsigned bit = 0; bit < 8; ++bit)
          if ((edited & (0x80u >> bit)) && rank(bit) >= 8) ++protectedEdits;
        row[x] = next;
      }
    }
  }
  return protectedEdits;
}

}  // namespace x4render
