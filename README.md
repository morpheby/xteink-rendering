# Xteink rendering and calibration

Header-only C++17 rendering core for the X4 Pro 800×480 panel. The matching
FreeInk transport and ESPHome integration are pinned by the consuming repository.
There is no platform dependency in the host algorithms; PSRAM allocation is
encapsulated in the ESP32 storage owner. All refresh workspace is allocated once.

`Frames` keeps displayed/new 8-bit luminance, recorded palette/mode settings, an
exact changed-pixel mask, and two reusable wire planes. It recomputes old/new
quantization at stable screen coordinates. A transaction commits only after its
preparation and (when selected) settle phases succeed. Failure preserves the
previous host frame and marks physical completion interrupted. Expected state
restored after MCU sleep is distinct from known physical state and must not be
used as a differential baseline. Persistence is not implemented at this checkpoint.

`rendering.h` provides B/W, single-settle, and dither/settle planning. Gray palettes
remain gated until calibration. Palette luminances must be ordered, distinct, and
include B/W endpoints; each entry stores both preparation polarity and selector.
Connectivity closes over old/new gray pixels with eight neighbors before either
plane is constructed. Exact masks, rather than expanded bounding boxes, drive
pixels. The current fixed-point closure favors bounded storage over a large work
queue; benchmark pathological long components before enabling this in production.

`pipeline.h` validates recipe composition at compile time. `waveform.h` contains
an explicitly experimental 7-byte interpretation, not a matching UC8279 datasheet
claim. `calibration.h` renders black/white starts, raw candidates, ramps, held
references, transition/repeat tests, and conditioning without normal-render
interleaving. Wire selectors map 00→R24, 01→R22, 10→R23, 11→R21. This ordering and
all optical effects still need confirmation on the actual unit. No synthetic QY
blob split or X3 quality-bank scaling is used in the calibration path.

No physical positive/negative polarity, floating-source code, zero-field hold,
frame period, repeat semantics, or waveform-end behavior is certified. Raw rail
codes remain raw; do not interpret source code 0 or 3 as a validated hold.

Run `python3 test/run.py` for C++17 tests under address/undefined-behavior checks.
The tests cover deterministic output, history commit/failure, sparse masks,
old/new gray connectivity, palette changes, endpoint preparation, reference holds,
conditioning ownership, pump shutdown, and failure recovery accounting.

The known x752 column observation comes from the existing esphome-xteink
investigation. The transport must activate complete visible lines; upload windows
are independent. The last calibration rectangle extends through x761.

The project uses the MIT license. FreeInk and the OpenX4 community retain their
upstream credits in their repositories; this library does not incorporate vendor
waveform blobs. The Bayer threshold matrix is the standard ordered pattern.
