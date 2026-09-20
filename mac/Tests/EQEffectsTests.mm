#include "editor/NativeEffects.hpp"
#include "editor/TrackerDocument.hpp"
#include "../Audio/AudioUnitHost.hpp"
#include "../Audio/AudioExport.hpp"
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
static void audited(bool ok) { uint64_t a, f, l; tracker_audit_end(&a, &f, &l); check(ok, "DSP processes"); check(!(a + f + l), "Filter/EQ callbacks do not allocate, free or lock"); }
static PluginDescriptor descriptor(std::string_view id) {
  for (const auto &d : NativePlugin::builtins()) if (d.classID == id) return d;
  throw std::runtime_error("Missing filter/EQ descriptor");
}
static void process(NativeEffect &effect, std::vector<float> &samples, uint32_t block = 128) {
  for (uint32_t pos = 0; pos < samples.size() / 2; pos += block) {
    const auto count = std::min<uint32_t>(block, samples.size() / 2 - pos);
    tracker_audit_begin(); const bool ok = effect.process(samples.data() + pos * 2, count); audited(ok);
  }
}
static std::vector<float> signal(uint32_t rate, double frequency) {
  std::vector<float> result(rate * 2);
  for (uint32_t i = 0; i < rate; ++i) {
    result[i * 2] = float(.1 * std::sin(2 * std::numbers::pi * frequency * i / rate));
    result[i * 2 + 1] = float(.07 * std::cos(i * .371));
  }
  return result;
}
static double ratio(const std::vector<float> &in, const std::vector<float> &out) {
  double a = 0, b = 0;
  for (size_t i = in.size() / 2; i < in.size(); i += 2) { a += double(in[i]) * in[i]; b += double(out[i]) * out[i]; }
  return std::sqrt(b / a);
}
static void responses(uint32_t rate) {
  const auto in = signal(rate, 1000);
  for (int shape = 0; shape < 5; ++shape) for (float q : {.5f, 2.f, 12.f}) {
    NativeEffect filter("resonance.digital-filter.v1", rate); filter.parameter(1, shape); filter.parameter(3, q);
    auto out = in; process(filter, out);
    const double expected = shape < 2 ? q : shape == 3 ? 0 : 1;
    check(std::abs(ratio(in, out) - expected) < 2e-6, "Digital filter center response: LP/HP Q, unity BP/AP and exact notch");
  }
  for (auto name : {"resonance.eq5.v1", "resonance.eq10.v1", "resonance.mixer-eq.v1"}) {
    NativeEffect flat(name, rate); auto out = in; process(flat, out, 17);
    check(out == in && flat.tail() == 0, "Flat EQ is exact identity with no silent export tail");
    for (int shape = 0; shape < 3; ++shape) for (float gain : {-18.f, 18.f}) {
      NativeEffect eq(name, rate); eq.parameter(10, 1000); eq.parameter(11, gain); eq.parameter(13, shape);
      out = in; process(eq, out);
      const double expected = std::pow(10., gain / (shape == 0 ? 20 : 40));
      check(std::abs(ratio(in, out) - expected) < 3e-6, "EQ bell/shelf center gains follow the dB convention");
      NativeEffect inverse(name, rate); inverse.parameter(10, 1000); inverse.parameter(11, -gain); inverse.parameter(13, shape);
      process(inverse, out, 4096);
      double error = 0; for (size_t i = 0; i < in.size(); ++i) error = std::max(error, std::abs(double(in[i]) - out[i]));
      check(error < 2e-7, "Opposite bell/shelf gains cancel across the entire signal, including transients");
    }
  }
  // Routing oracle is the independently qualified stereo bank, combined with
  // the dry mid/side signals. This exercises all five host channel selections.
  NativeEffect stereo("resonance.eq5.v1", rate); stereo.parameter(11, 12);
  auto filtered = in; process(stereo, filtered);
  for (uint32_t mode = 0; mode < 5; ++mode) {
    NativeEffect eq("resonance.eq5.v1", rate); eq.parameter(11, 12); eq.parameter(1, mode);
    auto out = in; process(eq, out, 17);
    for (size_t i = 0; i < in.size(); i += 2) {
      const double l = in[i], r = in[i + 1], fl = filtered[i], fr = filtered[i + 1];
      const double expectedL = mode == 0 || mode == 1 ? fl : mode == 2 ? l : mode == 3 ? (fl + fr + l - r) / 2 : (l + r + fl - fr) / 2;
      const double expectedR = mode == 0 || mode == 2 ? fr : mode == 1 ? r : mode == 3 ? (fl + fr - l + r) / 2 : (l + r - fl + fr) / 2;
      check(std::abs(out[i] - expectedL) < 3e-8 && std::abs(out[i + 1] - expectedR) < 3e-8, "Stereo/left/right/mid/side routing is correct and isolated");
    }
  }
  NativeEffect wet("resonance.digital-filter.v1", rate), bypassed("resonance.digital-filter.v1", rate);
  bypassed.parameter(0, 0); auto a = in, b = in; process(wet, a); process(bypassed, b);
  check(b == in, "Disabled filter passes exact dry signal while keeping histories warm");
  bypassed.parameter(0, 1); a = in; b = in; process(wet, a); process(bypassed, b, 17);
  const auto settled = uint32_t(std::ceil(rate * .005));
  check(std::equal(a.begin() + settled * 2, a.end(), b.begin() + settled * 2), "Enabling the warm filter matches an uninterrupted wet processor after smoothing");
}
static void tails(uint32_t rate) {
  NativeEffect eq("resonance.eq10.v1", rate); auto saved = eq.state();
  check(eq.includeParameterRange(10, 20, 20000) && eq.tail() == 0, "Frequency alone leaves a neutral EQ tail-free");
  eq.includeParameterRange(11, -18, 18); eq.includeParameterRange(12, .1f, 12); eq.includeParameterRange(13, 0, 5);
  check(eq.state() == saved && eq.tail() > 1 && eq.tail() <= 60, "Future automation expands tail budget without changing state");
  const auto budget = eq.tail();
  for (float frequency : {20.f, 50.f, 1000.f, 20000.f}) for (float q : {.1f, .707f, 12.f}) for (float gain : {-18.f, -4.f, 18.f}) for (int shape = 0; shape < 6; ++shape) {
    constexpr FilterShape types[]{FilterShape::Bell, FilterShape::LowShelf, FilterShape::HighShelf, FilterShape::LowPass, FilterShape::HighPass, FilterShape::Notch};
    check(StateVariableFilter::decaySeconds(types[shape], frequency, q, gain, rate) <= budget, "Prepared box bounds intermediate automation settings and shapes");
  }
  tracker_audit_begin(); const bool changed = eq.parameter(10, 20) && eq.parameter(11, 18) && eq.parameter(12, 12); audited(changed);
  check(eq.tail() == budget, "Live values inside the prepared range do not extend or shrink its tail");
  NativeEffect restored("resonance.eq10.v1", rate, saved); check(restored.tail() == 0, "Restoring flat EQ starts a fresh zero-tail run");
  NativeEffect resonant("resonance.digital-filter.v1", rate); resonant.parameter(2, 20); resonant.parameter(3, 12);
  auto input = signal(rate, 20); process(resonant, input);
  const auto frames = uint32_t(std::ceil(resonant.tail() * rate));
  std::vector<float> tail((frames + rate / 10) * 2); process(resonant, tail, 4096);
  double residual = 0; for (size_t i = frames * 2; i < tail.size(); ++i) residual = std::max(residual, std::abs(double(tail[i])));
  check(residual < 1e-8, "Resonant low-frequency release falls below -160 dB by the declared tail");
}
static void chainTails() {
  PluginState state{descriptor("resonance.eq5.v1")}; state.instanceID = "equalizer";
  PluginChain live({state}, 48000); check(live.tail() == 0, "Flat rack has zero tail");
  live.parameter(0, 11, 18); live.parameter(0, 10, 20); live.parameter(0, 12, 12);
  tracker_audit_begin(); live.applyPending(); const auto grown = live.tail(); audited(grown > 1);
  PluginChain absolute({state}, 48000, true, {{0, 11, 18, 100}, {0, 10, 20, 200}, {0, 12, 12, 300}});
  check(absolute.tail() >= grown, "Absolute automation prepares the rack tail before rendering");
  auto doc = Document::demo();
  doc->annotate([](NativeSong &n) {
    auto master = n.makeEntity().id;
    for (const auto &[index, track] : n.tracks) n.mixer.buses.push_back({track.id, master, MixerBusKind::Track, "Track"});
    n.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"}); n.mixer.buses.back().inserts = {"equalizer"};
    n.automation.push_back({n.makeEntity().id, n.patterns.at(0).id, "equalizer", 11, true, {{0, 1, AutomationCurve::Step}}});
    n.automation.push_back({n.makeEntity().id, n.patterns.at(0).id, "equalizer", 10, true, {{0, 0, AutomationCurve::Step}}});
    n.automation.push_back({n.makeEntity().id, n.patterns.at(0).id, "equalizer", 12, true, {{0, 1, AutomationCurve::Step}}});
  });
  PluginChain musical({state}, 48000, true); Renderer renderer(doc->serialize(), 48000);
  musical.attachInstruments(renderer, &doc->native()); musical.attachMusicalAutomation(renderer, doc->native());
  check(musical.tail() >= grown, "Pattern automation expands the already compiled mixer tail before export");
  auto initial = musical.tail(); musical.attachMusicalAutomation(renderer, doc->native());
  check(musical.tail() == initial, "Repeated lane preparation does not double-count tails");
}
static void drainingAutomation() {
  auto doc = Document::demo(); auto module = doc->serialize(); Renderer dry(module, 48000);
  std::array<float, 1024> audio{}; uint64_t length = 0;
  while (true) { auto count = dry.render(audio.data(), 512); length += count; if (count < 512) break; }
  NativeEffect eq("resonance.eq5.v1", 48000); eq.parameter(10, 20); eq.parameter(11, 18); eq.parameter(12, 12);
  PluginState state{descriptor("resonance.eq5.v1"), eq.state()};
  const auto base = std::string(NSTemporaryDirectory().UTF8String) + NSUUID.UUID.UUIDString.UTF8String;
  exportProjectAudio(module, {state}, {}, base + ".static.wav", 0, nullptr);
  exportProjectAudio(module, {state}, {{0, 10, 30, length + 48000}}, base + ".automated.wav", 0, nullptr);
  NSData *a = [NSData dataWithContentsOfFile:@((base + ".static.wav").c_str())], *b = [NSData dataWithContentsOfFile:@((base + ".automated.wav").c_str())];
  check(b.length >= a.length + 48000 * 8 && b.length <= a.length + (48000 + 512) * 8,
    "An in-range filter edit during draining restarts decay from the edit, within one export block");
  check(b.length <= 44 + (length + uint64_t(60) * 48000) * 8, "Automated tail still respects export's 60-second cap");
  [[NSFileManager defaultManager] removeItemAtPath:@((base + ".static.wav").c_str()) error:nil];
  [[NSFileManager defaultManager] removeItemAtPath:@((base + ".automated.wav").c_str()) error:nil];
}
static void apiAndExport() {
  TrackerSession *session = [TrackerSession new]; NSError *problem = nil;
  auto call = [&](NSString *method, NSDictionary *params, bool write = false) -> NSDictionary * {
    NSMutableDictionary *p = [params mutableCopy]; if (write) p[@"expectedRevision"] = session.automationRevision;
    auto reply = [session automationMethod:method params:p error:&problem];
    if (!reply) throw std::runtime_error(problem.localizedDescription.UTF8String); return reply;
  };
  NSString *base = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  NSString *plain = [base stringByAppendingString:@".plain.wav"], *flat = [base stringByAppendingString:@".flat.wav"], *filtered = [base stringByAppendingString:@".filtered.wav"], *project = [base stringByAppendingString:@".resonance"];
  check([TrackerSession exportData:session.serializedData path:plain error:&problem], "Plain project exports");
  NSArray *catalog = call(@"plugin.discover", @{@"format": @"Built-in"})[@"data"];
  NSDictionary *eq = nil; for (NSDictionary *d in catalog) if ([d[@"classID"] isEqual:@"resonance.eq10.v1"]) eq = d;
  check(eq != nil, "EQ10 available through scan-free API"); call(@"plugin.add", @{@"descriptor": eq}, true);
  NSArray *parameters = call(@"plugin.parameters.get", @{@"slot": @0})[@"data"];
  check(parameters.count == 43 && [parameters[3][@"unitLabel"] isEqual:@"Hz"] && [parameters[3][@"displayScale"] isEqual:@"logarithmic"] && [parameters[6][@"choices"] count] == 6, "All 10 bands expose stable IDs, units, scale and shape labels");
  check([TrackerSession exportData:session.serializedData path:flat error:&problem], "Flat EQ exports");
  check([[NSData dataWithContentsOfFile:plain] isEqual:[NSData dataWithContentsOfFile:flat]], "Flat EQ saved-project WAV is byte-identical and has no added silence");
  call(@"plugin.parameters.set", @{@"slot": @0, @"values": @[@{@"id": @10, @"value": @20}, @{@"id": @11, @"value": @18}, @{@"id": @12, @"value": @12}, @{@"id": @49, @"value": @2}]}, true);
  auto state = call(@"plugin.state.get", @{@"slot": @0})[@"data"];
  call(@"history.undo", @{@"domain": @"plugins"}, true);
  check([call(@"plugin.parameters.get", @{@"slot": @0})[@"data"][3][@"value"] floatValue] == 31, "EQ batch is one undo");
  call(@"history.redo", @{@"domain": @"plugins"}, true);
  check([session savePath:project error:&problem], "EQ project saves"); TrackerSession *reopened = [TrackerSession new];
  check([reopened openPath:project error:&problem], "EQ project reopens");
  auto restored = [reopened automationMethod:@"plugin.state.get" params:@{@"slot": @0} error:&problem];
  check([restored[@"data"][@"data"] isEqual:state[@"data"]], "Every EQ band survives save/reopen exactly");
  check([TrackerSession exportData:session.serializedData path:filtered error:&problem], "Resonant EQ exports from saved state");
  NSData *a = [NSData dataWithContentsOfFile:plain], *b = [NSData dataWithContentsOfFile:filtered];
  check(b.length > a.length + 48000 * 8, "High-Q low-frequency EQ appends the prepared audible decay budget");
  double lastPeak = 0; for (size_t at = b.length - 4800 * 8; at + 4 <= b.length; at += 4) {
    float value; std::memcpy(&value, static_cast<const char *>(b.bytes) + at, 4); lastPeak = std::max(lastPeak, std::abs(double(value)));
  }
  check(lastPeak < 1e-8, "Actual saved-project resonant tail has settled below -160 dB before WAV ends");
  std::cout << "EQ saved WAV added tail " << double(b.length - a.length) / (48000 * 8) << " seconds, final 100 ms peak " << lastPeak << '\n';
  for (NSString *path in @[plain, flat, filtered, project]) [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}
int main() { @autoreleasepool { try {
  for (uint32_t rate : {8000, 44100, 48000, 96000, 192000}) { responses(rate); tails(rate); }
  chainTails(); drainingAutomation(); apiAndExport();
  std::cout << "PASS Digital Filter/EQ5/EQ10/Mixer EQ: response, inverse gain, exact flat/bypass, L/R/M/S, warm histories, automated tails, RT audit, API/units/history/state and saved WAV\n"; return 0;
} catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } } }
