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
  bool otp = false, vertical = false;
  Window activation{};
  std::vector<std::vector<uint8_t>> uploads;
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
  bool activate(const x4render::LutBank&, Window w, bool v, uint32_t) {
    assert(w.x == 0 && w.width == 800);
    activation = w;
    vertical = v;
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
  lab.croppedUploadValidated=true;
  lab.verticalWindowValidated=false;
  uint32_t before=t.bytes;
  assert(lab.start(10, false));
  assert(t.bytes-before==96000); // full activation never exposes stale cropped RAM

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
  lab.croppedUploadValidated=false;
  t.state = Fake::Status::Fault;
  t.result = Fake::Result::Complete;
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
  std::cout << "calibration: endpoint preparation, wire selectors, held "
               "references, repeats, conditioning, timeout passed\n";
}
