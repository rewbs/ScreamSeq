#include "editor/NativeEffects.hpp"
#include "editor/FractionalDelay.hpp"
#include "../Audio/AudioUnitHost.hpp"
#import "../Bridge/TrackerSession.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <iostream>
#include <numbers>
using namespace Tracker;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) { *a = *f = *l = 0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
#endif
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static void audited(bool ok) { uint64_t a, f, l; tracker_audit_end(&a, &f, &l); check(ok, "Comb DSP succeeds"); check(!(a + f + l), "Delay/comb render path allocates, frees and locks nothing"); }
static void process(NativeEffect &effect, std::vector<float> &audio, uint32_t block = 128) {
  for (uint32_t at = 0; at < audio.size() / 2; at += block) {
    tracker_audit_begin(); auto ok = effect.process(audio.data() + at * 2, std::min<uint32_t>(block, audio.size() / 2 - at)); audited(ok);
  }
}
static void interpolation() {
  double polynomialError = 0, phaseError = 0, maximumGain = 0;
  for (unsigned degree = 0; degree <= 7; ++degree) {
    FractionalDelay delay(36);
    tracker_audit_begin();
    for (unsigned n = 0; n < 2000; ++n) {
      const double distance = 4 + (n % 317) / 10.; auto out = delay.read(distance);
      if (n > 64) {
        const double expected = std::pow((n - distance) / 2000., degree);
        polynomialError = std::max({polynomialError, std::abs(out[0] - expected), std::abs(out[1] + expected * .3)});
      }
      const double input = std::pow(n / 2000., degree); delay.push(input, -.3 * input);
    }
    audited(true);
    delay.clear(); auto zero = delay.read(20.5); check(zero[0] == 0 && zero[1] == 0, "Clearing delay removes both histories");
  }
  // An analytic complex sinusoid independently measures phase and magnitude.
  // At the desired fundamental, the ideal delay has exactly one cycle of lag.
  for (unsigned i = 0; i <= 200; ++i) {
    const double distance = 4 + i * .2, omega = 2 * std::numbers::pi / distance;
    FractionalDelay delay(50); std::complex<double> response{};
    tracker_audit_begin();
    for (unsigned n = 0; n < 1024; ++n) {
      const auto out = delay.read(distance);
      if (n > 100) response += std::complex<double>(out[0], out[1]) * std::polar(1., -omega * n) / 923.;
      delay.push(std::cos(omega * n), std::sin(omega * n));
    }
    audited(true);
    phaseError = std::max(phaseError, std::abs(std::arg(response)));
    maximumGain = std::max(maximumGain, std::abs(response));
    check(std::abs(response) > .97, "Interpolation preserves the fundamental within 0.27 dB");
  }
  check(polynomialError < 2e-15 && phaseError < .004 && maximumGain < 1 + 1e-12, "Eighth-tap interpolation has accurate polynomial, amplitude and phase response");
  std::cout << "Delay polynomial error " << polynomialError << ", fundamental phase error " << phaseError << " rad, maximum gain " << maximumGain << '\n';
}
static void echoes() {
  constexpr uint32_t rate = 44000, delay = 100, count = 25600; // A4 is exactly 100 samples at this rate.
  for (float feedback : {-95.f, -50.f, 0.f, 50.f, 95.f}) for (float transpose : {0.f, 12.f}) {
    NativeEffect effect("resonance.comb-filter.v1", rate); effect.parameter(3, feedback); effect.parameter(4, 100); effect.parameter(2, transpose);
    std::vector<float> impulse(count * 2); impulse[0] = 1; process(effect, impulse, 17);
    const auto period = transpose == 0 ? delay : delay / 2;
    const double g = feedback / 100., input = 1 - std::abs(g);
    for (uint32_t i = 0; i < count; ++i) {
      const double expected = i && i % period == 0 ? input * std::pow(g, i / period - 1) : 0;
      check(std::abs(impulse[i * 2] - expected) < 3e-8 && impulse[i * 2 + 1] == 0, "Integer-delay signed echo train matches independent feedback recurrence and semitone tuning");
    }
  }
  NativeEffect dry("resonance.comb-filter.v1", 48000); dry.parameter(4, 0);
  auto state = dry.state(); NativeEffect restored("resonance.comb-filter.v1", 48000, state);
  check(restored.tail() == 0, "A restored permanently dry comb adds no export tail");
  std::vector<float> input(20000); for (size_t i = 0; i < input.size(); ++i) input[i] = .2 * std::sin(i * .2);
  auto output = input; process(restored, output); check(output == input, "Zero wet mix is exact dry identity");
  check(restored.parameter(1, 69.4f) && restored.value(1) == 69 && restored.parameter(1, 69.6f) && restored.value(1) == 70, "Musical note values round at the processor boundary");
}
static void repeatedInertia() {
  NativeEffect a("resonance.comb-filter.v1", 48000), b("resonance.comb-filter.v1", 48000);
  a.parameter(1, 45); b.parameter(1, 45);
  std::vector<float> history(8000); for (size_t i = 0; i < history.size(); ++i) history[i] = .1 * std::sin(i * .041);
  auto copy = history; process(a, history); process(b, copy);
  a.parameter(1, 69); b.parameter(1, 69);
  bool same = true, ok = true; tracker_audit_begin();
  for (uint32_t i = 0; i < 3000; ++i) {
    if (i % 32 == 0) ok = a.parameter(5, 20) && ok;
    float left[]{float(.1 * std::sin(i * .1)), .05f}, right[]{left[0], left[1]};
    ok = a.process(left, 1) && b.process(right, 1) && ok;
    same = same && left[0] == right[0] && left[1] == right[1];
  }
  audited(ok); check(same, "Repeated inertia automation does not restart pitch glide");
  for (double rate : {8000., 44100., 96000.}) {
    NativeEffect clamped("resonance.comb-filter.v1", rate); clamped.parameter(1, 127); clamped.parameter(2, 12);
    clamped.parameter(3, 0); clamped.parameter(4, 100);
    std::vector<float> impulse(32); impulse[0] = 1; process(clamped, impulse);
    for (size_t i = 0; i < impulse.size(); ++i) check(impulse[i] == (i == 8 ? 1.f : 0.f), "Tuning above sample-rate/4 clamps to an exactly causal four-sample delay");
  }
}
static void routing() {
  std::vector<float> input(16000); for (size_t i = 0; i < input.size(); ++i) input[i] = .1 * std::sin(i * .107);
  NativeEffect stereo("resonance.comb-filter.v1", 48000); auto filtered = input; process(stereo, filtered);
  for (int channel = 0; channel < 5; ++channel) {
    NativeEffect effect("resonance.comb-filter.v1", 48000); effect.parameter(6, channel);
    auto out = input; process(effect, out, 17);
    for (size_t i = 0; i < out.size(); i += 2) {
      const double l = input[i], r = input[i + 1], fl = filtered[i], fr = filtered[i + 1];
      const double expectedL = channel < 2 ? fl : channel == 2 ? l : channel == 3 ? (fl + fr + l - r) / 2 : (l + r + fl - fr) / 2;
      const double expectedR = channel == 0 || channel == 2 ? fr : channel == 1 ? r : channel == 3 ? (fl + fr - l + r) / 2 : (l + r - fl + fr) / 2;
      check(std::abs(out[i] - expectedL) < 3e-8 && std::abs(out[i + 1] - expectedR) < 3e-8, "Comb channel routing isolates left/right/mid/side correctly");
    }
  }
}
static void stressAndTails() {
  double peak = 0, residual = 0;
  for (uint32_t rate : {8000, 44100, 48000, 96000, 192000}) {
    NativeEffect effect("resonance.comb-filter.v1", rate); effect.parameter(3, 95); effect.parameter(4, 100); effect.parameter(5, 5);
    uint32_t seed = 1729; bool ok = true; tracker_audit_begin();
    for (uint32_t i = 0; i < rate * 4; ++i) {
      if (i % 97 == 0) {
        seed = seed * 1664525 + 1013904223;
        ok = effect.parameter(1, seed & 1 ? 12 : 127) && effect.parameter(2, seed & 2 ? -12 : 12) && effect.parameter(3, seed & 4 ? -95 : 95) && ok;
      }
      float frame[]{float(.1 * std::sin(i * .077)), float(.1 * std::cos(i * .117))};
      ok = effect.process(frame, 1) && ok; peak = std::max({peak, std::abs(double(frame[0])), std::abs(double(frame[1]))});
    }
    audited(ok);
  }
  check(peak < 2, "Adversarial rapid retuning and feedback sign changes remain bounded");
  for (uint32_t rate : {8000, 48000}) for (float feedback : {-95.f, 95.f}) {
    NativeEffect effect("resonance.comb-filter.v1", rate); effect.parameter(1, 12); effect.parameter(2, -12);
    effect.parameter(3, feedback); effect.parameter(4, 100); effect.parameter(7, 24);
    std::vector<float> drive(rate * 4);
    for (size_t i = 0; i < drive.size() / 2; ++i) drive[i * 2] = drive[i * 2 + 1] = .2 + .1 * std::sin(2 * std::numbers::pi * 8.175798915643707 * (feedback < 0 ? .5 : 1) * i / rate);
    process(effect, drive);
    const auto tail = effect.tail(); check(tail > 40 && tail <= 60, "Lowest tuning and high feedback have a long bounded tail");
    const auto decay = uint32_t(std::ceil(tail * rate));
    std::vector<float> silence((decay + rate / 10) * 2); process(effect, silence, 4096);
    for (size_t i = decay * 2; i < silence.size(); ++i) residual = std::max(residual, std::abs(double(silence[i])));
  }
  check(residual < 1e-8, "Extreme-tuning release is below -160 dB by the estimated tail");
  std::cout << "Comb rapid-modulation peak " << peak << ", extreme decay residual " << residual << '\n';
}
static void api() {
  TrackerSession *session = [TrackerSession new]; NSError *problem = nil;
  auto call = [&](NSString *method, NSDictionary *params, bool write = false) -> NSDictionary * {
    NSMutableDictionary *p = [params mutableCopy]; if (write) p[@"expectedRevision"] = session.automationRevision;
    auto reply = [session automationMethod:method params:p error:&problem];
    if (!reply) throw std::runtime_error(problem.localizedDescription.UTF8String); return reply;
  };
  NSDictionary *comb = nil;
  for (NSDictionary *item in call(@"plugin.discover", @{@"format": @"Built-in"})[@"data"])
    if ([item[@"classID"] isEqual:@"resonance.comb-filter.v1"]) comb = item;
  check(comb != nil, "Comb available in scan-free API catalog"); call(@"plugin.add", @{@"descriptor": comb}, true);
  NSArray *parameters = call(@"plugin.parameters.get", @{@"slot": @0})[@"data"];
  check(parameters.count == 8 && [parameters[1][@"step"] floatValue] == 1 && [parameters[1][@"unitLabel"] isEqual:@"MIDI"] && [parameters[5][@"unitLabel"] isEqual:@"ms"], "Comb metadata exposes all controls with note quantization and time units");
  call(@"plugin.parameters.set", @{@"slot": @0, @"values": @[@{@"id": @1, @"value": @60.7}, @{@"id": @2, @"value": @7.125}, @{@"id": @3, @"value": @-87.5}, @{@"id": @5, @"value": @350}]}, true);
  check([call(@"plugin.parameters.get", @{@"slot": @0})[@"data"][1][@"value"] intValue] == 61, "API reads back actual quantized note");
  NSDictionary *saved = call(@"plugin.state.get", @{@"slot": @0})[@"data"];
  call(@"history.undo", @{@"domain": @"plugins"}, true);
  check([call(@"plugin.parameters.get", @{@"slot": @0})[@"data"][3][@"value"] floatValue] == 50, "Comb batch is one Undo");
  call(@"history.redo", @{@"domain": @"plugins"}, true);
  NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
  check([session savePath:path error:&problem], "Comb saves"); TrackerSession *reopened = [TrackerSession new];
  check([reopened openPath:path error:&problem], "Comb project reopens");
  auto restored = [reopened automationMethod:@"plugin.state.get" params:@{@"slot": @0} error:&problem];
  check([restored[@"data"][@"data"] isEqual:saved[@"data"]], "All comb settings persist exactly");
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}
static void exported() {
  TrackerSession *session = [TrackerSession new]; NSError *problem = nil;
  NSString *base = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  NSString *plain = [base stringByAppendingString:@".plain.wav"], *filtered = [base stringByAppendingString:@".comb.wav"];
  check([TrackerSession exportData:session.serializedData path:plain error:&problem], "Dry project exports");
  NSDictionary *comb = nil;
  for (NSDictionary *item in session.builtInPlugins) if ([item[@"classID"] isEqual:@"resonance.comb-filter.v1"]) comb = item;
  check([session addPlugin:comb error:&problem], "Saved-project fixture adds comb");
  check([session automationMethod:@"plugin.parameters.set" params:@{@"slot": @0, @"values": @[@{@"id": @3, @"value": @0}], @"expectedRevision": session.automationRevision} error:&problem] != nil, "Saved-project fixture sets feedforward comb");
  check([TrackerSession exportData:session.serializedData path:filtered error:&problem], "Comb project exports");
  NSData *a = [NSData dataWithContentsOfFile:plain], *b = [NSData dataWithContentsOfFile:filtered];
  check(b.length > a.length && b.length < a.length + 48000 * 8, "Saved comb retains its intentional delay tail without a long silent suffix");
  // 48000/440 = 109 + 1/11 samples. These exact rational Lagrange
  // coefficients form an independent full-file convolution oracle.
  constexpr int64_t numerators[]{-15824, 163744, -941528, 18830560, 1883056, -538016, 117691, -12512};
  auto sample = [](NSData *data, int64_t frame, int channel) {
    float result = 0;
    if (frame >= 0 && 44 + (uint64_t(frame) * 2 + channel + 1) * 4 <= data.length)
      std::memcpy(&result, static_cast<const char *>(data.bytes) + 44 + (frame * 2 + channel) * 4, 4);
    return result;
  };
  double maximum = 0, energy = 0;
  for (uint64_t n = 0; n < (b.length - 44) / 8; ++n) for (int c = 0; c < 2; ++c) {
    long double delayed = 0;
    for (int k = 0; k < 8; ++k) delayed += sample(a, int64_t(n) - (106 + k), c) * (static_cast<long double>(numerators[k]) / 19487171);
    const double expected = .5 * (sample(a, n, c) + delayed), actual = sample(b, n, c);
    maximum = std::max(maximum, std::abs(actual - expected)); energy += std::abs(actual);
  }
  check(energy > 1 && maximum < 3e-8, "Saved-project comb WAV matches independent rational convolution, including tail");
  std::cout << "Comb saved WAV maximum error " << maximum << '\n';
  [[NSFileManager defaultManager] removeItemAtPath:plain error:nil]; [[NSFileManager defaultManager] removeItemAtPath:filtered error:nil];
}
int main() { @autoreleasepool { try {
  interpolation(); echoes(); repeatedInertia(); routing(); stressAndTails(); api(); exported();
  std::cout << "PASS fractional delay/Comb Filter: eight-tap interpolation, signed echo train, musical transpose, note quantization, channel isolation, rapid retuning, long decay, RT audit and API/history/persistence\n";
  return 0;
} catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } } }
