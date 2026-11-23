#pragma once
#include "bw_approximation.h"
#include "bw_strips.h"
#include "rendering.h"
#include "waveform.h"
#include "waveform_inspection.h"
#include "test_image.h"

#include <optional>

namespace x4render {
// Display-independent diagnostic layout. All drawing is into two reusable
// packed WIRE planes; nothing may mutate them while the transport is busy.
template <class Transport>
class Calibration {
 public:
  enum class Job : uint8_t {
    Idle,
    PowerOn,
    Prime,
    Base,
    Pause,
    Settle,
    Conditioning,
    PowerOff,
    Failed
  };
  explicit Calibration(Transport& transport) : transport_(transport) {}
  bool allocate() {
    return first_.allocate(PlaneBytes) && second_.allocate(PlaneBytes);
  }
  bool busy() const { return job_ != Job::Idle && job_ != Job::Failed; }
  Job job() const { return job_; }
  const char* result() const { return result_; }
  uint32_t duration{0}, driveCount{0}, cycles{0};
  Rect requested{}, expanded{};
  std::array<Pulse, 3> parameters{
      {{2, 2, 0, 1, 1}, {2, 4, 0, 1, 1}, {2, 6, 0, 1, 1}}};
  uint8_t holdRail{0};
  BwPreparation bwPreparation{};
  Palette imagePalette = provisionalRelativePalette();
  std::optional<LutBank> factoryDerivedBw{};
  const uint8_t* boilerLuma{nullptr};   // Caller keeps its committed frame stable during the lab session.
  BwTexture bwTexture{BwTexture::Bayer};
  uint16_t bwBudget{80};
  bool bwSplitGuard{true};
  bool verticalWindowValidated{false}, croppedUploadValidated{false},
      verbose{false};
  bool start(uint32_t now, bool prepare = true, bool rotate = false,
             bool conditioning = false) {
    return startOperation(now, prepare, rotate, conditioning,
                          TestImage::Calibration, 0);
  }
  // LoadStrip takes a width, SplitCheckerboard a per-rail budget, and random
  // artwork a seed. Repeat Image replays the same argument.
  bool startImage(uint32_t now, TestImage image, uint32_t argument = 0) {
    if (busy() || image < TestImage::White || image > TestImage::BwBoiler) return false;
    if (image == TestImage::BwBoiler && !boilerLuma) {
      result_ = "boiler snapshot unavailable; show dashboard first";
      return false;
    }
    if (usesBwTexture(image) && (bwBudget < 32 || bwBudget > 160)) return false;
    if (image == TestImage::SplitCheckerboard) {
      if (!bwPreparation.programmable || !factoryDerivedBw || bwPreparation.factoryPrime) {
        result_ = "split requires Factory Tuned or Replay, prime off";
        return false;
      }
      if (argument < 32 || argument > 160) {
        result_ = "split rail budget must be 32 to 160";
        return false;
      }
    }
    return startOperation(now, true, false, false, image, argument);
  }
  TestImage image() const { return image_; }
  uint32_t imageSeed() const { return imageArgument_; }
  uint16_t stripWidth() const { return loadStripWidth(imageArgument_); }
  uint8_t stripCount() const { return stripPlan_.count; }
  BwLoad requestedBwLoad() const { return requestedBwLoad_; }
  BwLoad renderedBwLoad() const { return renderedBwLoad_; }
  BwTexture renderedBwTexture() const { return protectedFallback_ ? BwTexture::Bayer : bwTexture; }
  bool protectedFallback() const { return protectedFallback_; }

