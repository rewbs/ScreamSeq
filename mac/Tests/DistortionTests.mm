#include "editor/NativeEffects.hpp"
#include "editor/TrackerDocument.hpp"
#include "../Audio/AudioUnitHost.hpp"
#include "../Audio/AudioExport.hpp"
#include "OversamplingReference.hpp"
#import "../Bridge/TrackerSession.h"
#include <cmath>
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
static void audited(bool ok) { uint64_t a, f, l; tracker_audit_end(&a, &f, &l); check(ok, "Distortion processes"); check(!(a + f + l), "Distortion performs no RT allocation/free/lock"); }
static void process(NativeEffect &effect, std::vector<float> &audio, uint32_t block = 128) {
  for (uint32_t n = 0; n < audio.size() / 2; n += block) {
    tracker_audit_begin(); bool ok = effect.process(audio.data() + n * 2, std::min<uint32_t>(block, audio.size() / 2 - n)); audited(ok);
  }
}
static std::vector<float> signal(size_t frames) {
  std::vector<float> result(frames * 2);
  for (size_t i = 0; i < frames; ++i) {
    result[i * 2] = float(.3 * std::sin(i * .371) + .1 * std::cos(i * .132));
    result[i * 2 + 1] = float(.2 * std::sin(i * .491));
  }
  return result;
}
static double shape(double x, int mode) {
  if (mode == 0) return std::tanh(x);
  if (mode == 1) return std::min(1., std::max(-1., x));
  if (mode == 2) {
    // Triangle of period 4, independently expressed without remainder().
    const double period = x - 4 * std::floor((x + 1) / 4);
    return period < 1 ? period : 2 - period;
  }
  double positive = std::fmod(x + 1, 2); if (positive < 0) positive += 2;
  return positive - 1;
}
static std::vector<double> reference(const std::vector<double> &input, int mode, bool automate) {
  using namespace OversamplingReference;
  auto high = input; std::vector<std::vector<double>> filters;
  for (int stage = 0; stage < 4; ++stage) {
    filters.push_back(coefficients(stage == 0 ? 145 : stage == 1 ? 49 : 33, stage == 0 ? 11 : 13));
    std::vector<double> up(high.size() * 2);
    for (size_t n = 0; n < high.size(); ++n) up[n * 2] = high[n] * 2;
    high = convolution(up, filters.back());
  }
  for (size_t n = 0; n < high.size(); ++n) {
    const double progress = automate ? std::clamp((double(n) - (400 * 16 + 720) + 1) / (240 * 16), 0., 1.) : 0;
    const double gain = automate ? 1 + (std::pow(10., 36. / 20) - 1) * progress : 4;
    // A simultaneous mode change and drive ramp checks high-rate control delay
    // as well as the smoothed crossfade between two different transfer curves.
    const double x = high[n] * gain;
    high[n] = (1 - progress) * shape(x, mode) + progress * shape(x, (mode + 1) % 4);
  }
  for (int stage = 3; stage >= 0; --stage) {
    const auto down = convolution(high, filters[stage]); high.resize(down.size() / 2);
    for (size_t n = 0; n < high.size(); ++n) high[n] = down[n * 2];
  }
  const double pole = std::exp(-2 * std::numbers::pi * 5 / 48000); double previous = 0, output = 0;
  for (auto &x : high) { output = (x - previous) * (1 + pole) / 2 + pole * output; previous = x; x = output; }
  return high;
}
static void independent() {
  double maximum = 0;
  const auto original = signal(2048);
  for (int mode = 0; mode < 4; ++mode) for (bool automate : {false, true}) {
    NativeEffect actual("resonance.distortion.v1", 48000);
    actual.parameter(1, automate ? 0 : float(20 * std::log10(4.))); actual.parameter(2, mode); actual.parameter(6, 0);
    auto out = original;
    if (automate) {
      check(actual.process(out.data(), 400), "Timing fixture begins");
      tracker_audit_begin(); bool ok = actual.parameter(1, 36) && actual.parameter(2, (mode + 1) % 4) && actual.process(out.data() + 800, 1648); audited(ok);
    } else process(actual, out, 17);
    for (int c = 0; c < 2; ++c) {
      std::vector<double> input(2048); for (size_t n = 0; n < input.size(); ++n) input[n] = original[n * 2 + c];
      // Static drive is an actual float parameter; account for its exact target
      // while retaining an independently computed transfer/convolution oracle.
      if (!automate) for (auto &x : input) x *= std::pow(10., double(float(20 * std::log10(4.))) / 20) / 4;
      const auto expected = reference(input, mode, automate);
      for (size_t n = 0; n < expected.size(); ++n) maximum = std::max(maximum, std::abs(double(out[n * 2 + c]) - expected[n]));
    }
  }
  check(maximum < 8e-8, "Four curves and simultaneous drive/mode automation match independent high-rate convolution and control timing");
  std::cout << "Distortion independent nonlinear/automation reference maximum " << maximum << '\n';
}
static void dryAndOutputTiming(uint32_t rate) {
  const auto input = signal(4096); NativeEffect bypass("resonance.distortion.v1", rate);
  bypass.parameter(0, 0); auto dry = input; process(bypass, dry, 17);
  for (size_t i = 0; i < dry.size(); ++i) check(dry[i] == (i < 180 ? 0 : input[i - 180]), "Disabled oversampled device is exactly dry with its reported delay");
  NativeEffect controls("resonance.distortion.v1", rate); controls.parameter(4, 100); controls.parameter(5, 0); controls.parameter(6, 0);
  auto out = input; check(controls.process(out.data(), 123), "Output timing fixture starts"); controls.parameter(6, 6);
  check(controls.process(out.data() + 246, 3973), "Output timing fixture ends");
  const uint32_t smoothing = uint32_t(std::ceil(rate * .005)); double current = 1;
  const double target = std::pow(10., 6. / 20), increment = (target - 1) / smoothing;
  for (size_t n = 0; n < out.size() / 2; ++n) {
    if (n >= 123 + 90 && n < 123 + 90 + smoothing) { current += increment; if (n + 1 == 123 + 90 + smoothing) current = target; }
    for (size_t c = 0; c < 2; ++c) {
      const float expected = n < 90 ? 0 : float(input[(n - 90) * 2 + c] * current);
      check(std::abs(out[n * 2 + c] - expected) < 6e-8, "Output automation follows the latency-compensated input sample and exact smoothing duration");
    }
  }
  NativeEffect continuous("resonance.distortion.v1", rate), enabled("resonance.distortion.v1", rate);
  enabled.parameter(0, 0); auto a = input, b = input;
  process(continuous, a); check(enabled.process(b.data(), 1000), "Warm bypass begins"); enabled.parameter(0, 1);
  check(enabled.process(b.data() + 2000, 3096), "Warm bypass re-enables");
  check(std::equal(a.begin() + (1000 + smoothing + 90) * 2, a.end(), b.begin() + (1000 + smoothing + 90) * 2), "Re-enabled warm path matches uninterrupted wet audio exactly");
}
static void toneAndTails() {
  double residual = 0, maximum = 0;
  for (uint32_t rate : {8000u, 44100u, 48000u, 96000u, 192000u, 384000u}) {
    for (int mode = 0; mode < 4; ++mode) {
      NativeEffect effect("resonance.distortion.v1", rate);
      effect.parameter(1, 36); effect.parameter(2, mode); effect.parameter(3, mode & 1 ? 100 : -100); effect.parameter(6, 24);
      float silence[2]{}; check(effect.process(silence, 1) && silence[0] == 0 && silence[1] == 0, "Every curve maps silence to exact silence");
      std::vector<float> audio(4096 * 2);
      uint32_t seed = 1729;
      for (auto &x : audio) { seed = seed * 1664525 + 1013904223; x = float(double(int32_t(seed)) / 2147483648.); }
      process(effect, audio);
      for (auto x : audio) { check(std::isfinite(x), "Extreme drive/tone/mode stays finite"); maximum = std::max(maximum, std::abs(double(x))); }
      std::vector<float> tail((uint32_t(std::ceil(effect.tail() * rate)) + 1024) * 2); process(effect, tail);
      for (size_t i = tail.size() - 2048; i < tail.size(); ++i) residual = std::max(residual, std::abs(double(tail[i])));
    }
  }
  check(maximum < 64 && residual < 1e-10, "Extreme wet output is bounded and decay budget retains the DC/filter release below -200 dB");
  for (float tone : {-100.f, 100.f}) {
    NativeEffect neutral("resonance.distortion.v1", 48000), colored("resonance.distortion.v1", 48000);
    for (auto *e : {&neutral, &colored}) { e->parameter(1, 0); e->parameter(2, 1); e->parameter(6, 0); }
    colored.parameter(3, tone); std::vector<float> a(96000);
    for (size_t n = 0; n < 48000; ++n) a[n * 2] = float(.01 * std::sin(2 * std::numbers::pi * 2000 * n / 48000));
    auto b = a; process(neutral, a); process(colored, b); double pa = 0, pb = 0;
    for (size_t i = 48000; i < a.size(); i += 2) { pa += a[i] * a[i]; pb += b[i] * b[i]; check(b[i + 1] == 0, "Tone and nonlinear stages preserve stereo isolation"); }
    check(std::abs(std::sqrt(pb / pa) - std::pow(10., tone * .12 / 40)) < 2e-6, "Tone shelf measures the independent midpoint gain at 2 kHz");
  }
  std::cout << "Distortion extreme peak " << maximum << ", release residual " << residual << '\n';
}
static void interruptedAutomation() {
  double peak = 0;
  for (uint32_t rate : {8000u, 48000u, 384000u}) {
    NativeEffect effect("resonance.distortion.v1", rate); uint32_t seed = 1729; bool ok = true;
    tracker_audit_begin();
    for (uint32_t n = 0; n < 24000; ++n) {
      if (n % 37 == 0) for (const auto &p : effect.definition().parameters) {
        seed = seed * 1664525 + 1013904223;
        ok &= effect.parameter(p.id, seed & 256 ? p.minimum : p.maximum);
      }
      float frame[]{float(std::sin(n * .377)), float(std::cos(n * .499))};
      ok &= effect.process(frame, 1);
      peak = std::max({peak, std::abs(double(frame[0])), std::abs(double(frame[1]))});
    }
    audited(ok);
  }
  check(peak < 96, "Rapidly interrupted drive/mode/tone/mix/bypass changes remain bounded across supported rate extremes");
  std::cout << "Distortion interrupted automation peak " << peak << '\n';
}
static PluginDescriptor descriptor() {
  for (const auto &d : NativePlugin::builtins()) if (d.classID == "resonance.distortion.v1") return d;
  throw std::runtime_error("Missing distortion descriptor");
}
static void compensation() {
  auto doc = Document::demo();
  doc->annotate([](NativeSong &n) {
    const auto master = n.makeEntity().id;
    for (const auto &[index, track] : n.tracks) n.mixer.buses.push_back({track.id, master, MixerBusKind::Track, "Track"});
    n.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
  });
  for (uint32_t rate : {44100u, 48000u, 96000u}) {
    NativeEffect disabled("resonance.distortion.v1", rate); disabled.parameter(0, 0);
    PluginState state{descriptor()}; state.instanceID = "distortion"; state.state = disabled.state();
    auto render = [&](bool insert, uint32_t block) {
      auto native = doc->native(); if (insert) native.mixer.buses[0].inserts = {"distortion"};
      Renderer renderer(doc->serialize(), rate); PluginChain chain(insert ? std::vector<PluginState>{state} : std::vector<PluginState>{}, rate, true);
      chain.attachInstruments(renderer, &native);
      check(chain.latency() == (insert ? 90. / rate : 0), "Mixer includes the exact oversampling delay");
      std::vector<float> out(16384);
      for (uint32_t n = 0; n < 8192; n += block) {
        const auto count = std::min(block, 8192 - n); tracker_audit_begin(); chain.syncTransport(renderer);
        renderer.render(out.data() + n * 2, count); bool ok = chain.process(out.data() + n * 2, count); audited(ok);
      }
      return out;
    };
    const auto baseline = render(false, 128);
    for (uint32_t block : {17u, 128u, 4096u}) {
      const auto out = render(true, block);
      for (size_t i = 0; i < out.size(); ++i) check(std::abs(out[i] - (i < 180 ? 0 : baseline[i - 180])) < 2e-7, "Parallel mixer tracks align with the delayed distortion insert");
    }
  }
}
static void apiAndExport() {
  TrackerSession *session = [TrackerSession new]; NSError *problem = nil;
  auto call = [&](NSString *method, NSDictionary *params, bool write = false) -> NSDictionary * {
    NSMutableDictionary *p = [params mutableCopy]; if (write) p[@"expectedRevision"] = session.automationRevision;
    NSDictionary *result = [session automationMethod:method params:p error:&problem];
    if (!result) throw std::runtime_error(problem.localizedDescription.UTF8String); return result;
  };
  NSDictionary *effect = nil;
  for (NSDictionary *d in session.builtInPlugins) if ([d[@"classID"] isEqual:@"resonance.distortion.v1"]) effect = d;
  check(effect != nil, "Distortion is available without scanning");
  NSString *base = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  NSString *plain = [base stringByAppendingString:@".plain.wav"], *wet = [base stringByAppendingString:@".wet.wav"], *project = [base stringByAppendingString:@".resonance"];
  check([TrackerSession exportData:session.serializedData path:plain error:&problem], "Plain reference exports");
  call(@"plugin.add", @{@"descriptor": effect}, true);
  NSArray *parameters = call(@"plugin.parameters.get", @{@"slot": @0})[@"data"];
  check(parameters.count == 7 && [parameters[2][@"choices"] isEqual:@[@"Soft clip", @"Hard clip", @"Fold", @"Wrap"]], "API exposes all independent levels and four labeled modes");
  call(@"plugin.parameters.set", @{@"slot": @0, @"values": @[@{@"id": @1, @"value": @18}, @{@"id": @2, @"value": @2}, @{@"id": @3, @"value": @-25}, @{@"id": @4, @"value": @30}]}, true);
  NSDictionary *saved = call(@"plugin.state.get", @{@"slot": @0})[@"data"];
  call(@"history.undo", @{@"domain": @"plugins"}, true);
  check([call(@"plugin.parameters.get", @{@"slot": @0})[@"data"][1][@"value"] floatValue] == 6, "One Undo restores the entire distortion batch");
  call(@"history.redo", @{@"domain": @"plugins"}, true);
  check([session savePath:project error:&problem], "Distortion saves"); TrackerSession *restored = [TrackerSession new];
  check([restored openPath:project error:&problem], "Distortion reopens");
  check([[restored automationMethod:@"plugin.state.get" params:@{@"slot": @0} error:&problem][@"data"][@"data"] isEqual:saved[@"data"]], "All distortion settings persist exactly");
  check([TrackerSession exportData:restored.serializedData path:wet error:&problem], "Saved distortion project exports");
  NSData *a = [NSData dataWithContentsOfFile:plain], *b = [NSData dataWithContentsOfFile:wet];
  check(b.length > a.length && b.length < a.length + 48000 * 8 * 1.1, "Export retains the bounded filter tail");
  auto stateData = [[NSData alloc] initWithBase64EncodedString:saved[@"data"] options:0];
  NativeEffect processor("resonance.distortion.v1", 48000, {static_cast<const std::byte *>(stateData.bytes), stateData.length});
  const auto outputFrames = (b.length - 44) / 8;
  std::vector<float> reference((outputFrames + 90) * 2);
  std::memcpy(reference.data(), static_cast<const char *>(a.bytes) + 44, a.length - 44); process(processor, reference, 17);
  double maximum = 0, energy = 0;
  for (size_t i = 0; i < outputFrames * 2; ++i) {
    float actual; std::memcpy(&actual, static_cast<const char *>(b.bytes) + 44 + i * 4, 4);
    maximum = std::max(maximum, std::abs(double(actual) - reference[i + 180])); energy += std::abs(actual);
  }
  check(energy > 1 && maximum < 5e-8, "Saved WAV matches the separately qualified processor with all 90 latency frames removed");
  std::cout << "Distortion saved WAV/latency maximum " << maximum << '\n';
  for (NSString *path in @[plain, wet, project]) [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}
int main() { @autoreleasepool { try {
  independent(); for (auto rate : {8000u, 44100u, 48000u, 96000u, 192000u, 384000u}) dryAndOutputTiming(rate);
  toneAndTails(); interruptedAutomation(); compensation(); apiAndExport();
  std::cout << "PASS Distortion: four independently checked curves, tone, aligned dry/mix/output controls, warm bypass, control timing, stereo isolation, extreme releases, mixer compensation, API/history/persistence and saved WAV\n";
  return 0;
} catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } } }
