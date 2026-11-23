# Xteink rendering and calibration

Header-only C++17 rendering for the 800×480 X4 Pro panel. The consuming
ESPHome component pins this library and the FreeInk transport. The host
algorithms do not depend on ESPHome. Buffer allocation uses PSRAM on ESP32.

- `rendering.h` plans exact drive masks and builds the two wire planes.
  `Frames` records previous and target luminance and commits history after
  successful completion. Dither/settle connects affected old and new gray
  regions. Activation uses full visible lines to avoid the observed x≈752
  artifact. Upload extents are independent.
- `gray_profile.h` provides measured shade palettes and a fit for an optical
  sweep of 17 swatches. Each shade carries its starting B/W state and its
  settle selector. `calibration.h` exposes transport-independent test images,
  waveform inspection, and asynchronous results.
- `bw_approximation.h`, `bw_strips.h` and `bw_source_charge.h` estimate
  relative loading and choose up to three exact interleaved zones. See the
  [panel findings and limits](docs/drive-load.md) before choosing a budget.

## Repeat relative gray calibration

1. Use a supported factory table for B/W preparation and record the table,
   raw waveform, panel power settings, and test temperature. Maintain
   the same B/W and gray preparation for the photo and the fitted profile.
2. Render `WhiteCandidates` to compare the white-start tones. Use
   `NeighborSweep` for all adjacent pairs and `LinearRamp` to check the fit
   against B/W dithering. These images are available through
   `Calibration::startImage(now, image)`. Call `poll(now)` until completion
   and inspect `result()` before measuring the panel.
3. Measure the raw shades and the 17 mix steps per pair. Average swatch
   pixels in linear light and use local panel white for color-managed
   comparisons. Normalize raw black and white to 0 and 255. Select only
   visibly distinct shades. HDR HEIC needs explicit HDR decoding when
   using the HDR representation for comparisons.
4. Store the raw shade measurements in A/B/C-from-black, then
   A/B/C-from-white order. Use zero only for omitted shades. Set each
   coverage curve in sorted dark-to-light pair order with
   `fitCoverage(samples)`. If a sweep does not provide a reliable
   positive span, use the zero-initialized linear fallback and record
   that choice. Inspect the measured ramp before adopting the profile.

```cpp
#include <xteink/gray_profile.h>

x4render::RelativeGrayProfile fit(const std::array<float, 17>& firstPair) {
  x4render::RelativeGrayProfile profile;
  profile.luminance = {35, 108, 187, 206, 232, 248};  // One measured panel.
  if (auto curve = x4render::fitCoverage(firstPair)) profile.coverage[0] = *curve;
  return profile;
}
// Build with profile.palette() and check palette.calibrated before use.
```

`fitCoverage` pools decreasing samples into a nondecreasing table. It
preserves endpoint counts and keeps equal outputs for unresolved steps.
A valid table does not establish that a waveform is optically stable.

## Test stable gray candidates

Use `Calibration::startStable(now, frames, rebuild, rotate)` with a verified
factory-derived B/W bank for the panel temperature. The three frame counts
set the lightening pulse lengths after the factory black reset. Rebuild to
prepare black and white starts and initialize the held gray witness. Repeat
or rotate the same counts to test convergence from previously displayed
grays while retaining the reference patches.

`stableGrayBank` builds the combined LUT for transport consumers.
`measuredStablePalette` orders the three measured tones between the black and
white endpoints while retaining their LUT selectors.

## Use the calibrated profiles

`Frames::plan` compares the new and displayed frames in the selected
palette. With `Mode::SingleSettle`, the mask starts with changed pixels.
`Settings::margin` can add a square kernel around each of them.
`Mode::DitherSettle` also includes connected gray pixels from both frames.
Both modes can use `GrayRefresh` under an exclusive transport lease, with
a factory-derived B/W bank and a calibrated settle bank. It uses at most
three column zones for each stage. Split B/W preparation ends with a
panel-wide VCOM-matched hold. It can reject an over-budget frame before
touching the panel. The caller commits frame history after all activations and
power-off complete. In single-settle mode, gray pixels get their reset in
the combined LUT and bypass the endpoint preparation pass.

`Frames::planPreview` uses ordered B/W for the changed portion of a
known frame. After a successful preview, call `commitPreview`. The next
normal plan includes the union of previewed pixels and any new changes,
even when the final input equals the last fully rendered gray frame.

Run `python3 test/run.py` for the host sanitizer tests. Code is MIT licensed.
The public-domain photo and upstream credits are in the
[asset notes](assets/README.md).