 private:
  bool startOperation(uint32_t now, bool prepare, bool rotate, bool conditioning,
                      TestImage image, uint32_t argument) {
    if (busy() || !first_.data() || !bwPreparation.valid() || holdRail > 3)
      return false;
    for (auto p : parameters)
      if (!p.valid() || p.repeats != parameters[0].repeats) return false;
    conditioning_ = conditioning;
    image_ = image;
    imageArgument_ = argument;
    stripPlan_.count = nextStrip_ = 0;
    protectedFallback_ = false;
    requestedBwLoad_ = renderedBwLoad_ = {};
    pauseMs_ = bwPreparation.pauseMs;
    prepare_ = prepare || !references_;
    if (conditioning) prepare_ = false;
    started_ = now;
    uploaded_ = 0;
    duration = 0;
    result_ = "running";
    if (transport_.status() == Transport::Status::Fault ||
        transport_.status() == Transport::Status::Sleeping) {
      // Fault recovery must establish power-off first. Resetting a possibly
      // powered controller hides the ownership error and can interrupt HV.
      return fail();
    }
    // One owner for rotation: explicit or implicit rebuild resets it. A
    // rejected/overlapping request never advances it.
    rotation_ = prepare_ ? 0 : uint8_t((rotation_ + (rotate ? 1 : 0)) % 3);
    bytesAtStart_ = transport_.uploadedBytes();
    if (transport_.status() == Transport::Status::Off) {
      otpPrimed_ = false;
      if (!transport_.powerOn(now)) return fail();
      job_ = Job::PowerOn;
      return true;
    }
    return beginPass(now);
  }

 public:
  bool poll(uint32_t now) {
    if (!busy()) return false;
    if (job_ == Job::Pause) {
      if (uint32_t(now - baseCompleted_) >= pauseMs_) beginSettle(now);
      return job_ == Job::Failed;
    }
    const auto r = transport_.poll(now);
    if (r == Transport::Result::Pending) return false;
    if (r != Transport::Result::Complete) {
      result_ = r == Transport::Result::AssertionTimeout
                    ? "BUSY assertion timeout"
                    : "BUSY completion timeout";
      job_ = Job::Failed;
      references_ = false;
      duration = now - started_;
      uploaded_ = transport_.uploadedBytes() - bytesAtStart_;
      return true;
    }
    if (job_ == Job::PowerOff) {
      otpPrimed_ = false;
      duration = now - started_;
      job_ = Job::Idle;
      result_ = "power off complete";
      return true;
    }
    if (job_ == Job::PowerOn) {
      return !beginPass(now);
    }
    if (job_ == Job::Prime) {
      otpPrimed_ = true;
      // OTP may copy NEW to OLD. Rebuild and re-upload both planes before the
      // programmable pass, without resetting or power-cycling the controller.
      return !beginPass(now);
    }
    if (job_ == Job::Base) {
      if (nextStrip_ < stripPlan_.count)
        return !beginSplitStrip(now);
      if (factoryPreparation_) otpPrimed_ = true;
      prepare_ = false;
      if (image_ != TestImage::Calibration && !usesRelativeGray(image_))
        return complete(now, false);
      if (pauseMs_) {
        baseCompleted_ = now;
        job_ = Job::Pause;
        return false;
      }
      return !beginSettle(now);
    }
    return complete(now, !conditioning_ && image_ == TestImage::Calibration);
  }
  bool complete(uint32_t now, bool references) {
    duration = now - started_;
    cycles++;
    references_ = references;
    job_ = Job::Idle;
    result_ = "complete";
    uploaded_ = transport_.uploadedBytes() - bytesAtStart_;
    return true;
  }
  bool powerOff(uint32_t now) {
    if (busy()) return false;
    started_ = now;
    bytesAtStart_ = transport_.uploadedBytes();
    uploaded_ = 0;
    if (!transport_.powerOff(now)) return false;
    job_ = Job::PowerOff;
    result_ = "powering off";
    return true;
  }
  uint32_t uploadedBytes() const { return uploaded_; }
  void invalidate() {
    references_ = false;
    job_ = Job::Idle;
    rotation_ = 0;
    otpPrimed_ = false;
    settleValid_ = preparationValid_ = factoryPreparation_ = false;
  }
  uint8_t rotation() const { return rotation_; }
  uint8_t programmedRotation() const { return programmedRotation_; }
  const Lut* settleLut(uint8_t row) const {
    return settleValid_ && row < 5 ? &settleBank_[row] : nullptr;
  }
  const Lut* preparationLut(uint8_t row) const {
    return preparationValid_ && row < 5 ? &preparationBank_[row] : nullptr;
  }
  bool factoryPreparation() const { return factoryPreparation_; }
  uint32_t waveformId() const { return waveformId_; }
  Rect activationWindow() const { return lastActivation_; }
  uint8_t programmedHoldRail() const { return programmedHoldRail_; }

