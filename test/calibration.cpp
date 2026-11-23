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
  unsigned otpCalls = 0;
  Window activation{};
  std::vector<std::vector<uint8_t>> uploads;
  std::vector<x4render::LutBank> banks;
  std::vector<uint8_t> commands, hiddenFills;
  std::vector<uint8_t> oldRam, newRam;
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
  bool upload(uint8_t command, Plane p, Window w, uint8_t hiddenFill = 0) {
    assert(p.size == 48000);
    commands.push_back(command);
    hiddenFills.push_back(hiddenFill);
    // Model a controller that advances OLD when NEW transmission begins.
    // Explicit OLD must be written afterward; absolute B/W must tolerate OLD.
    if (command == 0x13) { oldRam = newRam; newRam.assign(p.data,p.data+p.size); }
    else oldRam.assign(p.data,p.data+p.size);
    uploads.emplace_back(p.data, p.data + p.size);
    bytes += w.width / 8 * w.height;
    return true;
  }
  bool activateOtp(bool, uint32_t) {
    otp = true;
    ++otpCalls;
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
  assert(!pixel(t.uploads[0], 140, 120));
  assert(pixel(t.uploads[0], 140, 250));
  lab.poll(3);
  assert(lab.busy());
  assert(lab.poll(4));
  assert(lab.cycles == 1 && lab.driveCount == 111600);
  assert(pixel(t.uploads[3], 560, 420));
  assert(!pixel(t.uploads[2], 560, 420));  // gray ref=10
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
  assert(rotated.commands[uploadStart] == 0x13 && rotated.commands[uploadStart+1] == 0x10);
  for (size_t i=0; i<48000; ++i) {
    assert(rotated.oldRam[i] == uint8_t(~rotated.newRam[i]));
    assert(rotated.oldRam[i] == rotated.uploads[uploadStart+1][i]);
  }
  assert(!pixel(rotated.uploads[uploadStart], 140, 120));
  assert(pixel(rotated.uploads[uploadStart], 140, 250));
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
  // Pure clears stop after one pass. Image targets with gray use both passes
  // and do not establish a held-reference calibration chart.
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
        for (auto b : rotated.uploads[uploads])
          assert(b == (image == x4render::TestImage::White ? 255 : 0));
      if (x4render::usesRelativeGray(image)) {
        assert(!r.poll(72) && r.poll(73));
        assert(rotated.banks.size() == banks + 2 && r.settleLut(0));
        assert(rotated.uploads.size() == uploads + 4);
      } else {
        assert(r.poll(72));
        assert(rotated.banks.size() == banks + 1 && !r.settleLut(0));
      }
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
  Fake priming;
  x4render::Calibration<Fake> p(priming);
  assert(p.allocate());
  p.bwPreparation.programmable = p.bwPreparation.factoryPrime = true;
  assert(p.startImage(0, x4render::TestImage::Black));
  p.poll(1);
  assert(p.job() == decltype(p)::Job::Prime && priming.otpCalls == 1);
  p.poll(2);
  assert(p.job() == decltype(p)::Job::Base && priming.uploads.size() == 4);
  assert(p.poll(3));
  assert(p.startImage(4, x4render::TestImage::White));
  assert(p.poll(5) && priming.otpCalls == 1);  // no repeated warm primer
  assert(p.powerOff(6) && p.poll(7));
  assert(p.startImage(8, x4render::TestImage::White)); p.poll(9);
  assert(p.job() == decltype(p)::Job::Prime && priming.otpCalls == 2);
  Fake landscape;
  x4render::Calibration<Fake> photo(landscape);
  assert(photo.allocate());
  assert(photo.startImage(0, x4render::TestImage::LandscapeDither));
  photo.poll(1); assert(photo.poll(2) && landscape.banks.empty());
  assert(photo.startImage(3, x4render::TestImage::LandscapeGray));
  photo.poll(4);
  assert(photo.busy() && photo.driveCount > 100000);
  assert(!landscape.vertical && landscape.activation.height == 480);
  assert(photo.poll(5));
  for (auto image : {x4render::TestImage::NeighborSweep, x4render::TestImage::LinearRamp}) {
    const auto start = landscape.uploads.size();
    assert(photo.startImage(6, image));
    assert(!photo.poll(7) && photo.poll(8));
    assert(landscape.uploads.size() == start + 4);
    for (const auto position : {std::pair{0, 100}, std::pair{400, 100}, std::pair{799, 128}}) {
      const auto x = position.first, y = position.second;
      const auto shade = [&] {
        x4render::Shade q{255,1,0};
        x4render::Settings settings;
        settings.mode = x4render::Mode::DitherSettle;
        settings.palette = photo.imagePalette;
        x4render::paintRelativeShades(image, 0, settings,
          [&](int px, int py, auto candidate) {
            if (px == x && py == y) q = candidate;
          });
        return q;
      }();
      // The old/new transport planes carry the same pixel's base and selector.
      assert(pixel(landscape.uploads[start], x, y) == bool(shade.base));
      assert(pixel(landscape.uploads[start + 2], x, y) == bool(shade.selector & 1));
      assert(pixel(landscape.uploads[start + 3], x, y) == bool(shade.selector & 2));
    }
    if (image == x4render::TestImage::LinearRamp)
      for (int y = 278; y < 460; ++y)
        for (int x = 0; x < 800; ++x)
          assert(!pixel(landscape.uploads[start + 2], x, y) &&
                 !pixel(landscape.uploads[start + 3], x, y));
  }
  const auto ordinary = landscape.uploads[landscape.uploads.size() - 4];
  for (const auto image : {x4render::TestImage::RampBase, x4render::TestImage::RampInverse}) {
    const auto start = landscape.uploads.size();
    const auto bankCount = landscape.banks.size();
    assert(photo.startImage(9, image));
    assert(photo.poll(10) && !photo.settleLut(0));
    assert(landscape.uploads.size() == start + 2);
    assert(landscape.banks.size() == bankCount);  // this fixture uses Factory OTP
    for (size_t i = 0; i < ordinary.size(); ++i) {
      const auto target = image == x4render::TestImage::RampInverse ? uint8_t(~ordinary[i]) : ordinary[i];
      assert(landscape.uploads[start][i] == target);
      assert(landscape.uploads[start + 1][i] == uint8_t(~target));
    }
  }
  // The load controls share the B/W recipe, reference patches, and line density.
  photo.bwPreparation.programmable = true;
  const auto loadBankStart = landscape.banks.size();
  const auto loadPlaneStart = landscape.uploads.size();
  for (auto image : {x4render::TestImage::LoadStripes, x4render::TestImage::LoadCheckerboard}) {
    const auto uploads = landscape.uploads.size();
    assert(photo.startImage(9, image) && photo.poll(10));
    assert(landscape.uploads.size() == uploads + 2 && landscape.fullBw && !photo.settleLut(0));
    const auto& target = landscape.uploads[uploads];
    const bool checkerboard = image == x4render::TestImage::LoadCheckerboard;
    for (int y = 0; y < x4render::Height; ++y) {
      const bool flipped = checkerboard && (y & 1);
      int patchStart = -1;
      if (y >= 72 && y < 136) patchStart = 16;
      if (y >= 208 && y < 272) patchStart = 144;
      if (y >= 344 && y < 408) patchStart = 280;
      unsigned white = 0, switches = 0;
      for (int x = 0; x < x4render::Width; ++x) {
        const bool bit = pixel(target, x, y);
        bool expected = (x % 2 == 0) != flipped;
        if (patchStart >= 0) {
          for (int start : {patchStart, patchStart + 400}) {
            const int offset = x - start;
            if (offset >= 0 && offset < 96)
              expected = offset >= 64 || (offset >= 32 && (offset / 2) % 2 == 0);
          }
        }
        assert(bit == expected);
        white += bit;
        if (y) switches += bit != pixel(target, x, y - 1);
      }
      assert(white == x4render::Width / 2);
      if (y == 72 || y == 136 || y == 208 || y == 272 || y == 344 || y == 408)
        assert(switches == (checkerboard ? 704 : 96));
      else if (y && checkerboard)
        assert(switches == (patchStart >= 0 ? 608 : 800));
      else
        assert(switches == 0);
    }
  }
  assert(landscape.banks.size() == loadBankStart + 2 &&
         landscape.banks[loadBankStart] == landscape.banks[loadBankStart + 1]);
  photo.bwPreparation.programmable = false;
  const auto otpCount = landscape.otpCalls;
  const auto otpPlaneStart = landscape.uploads.size();
  for (auto image : {x4render::TestImage::LoadStripes, x4render::TestImage::LoadCheckerboard}) {
    assert(photo.startImage(11, image) && photo.poll(12));
    assert(photo.factoryPreparation() && !photo.settleLut(0));
  }
  assert(landscape.otpCalls == otpCount + 2 && landscape.uploads.size() == otpPlaneStart + 4);
  for (size_t i = 0; i < 4; ++i)
    assert(landscape.uploads[loadPlaneStart + i] == landscape.uploads[otpPlaneStart + i]);

  // In the strip test the switch count is exact on every visible row after
  // the first, including in the reference lanes. Each rail sees half the changes.
  constexpr std::pair<uint32_t, int> stripWidths[] = {
      {0, 0}, {240, 240}, {320, 320}, {400, 400}, {576, 576}, {255, 240}, {800, 576}};
  for (const auto& [requested, width] : stripWidths) {
    const auto uploads = landscape.uploads.size();
    const auto activations = landscape.otpCalls;
    assert(photo.startImage(13, x4render::TestImage::LoadStrip, requested) && photo.poll(14));
    assert(photo.stripWidth() == width && landscape.uploads.size() == uploads + 2);
    assert(landscape.otpCalls == activations + 1 && photo.factoryPreparation() && !photo.settleLut(0));
    const auto& target = landscape.uploads[uploads];
    for (int y = 0; y < x4render::Height; ++y) {
      unsigned white = 0, toWhite = 0, toBlack = 0;
      for (int x = 0; x < x4render::Width; ++x) {
        const bool center = x >= (800 - width) / 2 && x < (800 + width) / 2;
        bool expected = (x % 2 == 0) != (center && (y % 2 == 1));
        for (int start : {16, 688}) {
          const int offset = x - start;
          if (offset >= 0 && offset < 96)
            expected = offset >= 64 || (offset >= 32 && (offset / 2) % 2 == 0);
        }
        const bool bit = pixel(target, x, y);
        assert(bit == expected);
        assert(pixel(landscape.uploads[uploads + 1], x, y) != bit);
        white += bit;
        if (y && bit != pixel(target, x, y - 1)) {
          toWhite += bit;
          toBlack += !bit;
        }
      }
      assert(white == x4render::Width / 2);
      assert(toWhite == (y ? unsigned(width / 2) : 0) && toBlack == toWhite);
    }
    if (requested == 320) {
      photo.bwPreparation.programmable = true;
      assert(photo.startImage(15, x4render::TestImage::LoadStrip, requested) && photo.poll(16));
      assert(landscape.fullBw && !photo.settleLut(0) && landscape.uploads.size() == uploads + 4);
      assert(landscape.otpCalls == activations + 1);
      assert(landscape.uploads[uploads] == landscape.uploads[uploads + 2]);
      assert(landscape.uploads[uploads + 1] == landscape.uploads[uploads + 3]);
      photo.bwPreparation.programmable = false;
    }
  }

  // Equal whole-row peaks can have different local and sustained loads.
  x4render::Buffer loadProbe;
  assert(loadProbe.allocate(x4render::PlaneBytes));
  std::array<x4render::BwLoad, 4> probes;
  for (unsigned pattern = 0; pattern < probes.size(); ++pattern) {
    x4render::paintTestImage(loadProbe, x4render::TestImage::LoadStrip, (pattern << 16) | 320);
    probes[pattern] = x4render::measureBwLoad(loadProbe);
    assert(probes[pattern].peak == 160 && probes[pattern].firstWhite == 400);
  }
  assert(probes[0].localPeak > probes[1].localPeak);
  assert(probes[0].direction[0].total == probes[1].direction[0].total);
  assert(probes[2].direction[0].total == probes[3].direction[0].total);
  assert(probes[2].recentPeak < probes[3].recentPeak);
  x4render::LutBank phaseBank{};
  for (auto& row : phaseBank) row = x4render::Lut{1, 2, 1, 0, 0, 1, 1};
  phaseBank[x4render::Row[1]][1] = phaseBank[x4render::Row[3]][1] = 0x42;
  phaseBank[x4render::Row[2]][1] = 0x82;
  phaseBank[x4render::Row[1]][2] = phaseBank[x4render::Row[3]][2] = 0x81;
  const auto charge = x4render::estimateBwSourceCharge(probes[0], x4render::Width, phaseBank, false);
  assert(charge.frames == 3 && charge.both == 320 && charge.one == 160 && charge.boundary >= 800);
  assert(charge.total[0] && charge.total[1] && charge.recent > 0 && charge.sustained > 0);
  assert(x4render::estimateBwSourceCharge(probes[1], x4render::Width, phaseBank, false).recent < charge.recent);

  // Random timing preserves the exact total per rail and leaves the fixed
  // references alone. The jittered probe has full bursts, the staggered one
  // spreads the same dose over all visible row pairs.
  x4render::Buffer noiseControl;
  assert(noiseControl.allocate(x4render::PlaneBytes));
  for (unsigned width : {0u, 16u, 160u, 320u, 576u}) {
    x4render::paintTestImage(noiseControl, x4render::TestImage::LoadStrip,
                             (uint32_t(x4render::StripPattern::BurstOne) << 16) | width);
    const auto control = x4render::estimateBwSourceCharge(x4render::measureBwLoad(noiseControl),
                                                          x4render::Width, phaseBank, false);
    for (auto mode : {x4render::StripPattern::Jittered, x4render::StripPattern::Staggered}) {
      const uint32_t argument = (uint32_t(mode) << 16) | width;
      x4render::paintTestImage(loadProbe, x4render::TestImage::LoadStrip, argument);
      const auto load = x4render::measureBwLoad(loadProbe);
      const bool staggered = mode == x4render::StripPattern::Staggered;
      assert(load.peak == (staggered && width ? width / 4 + 1 : width / 2));
      for (const auto& rail : load.direction) assert(rail.total == x4render::Height / 2 * width / 2);
      const auto estimate = x4render::estimateBwSourceCharge(load, x4render::Width, phaseBank, false);
      assert(estimate.total == control.total && estimate.boundary == control.boundary);
      unsigned burst = 0;
      for (size_t y = 0; y < x4render::Height; ++y) {
        const auto row = y * (x4render::Width / 8);
        unsigned whites = 0, switches = 0;
        for (size_t x = 0; x < x4render::Width / 8; ++x) {
          whites += x4render::bwPopcount(loadProbe[row + x]);
          if (y) switches += x4render::bwPopcount(loadProbe[row + x] ^ loadProbe[row + x - x4render::Width / 8]);
          if (x < (x4render::Width - width) / 16 || x >= (x4render::Width + width) / 16)
            assert(loadProbe[row + x] == noiseControl[row + x]);
        }
        assert(whites == 400);
        if (!y) continue;
        if (staggered) {
          assert(switches == width / 2 || switches == width / 2 + 2);
        } else {
          assert(switches == 0 || switches == width);
          burst = switches ? burst + 1 : 0;
          assert(burst <= 2);
        }
      }
    }
  }

  // An all-white/all-black alternating row puts every transition on one rail.
  x4render::Buffer oneDirection;
  assert(oneDirection.allocate(x4render::PlaneBytes));
  for (int y = 0; y < x4render::Height; ++y)
    std::memset(oneDirection.data() + y * 100, y % 2 ? 255 : 0, 100);
  const auto unbalanced = x4render::planBwStrips(oneDirection, 80);
  assert(unbalanced.count == 10);
  for (unsigned n = 0; n < unbalanced.count; ++n)
    assert(unbalanced.ends[n] == (n + 1) * 80);

  Fake splitTransport;
  splitTransport.state = Fake::Status::Ready;
  x4render::Calibration<Fake> split(splitTransport);
  assert(split.allocate());
  assert(!split.startImage(17, x4render::TestImage::SplitCheckerboard, 80));
  split.bwPreparation.programmable = true;
  const auto source = x4render::preparation(split.bwPreparation);
  split.factoryDerivedBw = source;
  assert(split.startImage(18, x4render::TestImage::SplitCheckerboard, 80));
  assert(split.stripCount() == 5 && splitTransport.otpCalls == 0);
  const auto& checkerboard = landscape.uploads[loadPlaneStart + 2];
  for (unsigned n = 0; n < 5; ++n) {
    const size_t upload = 2 * n;
    assert(splitTransport.uploads.size() == upload + 2 && splitTransport.fullBw && !splitTransport.vertical);
    assert(splitTransport.commands[upload] == 0x13 && splitTransport.commands[upload + 1] == 0x10);
    assert(splitTransport.hiddenFills[upload] == 0 && splitTransport.hiddenFills[upload + 1] == 0);
    const auto& bank = splitTransport.banks[n];
    for (unsigned row = 0; row < 5; ++row)
      assert(bank[row] == source[row == x4render::Row[0] || row == x4render::Row[3] ? 0 : row]);
    for (int y = 0; y < x4render::Height; ++y) {
      for (int x = 0; x < x4render::Width; ++x) {
        const bool active = unsigned(x) / 160 == n;
        const bool target = pixel(checkerboard, x, y);
        assert(pixel(splitTransport.uploads[upload], x, y) == (active && target));
        assert(pixel(splitTransport.uploads[upload + 1], x, y) == (active && !target));
      }
    }
    assert(split.poll(19 + n) == (n == 4));
  }
  assert(!split.busy() && split.cycles == 1 && split.driveCount == x4render::Pixels);
  assert(!split.settleLut(0) && split.uploadedBytes() == 10 * x4render::PlaneBytes);
  assert(split.startImage(25, x4render::TestImage::SplitCheckerboard, 80));
  splitTransport.result = Fake::Result::CompletionTimeout;
  assert(split.poll(26) && !split.busy() && split.cycles == 1);
  assert(splitTransport.banks.size() == 6);

  // The two approximation modes use the same B/W transport and have no settle.
  Fake textureTransport;
  textureTransport.state = Fake::Status::Ready;
  x4render::Calibration<Fake> texture(textureTransport);
  assert(texture.allocate());
  texture.bwPreparation.programmable = true;
  texture.factoryDerivedBw = source;
  for (const auto scene : {x4render::TestImage::BwPhoto, x4render::TestImage::BwChecker}) {
    for (const auto mode : {x4render::BwTexture::Columns, x4render::BwTexture::Repair}) {
      texture.bwTexture = mode;
      const auto uploads = textureTransport.uploads.size();
      assert(texture.startImage(27, scene) && texture.poll(28));
      assert(texture.requestedBwLoad().peak > 80 && texture.renderedBwLoad().peak <= 80);
      assert(texture.renderedBwLoad().localPeak <= 60 && texture.renderedBwLoad().recentPeak <= 40);
      assert(texture.renderedBwTexture() == mode && !texture.stripCount() && !texture.settleLut(0));
      assert(textureTransport.uploads.size() == uploads + 2 && textureTransport.fullBw);
      const auto& output = textureTransport.uploads[uploads];
      for (size_t i = 0; i < x4render::PlaneBytes; ++i)
        assert(textureTransport.uploads[uploads + 1][i] == uint8_t(~output[i]));
      if (scene == x4render::TestImage::BwChecker && mode == x4render::BwTexture::Columns)
        for (uint16_t y = 0; y < x4render::Height; ++y)
          for (uint16_t x = 0; x < x4render::Width; ++x)
            assert(pixel(output, x, y) ==
                   (x4render::isLoadPatch(x, y) ? pixel(checkerboard, x, y) : (x % 2 == 0)));
    }
  }
  texture.bwSplitGuard = false;
  for (const auto mode : {x4render::BwTexture::Columns, x4render::BwTexture::Repair}) {
    texture.bwTexture = mode;
    texture.columnStrength = texture.repairStrength = 0;
    const auto uploads = textureTransport.uploads.size();
    assert(texture.startImage(27, x4render::TestImage::BwChecker) && texture.poll(28));
    assert(textureTransport.uploads[uploads] == checkerboard);
    texture.columnStrength = texture.repairStrength = 75;
    assert(texture.startImage(27, x4render::TestImage::BwChecker) && texture.poll(28));
    assert(texture.bwStrength() == 75 && !texture.stripCount());
    assert(texture.renderedBwLoad().peak > 80 && texture.renderedBwLoad().peak < 400);
  }
  texture.bwSplitGuard = true;
  texture.columnStrength = texture.repairStrength = 100;

  // A protected full-width 1 px rule must be drawn exactly, using the split
  // path when one scan line exceeds the directed transition limit.
  x4render::Buffer boiler;
  assert(boiler.allocate(x4render::Pixels));
  boiler.clear(255);
  std::memset(boiler.data() + 90 * x4render::Width, 0, x4render::Width);
  assert(!texture.startImage(29, x4render::TestImage::BwBoiler));
  texture.boilerLuma = boiler.data();
  for (const auto mode : {x4render::BwTexture::Columns, x4render::BwTexture::Repair}) {
    texture.bwTexture = mode;
    const auto uploads = textureTransport.uploads.size();
    assert(texture.startImage(30, x4render::TestImage::BwBoiler));
    assert(texture.stripCount() == 20 && texture.protectedFallback() == (mode == x4render::BwTexture::Repair));
    assert(texture.renderedBwLoad().peak == 800);
    for (size_t n = 0; n < 20; ++n) {
      const auto& output = textureTransport.uploads[uploads + 2 * n];
      const auto& old = textureTransport.uploads[uploads + 2 * n + 1];
      for (uint16_t x = 0; x < x4render::Width; ++x) {
        const bool active = size_t(x) / 40 == n;
        assert(pixel(output, x, 89) == active && !pixel(old, x, 89));
        assert(!pixel(output, x, 90) && pixel(old, x, 90) == active);
      }
      assert(texture.poll(31 + n) == (n == 19));
    }
    assert(texture.driveCount == x4render::Pixels && !texture.settleLut(0));
  }

  const auto oldUploads = landscape.uploads.size();
  assert(photo.start(6, false));
  assert(landscape.uploads.size() == oldUploads + 2);  // photo is not calibration refs
  std::cout << "calibration: endpoint preparation, wire selectors, held "
               "references, repeats, conditioning, timeout passed\n";
}
