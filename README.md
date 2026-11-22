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

## Second calibration round

Panel observations supplied by the user: source code 1 darkens, code 2 brightens,
and hold code 0 preserved references in the tested setup. Black-start patches
were uniform while white-start patches were noisy; no consistent palette exists.
These observations do not establish matching-silicon voltages or long-term hold.

The candidate sequence is now common opposite prefix, candidate A/B/C pulse,
then equalizing hold padding. Code 2 as primary gives darkening-prefix code 1
followed by different brightening doses. Hold defaults to the observed code 0.
Rotation lives in Calibration alone: rebuild resets it, repeat preserves it,
rotate advances only accepted jobs. Implicit reconstruction after conditioning
also resets rotation. Diagnostic snapshots store exactly the bytes accepted by
transport activation and survive subsequent control changes.

`waveform_inspection.h` exposes exact 49-byte hex/binary records and compact
binary-rail timelines. In `g0 x1 s0 x1[01*3 10*6] s1 x1[00*57]`, the two-bit values
are source-level codes and counts are programmed frame fields. Group/state
repeats are explicit. This is a model of programmed data, not oscilloscope data.
Trailing groups remain visible in the full raw record; electrical end behavior
is still unresolved.

B/W preparation can use factory OTP, whose payload has not been read back, or
programmable endpoint drive. Programmable preparation applies code 1 to wire
selector 10 (black) and code 2 to selector 11 (white), for an adjustable 1–63 frame
count and 1–16 group repeats, then one hold count. VCOM remains code 00. Longer
drive is a test for endpoint saturation/noise, not a proven noise fix. Defaults
are 16 frames and one repeat, with factory OTP selected until explicitly changed.

## Inverted-image B/W preparation correction

The earlier programmable B/W waveform omitted inverted-image drive. Following
the user's observation of progressive white noise/deterioration, preparation
now drives the complement of the requested image first, then the requested B/W
endpoints, then one hold count. For requested black the source sequence is
brightening code 2 → darkening code 1 → hold; requested white uses code 1 → code
2 → hold. VCOM stays at programmed code 00 throughout. This adds the missing
phase but does not claim a reconstruction of all factory OTP behavior.

`BwPreparation::inversionFrames` independently controls 0–63 complement counts
(default 16; zero deliberately disables it for comparison). `frames` remains the
existing 1–63 target count. `repeats` repeats the entire complement/target/hold
cycle. All five LUT rows have `(inversionFrames + frames + 1) * repeats` modeled
counts. Raw binary/hex inspection includes every phase. Long settings remain
subject to the transport's bounded BUSY deadline and physical validation.