 private:
  bool fail() {
    result_ = "invalid transaction";
    job_ = Job::Failed;
    references_ = false;
    return false;
  }
  void rect(Buffer& plane, Rect r, bool value) {
    for (uint16_t y = r.y; y < r.y + r.height; ++y)
      for (uint16_t x = r.x; x < r.x + r.width; ++x)
        packedBit(plane, size_t(y) * Width + x, value);
  }
  static std::array<uint8_t, 7> glyph(char c) {
    // Original compact 5x7 lettering; no borrowed font dependency.
    switch (c) {
      case 'A':
        return {14, 17, 17, 31, 17, 17, 17};
      case 'B':
        return {30, 17, 17, 30, 17, 17, 30};
      case 'C':
        return {14, 17, 16, 16, 16, 17, 14};
      case 'D':
        return {30, 17, 17, 17, 17, 17, 30};
      case 'E':
        return {31, 16, 16, 30, 16, 16, 31};
      case 'F':
        return {31, 16, 16, 30, 16, 16, 16};
      case 'G':
        return {14, 17, 16, 23, 17, 17, 14};
      case 'H':
        return {17, 17, 17, 31, 17, 17, 17};
      case 'I':
        return {31, 4, 4, 4, 4, 4, 31};
      case 'J':
        return {7, 2, 2, 2, 18, 18, 12};
      case 'K':
        return {17, 18, 20, 24, 20, 18, 17};
      case 'L':
        return {16, 16, 16, 16, 16, 16, 31};
      case 'M':
        return {17, 27, 21, 21, 17, 17, 17};
      case 'N':
        return {17, 25, 21, 19, 17, 17, 17};
      case 'O':
        return {14, 17, 17, 17, 17, 17, 14};
      case 'P':
        return {30, 17, 17, 30, 16, 16, 16};
      case 'Q':
        return {14, 17, 17, 17, 21, 18, 13};
      case 'R':
        return {30, 17, 17, 30, 20, 18, 17};
      case 'S':
        return {15, 16, 16, 14, 1, 1, 30};
      case 'T':
        return {31, 4, 4, 4, 4, 4, 4};
      case 'U':
        return {17, 17, 17, 17, 17, 17, 14};
      case 'V':
        return {17, 17, 17, 17, 17, 10, 4};
      case 'W':
        return {17, 17, 17, 21, 21, 21, 10};
      case 'X':
        return {17, 17, 10, 4, 10, 17, 17};
      case 'Y':
        return {17, 17, 10, 4, 4, 4, 4};
      case 'Z':
        return {31, 1, 2, 4, 8, 16, 31};
      case '0':
        return {14, 17, 19, 21, 25, 17, 14};
      case '1':
        return {4, 12, 4, 4, 4, 4, 14};
      case '2':
        return {14, 17, 1, 2, 4, 8, 31};
      case '3':
        return {30, 1, 1, 14, 1, 1, 30};
      case '4':
        return {2, 6, 10, 18, 31, 2, 2};
      case '5':
        return {31, 16, 16, 30, 1, 1, 30};
      case '6':
        return {14, 16, 16, 30, 17, 17, 14};
      case '7':
        return {31, 1, 2, 4, 8, 8, 8};
      case '8':
        return {14, 17, 17, 14, 17, 17, 14};
      case '9':
        return {14, 17, 17, 15, 1, 1, 14};
      case '-':
        return {0, 0, 0, 31, 0, 0, 0};
      case '=':
        return {0, 0, 31, 0, 31, 0, 0};
      default:
        return {};
    }
  }
  void text(uint16_t x, uint16_t y, const char* value, uint8_t scale = 2) {
    for (; *value; ++value, x += 6 * scale) {
      auto g = glyph(*value);
      for (uint8_t gy = 0; gy < 7; ++gy)
        for (uint8_t gx = 0; gx < 5; ++gx)
          if (g[gy] & (1u << (4 - gx)))
            rect(second_,
                 {uint16_t(x + gx * scale), uint16_t(y + gy * scale), scale,
                  scale},
                 false);
    }
  }
  void number(uint16_t x, uint16_t y, uint16_t n) {
    char chars[4] = {char('0' + (n / 100) % 10), char('0' + (n / 10) % 10),
                     char('0' + n % 10), 0};
    text(x, y, chars);
  }
  Settings imageSettings() const {
    Settings settings;
    settings.mode = Mode::DitherSettle;
    settings.palette = imagePalette;
    return settings;
  }
  void shadeChartLabels() {
    text(8, 3, "NEIGHBOR MIXES - STEP IS LIGHTER PIXELS PER 16", 2);
    text(8, 19, imagePalette.calibrated ? "RAW REFERENCES" : "RAW REFERENCES - PROVISIONAL ORDER", 1);
    for (uint8_t i = 0; i < 8; ++i) text(i * 100 + 7, 58, shadeLabel(i), 1);
    for (uint8_t step = 0; step <= 16; ++step) {
      char label[3]{char('0' + step / 10), char('0' + step % 10), 0};
      text((neighborColumn(step) + neighborColumn(step + 1)) / 2 - 6, 69, label, 1);
    }
    for (uint8_t i = 0; i + 1 < imagePalette.count; ++i) {
      char label[48];
      std::snprintf(label, sizeof(label), "%u - %s TO %s", unsigned(i + 1),
                    shadeLabel(relativeShadeId(imagePalette.shades[i])),
                    shadeLabel(relativeShadeId(imagePalette.shades[i + 1])));
      text(8, NeighborTop + i * NeighborRowHeight + NeighborSwatchHeight + 2, label, 1);
    }
  }
  bool beginPass(uint32_t now) {
    if (!prepare_) return beginSettle(now);
    references_ = false;
    if (image_ == TestImage::SplitCheckerboard) {
      paintTestImage(second_, image_, imageArgument_);
      return beginSplit(now, uint16_t(imageArgument_));
    }
    first_.clear(255);
    second_.clear(255);
    if (usesBwTexture(image_)) {
      renderBwPreview();
      if (bwTexture != BwTexture::Bayer && bwSplitGuard && exceedsBwBudget()) {
        if (!bwPreparation.programmable || !factoryDerivedBw || bwPreparation.factoryPrime) {
          fail();
          result_ = "split guard requires Factory Tuned or Replay, prime off";
          return false;
        }
        return beginSplit(now, bwBudget / 2);
      }
    } else if (image_ != TestImage::Calibration) {
      paintTestImage(second_, image_, imageArgument_);
      if (usesRelativeGray(image_) || isRampImage(image_)) {
        if (!imagePalette.valid()) return fail();
        paintRelativeShades(image_, imageArgument_, imageSettings(), [this](uint16_t x, uint16_t y, Shade shade) {
          packedBit(second_, size_t(y) * Width + x, shade.base);
        });
      }
      if (image_ == TestImage::NeighborSweep) shadeChartLabels();
      if (isRampImage(image_)) {
        text(10, 8, "A BLACK TO C WHITE - LINEAR INPUT RAMP", 2);
        text(10, 32, imagePalette.calibrated ? "MEASURED PALETTE" : "PROVISIONAL PALETTE", 1);
        text(10, 57, "NEIGHBOR GRAY DITHER", 2);
        text(10, 260, "B-W DITHER - SAME INPUT LEVELS", 2);
      }
      if (image_ == TestImage::Reference || image_ == TestImage::Random) {
        text(20, 12, image_ == TestImage::Reference ? "X4 KNOWN REFERENCE"
                                                 : "X4 RANDOM EDGES", 3);
        char label[40];
        std::snprintf(label, sizeof(label), "SEED %08lX", (unsigned long)imageArgument_);
        text(20, 452, label, 2);
      }
      if (image_ == TestImage::LandscapeDither || image_ == TestImage::LandscapeGray) {
        text(10, 4, image_ == TestImage::LandscapeGray
                         ? "LANDSCAPE - DITHER THEN SETTLE"
                         : "LANDSCAPE - B-W DITHER", 1);
        text(10, 467, "ANSEL ADAMS - NARA 519904", 1);
      }
    } else {
    text(18, 12, "X4 WAVEFORM LAB - EXPERIMENTAL", 3);
    text(18, 43, "OPPOSITE FIRST - THEN A B C");
    if (bwPreparation.programmable) {
      if (factoryDerivedBw) {
        text(18, 65, "BW MTP", 1);
      } else {
        text(18, 65, "BW F", 1);
        number(43, 62, bwPreparation.frames);
        text(18, 85, "BW I", 1);
        number(43, 82, bwPreparation.inversionFrames);
        text(88, 65, "R", 1);
        number(99, 62, bwPreparation.repeats);
        if (bwPreparation.conditioned) text(150, 65, "COND", 1);
      }
    } else
      text(18, 65, "BW OTP", 1);
    for (uint8_t c = 0; c < 3; ++c) {
      uint16_t x = uint16_t(130 + c * 215 + (c == 2 ? 12 : 0));
      char label[2] = {char('A' + c), 0};
      text(x + 75, 72, label, 3);
      rect(second_, {x, 110, 190, 90}, false);
      text(x, 207, "BLACK START");
      text(x, 337, "WHITE START");
      text(x, 360, "F");
      number(x + 15, 360, parameters[c].frames);
      text(x + 65, 360, "R");
      number(x + 80, 360, parameters[c].repeats);
      text(x + 130, 360, "P");
      number(x + 145, 360, parameters[c].rail);
      text(x, 378, "O");
      number(x + 15, 378, parameters[c].opposite);
      text(x + 65, 378, "N");
      number(x + 80, 378, parameters[c].neutral);
      text(x + 130, 378, "H");
      number(x + 145, 378, holdRail);
    }
    rect(second_, {20, 110, 80, 90}, false);
    text(20, 207, "HOLD B");
    text(20, 337, "HOLD W");
    text(18, 395, "HELD REFERENCES", 1);
    rect(second_, {20, 413, 170, 45}, false);
    text(220, 432, "WHITE");
    text(550, 397, "GRAY REF", 1);
    text(18, 466, "REPEAT HOLDS REFERENCES - REBUILD RESETS STARTS", 1);
    }
    // Unknown physical history must not be represented as "OLD was white".
    // Match the normal driver's scrub: every requested pixel transitions.
    for (size_t i = 0; i < first_.size(); ++i) {
      if (image_ == TestImage::RampInverse) second_[i] = uint8_t(~second_[i]);
      first_[i] = uint8_t(~second_[i]);
    }
    if (!upload({0, 0, 800, 480}, true)) return fail();
    if (bwPreparation.programmable && bwPreparation.factoryPrime && !otpPrimed_) {
      if (!transport_.activateOtp(false, now)) return fail();
      factoryPreparation_ = true;
      preparationValid_ = false;
      job_ = Job::Prime;
      return true;
    }
    bool activated;
    if (bwPreparation.programmable) {
      const auto bank = factoryDerivedBw ? *factoryDerivedBw
                                         : preparation(bwPreparation, holdRail);
      activated = transport_.activate(bank, {0, 0, 800, 480}, false, now, true);
      if (activated) {
        preparationBank_ = bank;
        preparationValid_ = true;
        factoryPreparation_ = false;
      }
    } else {
      activated = transport_.activateOtp(false, now);
      if (activated) {
        preparationValid_ = false;
        factoryPreparation_ = true;
      }
    }
    if (!activated) return fail();
    requested = expanded = lastActivation_ = {0, 0, 800, 480};
    driveCount = 384000;
    programmedHoldRail_ = bwPreparation.programmable && bwPreparation.conditioned
                              ? bwPreparation.restRail : holdRail;
    settleValid_ = false;
    job_ = Job::Base;
    return true;
  }
  void renderBwPreview() {
    const bool boiler = image_ == TestImage::BwBoiler;
    const bool checker = image_ == TestImage::BwChecker;
    auto luma = [&](size_t i) { return boiler ? boilerLuma[i] : checker ? uint8_t(128) : landscapeLuma(i); };
    auto protectedMark = [&](uint16_t x, uint16_t y) {
      if (checker) return isLoadPatch(x, y);
      if (!boiler) return false;
      const size_t i = size_t(y) * Width + x;
      const int v = boilerLuma[i];
      // Preserve hard endpoints and the antialiasing next to a strong edge.
      return v == 0 || v == 255 ||
             (x && std::abs(v - boilerLuma[i - 1]) >= 64) ||
             (x + 1 < Width && std::abs(v - boilerLuma[i + 1]) >= 64) ||
             (y && std::abs(v - boilerLuma[i - Width]) >= 64) ||
             (y + 1 < Height && std::abs(v - boilerLuma[i + Width]) >= 64);
    };
    auto bayer = [&]() {
      if (!boiler) {
        paintTestImage(second_, image_, imageArgument_);
        return;
      }
      const Settings bw;
      for (uint16_t y = 0; y < Height; ++y)
        for (uint16_t x = 0; x < Width; ++x) {
          const size_t i = size_t(y) * Width + x;
          packedBit(second_, i, quantize(luma(i), x, y, bw).base);
        }
    };
    bayer();
    requestedBwLoad_ = measureBwLoad(second_);
    if (bwTexture == BwTexture::Columns) {
      for (uint16_t y = 0; y < Height; ++y)
        for (uint16_t x = 0; x < Width; ++x) {
          if (protectedMark(x, y)) continue;
          const size_t i = size_t(y) * Width + x;
          packedBit(second_, i, columnDither(luma(i), x));
        }
    } else if (bwTexture == BwTexture::Repair) {
      const auto edits = repairBwRows(second_, bwBudget, [&](uint16_t x, uint16_t y) {
        const int tone = luma(size_t(y) * Width + x);
        return std::min(3, std::abs(tone - 128) / 32) + 8 * protectedMark(x, y);
      });
      // The checkerboard witnesses may show small repair artifacts. Do not
      // trade away actual dashboard marks to make a load count pass.
      if (boiler && edits) {
        bayer();
        protectedFallback_ = true;
      }
    }
    renderedBwLoad_ = bwTexture == BwTexture::Bayer || protectedFallback_
        ? requestedBwLoad_ : measureBwLoad(second_);
  }
  bool exceedsBwBudget() const {
    return renderedBwLoad_.peak > bwBudget ||
           renderedBwLoad_.localPeak > 3 * bwBudget / 4 ||
           renderedBwLoad_.recentPeak > bwBudget / 2;
  }
  bool beginSplit(uint32_t now, uint16_t budget) {
    stripPlan_ = planBwStrips(second_, budget);
    if (!stripPlan_.count || !splitTarget_.allocate(PlaneBytes)) return fail();
    std::memcpy(splitTarget_.data(), second_.data(), PlaneBytes);
    driveCount = 0;
    preparationValid_ = factoryPreparation_ = settleValid_ = false;
    preparationBank_ = *factoryDerivedBw;
    // Align code 0 in unused selectors with the verified factory VCOM timing.
    preparationBank_[Row[0]] = preparationBank_[Row[3]] = preparationBank_[0];
    return beginSplitStrip(now);
  }
  bool beginSplitStrip(uint32_t now) {
    constexpr size_t stride = Width / 8;
    const uint16_t left = nextStrip_ ? stripPlan_.ends[nextStrip_ - 1] : 0;
    const uint16_t right = stripPlan_.ends[nextStrip_];
    const size_t beginByte = left / 8, endByte = right / 8;
    first_.clear();
    second_.clear();
    for (size_t y = 0; y < Height; ++y) {
      auto* target = second_.data() + y * stride;
      auto* previous = first_.data() + y * stride;
      std::memcpy(target + beginByte, splitTarget_.data() + y * stride + beginByte, endByte - beginByte);
      for (size_t x = beginByte; x < endByte; ++x)
        previous[x] = uint8_t(~target[x]);
    }
    // Full addressing with selector 00 outside the strip and on hidden gates.
    const Rect full{0, 0, Width, Height};
    if (!upload(full) || !transport_.activate(preparationBank_, {0, 0, Width, Height}, false, now, true))
      return fail();
    requested = {left, 0, uint16_t(right - left), Height};
    expanded = lastActivation_ = full;
    driveCount += uint32_t(requested.width) * Height;
    programmedHoldRail_ = 0;
    preparationValid_ = true;
    ++nextStrip_;
    job_ = Job::Base;
    return true;
  }
  bool upload(Rect r, bool bw = false) {
    typename Transport::Window w{r.x, r.y, r.width, r.height};
    // Match the proven driver. A NEW write may alter the controller's OLD
    // baseline; commit our exact OLD/selector plane LAST on every operation.
    return transport_.upload(0x13, {second_.data(), second_.size()}, w, bw ? 255 : 0) &&
           transport_.upload(0x10, {first_.data(), first_.size()}, w, bw ? 255 : 0);
  }
  void select(Rect r, uint8_t selector) {
    rect(first_, r, selector & 2);
    rect(second_, r, selector & 1);
    driveCount += uint32_t(r.width) * r.height;
    requested.include(r.x, r.y);
    requested.include(r.x + r.width - 1, r.y + r.height - 1);
  }
  bool beginSettle(uint32_t now) {
    first_.clear();
    second_.clear();
    driveCount = 0;
    requested = {};
    const bool fullImage = usesRelativeGray(image_);
    if (fullImage) {
      paintRelativeShades(image_, imageArgument_, imageSettings(), [this](uint16_t x, uint16_t y, Shade shade) {
        if (!shade.selector) return;
        const size_t index = size_t(y) * Width + x;
        packedBit(first_, index, shade.selector & 2);
        packedBit(second_, index, shade.selector & 1);
        ++driveCount;
        requested.include(x, y);
      });
    } else if (conditioning_)
      select({0, 0, 800, 480}, 1);
    else {
      for (uint8_t c = 0; c < 3; ++c) {
        select({uint16_t(130 + c * 215 + (c == 2 ? 12 : 0)), 110, 190, 90},
               uint8_t(1 + (c + rotation_) % 3));
        select({uint16_t(130 + c * 215 + (c == 2 ? 12 : 0)), 240, 190, 90},
               uint8_t(1 + (c + rotation_) % 3));
      }
      if (!references_) select({550, 413, 200, 45}, 2);
    }
    expanded = requested.fullLines();
    // Cropped uploads stay disabled until the specific addressing behavior is
    // measured. Even when enabled, activation is restored to full visible
    // lines.
    // With a full-height activation, cropped writes could expose stale
    // selectors outside the uploaded rows. Until a separate RAM hold baseline
    // is implemented, crop only when activation is confined to those same rows.
    Rect uploadRect = !fullImage && croppedUploadValidated && verticalWindowValidated
                          ? expanded
                          : Rect{0, 0, 800, 480};
    if (!upload(uploadRect)) return fail();
    const auto bank = candidates(parameters, holdRail);
    const Rect activation = !fullImage && verticalWindowValidated
                                ? expanded : Rect{0, 0, 800, 480};
    if (!transport_.activate(bank, {0, activation.y, 800, activation.height},
                             !fullImage && verticalWindowValidated, now))
      return fail();
    // Save the exact bytes only after the transport accepted activation.
    settleBank_ = bank;
    settleValid_ = true;
    programmedRotation_ = rotation_;
    programmedHoldRail_ = holdRail;
    ++waveformId_;
    lastActivation_ = !fullImage && verticalWindowValidated ? expanded : Rect{0, 0, 800, 480};
    job_ = conditioning_ ? Job::Conditioning : Job::Settle;
    return true;
  }
  LutBank settleBank_{}, preparationBank_{};
  bool settleValid_{false}, preparationValid_{false},
      factoryPreparation_{false};
  uint8_t programmedRotation_{0}, programmedHoldRail_{0};
  uint32_t waveformId_{0};
  Rect lastActivation_{};
  Transport& transport_;
  Buffer first_, second_, splitTarget_;
  Job job_{Job::Idle};
  bool references_{false}, prepare_{true}, conditioning_{false};
  bool otpPrimed_{false};
  TestImage image_{TestImage::Calibration};
  uint32_t imageArgument_{0}, baseCompleted_{0};
  BwStripPlan stripPlan_{};
  BwLoad requestedBwLoad_{}, renderedBwLoad_{};
  bool protectedFallback_{false};
  uint8_t nextStrip_{0};
  uint16_t pauseMs_{0};
  uint8_t rotation_{0};
  uint32_t started_{0}, bytesAtStart_{0}, uploaded_{0};
  const char* result_{"idle"};
};
}  // namespace x4render
