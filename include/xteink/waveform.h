#pragma once
#include <array>
#include <cstdint>
namespace xteink {
using Lut = std::array<uint8_t,49>;
using LutBank = std::array<Lut,5>;
// Experimental UC8279 7-byte group interpretation, consistent with the SDK's
// recovered AA rows. NOT a verified matching-silicon datasheet decode. Keep
// rail codes/raw bytes visible in calibration. No physical voltage/time claims.
struct Pulse {
  uint8_t rail{1}, frames{4}, opposite{0}, repeats{1}, neutral{1};
  constexpr bool valid() const { return rail>=1 && rail<=2 && frames>=1 && frames<=63 &&
                                     opposite<=63 && repeats>=1 && repeats<=16 && neutral<=63; }
};
constexpr Lut pulse(Pulse p, uint8_t holdRail=3) {
  Lut out{};
  if(!p.valid()) return out;
  out[0]=p.repeats;
  out[1]=uint8_t((p.rail<<6)|p.frames);
  out[2]=p.opposite ? uint8_t(((3-p.rail)<<6)|p.opposite) : 0;
  out[3]=uint8_t((holdRail<<6)|p.neutral); out[4]=0; out[5]=1; out[6]=1;
  return out;
}
constexpr Lut held(uint8_t duration, uint8_t repeats, uint8_t rail=3) {
  Lut out{}; out[0]=repeats; out[1]=uint8_t((rail<<6)|duration); out[5]=out[6]=1; return out;
}
// Register mapping uses direct WIRE DTM1/DTM2 bits (no hidden inversion).
// 00->BB/R24, 01->BW/R22, 10->WB/R23, 11->WW/R21.
constexpr uint8_t Row[4]={4,2,3,1};
constexpr LutBank candidates(std::array<Pulse,3> p, uint8_t holdRail=3) {
  LutBank out{};
  uint16_t longest=1;
  const uint8_t repeats=p[0].repeats;
  for(auto v:p) if(v.frames+v.opposite+v.neutral>longest) longest=v.frames+v.opposite+v.neutral;
  for(uint8_t s=0;s<3;++s) {
    auto& lut=out[Row[s+1]]; lut=pulse(p[s],holdRail); lut[0]=repeats;
    uint16_t remaining=longest-p[s].frames-p[s].opposite;
    for(uint8_t phase=3;phase<=4;++phase) {
      const uint8_t n=remaining>63?63:uint8_t(remaining);
      lut[phase]=uint8_t((holdRail<<6)|n); remaining-=n;
    }
  }
  for(uint8_t row: {uint8_t(0),uint8_t(4)}) {
    auto& lut=out[row]; lut[0]=repeats; lut[5]=lut[6]=1;
    uint16_t left=longest;
    for(uint8_t phase=1;phase<=4;++phase) { uint8_t n=left>63?63:uint8_t(left); lut[phase]=uint8_t(((row?holdRail:0)<<6)|n);left-=n; }
  }
  return out;
}
static_assert(pulse(Pulse{})[1]==0x44,"wire encoding changed");
static_assert(!Pulse{1,64,0,1,0}.valid(),"phase overflow must fail");
} // namespace xteink
