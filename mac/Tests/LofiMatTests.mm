#include "editor/NativeEffects.hpp"
#include "../Audio/AudioUnitHost.hpp"
#import "../Bridge/TrackerSession.h"
#include <cmath>
#include <complex>
#include <cstring>
#include <iostream>
#include <numbers>
using namespace Tracker;
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) { *a = *f = *l = 0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
#endif
static void audited(bool ok) { uint64_t a, f, l; tracker_audit_end(&a, &f, &l); check(ok, "LofiMat processes"); check(!(a + f + l), "LofiMat allocates/frees/locks nothing in render"); }
static void process(NativeEffect &effect, std::vector<float> &audio, uint32_t block = 128) {
  for (uint32_t n = 0; n < audio.size() / 2; n += block) {
    tracker_audit_begin(); const bool ok = effect.process(audio.data() + n * 2, std::min<uint32_t>(block, audio.size() / 2 - n)); audited(ok);
  }
}
static double quantize(double x, double bits) {
  // Independent magnitude/threshold formulation: symmetric mid-tread grid,
  // halfway values away from zero, with full-scale endpoints retained.
  const double step = std::pow(2., 1 - bits);
  const double level = std::floor(std::min(1., std::abs(x)) / step + .5);
  return std::copysign(std::min(1., level * step), x);
}
static std::vector<float> signal(size_t frames) {
  std::vector<float> audio(frames * 2);
  for (size_t n = 0; n < frames; ++n) {
    audio[n * 2] = float((double(n % 3001) - 1500) / 2048);
    audio[n * 2 + 1] = float(.3 * std::sin(n * .071));
  }
  return audio;
}
static void quantizationAndClocks() {
  double maximum = 0;
  for (float bits : {1.f, 2.f, 2.5f, 7.25f, 8.f, 16.f, 24.f}) {
    NativeEffect effect("resonance.lofimat.v1", 48000); effect.parameter(1, bits); effect.parameter(2, 384000);
    std::vector<float> input(8192 * 2);
    for (size_t n = 0; n < 8192; ++n) { input[n * 2] = float((double(n) - 4096) / 3072); input[n * 2 + 1] = -input[n * 2]; }
    auto out = input; process(effect, out, 17);
    for (size_t i = 0; i < out.size(); ++i) maximum = std::max(maximum, std::abs(double(out[i]) - quantize(input[i], bits)));
    for (size_t n = 0; n < 8192; ++n) check(out[n * 2] == -out[n * 2 + 1] && std::abs(out[n * 2]) <= 1, "Symmetric quantization retains silence/full-scale bounds without DC bias between opposite inputs");
  }
  const auto input = signal(32768);
  for (uint32_t host : {8000u, 44100u, 48000u, 96000u, 192000u, 384000u}) {
    for (float requested : {20.f, 8000.f, 11025.f, 11111.5f, 48000.f, 384000.f}) {
      NativeEffect effect("resonance.lofimat.v1", host); effect.parameter(1, 24); effect.parameter(2, requested);
      auto out = input; process(effect, out, 17);
      // Exact rational event indices, independent of the processor's phase
      // accumulator. Doubled rates cover the half-Hz setting without rounding.
      const uint64_t r = uint64_t(std::min(float(host), requested) * 2), h = uint64_t(host) * 2;
      for (uint64_t n = 0; n < input.size() / 2; ++n) {
        const uint64_t captures = n * r / h, at = (captures * h + r - 1) / r;
        for (int c = 0; c < 2; ++c) check(out[n * 2 + c] == float(quantize(input[at * 2 + c], 24)), "Integer/fractional rate capture times match exact rational scheduling at every frame");
      }
    }
  }
  check(maximum < 6e-8, "Integer and fractional bit depths agree with independent quantization within float precision");
  std::cout << "LofiMat quantizer maximum error " << maximum << "; exact rate clocks from 8 to 384 kHz\n";
}
// Leap directly to draw n with exponentiation of the affine LCG transform. This
// is independent of the production engine's sequential operator and pins the
// generator contract without using a standard-library distribution.
static uint32_t randomAt(uint32_t seed, uint64_t n) {
  uint64_t multiplier = 6364136223846793005ull, increment = 1, state = seed;
  for (++n; n; n >>= 1) {
    if (n & 1) state = state * multiplier + increment;
    increment *= multiplier + 1; multiplier *= multiplier;
  }
  return uint32_t(state >> 32);
}
static double noiseAt(uint32_t seed, uint64_t draw) { return (double(randomAt(seed, draw)) + .5) / 2147483648. - 1; }
static void noise() {
  constexpr size_t frames = 65536;
  NativeEffect effect("resonance.lofimat.v1", 48000); effect.parameter(1, 24); effect.parameter(3, 100); effect.parameter(8, 1234567);
  std::vector<float> output(frames * 2); process(effect, output);
  double sum[2]{}, square[2]{}, cross = 0, lag = 0;
  for (size_t n = 0; n < frames; ++n) {
    for (int c = 0; c < 2; ++c) {
      check(output[n * 2 + c] == float(quantize(noiseAt(1234567, n * 2 + c), 24)), "Seeded stereo noise matches independent skip-ahead reference");
      sum[c] += output[n * 2 + c]; square[c] += double(output[n * 2 + c]) * output[n * 2 + c];
    }
    cross += double(output[n * 2]) * output[n * 2 + 1];
    if (n) lag += double(output[n * 2]) * output[(n - 1) * 2];
  }
  for (int c = 0; c < 2; ++c) check(std::abs(sum[c] / frames) < .01 && std::abs(square[c] / frames - 1. / 3) < .01, "Noise fixture has centered uniform amplitude statistics");
  check(std::abs(cross / frames) < .01 && std::abs(lag / frames) < .01, "Noise fixture has no excessive stereo or adjacent-sample correlation");
  NativeEffect reduced("resonance.lofimat.v1", 48000); reduced.parameter(1, 8); reduced.parameter(2, 8000); reduced.parameter(3, 100); reduced.parameter(8, 37);
  std::vector<float> held(4096 * 2); process(reduced, held);
  for (size_t n = 0; n < 4096; ++n) for (int c = 0; c < 2; ++c)
    check(held[n * 2 + c] == float(quantize(noiseAt(37, (n / 6) * 2 + c), 8)), "Noise enters before both bit crushing and rate reduction");
  NativeEffect a("resonance.lofimat.v1", 48000), b("resonance.lofimat.v1", 48000);
  for (auto *e : {&a, &b}) { e->parameter(2, 1000); e->parameter(3, 100); e->parameter(8, 99); }
  std::vector<float> first(4096 * 2), repeated(first.size()); process(a, first);
  for (size_t n = 0; n < 4096; ++n) { b.parameter(8, 99); check(b.process(repeated.data() + n * 2, 1), "Repeated seed processes"); }
  check(first == repeated, "Repeated seed target does not restart noise");
  NativeEffect restored("resonance.lofimat.v1", 48000, b.state()); std::fill(repeated.begin(), repeated.end(), 0); process(restored, repeated, 4096);
  check(first == repeated, "Saved seed restarts the same stream for a new render, without persisting live history");
  NativeEffect edit("resonance.lofimat.v1", 48000); edit.parameter(1, 24); edit.parameter(2, 1000); edit.parameter(3, 100);
  std::vector<float> changed(500 * 2); check(edit.process(changed.data(), 137), "Seed edit starts between capture ticks"); edit.parameter(8, 42.7f);
  check(edit.value(8) == 43 && edit.process(changed.data() + 274, 363), "Seed quantizes and live edit continues");
  for (size_t n = 0; n < 500; ++n) for (int c = 0; c < 2; ++c) {
    const auto tick = n / 48; const bool reset = tick >= 3;
    check(changed[n * 2 + c] == float(quantize(noiseAt(reset ? 43 : 1, ((reset ? tick - 3 : tick) * 2) + c), 24)), "Seed edit preserves capture phase and becomes audible at the next capture");
  }
  std::cout << "LofiMat noise means " << sum[0] / frames << '/' << sum[1] / frames << ", stereo covariance " << cross / frames << '\n';
}
static void smoothingAndBypass() {
  constexpr uint32_t rate = 48000; const auto input = signal(4096);
  NativeEffect dry("resonance.lofimat.v1", rate); dry.parameter(5, 100); dry.parameter(6, 0); auto out = input; process(dry, out);
  check(out == input, "Independent dry path is exact identity and zero latency");
  for (float smooth : {0.f, 1.f}) {
    NativeEffect always("resonance.lofimat.v1", rate), bypass("resonance.lofimat.v1", rate);
    for (auto *e : {&always, &bypass}) { e->parameter(1, 5); e->parameter(2, 1000); e->parameter(3, 30); e->parameter(4, smooth); }
    bypass.parameter(0, 0); auto a = input, b = input; process(always, a);
    check(bypass.process(b.data(), 1000), "Warm bypass starts");
    check(std::equal(b.begin(), b.begin() + 2000, input.begin()), "Disabled LofiMat is exact dry");
    bypass.parameter(0, 1); check(bypass.process(b.data() + 2000, 3096), "Warm bypass re-enables");
    check(std::equal(a.begin() + 2480, a.end(), b.begin() + 2480), "Re-enabled audio and noise match the continuously running effect after 5 ms");
  }
  NativeEffect hold("resonance.lofimat.v1", rate), smooth("resonance.lofimat.v1", rate);
  for (auto *e : {&hold, &smooth}) { e->parameter(1, 24); e->parameter(2, 8000); }
  smooth.parameter(4, 1); std::vector<float> sine(rate * 2);
  const double w = 2 * std::numbers::pi * 1000 / rate;
  for (size_t n = 0; n < rate; ++n) sine[n * 2] = float(.3 * std::sin(w * n));
  auto shaped = sine; process(hold, sine); process(smooth, shaped, 17);
  std::complex<double> before{}, after{};
  for (size_t n = rate / 2; n < rate; ++n) {
    before += double(sine[n * 2]) * std::polar(1., -w * n); after += double(shaped[n * 2]) * std::polar(1., -w * n);
    check(shaped[n * 2 + 1] == 0, "Crushing and smoothing do not leak channels when noise is off");
  }
  const double pole = std::exp(-2 * std::numbers::pi * 3600 / rate);
  const auto expected = (1 - pole) / (1. - pole * std::polar(1., -w));
  check(std::abs(after / before - expected) < 1e-7, "Smooth amplitude and phase match an independent one-pole transfer function");
  // A live rate sweep must preserve phase. Integrate its linear ramp in long
  // double, independently of the processor's per-frame double accumulator.
  NativeEffect sweep("resonance.lofimat.v1", rate); sweep.parameter(1, 24); sweep.parameter(2, 9600);
  auto changing = input; check(sweep.process(changing.data(), 997), "Rate sweep starts"); sweep.parameter(2, 18750);
  check(sweep.process(changing.data() + 1994, 3099), "Rate sweep ends");
  uint64_t last = 0, captures = 0;
  for (uint64_t n = 0; n < 4096; ++n) {
    const long double elapsed = n < 997 ? 0 : n - 997 + 1, m = std::min(240.L, elapsed);
    const long double integrated = n * 9600.L + 9150.L * (m * (m + 1) / 480 + std::max(0.L, elapsed - 240));
    const auto expectedCaptures = uint64_t(std::floor(integrated / rate));
    if (expectedCaptures > captures) { captures = expectedCaptures; last = n; }
    for (int c = 0; c < 2; ++c) check(changing[n * 2 + c] == float(quantize(input[last * 2 + c], 24)), "Rate sweep follows integrated frequency without resetting clock phase");
  }
}
static void boundariesAndTails() {
  double residual = 0, peak = 0;
  for (uint32_t rate : {8000u, 44100u, 48000u, 96000u, 192000u, 384000u}) {
    NativeEffect effect("resonance.lofimat.v1", rate); effect.parameter(2, 20); effect.parameter(4, 1); effect.parameter(7, 24);
    std::vector<float> audio(4096 * 2, 1); process(effect, audio);
    const auto tail = effect.tail(); check(tail > .5 && tail < .6, "Lowest rate/smoother/boost has a bounded half-second decay budget");
    audio.assign((size_t(std::ceil(tail * rate)) + 1024) * 2, 0); process(effect, audio);
    for (size_t n = audio.size() - 2048; n < audio.size(); ++n) residual = std::max(residual, std::abs(double(audio[n])));
    NativeEffect automated("resonance.lofimat.v1", rate); uint32_t seed = 1729; bool ok = true; tracker_audit_begin();
    for (uint32_t n = 0; n < 24000; ++n) {
      if (n % 37 == 0) for (const auto &p : automated.definition().parameters) {
        seed = seed * 1664525 + 1013904223; ok &= automated.parameter(p.id, seed & 256 ? p.minimum : p.maximum);
      }
      float frame[]{float(std::sin(n * .131)), float(std::cos(n * .347))}; ok &= automated.process(frame, 1);
      peak = std::max({peak, std::abs(double(frame[0])), std::abs(double(frame[1]))});
    }
    audited(ok);
  }
  check(residual < 1e-10 && peak < 32, "Noiseless release reaches below -200 dB and interrupted parameter extremes remain bounded");
  NativeEffect planned("resonance.lofimat.v1", 48000); const auto original = planned.state();
  planned.includeParameterRange(2, 20, 384000); planned.includeParameterRange(4, 0, 1); planned.includeParameterRange(7, -24, 24);
  check(planned.tail() > .5 && planned.state() == original, "Future automation prepares decay without changing saved targets");
  const auto budget = planned.tail(); planned.parameter(2, 20); planned.parameter(4, 1); planned.parameter(7, 24);
  check(planned.tail() == budget, "Prepared range covers the worst LofiMat release");
  NativeEffect dry("resonance.lofimat.v1", 48000); dry.parameter(6, 0); NativeEffect restored("resonance.lofimat.v1", 48000, dry.state());
  check(restored.tail() == 0, "Permanently dry restored state needs no tail");
  std::cout << "LofiMat release residual " << residual << ", interrupted automation peak " << peak << '\n';
}
static void apiAndExport() {
  TrackerSession *session = [TrackerSession new]; NSError *problem = nil;
  auto call = [&](NSString *method, NSDictionary *params, bool write = false) -> NSDictionary * {
    NSMutableDictionary *p = [params mutableCopy]; if (write) p[@"expectedRevision"] = session.automationRevision;
    auto result = [session automationMethod:method params:p error:&problem];
    if (!result) throw std::runtime_error(problem.localizedDescription.UTF8String); return result;
  };
  NSDictionary *effect = nil;
  for (NSDictionary *d in session.builtInPlugins) if ([d[@"classID"] isEqual:@"resonance.lofimat.v1"]) effect = d;
  check(effect != nil, "LofiMat is discoverable without scanning");
  NSString *base = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  NSString *plain = [base stringByAppendingString:@".plain.wav"], *wet = [base stringByAppendingString:@".lofi.wav"], *project = [base stringByAppendingString:@".resonance"];
  check([TrackerSession exportData:session.serializedData path:plain error:&problem], "Plain reference exports");
  call(@"plugin.add", @{@"descriptor": effect}, true);
  NSArray *parameters = call(@"plugin.parameters.get", @{@"slot": @0})[@"data"];
  check(parameters.count == 9 && [parameters[1][@"unitLabel"] isEqual:@"bits"] && [parameters[2][@"displayScale"] isEqual:@"logarithmic"] && [parameters[8][@"step"] floatValue] == 1, "API exposes bit/rate units and quantized noise seed");
  call(@"plugin.parameters.set", @{@"slot": @0, @"values": @[@{@"id": @1, @"value": @7.25}, @{@"id": @2, @"value": @11025}, @{@"id": @3, @"value": @7}, @{@"id": @4, @"value": @1}, @{@"id": @5, @"value": @30}, @{@"id": @7, @"value": @-6}, @{@"id": @8, @"value": @16777214.7}]}, true);
  check([call(@"plugin.parameters.get", @{@"slot": @0})[@"data"][8][@"value"] floatValue] == 16777215, "API reads actual quantized seed without truncating 24-bit precision");
  NSDictionary *saved = call(@"plugin.state.get", @{@"slot": @0})[@"data"];
  call(@"history.undo", @{@"domain": @"plugins"}, true);
  check([call(@"plugin.parameters.get", @{@"slot": @0})[@"data"][1][@"value"] floatValue] == 16, "LofiMat batch is one Undo");
  call(@"history.redo", @{@"domain": @"plugins"}, true);
  check([session savePath:project error:&problem], "LofiMat saves"); TrackerSession *restored = [TrackerSession new];
  check([restored openPath:project error:&problem], "LofiMat reopens");
  check([[restored automationMethod:@"plugin.state.get" params:@{@"slot": @0} error:&problem][@"data"][@"data"] isEqual:saved[@"data"]], "All LofiMat controls persist exactly");
  check([TrackerSession exportData:restored.serializedData path:wet error:&problem], "Saved LofiMat exports");
  NSData *a = [NSData dataWithContentsOfFile:plain], *b = [NSData dataWithContentsOfFile:wet];
  check(b.length > a.length && b.length < a.length + 48000 * 8, "Generated noise does not make export's finite tail unbounded");
  auto sample = [](NSData *data, uint64_t frame, int c) {
    float value = 0; const auto offset = 44 + (frame * 2 + c) * 4;
    if (offset + 4 <= data.length) std::memcpy(&value, static_cast<const char *>(data.bytes) + offset, 4); return double(value);
  };
  double maximum = 0, memory[2]{}, held[2]{}; uint64_t lastCapture = UINT64_MAX;
  const double pole = std::exp(-2 * std::numbers::pi * .45 * 11025 / 48000), gain = std::pow(10., -6. / 20);
  for (uint64_t n = 0; n < (b.length - 44) / 8; ++n) {
    const uint64_t tick = n * 11025 / 48000;
    if (tick != lastCapture) {
      lastCapture = tick;
      for (int c = 0; c < 2; ++c) held[c] = quantize(sample(a, n, c) + .07 * noiseAt(16777215, tick * 2 + c), 7.25);
    }
    for (int c = 0; c < 2; ++c) {
      memory[c] = held[c] * (1 - pole) + memory[c] * pole;
      maximum = std::max(maximum, std::abs(sample(b, n, c) - gain * (.3 * sample(a, n, c) + memory[c])));
    }
  }
  check(maximum < 3e-8, "Entire saved WAV matches independent rational clock, skip-ahead noise, quantizer and one-pole convolution including the finite noise tail");
  std::cout << "LofiMat independent saved WAV maximum " << maximum << '\n';
  for (NSString *path in @[plain, wet, project]) [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}
int main() { @autoreleasepool { try {
  quantizationAndClocks(); noise(); smoothingAndBypass(); boundariesAndTails(); apiAndExport();
  std::cout << "PASS LofiMat: amplitude quantization, exact rational clocks, deterministic stereo noise, smoothing phase, clock-preserving sweeps, warm bypass, extreme controls, finite decay, API/history/persistence and independent saved audio\n";
  return 0;
} catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } } }
