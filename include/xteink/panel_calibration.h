#pragma once

#include "gray_profile.h"
#include "waveform.h"

namespace x4render {

struct PanelCalibration {
  RelativeGrayProfile relative;
  std::array<Pulse, 3> relativePulses;
  std::array<uint8_t, 3> stableFrames;
  std::array<uint8_t, 3> stableLuminance;
  FactoryBwTuning bw;
  uint16_t bwRailBudget;
  uint16_t grayRailBudget;
  uint16_t localRailBudget;
  uint8_t updateMargin;
  uint8_t factoryPowerSetup;
  SingleMask singleSettleMask;

  bool valid() const {
    return relative.palette().calibrated && measuredStablePalette(stableLuminance).calibrated &&
           std::all_of(stableFrames.begin(), stableFrames.end(), [](uint8_t frames) {
             return frames >= 1 && frames <= 30;
           }) &&
           candidates(relativePulses)[0][0] && bw.valid() &&
           bwRailBudget && grayRailBudget && localRailBudget && updateMargin <= 2 &&
           (factoryPowerSetup == 1 || factoryPowerSetup == 2);
  }
};

// The factory bank itself is read from the panel for the current temperature.
// The stable lightening doses are verified visually. Their input luminances
// are nominal until the three stable tones are measured.
inline constexpr PanelCalibration DefaultX4ProCalibration{
    RelativeGrayProfile{{35, 108, 187, 206, 232, 248}, {{
        {0, 0, 0, 0, 0, 8, 20, 30, 47, 76, 115, 137, 152, 179, 204, 211, 255},
        {0, 1, 5, 16, 30, 52, 64, 75, 79, 111, 135, 165, 174, 191, 208, 238, 255},
        {0, 13, 15, 22, 63, 77, 100, 116, 127, 156, 186, 201, 207, 224, 231, 253, 255},
        {0, 7, 14, 38, 40, 86, 97, 102, 102, 126, 127, 127, 127, 132, 159, 200, 255},
        {0, 0, 0, 11, 30, 54, 66, 92, 110, 135, 144, 165, 173, 182, 218, 247, 255},
        {0, 20, 20, 66, 111, 129, 129, 169, 172, 187, 187, 197, 204, 221, 237, 251, 255},
        {0, 38, 38, 38, 58, 58, 58, 120, 121, 126, 143, 192, 192, 192, 192, 198, 255},
    }}},
    {{{2, 2, 3, 1, 1}, {2, 3, 3, 1, 1}, {2, 4, 3, 1, 1}}},
    {3, 5, 7},
    {64, 128, 192},
    {1, 2, 5, 0, 3},
    160, 96, 40,
    1, 2,
    SingleMask::Rectangle,
};

}  // namespace x4render
