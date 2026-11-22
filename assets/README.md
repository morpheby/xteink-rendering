# Landscape test asset

Ansel Adams, **The Tetons - Snake River**, Grand Teton National Park, 1942.
National Archives identifier **519904**, local identifier **79-AAG-1**.

Source and original scan:
https://www.archives.gov/research/still-pictures/highlights/the-tetons-snake-river
https://www.archives.gov/files/research/still-pictures/079-aag-01-14-1374m.jpg

The USGS identifies this 1942 photograph as public domain:
https://www.usgs.gov/media/images/ansel-adams-photo-snake-river-and-teton-range

`tetons-source.jpg` preserves the downloaded scan. `landscape-preview.png` is a
cropped 800x480 grayscale input with the scan border removed and a 1% contrast
stretch. `include/xteink/landscape.h` packs that input to 16 luma levels, two
pixels per byte, for a fixed 192000-byte flash asset. No download occurs on-device.

The dither test uses the ordinary deterministic B/W quantizer. The gray trial
uses eight **provisional** evenly spaced input buckets: black, three current
A/B/C doses from black, three A/B/C doses from white, and white. These input
buckets are not measured output tones and do not establish a calibrated palette.
It uses full-screen upload/activation, leaving its title and credit in B/W.
