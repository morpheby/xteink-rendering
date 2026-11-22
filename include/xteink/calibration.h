#pragma once
#include "rendering.h"
#include "waveform.h"

namespace xteink {
// Display-independent diagnostic layout. All drawing is into two reusable
// packed WIRE planes; nothing may mutate them while the transport is busy.
template<class Transport> class Calibration {
 public:
  enum class Job : uint8_t { Idle, PowerOn, Base, Settle, Conditioning, Failed };
  explicit Calibration(Transport& transport): transport_(transport) {}
  bool allocate() { return first_.allocate(PlaneBytes) && second_.allocate(PlaneBytes); }
  bool busy() const { return job_!=Job::Idle && job_!=Job::Failed; }
  Job job() const { return job_; }
  const char* result() const { return result_; }
  uint32_t duration{0}, driveCount{0}, cycles{0};
  Rect requested{}, expanded{};
  std::array<Pulse,3> parameters{{{1,2,0,1,1},{1,4,0,1,1},{1,6,0,1,1}}};
  uint8_t holdRail{3};
  bool verticalWindowValidated{false}, croppedUploadValidated{false}, verbose{false};
  bool start(uint32_t now, bool prepare=true, uint8_t rotation=0, bool conditioning=false) {
    if(busy() || !first_.data()) return false;
    for(auto p:parameters) if(!p.valid() || p.repeats!=parameters[0].repeats) return false;
    rotation_=rotation%3; conditioning_=conditioning; prepare_=prepare || !references_;
    if(conditioning) prepare_=false;
    started_=now; bytesAtStart_=transport_.uploadedBytes(); duration=0;
    result_="running";
    if(transport_.status()==Transport::Status::Fault || transport_.status()==Transport::Status::Sleeping) {
      transport_.reset(); references_=false;
      if(!conditioning) prepare_=true;
    }
    if(transport_.status()==Transport::Status::Off) {
      if(!transport_.powerOn(now)) return fail();
      job_=Job::PowerOn; return true;
    }
    return beginPass(now);
  }
  bool poll(uint32_t now) {
    if(!busy()) return false;
    const auto r=transport_.poll(now);
    if(r==Transport::Result::Pending) return false;
    if(r!=Transport::Result::Complete) {
      result_=r==Transport::Result::AssertionTimeout?"BUSY assertion timeout":"BUSY completion timeout";
      job_=Job::Failed; references_=false; duration=now-started_;return true;
    }
    if(job_==Job::PowerOn) { beginPass(now); return false; }
    if(job_==Job::Base) { prepare_=false; beginSettle(now); return false; }
    duration=now-started_; cycles++; references_=!conditioning_; job_=Job::Idle; result_="complete";
    uploaded_=transport_.uploadedBytes()-bytesAtStart_; return true;
  }
  uint32_t uploadedBytes() const { return uploaded_; }
  void invalidate() { references_=false; job_=Job::Idle; }
 private:
  bool fail() { result_="invalid transaction"; job_=Job::Failed; references_=false; return false; }
  void rect(Buffer& plane, Rect r, bool value) {
    for(uint16_t y=r.y;y<r.y+r.height;++y) for(uint16_t x=r.x;x<r.x+r.width;++x) bit(plane,size_t(y)*Width+x,value);
  }
  static std::array<uint8_t,7> glyph(char c) {
    // Original compact 5x7 lettering; no borrowed font dependency.
    switch(c) {
      case 'A':return{14,17,17,31,17,17,17};case 'B':return{30,17,17,30,17,17,30};
      case 'C':return{14,17,16,16,16,17,14};case 'D':return{30,17,17,17,17,17,30};
      case 'E':return{31,16,16,30,16,16,31};case 'F':return{31,16,16,30,16,16,16};
      case 'G':return{14,17,16,23,17,17,14};case 'H':return{17,17,17,31,17,17,17};
      case 'I':return{31,4,4,4,4,4,31};case 'J':return{7,2,2,2,18,18,12};
      case 'K':return{17,18,20,24,20,18,17};case 'L':return{16,16,16,16,16,16,31};
      case 'M':return{17,27,21,21,17,17,17};case 'N':return{17,25,21,19,17,17,17};
      case 'O':return{14,17,17,17,17,17,14};case 'P':return{30,17,17,30,16,16,16};
      case 'Q':return{14,17,17,17,21,18,13};case 'R':return{30,17,17,30,20,18,17};
      case 'S':return{15,16,16,14,1,1,30};case 'T':return{31,4,4,4,4,4,4};
      case 'U':return{17,17,17,17,17,17,14};case 'V':return{17,17,17,17,17,10,4};
      case 'W':return{17,17,17,21,21,21,10};case 'X':return{17,17,10,4,10,17,17};
      case 'Y':return{17,17,10,4,4,4,4};case 'Z':return{31,1,2,4,8,16,31};
      case '0':return{14,17,19,21,25,17,14};case '1':return{4,12,4,4,4,4,14};
      case '2':return{14,17,1,2,4,8,31};case '3':return{30,1,1,14,1,1,30};
      case '4':return{2,6,10,18,31,2,2};case '5':return{31,16,16,30,1,1,30};
      case '6':return{14,16,16,30,17,17,14};case '7':return{31,1,2,4,8,8,8};
      case '8':return{14,17,17,14,17,17,14};case '9':return{14,17,17,15,1,1,14};
      case '-':return{0,0,0,31,0,0,0};case '=':return{0,0,31,0,31,0,0};
      default:return{};
    }
  }
  void text(uint16_t x,uint16_t y,const char* value,uint8_t scale=2) {
    for(;*value;++value,x+=6*scale) {
      auto g=glyph(*value);
      for(uint8_t gy=0;gy<7;++gy) for(uint8_t gx=0;gx<5;++gx)
        if(g[gy]&(1u<<(4-gx))) rect(second_,{uint16_t(x+gx*scale),uint16_t(y+gy*scale),scale,scale},false);
    }
  }
  void number(uint16_t x,uint16_t y,uint16_t n) {
    char chars[4]={char('0'+(n/100)%10),char('0'+(n/10)%10),char('0'+n%10),0}; text(x,y,chars);
  }
  bool beginPass(uint32_t now) {
    if(!prepare_) return beginSettle(now);
    references_=false;
    first_.clear(255); second_.clear(255);
    text(18,12,"X4 WAVEFORM LAB - EXPERIMENTAL",3);
    text(18,43,"RAW RAIL CODES - BRIGHTNESS ORDER UNKNOWN");
    for(uint8_t c=0;c<3;++c) {
      uint16_t x=uint16_t(130+c*215);
      char label[2]={char('A'+c),0}; text(x+75,72,label,3);
      rect(second_,{x,110,190,90},false);
      text(x,207,"BLACK START");text(x,337,"WHITE START");
      text(x,360,"F");number(x+15,360,parameters[c].frames);
      text(x+65,360,"R");number(x+80,360,parameters[c].repeats);
      text(x+130,360,"P");number(x+145,360,parameters[c].rail);
    }
    rect(second_,{20,110,80,90},false);text(20,207,"HOLD B");text(20,337,"HOLD W");
    text(18,387,"HELD REFERENCES");
    rect(second_,{20,413,170,45},false);
    text(220,432,"WHITE");text(550,388,"GRAY REF");
    text(18,466,"REPEAT HOLDS REFERENCES - REBUILD RESETS STARTS",1);
    if(!upload({0,0,800,480}) || !transport_.activateOtp(false,now)) return fail();
    job_=Job::Base;return true;
  }
  bool upload(Rect r) {
    typename Transport::Window w{r.x,r.y,r.width,r.height};
    return transport_.upload(0x10,{first_.data(),first_.size()},w) &&
           transport_.upload(0x13,{second_.data(),second_.size()},w);
  }
  void select(Rect r,uint8_t selector) {
    rect(first_,r,selector&2);rect(second_,r,selector&1);driveCount+=uint32_t(r.width)*r.height;
    requested.include(r.x,r.y);requested.include(r.x+r.width-1,r.y+r.height-1);
  }
  bool beginSettle(uint32_t now) {
    first_.clear();second_.clear();driveCount=0;requested={};
    if(conditioning_) select({0,0,800,480},1);
    else {
      for(uint8_t c=0;c<3;++c) {
        select({uint16_t(130+c*215),110,190,90},uint8_t(1+(c+rotation_)%3));
        select({uint16_t(130+c*215),240,190,90},uint8_t(1+(c+rotation_)%3));
      }
      if(!references_) select({550,413,200,45},2);
    }
    expanded=requested.fullLines();
    // Cropped uploads stay disabled until the specific addressing behavior is
    // measured. Even when enabled, activation is restored to full visible lines.
    Rect uploadRect=croppedUploadValidated ? expanded : Rect{0,0,800,480};
    if(!upload(uploadRect)) return fail();
    const auto bank=candidates(parameters,holdRail);
    if(!transport_.activate(bank,{0,expanded.y,800,expanded.height},verticalWindowValidated,now)) return fail();
    job_=conditioning_?Job::Conditioning:Job::Settle;return true;
  }
  Transport& transport_;
  Buffer first_,second_;
  Job job_{Job::Idle}; bool references_{false},prepare_{true},conditioning_{false};
  uint8_t rotation_{0}; uint32_t started_{0},bytesAtStart_{0},uploaded_{0};
  const char* result_{"idle"};
};
} // namespace xteink
