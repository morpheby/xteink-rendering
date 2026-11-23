# Scan load on the X4 Pro UC8279 panel

## Panel observations

The dense checkerboard and the dithered ramp underdrove during the B/W
inversion phase. Vertical stripes drove cleanly. The issue reproduced
with factory OTP and the factory-derived programmable recipe, including
the verified cold-start power setup. Raising waveform dose was not a
general fix. In the checkerboard the later one-polarity portions could
still drive when the inversion was barely visible.

In the centered-width experiment the user observed:

| Driven width | Observation |
| --- | --- |
| 400 px | Checkerboard visible only in the late phases |
| 320 px | Audible load and weaker inversion |
| 240 px | Better rendering and less ghosting |
| 160 px | Crisp inversion, barely audible switching |

One alternating row followed by one quiet transition performed better
than eight active followed by eight quiet. Spreading each transition
across staggered columns helped more than simply jittering its timing.
The user heard repeated high-pitched crackling on failing patterns.
These observations are consistent with finite source-rail recovery
and local loading. The capacitances, rail currents and acoustic
mechanism have not been measured.

## Model and mitigation

Count directed B/W changes between consecutive scan rows for each rail.
Track short-term whole-rail load and the short- and longer-term load
within overlapping 128-column regions. Scores are switch-count
estimates. Pixel capacitance, source-line capacitance and the active
waveform phases all influence charge. A peak switch count alone is
insufficient. The boiler frame's sharp isolated edge was overcounted by
the previous peak-only splitter and fit the recovery plan with one
full-width pass.

The zoned drive tries the original bits first, then two or three
interleaved 16- or 8-pixel lane masks. Each pass writes both planes and
activates full visible lines. Other lanes use the VCOM-aligned hold
selector. Use exact masks and preserve each gray pixel's starting
polarity and settle selector together. An over-budget plan stops if it
requires more than three zones. Row Repair can lower load at a cost in
texture. Column Stable makes stripes visible and is unsuitable for
measuring how small dither dots blend.

Three 16-pixel zones can leave a 48-pixel optical band. In the matched
HDR photo comparisons, a panel-wide hold after the B/W masks reduced
that component by about 36% relative to an equal-duration idle wait.
Applying the same masks to gray settle reduced it by about 57% in the
later comparison. Both are diagnostic results for the tested unit and
settings. An added hold pass changes the panel response, so use the same
hold and masking when capturing and validating a palette.

The boiler preset uses Factory Tuned, Row Repair 30%, Rail Budget 160,
Local Budget 40, and Burst Headroom 80. The accepted optical gray profile
used Bayer, Rail Budget 96, Local Budget 40, Panel Hold and matched gray
zones. These are starting points for this panel and are not electrical
ratings for other modules. Hold has been observed in test patterns.
Validate repeated hold and physical completion before enabling
differential updates on another panel.
