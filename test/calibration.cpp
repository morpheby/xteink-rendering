#include <xteink/calibration.h>

#include <cassert>
#include <iostream>
#include <vector>
struct Fake {
  enum class Status { Off, Ready, Fault, Sleeping };
  enum class Result {
    Pending,
    Complete,
    AssertionTimeout,
    CompletionTimeout,
    Invalid
  };
  struct Window {
    uint16_t x, y, width, height;
  };
  struct Plane {
    const uint8_t* data;
    size_t size;
  };
  Status state = Status::Off;
  Result result = Result::Complete;
  uint32_t bytes = 0;
  bool otp = false, vertical = false, fullBw = false, accepted = true;
  Window activation{};
  std::vector<std::vector<uint8_t>> uploads;
  std::vector<x4render::LutBank> banks;
  void reset() {
    state = Status::Off;
    bytes = 0;
  }
  Status status() const { return state; }
  uint32_t uploadedBytes() const { return bytes; }
  bool powerOn(uint32_t) {
    state = Status::Ready;
    return true;
  }
  bool powerOff(uint32_t) {
    state = Status::Off;
    return true;
  }
  Result poll(uint32_t) { return result; }
  bool upload(uint8_t, Plane p, Window w) {
    assert(p.size == 48000);
    uploads.emplace_back(p.data, p.data + p.size);
    bytes += w.width / 8 * w.height;
    return true;
  }
  bool activateOtp(bool, uint32_t) {
    otp = true;
    return true;
  }
  bool activate(const x4render::LutBank& bank, Window w, bool v, uint32_t,
                bool bw = false) {
    if (!accepted) return false;
    assert(w.x == 0 && w.width == 800);
    banks.push_back(bank);
    activation = w;
    vertical = v;
    fullBw = bw;
    return true;
  }
};
bool pixel(const std::vector<uint8_t>& p, int x, int y) {
  size_t i = y * 800 + x;
  return p[i / 8] & (0x80u >> (i % 8));
}
int main() {
  Fake t;
  x4render::Calibration<Fake> lab(t);
  assert(lab.allocate());
  assert(lab.start(0));
  assert(!lab.start(1));
  lab.poll(2);
  assert(t.otp);
  assert(!pixel(t.uploads[1], 140, 120));
  assert(pixel(t.uploads[1], 140, 250));
  lab.poll(3);
  assert(lab.busy());
  assert(lab.poll(4));
  assert(lab.cycles == 1 && lab.driveCount == 111600);
  assert(pixel(t.uploads[2], 560, 420));
  assert(!pixel(t.uploads[3], 560, 420));  // gray ref=10
  lab.croppedUploadValidated = true;
  lab.verticalWindowValidated = false;
  uint32_t before = t.bytes;
  assert(lab.start(10, false));
  assert(t.bytes - before ==
         96000);  // full activation never exposes stale cropped RAM

  assert(lab.poll(15));
  assert(lab.driveCount == 102600);
  assert(!pixel(t.uploads[4], 560, 420) &&
         !pixel(t.uploads[5], 560, 420));  // hold=00
  assert(!pixel(t.uploads[4], 30, 120) &&
         !pixel(t.uploads[5], 30, 120));  // held black
  assert(lab.start(20, true));
  lab.poll(21);
  lab.poll(22);
  assert(lab.driveCount == 111600);
  assert(lab.start(30, false, 0, true));
  assert(lab.poll(31));
  assert(lab.driveCount == 384000);
  assert(lab.start(40, false));
  lab.poll(41);
  lab.poll(42);
  assert(lab.driveCount == 111600);  // condition invalidates references
  lab.verticalWindowValidated = true;
  lab.croppedUploadValidated = true;
  assert(lab.start(50, false));
  assert(t.activation.x == 0 && t.activation.width == 800);
  t.result = Fake::Result::CompletionTimeout;
  assert(lab.poll(51));
  assert(lab.job() == decltype(lab)::Job::Failed);
  assert(lab.cycles == 5);  // failures never become completed observations
  lab.croppedUploadValidated = false;
  t.state = Fake::Status::Fault;
  t.result = Fake::Result::Complete;
  assert(!lab.start(57));  // faults never reset a possibly powered controller
  assert(lab.powerOff(58));
  assert(lab.poll(59));
  assert(lab.start(60));
  lab.poll(61);
  lab.poll(62);
  assert(lab.poll(63));
  assert(lab.uploadedBytes() ==
         192000);  // reset never underflows prior accounting
  assert(lab.powerOff(64));
  assert(lab.poll(65));
  assert(lab.cycles == 6);
  assert(lab.uploadedBytes() == 0 && lab.duration == 1);
  // Rebuild/rotate/repeat share one rotation state. Failed requests cannot
  // advance it, and the immutable programmed snapshot matches the actual bus.
  Fake rotated;
  x4render::Calibration<Fake> r(rotated);
  assert(r.allocate());
  r.parameters = {{{2, 2, 6, 1, 1}, {2, 4, 6, 1, 1}, {2, 6, 6, 1, 1}}};
  assert(r.start(0));
  r.poll(1);
  r.poll(2);
  r.poll(3);
  assert(r.rotation() == 0);
  assert(*r.settleLut(2) == rotated.banks.back()[2]);
  const auto saved = *r.settleLut(2);
  r.parameters[0].frames = 3;
  assert(*r.settleLut(2) == saved);
  r.parameters[0].frames = 2;
  assert(r.start(10, false, true));
  assert(r.rotation() == 1);
  assert(!r.start(11, false, true));
  assert(r.rotation() == 1);
  r.poll(12);
  assert(r.programmedRotation() == 1);
  assert(r.start(20, false));
  r.poll(21);
  assert(r.rotation() == 1);
  assert(r.start(30, true));
  r.poll(31);
  r.poll(32);
  assert(r.rotation() == 0);
  assert(r.start(40, false));
  r.poll(41);
  assert(r.rotation() == 0);
  // Programmable B/W duration belongs only to preparation. Factory OTP
  // remains available, and diagnostics cannot invent its unrecovered payload.
  r.bwPreparation = {true, 37, 2, 5};
  const auto uploadStart = rotated.uploads.size();
  assert(r.start(50, true));
  for (auto byte : rotated.uploads[uploadStart])
    assert(byte == 255);  // every visible pixel selects black or white drive
  assert(!pixel(rotated.uploads[uploadStart + 1], 140, 120));
  assert(pixel(rotated.uploads[uploadStart + 1], 140, 250));
  assert(rotated.banks.back() == x4render::preparation(r.bwPreparation, 0));
  assert(r.preparationLut(3)->at(1) == uint8_t(0x80 | 5));
  assert(r.preparationLut(3)->at(2) == uint8_t(0x40 | 37));
  assert(r.preparationLut(1)->at(1) == uint8_t(0x40 | 5));
  assert(r.preparationLut(1)->at(2) == uint8_t(0x80 | 37));
  r.poll(51);
  r.poll(52);
  assert(r.settleLut(2)->at(1) == 0x46);
  r.bwPreparation = {false, 37, 2};
  assert(r.start(60, true));
  assert(r.factoryPreparation() && !r.preparationLut(0));
  r.poll(61);
  r.poll(62);
  // Explicit test images stop after the selected B/W operation. No candidate
  // pass or stale held-reference assumption may follow them.
  for (bool conditioned : {false, true}) {
    r.bwPreparation.programmable = true;
    r.bwPreparation.conditioned = conditioned;
    for (auto image : {x4render::TestImage::White, x4render::TestImage::Black,
                       x4render::TestImage::Reference, x4render::TestImage::Random}) {
      const auto uploads = rotated.uploads.size(), banks = rotated.banks.size();
      assert(r.startImage(70, image, 0x1234));
      assert(!r.startImage(71, image, 5));
      assert(r.imageSeed() == 0x1234 && rotated.fullBw);
      assert(rotated.uploads.size() == uploads + 2);
      if (image == x4render::TestImage::White || image == x4render::TestImage::Black)
        for (auto b : rotated.uploads.back())
          assert(b == (image == x4render::TestImage::White ? 255 : 0));
      assert(r.poll(72));
      assert(rotated.banks.size() == banks + 1 && !r.settleLut(0));
    }
  }
  const auto beforeRebuild = rotated.uploads.size();
  assert(r.start(80, false));
  assert(rotated.uploads.size() == beforeRebuild + 2);  // image invalidates refs
  r.poll(81); r.poll(82);
  r.bwPreparation.pauseMs = 20;
  const auto beforePause = rotated.banks.size();
  assert(r.start(0xFFFFFFF0));
  assert(!r.poll(0xFFFFFFF5));
  assert(r.job() == decltype(r)::Job::Pause);
  assert(!r.poll(5) && rotated.banks.size() == beforePause + 1);
  assert(!r.poll(9) && rotated.banks.size() == beforePause + 2);
  assert(r.poll(10));
  x4render::Buffer imageA, imageB, imageC;
  assert(imageA.allocate(x4render::PlaneBytes));
  assert(imageB.allocate(x4render::PlaneBytes));
  assert(imageC.allocate(x4render::PlaneBytes));
  x4render::paintTestImage(imageA, x4render::TestImage::Random, 123);
  x4render::paintTestImage(imageB, x4render::TestImage::Random, 123);
  x4render::paintTestImage(imageC, x4render::TestImage::Random, 124);
  assert(std::equal(imageA.data(), imageA.data()+imageA.size(), imageB.data()));
  assert(!std::equal(imageA.data(), imageA.data()+imageA.size(), imageC.data()));
  Fake failing;
  x4render::Calibration<Fake> f(failing);
  assert(f.allocate() && f.start(0));
  f.poll(1);  // OTP base has started
  failing.accepted = false;
  assert(f.poll(2));  // settle failure must notify the owner, not silently idle
  assert(f.job() == decltype(f)::Job::Failed && !f.busy());
  std::cout << "calibration: endpoint preparation, wire selectors, held "
               "references, repeats, conditioning, timeout passed\n";
}
