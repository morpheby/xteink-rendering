"""Regenerate the flash asset from the preserved public-domain scan (Pillow)."""
from pathlib import Path
from PIL import Image, ImageOps

root = Path(__file__).resolve().parents[1]
image = Image.open(root/'assets/tetons-source.jpg').convert('L')
image = image.crop((8, 8, image.width-8, image.height-8))
image = ImageOps.fit(image, (800, 480), Image.Resampling.LANCZOS, centering=(.5,.55))
image = ImageOps.autocontrast(image, cutoff=1)
image.save(root/'assets/landscape-preview.png')
levels = [min(15, (v+8)//17) for v in image.tobytes()]
packed = bytes((levels[i]<<4)|levels[i+1] for i in range(0,len(levels),2))
header = '''#pragma once
#include <cstdint>
// Ansel Adams, The Tetons - Snake River, 1942. NARA 519904. Public domain.
// See assets/README.md. Generated from the preserved source; two 4-bit lumas/byte.
namespace x4render {
inline constexpr uint8_t LandscapePixels[] = {
'''
header += '\n'.join('  '+','.join(f'0x{b:02X}' for b in packed[i:i+24])+','
                    for i in range(0,len(packed),24))
header += '''
};
static_assert(sizeof(LandscapePixels) == 192000, "landscape dimensions");
inline uint8_t landscapeLuma(uint32_t index) {
  const uint8_t packed = LandscapePixels[index / 2];
  return uint8_t(((index & 1) ? (packed & 15) : (packed >> 4)) * 17);
}
// Provisional test buckets, NOT a measured panel palette.
inline uint8_t landscapeBucket(uint32_t index) {
  return uint8_t((uint16_t(landscapeLuma(index)) * 7 + 127) / 255);
}
}  // namespace x4render
'''
(root/'include/xteink/landscape.h').write_text(header)
