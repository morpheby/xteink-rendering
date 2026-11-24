# Calibrate an X4 Pro panel

Start with C++ firmware and the [default X4 Pro profile](../include/xteink/panel_calibration.h). The profile separates measured luminance, experimental waveform adjustments, and source-load budgets. Change one group at a time so the optical measurements remain comparable.

## Draw reference patterns

1. Read the controller's factory table and choose its entry for the operating temperature. Configure power and clock from the panel's verified factory data. Record the panel identification, table, temperature, and pulse settings.
2. Acquire the controller through the firmware's FreeInk display instance. Construct `Calibration<Transport>` with that transport and check `allocate()`. Supply a factory-derived B/W bank in `factoryDerivedBw` and set `parameters` to the relative pulse candidates being measured.
3. Use `startImage(now, image)` with `WhiteCandidates` for white-start tones, `NeighborSweep` for adjacent shade pairs, or `LinearRamp` to compare the fit with B/W dithering. Poll the job and check `result()` before taking measurements. Keep B/W preparation, dither, zoning, and any hold pass consistent between the chart and the renderer you intend to use.
4. Confirm controller power-off before releasing the session or returning to ordinary rendering. Rebuild the application's frame history after calibration.

## Fit a relative palette

Measure the raw shades and the 17 dither steps for each neighboring pair. For photographs, use a fixed exposure and white balance across a sweep, decode the embedded color profile and any HDR representation, and average the selected pixels in linear light. Use nearby black and white references to normalize endpoints to 0 and 255. Compare reference patches on the same panel area when illumination varies.

Store luminance in **A/B/C from black, then A/B/C from white** order. Set an omitted shade to zero. Write coverage curves in ascending luminance order, including the pairs adjoining black and white. Use `fitCoverage()` for each measured 17-step sweep. A zero-initialized curve uses linear coverage when measurements are unavailable or the fit fails.

```cpp
#include <xteink/gray_refresh.h>

bool install_coverage(x4render::GrayRefresh& renderer, size_t pair,
                      const std::array<float, 17>& samples) {
  auto profile = renderer.calibration();
  if (pair >= profile.relative.coverage.size()) return false;
  auto fitted = x4render::fitCoverage(samples);
  if (!fitted) return false;
  profile.relative.coverage[pair] = *fitted;
  return renderer.loadCalibration(profile);
}
```

After loading the profile, draw a complete image and compare a measured ramp with the reference B/W dither. Verify that the lightest shades remain distinguishable from the paper white. The same fitted palette can look different if you change waveform timing or zoning.

## Test Single Settle

Call `Calibration::startStable(now, frames, rebuild, rotate)` with a supported factory-derived B/W bank. The three frame counts set the lightening dose after the black reset. Rebuild to prepare black and white starting patches. Repeat and rotate to test convergence from previously displayed gray while leaving the reference patches held. Verify the resulting tones before entering their measured luminance values.

The bundled profile uses 3, 5, and 7 frames for its three doses. The input levels 64, 128, and 192 are nominal. Set `stableFrames` and `stableLuminance` on a copy of the default profile to load measurements for another panel.

## Keep a reproducible record

Report the selected factory table, temperature, LUT bytes or pulse parameters, starting image, gray and B/W zone counts, and any hold waveform. Record optical evidence separately from software and transport tests. [Scan-load notes](drive-load.md) describes the patterns that exposed underdrive and the limits of the empirical load model.
