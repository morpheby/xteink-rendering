# xteink-rendering

**A very over-engineered e-ink driver overlay for the Xteink X4 Pro (UC8279).**
Changing one clock digit turned out to require a surprising amount of bookkeeping.

This header-only C++17 library renders to the X4 Pro's 800 × 480 UC8279 panel. It handles ordered B/W dithering, calibrated grayscale, regional updates after sleep, and test patterns for measuring a panel. It was developed with OpenAI Codex. The [ESPHome integration](https://github.com/morpheby/esphome-xteink/tree/codex/x4-rendering-calibration) uses it with the FreeInk SDK. C++ firmware can use it directly with a compatible transport.

## Install

For ESPHome, follow the [ESPHome integration's quick start](https://github.com/morpheby/esphome-xteink/tree/codex/x4-rendering-calibration#quick-start). The integration already pins this library and FreeInk.

For PlatformIO, add the library to your existing firmware environment. Pin a tested commit for reproducible builds.

```ini
lib_deps =
    https://github.com/morpheby/xteink-rendering.git#codex/calibration
```

For CMake or another C++17 build, clone the repository and add `include/` to the compiler's include path. Include `<xteink/gray_refresh.h>` for rendering or `<xteink/calibration.h>` for test patterns. Keep the board and SPI transport in your firmware. On ESP32 the frame buffers use PSRAM when available.

## Choose a rendering mode

| C++ mode | What it does |
| --- | --- |
| `Mode::BwDither` | Uses ordered black and white pixels. The factory-derived B/W waveform updates the image. |
| `Mode::SingleSettle` | Uses black, white, and three gray tones. Each gray selector combines a black reset and its lightening dose. |
| `Mode::DitherSettle` | Dithers between adjacent measured shades. A B/W pass establishes the starting tones, followed by a gray settling pass. The default profile has eight physical tones. |

A separate preview path uses a short direct B/W waveform for responsive touch feedback. On release, the normal renderer restores the gray image. An absolute regional update can repaint a clock or status rectangle after sleep without saving the frame buffers.

## Draw and plan a region

Create one `Frames` and `GrayRefresh` for a display. Call `frames.allocate()` once and check its result. Pixels use physical panel coordinates, with `0` for black and `255` for white. A region update needs every pixel inside the rectangle, including the background.

```cpp
#include <xteink/gray_refresh.h>

bool plan_region(x4render::Frames& frames, x4render::GrayRefresh& renderer,
                 const x4render::LutBank& factory, x4render::Rect region,
                 const uint8_t* luminance) {
  if (!luminance || !frames.target.data() || frames.busy() || region.empty() ||
      region.x + region.width > x4render::Width ||
      region.y + region.height > x4render::Height)
    return false;

  for (unsigned row = 0; row < region.height; ++row) {
    for (unsigned column = 0; column < region.width; ++column) {
      const size_t index = size_t(region.y + row) * x4render::Width + region.x + column;
      frames.draw(index, luminance[size_t(row) * region.width + column]);
    }
  }

  if (!frames.planRegion(region, renderer.settings(x4render::Mode::DitherSettle)))
    return false;
  if (renderer.planFactory(factory, /*windowedUpload=*/true) !=
      x4render::GrayRefresh::FactoryPlan::Ready) {
    frames.fail();
    return false;
  }
  return true;
}
```

Here `factory` is the supported B/W bank read from the panel for the operating temperature, and `luminance` is a row-major rectangle. Hold an exclusive controller session while configuring panel power and calling `start()` and `poll()`. After `Stage::Done`, request panel power-off and check completion. Call `frames.resetController()` after a regional update so later differential updates rebuild their baseline.

For a normal whole-frame or differential update, use `frames.plan(settings)` and `frames.commit()` after successful power-off. If the plan has zero driven pixels, you can commit without a transport transaction. If a transaction fails, call `frames.fail()` and resynchronize on the next normal refresh.

## API at a glance

| API | When to use it |
| --- | --- |
| `Frames::allocate()`, `draw(index, luma)` | Allocate the buffers and write the target image before planning. `index` is `y * Width + x`. |
| `Frames::plan(settings, force)`, `planRegion(rect, settings)` | Plan a normal refresh or repaint an exact rectangle. A normal refresh can expand its mask to cover connected gray pixels. |
| `GrayRefresh::settings(mode)`, `planFactory(bank, windowedUpload, preview)` | Select the profile's palette and build a factory-derived drive plan. Check for `Ready`, `UnsupportedWaveform`, or `OverBudget`. |
| `GrayRefresh::start(transport, now)`, `poll(transport, now)` | Start and advance the asynchronous waveform. `now` is in milliseconds. When `poll()` returns `true`, inspect `stage` to distinguish completion from failure. The caller owns power-off and session release. |
| `Frames::planPreview(margin)`, `commitPreview()` | Track a temporary B/W preview over a known image. Use `planFactory(..., true, true)` to select the direct waveform. The next normal update includes the previewed pixels. |
| `Frames::commit()`, `fail()`, `resetController()` | Confirm a normal refresh, mark an interrupted transaction, or discard the display baseline after a region update or reset. |
| `GrayRefresh::loadCalibration(profile)` | Validate and copy a replacement profile while idle. A successful load invalidates the old frame baseline. |
| `Calibration<Transport>`, `fitCoverage(samples)` | Draw physical reference patterns and fit a measured 17-step curve. See the [calibration guide](docs/calibration.md). |

## Use a different calibration

The default profile includes the measured relative palette, seven coverage curves, pulse counts, and load budgets. The three Single Settle frame counts were visually tested. Their default input luminances are approximate until measured on the target panel.

```cpp
#include <xteink/gray_refresh.h>

bool load_measured(x4render::GrayRefresh& renderer,
                   const x4render::RelativeGrayProfile& measured) {
  auto profile = x4render::DefaultX4ProCalibration;
  profile.relative = measured;
  return renderer.loadCalibration(profile);
}
```

Use C++ firmware to measure a different panel. The [calibration guide](docs/calibration.md) explains the reference patterns, measurements, and profile fields.

## How it fits together

1. **Quantization.** `Frames` keeps the target and the last confirmed image in 8-bit luminance buffers. Dithering uses physical coordinates so a pixel's pattern stays consistent across full and partial draws.
2. **Mask and planes.** The planner compares quantized states and encodes the selected pixels into the two UC8279 data planes. An absolute region only drives its requested pixels. Other columns in those rows use a hold selector.
3. **Drive planning.** The library tunes a supported temperature-selected factory bank, estimates source-rail load, and tries up to three interleaved column zones. Windowed transfers upload the affected full-width rows. Full-width activation avoids an observed defect near x≈752 with horizontally bounded activation. See the [scan-load notes](docs/drive-load.md).
4. **Hardware and state.** FreeInk owns controller discovery, SPI, power, and BUSY handling. Firmware owns the exclusive lease, the final power-off check, and when to commit history. The physical image, controller RAM, and software history are separate states.

## Test and contribute

Run `python3 test/run.py` with a C++17 compiler that supports AddressSanitizer and UndefinedBehaviorSanitizer. The host tests exercise quantization, calibration, and transport sequencing. Use a physical panel to evaluate gray tone, ghosting, repeated hold, and timing. See [AGENTS.md](AGENTS.md) for maintenance and validation guidance.

The library is [MIT licensed](LICENSE). The landscape test asset has separate [public-domain attribution](assets/README.md).
