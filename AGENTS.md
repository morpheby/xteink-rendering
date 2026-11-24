# Working on xteink-rendering

This repository is the header-only C++17 rendering and calibration layer for the 800 × 480 Xteink X4 Pro UC8279. Start with [README.md](README.md), [calibration.md](docs/calibration.md), and [drive-load.md](docs/drive-load.md). These describe the current interfaces, which settings were measured, and which explanations are still hypotheses.

## Repository boundaries

- This library owns luminance quantization, frame history, drive masks, waveform construction, drive-load estimates, and calibration patterns. Keep ESPHome, LVGL, Home Assistant, GPIO, SPI, and sleep policy out of its headers.
- FreeInk owns panel discovery, factory-table reads, board power settings, and the controller transport. The caller acquires its exclusive controller session, checks power-off, and releases the session.
- [esphome-xteink](https://github.com/morpheby/esphome-xteink/tree/codex/x4-rendering-calibration) owns YAML, LVGL integration, and the application lifecycle. Its `dependencies.json` and rendering gitlink must agree. Commit and publish upstream library changes before updating that pin.
- Keep default calibrated values in `panel_calibration.h`. Integrations should select a mode and load optional replacement values through `GrayRefresh::loadCalibration`. Keep new calibration controls in C++ firmware or test harnesses.

## Where to change things

| Files | Responsibility |
| --- | --- |
| `rendering.h`, `pipeline.h` | Frame allocation, coordinate-stable quantization, before/after image knowledge, mask planning, and phase transitions |
| `gray_refresh.h`, `waveform.h` | Relative and stable waveform construction, factory compatibility checks, zone planning, and the asynchronous transport lifecycle |
| `panel_calibration.h`, `gray_profile.h` | Default pulse and palette data, validated profile loading, and coverage fitting |
| `bw_source_charge.h`, `bw_strips.h`, `bw_approximation.h` | Source-load estimates, exact lane zoning, and experimental approximations |
| `calibration.h`, `test_image.h`, `waveform_inspection.h` | Controlled reference patterns, diagnostic transactions, and inspection of actual waveform bytes |
| `test/` | Host regression tests and fake transports |
| `assets/`, `tools/make_landscape.py` | Test photo, attribution, and its generated packed asset |

## Keep rendering state consistent

- Distinguish the target buffer, last confirmed visible image, and controller DTM RAM. A transfer or an accepted BUSY transition alone is insufficient evidence that a full image was displayed. Reset and sleep can invalidate controller RAM while the image remains visible.
- Draw before planning. A planned or active transaction borrows the target until completion. Commit after the last activation and confirmed power-off. A failure invalidates history and the next ordinary refresh resynchronizes it. Keep preview state until the restoring update accounts for every previewed pixel.
- An absolute region drives every pixel within its exact rectangle, including the background. Outside the rectangle use hold. A successful region does not establish a whole-frame baseline. Reset that history before normal differential work.
- Anchor ordered dithering to physical panel coordinates. When changing gray masks, include connected affected gray from both old and new frames. Preserve each pixel's selected starting polarity with its settle selector through all zones.
- For cropped uploads, write NEW (`0x13`) before OLD (`0x10`) and scan only rows with current selectors. Preserve full visible scan-line activation. The historical x≈752 defect occurred with horizontally bounded activation. Keep host geometry, hardware gate offsets, upload extent, active pixel mask, and activation window distinct.
- Treat hold as a source-to-VCOM relationship. Preserve the matched waveform and validate retained patches on hardware before changing it. Keep the panel-wide hold after split B/W preparation unless the task explicitly changes that waveform.

## Calibrate and report evidence

- Always start from a verified factory table for the panel and operating temperature. Check the supported table shape before editing phases. Pulse counts in LUT bytes are controller frames, with elapsed time measured separately.
- Preserve the measured A/B/C raw shade ordering. Keep fit data in ascending displayed luminance order. Record the illumination, white reference, temperature, source profile, test pattern, and exact waveform when claiming an optical change. The stable grayscale defaults currently use nominal luminance levels.
- The drive model estimates load from switching patterns. Keep measured panel behavior separate from estimates of rail recovery, capacitance, and resonance. When comparing a change, check dense and sparse patterns, striping, and repeated hold before making a broad hardware claim.
- Avoid editing generated landscape data by hand. Use the generator for asset changes and preserve the [public-domain attribution](assets/README.md).

## Validate and hand off

Run the sanitizer-backed C++17 tests from the repository root.

```sh
python3 test/run.py
CXX=clang++ python3 test/run.py
```

The second command is an equivalent way to select a compiler. Extend a focused case in `test/test.cpp`, `test/calibration.cpp`, or `test/waveform.cpp` when a behavior change creates a regression risk. The tests use fake transports and assert plane bytes, activation geometry, and phase completion. Report physical tone, ghosting, and actual update timing separately from host results.

For changes consumed by ESPHome, also validate the adapter with the pinned library revision. Read its own `AGENTS.md` before editing the integration. Use isolated checkouts for dependency experiments, avoid unrelated submodule updates, and record the tested revisions and commands in the handoff. Device flashing, live Home Assistant changes, and publishing are separate operations. Confirm their target and authorization before doing them. Keep credentials, local paths, device IDs, and unpublished captures out of public commits.
