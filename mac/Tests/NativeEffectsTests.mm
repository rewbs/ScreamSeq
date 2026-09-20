#include "editor/NativeEffects.hpp"
#include "editor/TrackerDocument.hpp"
#include "../Audio/AudioUnitHost.hpp"
#import "../Bridge/TrackerSession.h"
#include <cmath>
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
template<typename F> static void rejects(F action) {
  bool rejected = false; try { action(); } catch (const std::exception &) { rejected = true; }
  check(rejected, "Invalid effect state is rejected");
}
static void audited(bool ok) {
  uint64_t a, f, l; tracker_audit_end(&a, &f, &l);
  check(ok, "Built-in processing succeeds"); check(!(a + f + l), "Built-in callbacks do not allocate, free or lock");
}
static void process(NativeEffect &effect, std::vector<float> &samples, uint32_t block) {
  for (uint32_t pos = 0; pos < samples.size() / 2; pos += block) {
    auto count = std::min<uint32_t>(block, samples.size() / 2 - pos);
    tracker_audit_begin(); const bool ok = effect.process(samples.data() + pos * 2, count); audited(ok);
  }
}
static void utilities(uint32_t rate) {
  NativeEffect gain("resonance.gainer.v1", rate);
  float pair[]{.25f, -.5f}; check(gain.process(pair, 1) && pair[0] == .25f && pair[1] == -.5f, "Default gain is exact unity");
  gain.parameter(1, -6); gain.parameter(2, -50); gain.parameter(3, 1);
  std::vector<float> dc(rate * 2, .25f); process(gain, dc, 128);
  const double expected = .25 * std::pow(10., -6. / 20);
  check(std::abs(dc.back() - expected * .5) < 1e-8 && std::abs(dc[dc.size() - 2] + expected) < 1e-8, "Gain, balance and polarity independently match expected amplitude");
  gain.parameter(0, 0); std::fill(dc.begin(), dc.end(), .25f); process(gain, dc, 17);
  check(dc.back() == .25f && dc[dc.size() - 2] == .25f, "Smoothed bypass reaches exact dry signal");
  NativeEffect offset("resonance.dc-offset.v1", rate);
  std::fill(dc.begin(), dc.end(), .25f); process(offset, dc, 4096);
  check(std::abs(dc.back()) < 1e-12, "Auto DC removes a constant offset after one second");
  NativeEffect manual("resonance.dc-offset.v1", rate); manual.parameter(2, 0); manual.parameter(1, -25);
  std::fill(dc.begin(), dc.end(), .25f); process(manual, dc, 512);
  check(std::all_of(dc.begin(), dc.end(), [](auto x) { return x == 0; }), "Manual correction shifts DC by exact full-scale percentage");
  for (float source : {0.f, 1.f, 2.f}) {
    NativeEffect stereo("resonance.stereo-expander.v1", rate); stereo.parameter(1, 0); stereo.parameter(3, source);
    float input[]{.6f, .2f}; check(stereo.process(input, 1), "Mono fold-down processes");
    const double expectedMono = source == 0 ? .4 : source == 1 ? .6 : .2;
    check(std::abs(input[0] - expectedMono) < 3e-8 && input[0] == input[1], "Mono source retains average, left or right");
  }
  NativeEffect stereo("resonance.stereo-expander.v1", rate); stereo.parameter(1, 200);
  float wide[]{.75f, .25f}; check(stereo.process(wide, 1) && wide[0] == 1 && wide[1] == 0, "Width doubles side without changing mid");
  float mono[]{.25f, .25f}; stereo.process(mono, 1); check(mono[0] == .25f && mono[1] == .25f, "Width preserves mono");
  // Independent steady-state measurements: 5 Hz high-pass and unity-magnitude
  // first-order all-pass, with no reliance on implementation coefficients.
  for (double frequency : {5., 1000., 10000.}) {
    NativeEffect dcFilter("resonance.dc-offset.v1", rate), phase("resonance.stereo-expander.v1", rate);
    phase.parameter(2, 100);
    std::vector<float> signal(rate * 4), shifted;
    for (uint32_t i = 0; i < rate * 2; ++i) signal[i * 2] = signal[i * 2 + 1] = float(.2 * std::sin(2 * std::numbers::pi * frequency * i / rate));
    shifted = signal; auto high = signal; process(dcFilter, high, 128); process(phase, shifted, 17);
    double inputPower = 0, highPower = 0, phasePower = 0, spread = 0;
    for (uint32_t i = rate; i < rate * 2; ++i) {
      inputPower += double(signal[i * 2]) * signal[i * 2]; highPower += double(high[i * 2]) * high[i * 2];
      phasePower += double(shifted[i * 2]) * shifted[i * 2]; spread += std::abs(shifted[i * 2] - shifted[i * 2 + 1]);
      check(shifted[i * 2 + 1] == signal[i * 2 + 1], "Phase spread preserves right channel");
    }
    const double expectedHigh = frequency / std::sqrt(frequency * frequency + 25);
    check(std::abs(std::sqrt(highPower / inputPower) - expectedHigh) < 3e-5, "DC frequency response matches 5 Hz high-pass");
    check(std::abs(phasePower / inputPower - 1) < 2e-6 && spread > 1, "Phase spread changes mono phase with unity steady-state magnitude");
  }
}
static std::vector<float> automated(const PluginDescriptor &descriptor, uint32_t rate, uint32_t block, bool offline) {
  NativePlugin plugin({descriptor}, rate, offline); plugin.prepareMusicalAutomation();
  const auto &definition = nativeEffect(descriptor.classID);
  for (const auto &p : definition.parameters) {
    plugin.schedule(p.id, p.maximum, 37); plugin.schedule(p.id, p.minimum, 113);
    plugin.schedule(p.id, p.initial, 4097);
  }
  std::vector<float> output(rate * 2);
  for (uint32_t i = 0; i < rate; ++i) { output[i * 2] = .1f + .2f * std::sin(i * .03); output[i * 2 + 1] = .1f * std::cos(i * .07); }
  for (uint32_t pos = 0; pos < rate; pos += block) {
    tracker_audit_begin(); bool ok = plugin.process(output.data() + pos * 2, std::min(block, rate - pos), pos); audited(ok);
  }
  const double expectedLatency = (definition.kind == EffectKind::Maximizer || definition.kind == EffectKind::BusCompressor) ? std::ceil(rate * .005) / rate : definition.kind == EffectKind::Cabinet ? (rate <= 48000 ? 90. : rate <= 96000 ? 88. : rate <= 192000 ? 84. : 72.) / rate : definition.kind == EffectKind::Distortion ? 90. / rate : 0;
  check(plugin.latency() == expectedLatency && plugin.buses().size() == (definition.sidechain ? 3 : 2), "Built-in stereo host reports its exact latency and correct ports");
  auto state = plugin.state(); NativePlugin restored(state, rate, offline);
  check(restored.state().state == state.state, "Native host preserves exact built-in parameter state");
  return output;
}
static void stateAndSmoothing(uint32_t rate) {
  for (const auto &definition : nativeEffects()) {
    NativeEffect effect(definition.identifier, rate); const auto original = effect.state();
    for (size_t length = 1; length < original.size(); ++length)
      rejects([&] { NativeEffect bad(definition.identifier, rate, {original.data(), length}); });
    auto extra = original; extra.push_back(std::byte(0)); rejects([&] { NativeEffect bad(definition.identifier, rate, extra); });
    auto version = original; version[4] = std::byte(2); rejects([&] { NativeEffect bad(definition.identifier, rate, version); });
    const auto other = definition.identifier == nativeEffects()[0].identifier ? nativeEffects()[1].identifier : nativeEffects()[0].identifier;
    rejects([&] { NativeEffect bad(other, rate, original); });
    check(!effect.parameter(999, 0) && !effect.parameter(1, NAN) && !effect.parameter(1, INFINITY), "Invalid parameter IDs and nonfinite values reject");
    check(!effect.parameter(1, definition.parameters[1].maximum + 1), "Out-of-range values reject");
    check(original == effect.state(), "Rejected values preserve state");
    float poison[]{NAN, 0}; check(!effect.process(poison, 1), "Nonfinite input faults");
  }
  NativeEffect gain("resonance.gainer.v1", rate); float start[]{1, 1}; gain.process(start, 1);
  tracker_audit_begin(); bool ok = gain.parameter(3, 1); audited(ok);
  const auto ramp = uint32_t(std::ceil(rate * .005)); std::vector<float> input((ramp + 1) * 2, 1); process(gain, input, 17);
  for (uint32_t i = 0; i < ramp; ++i) {
    const double expected = 1 - 2. * (i + 1) / ramp;
    check(std::abs(input[i * 2] - expected) < 6e-8 && input[i * 2 + 1] == 1, "Polarity transitions linearly over 5 ms without a discontinuity");
  }
  check(input.back() == 1 && input[input.size() - 2] == -1, "Smoothing reaches exact destination");
}
static std::vector<float> songRender(Document &doc, PluginState effect, uint32_t rate, uint32_t block, bool offline) {
  Renderer renderer(doc.serialize(), rate); PluginChain chain({effect}, rate, offline);
  chain.attachInstruments(renderer, &doc.native()); chain.attachMusicalAutomation(renderer, doc.native());
  const auto preparedTail = chain.tail();
  std::vector<float> out(rate * 2);
  for (uint32_t pos = 0; pos < rate; pos += block) {
    auto count = std::min(block, rate - pos); tracker_audit_begin(); chain.syncTransport(renderer);
    renderer.render(out.data() + pos * 2, count); const bool ok = chain.process(out.data() + pos * 2, count); audited(ok);
  }
  check(chain.tail() == preparedTail, "Prepared musical bounds cover every scheduled value without growing during export");
  return out;
}
static void nativeSong(uint32_t rate, const PluginDescriptor &descriptor) {
  auto doc = Document::demo(); PluginState state{descriptor}; state.instanceID = "native-gain";
  const uint32_t parameter = (descriptor.classID == "resonance.digital-filter.v1" || descriptor.classID == "resonance.stereo-expander.v1") ? 2 : nativeEffect(descriptor.classID).kind == EffectKind::Equalizer ? 11 : 1;
  doc->annotate([&](NativeSong &n) {
    auto master = n.makeEntity().id;
    for (const auto &[index, track] : n.tracks) n.mixer.buses.push_back({track.id, master, MixerBusKind::Track, "Track"});
    n.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"}); n.mixer.buses.back().inserts = {"native-gain"};
    n.automation.push_back({n.makeEntity().id, n.patterns.at(0).id, "native-gain", parameter, true,
      {{0, .7, AutomationCurve::Linear}, {256, .8, AutomationCurve::Step}, {512, .5, AutomationCurve::Smooth}}});
  });
  auto expected = songRender(*doc, state, rate, 128, false);
  for (uint32_t block : {17, 512, 4096}) check(expected == songRender(*doc, state, rate, block, true), "Musical built-in automation and graph agree exactly live/offline across callback sizes");
  doc->annotate([](NativeSong &n) { n.automation.clear(); });
  if (expected == songRender(*doc, state, rate, 128, false)) throw std::runtime_error("Musical lane did not change " + descriptor.classID);
}
static void api() {
  TrackerSession *session = [TrackerSession new]; NSError *problem = nil;
  auto call = [&](NSString *method, NSDictionary *params, bool write = false) -> NSDictionary * {
    auto p = [params mutableCopy]; if (write) p[@"expectedRevision"] = session.automationRevision;
    auto reply = [session automationMethod:method params:p error:&problem];
    if (!reply) throw std::runtime_error(problem.localizedDescription.UTF8String); return reply;
  };
  NSArray *descriptors = call(@"plugin.discover", @{@"format": @"Built-in"})[@"data"];
  check(descriptors.count == nativeEffects().size(), "Scan-free API exposes the native catalog");
  for (NSDictionary *descriptor in descriptors) call(@"plugin.add", @{@"descriptor": descriptor}, true);
  NSArray *parameters = call(@"plugin.parameters.get", @{@"slot": @0})[@"data"];
  check([parameters[1][@"unitLabel"] isEqual:@"dB"] && [parameters[3][@"choices"] count] == 2, "API provides units and discrete choice labels");
  call(@"plugin.parameters.set", @{@"slot": @0, @"values": @[@{@"id": @1, @"value": @-12}, @{@"id": @4, @"value": @1}]}, true);
  call(@"history.undo", @{@"domain": @"plugins"}, true);
  check([call(@"plugin.parameters.get", @{@"slot": @0})[@"data"][1][@"value"] floatValue] == 0, "Parameter batch has one plugin Undo");
  call(@"history.redo", @{@"domain": @"plugins"}, true);
  check([call(@"plugin.parameters.get", @{@"slot": @0})[@"data"][1][@"value"] floatValue] == -12, "Parameter batch Redo restores values");
  NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
  check([session savePath:path error:&problem], "Built-in project saves"); TrackerSession *reopened = [TrackerSession new];
  check([reopened openPath:path error:&problem], "Built-in project reopens");
  auto restored = [reopened automationMethod:@"plugin.parameters.get" params:@{@"slot": @0} error:&problem];
  check([restored[@"data"][1][@"value"] floatValue] == -12 && [restored[@"data"][4][@"value"] floatValue] == 1, "Built-in parameters persist across reopen");
  auto state = call(@"plugin.state.get", @{@"slot": @0})[@"data"];
  call(@"plugin.state.set", @{@"slot": @0, @"data": state[@"data"]}, true);
  auto before = session.automationRevision;
  check(![session automationMethod:@"plugin.state.set" params:@{@"slot": @1, @"data": state[@"data"], @"expectedRevision": before} error:&problem] && [before isEqual:session.automationRevision], "Cross-device state rejects atomically");
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}
static void exported() {
  TrackerSession *session = [TrackerSession new]; NSError *problem = nil;
  NSData *plainProject = [session serializedData];
  check([session addPlugin:session.builtInPlugins[0] error:&problem], "Export fixture adds native gain");
  check([session automationMethod:@"plugin.parameters.set" params:@{@"slot": @0, @"values": @[@{@"id": @1, @"value": @-6}],
    @"expectedRevision": session.automationRevision} error:&problem] != nil, "Export fixture sets gain");
  NSString *plain = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".wav"]];
  NSString *changed = [plain stringByAppendingString:@".effect.wav"];
  check([TrackerSession exportData:plainProject path:plain error:&problem] && [TrackerSession exportData:session.serializedData path:changed error:&problem], "Actual saved-project effect export succeeds");
  NSData *a = [NSData dataWithContentsOfFile:plain], *b = [NSData dataWithContentsOfFile:changed];
  check(a.length == b.length && a.length > 44, "Zero-tail gainer preserves exported length");
  double maximum = 0, energy = 0;
  for (size_t at = 44; at + 4 <= a.length; at += 4) {
    float x, y; std::memcpy(&x, static_cast<const char *>(a.bytes) + at, 4); std::memcpy(&y, static_cast<const char *>(b.bytes) + at, 4);
    energy += std::abs(x); maximum = std::max(maximum, std::abs(y - x * std::pow(10., -.3)));
  }
  check(energy > 1 && maximum < 2e-7, "Saved native gain applies the independently expected amplitude to exported audio");
  std::cout << "Built-in saved-project WAV gain maximum difference " << maximum << '\n';
  [[NSFileManager defaultManager] removeItemAtPath:plain error:nil]; [[NSFileManager defaultManager] removeItemAtPath:changed error:nil];
}
int main() { @autoreleasepool { try {
  for (uint32_t rate : {44100, 48000, 96000}) {
    utilities(rate); stateAndSmoothing(rate);
    for (const auto &d : NativePlugin::builtins()) {
      nativeSong(rate, d);
      auto expected = automated(d, rate, 128, false);
      for (uint32_t block : {17, 512, 4096}) check(expected == automated(d, rate, block, true), "Sample-timed built-in automation is exactly callback independent");
    }
  }
  api(); exported(); std::cout << "PASS all built-in devices (utilities, filters/EQ, comb, distortion and LofiMat): gain/balance/polarity, 5 Hz DC response, manual correction, mono/width/all-pass, 5 ms smoothing, exact callback/live/offline and musical automation, RT audit, versioned state, API/history/persistence/export\n";
  return 0;
} catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } } }
