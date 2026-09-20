#include "editor/MixerRuntime.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
using namespace Tracker;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) { *a = *f = *l = 0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
#endif
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
namespace {
struct Processor {
  std::vector<float> delay;
  size_t cursor = 0;
  uint64_t through = 0;
};
struct Effects {
  std::array<Processor, 4> processors;
  bool duplicate = false;
  Effects() { processors[0].delay.resize(64); processors[1].delay.resize(32); processors[3].delay.resize(128); }
  static bool process(void *opaque, size_t index, float *buffer, uint32_t frames, uint64_t position) noexcept {
    auto &self = *static_cast<Effects *>(opaque); auto &p = self.processors[index];
    if (p.through != position) self.duplicate = true;
    p.through = position + frames;
    for (uint32_t n = 0; n < frames * 2; ++n) {
      std::swap(buffer[n], p.delay[p.cursor]); if (++p.cursor == p.delay.size()) p.cursor = 0;
    }
    return true;
  }
};
MixerGraph graph() {
  MixerGraph g;
  g.buses = {{1, 10, MixerBusKind::Track, "Drums"}, {2, 10, MixerBusKind::Track, "Bass"},
             {10, 20, MixerBusKind::Group, "Rhythm"}, {11, 20, MixerBusKind::Return, "Space"},
             {20, 0, MixerBusKind::Master, "Master"}};
  g.buses[0].inserts = {"delay"}; g.buses[0].sends = {{11, -6, true, true}};
  g.buses[3].inserts = {"reverb"}; g.instruments = {{"synth", 2, 0}};
  return g;
}
std::vector<MixerProcessorInfo> processors() { return {{"delay", 32, 0}, {"reverb", 16, 0}, {"synth", 8, 0, true}, {"master", 64, 0}}; }
std::vector<float> render(MixerGraph graph, uint32_t block, uint32_t rate) {
  auto mixer = std::make_unique<MixerRuntime>(graph, compileMixer(graph, {1, 2}, processors(), rate), rate);
  Effects effects;
  std::array<float, 4096> left{}, right{}; std::array<float, 8192> instrument{};
  std::vector<float> output(6000 * 2);
  for (uint32_t pos = 0; pos < 6000; pos += block) {
    auto count = std::min(block, 6000 - pos);
    instrument.fill(0);
    if (pos <= 45 && pos + count > 45) instrument[(45 - pos) * 2] = instrument[(45 - pos) * 2 + 1] = 4;
    uint64_t a, f, l; tracker_audit_begin();
    mixer->begin(count, pos); mixer->instrument(2, 0, instrument.data());
    for (auto bus : mixer->plan().order) {
      left.fill(0); right.fill(0);
      if (pos <= 37 && pos + count > 37) left[37 - pos] = right[37 - pos] = bus == 0 ? 1 : bus == 1 ? 2 : bus == 4 ? 8 : 0;
      const auto *result = mixer->process(bus, left.data(), right.data(), Effects::process, &effects);
      if (bus == mixer->plan().master) std::copy_n(result, count * 2, output.data() + pos * 2);
    }
    mixer->complete(); tracker_audit_end(&a, &f, &l);
    check(a + f + l == 0 && !mixer->failed(), "Graph rendering has no realtime allocation, free, lock or fault");
  }
  check(!effects.duplicate && mixer->through() == 6000, "Each insert executes exactly once per stream sample");
  check(mixer->meters().size() == 5 && mixer->meters()[4].left > 0, "Meters are independently readable after rendering");
  return output;
}
void impulse(const std::vector<float> &out, std::vector<std::pair<size_t, float>> expected) {
  for (size_t i = 0; i < out.size() / 2; ++i) {
    float value = 0; for (const auto &point : expected) if (point.first == i) value += point.second;
    check(std::abs(out[i * 2] - value) < 2e-6 && std::abs(out[i * 2 + 1] - value) < 2e-6,
          "Rendered impulse arrivals and gains match the independently calculated graph");
  }
}
std::vector<float> smooth(uint32_t block) {
  MixerGraph g; g.buses = {{1, 2, MixerBusKind::Track, "Track"}, {2, 0, MixerBusKind::Master, "Master"}};
  auto mixer = std::make_unique<MixerRuntime>(g, compileMixer(g, {1}, {}, 48000), 48000);
  std::vector<MixerControls> controls(2); controls[0].gainDB = -6; controls[0].pan = .5; controls[0].width = 0;
  check(mixer->controls(controls), "Live controls queue accepts a complete snapshot");
  std::array<float, 4096> l{}, r{}; l.fill(1); r.fill(.5);
  std::vector<float> out(2048);
  for (uint32_t p = 0; p < 1024; p += block) {
    auto n = std::min(block, 1024 - p);
    uint64_t a, f, locks; tracker_audit_begin();
    mixer->begin(n, p); mixer->process(0, l.data(), r.data(), nullptr, nullptr);
    const auto *master = mixer->process(1, nullptr, nullptr, nullptr, nullptr);
    std::copy_n(master, n * 2, out.data() + p * 2); mixer->complete();
    tracker_audit_end(&a, &f, &locks); check(a + f + locks == 0, "Control ramps remain realtime-safe");
  }
  check(std::abs(out[478] - .75 * std::pow(10, -.3) * .5) < 2e-7 && std::abs(out[479] - .75 * std::pow(10, -.3)) < 2e-7,
        "Gain, balance and width reach their target at five milliseconds");
  check(out[0] > .99 && out[0] < 1, "A control change begins smoothly instead of jumping");
  return out;
}
// A channel-swapping insert makes pre/post balance observably different.
// The pre-fader return and sidechain must both hear the processed input balance.
std::vector<float> inputBalance(uint32_t rate, uint32_t block, double balance, bool live) {
  MixerGraph g; g.buses = {{1, 4, MixerBusKind::Track, "Track"}, {2, 4, MixerBusKind::Return, "Return"},
                          {3, 4, MixerBusKind::Return, "Detector"}, {4, 0, MixerBusKind::Master, "Master"}};
  g.buses[0].preGainDB = -6; g.buses[0].gainDB = -12; g.buses[0].pan = -.25;
  g.buses[0].prePan = live ? 0 : balance; g.buses[0].inserts = {"swap"};
  g.buses[0].sends = {{2, 0, true, true}}; g.buses[2].inserts = {"detector"};
  g.sidechains = {{1, "detector", 1, 0, true, true}};
  std::vector<MixerProcessorInfo> p = {{"swap", 0, 0}, {"detector", 0, 0}};
  p[1].activeInputs = 2;
  auto mixer = std::make_unique<MixerRuntime>(g, compileMixer(g, {1}, p, rate), rate);
  if (live) {
    std::vector<MixerControls> controls(4);
    controls[0].preGainDB = -6; controls[0].gainDB = -12; controls[0].pan = -.25; controls[0].prePan = balance;
    check(mixer->controls(controls), "Input balance is accepted by live control snapshots");
  }
  const uint32_t total = rate / 50, ramp = uint32_t(std::round(rate * .005));
  std::vector<float> out(total * 2);
  std::array<float, 4096> left{}, right{}; left.fill(.8f); right.fill(.2f);
  auto process = [](void *context, size_t processor, float *audio, uint32_t frames, uint64_t) noexcept {
    if (processor == 0) for (uint32_t i = 0; i < frames; ++i) std::swap(audio[i * 2], audio[i * 2 + 1]);
    else {
      auto inputs = static_cast<MixerRuntime *>(context)->inputs(processor);
      if (inputs.size() != 1) return false;
      std::copy_n(inputs[0].samples, frames * 2, audio);
    }
    return true;
  };
  for (uint32_t pos = 0; pos < total; pos += block) {
    const auto count = std::min(block, total - pos);
    uint64_t a, f, locks; tracker_audit_begin();
    mixer->begin(count, pos);
    mixer->process(0, left.data(), right.data(), process, mixer.get());
    mixer->process(1, nullptr, nullptr, process, mixer.get());
    mixer->process(2, nullptr, nullptr, process, mixer.get());
    const float *master = mixer->process(3, nullptr, nullptr, process, mixer.get());
    std::copy_n(master, count * 2, out.data() + pos * 2); mixer->complete();
    tracker_audit_end(&a, &f, &locks);
    check(!mixer->failed() && a + f + locks == 0, "Input balance, sends and sidechains allocate/free/lock zero times on callback");
  }
  for (uint32_t i = 0; i < total; ++i) {
    const double b = balance * (live ? std::min(1.0, double(i + 1) / ramp) : 1.0);
    const double leftAfterSwap = .2 * std::pow(10, -.3) * (b < 0 ? 1 + b : 1);
    const double rightAfterSwap = .8 * std::pow(10, -.3) * (b > 0 ? 1 - b : 1);
    const double expectedL = leftAfterSwap * (2 + std::pow(10, -.6)), expectedR = rightAfterSwap * (2 + .75 * std::pow(10, -.6));
    if (std::abs(out[i * 2] - expectedL) >= 3e-7 || std::abs(out[i * 2 + 1] - expectedR) >= 3e-7)
      std::cerr << "Input balance reference: rate " << rate << " block " << block << " balance " << balance << " live " << live
                << " sample " << i << " actual " << out[i * 2] << "," << out[i * 2 + 1] << " expected " << expectedL << "," << expectedR << '\n';
    check(std::abs(out[i * 2] - expectedL) < 3e-7 && std::abs(out[i * 2 + 1] - expectedR) < 3e-7,
          "Independent sample reference verifies pre-insert balance, output balance, pre-fader return and sidechain");
  }
  return out;
}
std::vector<float> interruptedBalance(uint32_t block) {
  MixerGraph g; g.buses = {{1, 2, MixerBusKind::Track, "Track"}, {2, 0, MixerBusKind::Master, "Master"}};
  auto mixer = std::make_unique<MixerRuntime>(g, compileMixer(g, {1}, {}, 48000), 48000);
  std::vector<MixerControls> controls(2); controls[0].prePan = .8; check(mixer->controls(controls), "Initial balance gesture");
  std::array<float, 4096> left{}, right{}; left.fill(1); right.fill(.5);
  std::vector<float> out(1440);
  for (uint32_t pos = 0; pos < 720;) {
    if (pos == 93 || pos == 200) { controls[0].prePan = -.5; check(mixer->controls(controls), "Revised or duplicate gesture snapshot"); }
    const auto boundary = pos < 93 ? 93u : pos < 200 ? 200u : 720u;
    const auto count = std::min(block, boundary - pos);
    mixer->begin(count, pos); mixer->process(0, left.data(), right.data(), nullptr, nullptr);
    const auto *master = mixer->process(1, nullptr, nullptr, nullptr, nullptr);
    std::copy_n(master, count * 2, out.data() + pos * 2); mixer->complete(); pos += count;
  }
  const double origin = .8 * 93 / 240;
  for (size_t i = 0; i < 720; ++i) {
    const double balance = i < 93 ? .8 * (i + 1) / 240 : origin + (-.5 - origin) * std::min(1.0, double(i - 93 + 1) / 240);
    check(std::abs(out[i * 2] - (1 - std::max(0.0, balance))) < 2e-7 &&
          std::abs(out[i * 2 + 1] - .5 * (1 + std::min(0.0, balance))) < 2e-7,
          "A changed gesture starts at its last rendered value and a repeated target never restarts smoothing");
  }
  return out;
}
}
int main() {
  try {
    for (uint32_t rate : {44100, 48000, 96000}) {
      for (double balance : {-1., -.4, 0., .7, 1.}) for (bool live : {false, true}) {
        const auto reference = inputBalance(rate, 128, balance, live);
        for (uint32_t block : {1, 17, 512, 4096}) {
          const auto candidate = inputBalance(rate, block, balance, live);
          check(candidate == reference, "Input balance and ramps are exactly block-independent");
        }
      }
      const auto plain = render(graph(), 128, rate);
      impulse(plain, {{149, float(15 + std::pow(10, -.3))}});
      for (auto block : {17u, 512u, 4096u}) check(plain == render(graph(), block, rate), "Routing/PDC is exactly callback-size independent");
      auto g = graph(); g.buses[0].timingMS = -10; g.buses[1].timingMS = 20;
      impulse(render(g, 17, rate), {{149, float(1 + std::pow(10, -.3))}, {149 + rate / 100, 8}, {149 + rate * 3 / 100, 6}});
      g = graph(); g.buses[0].gainDB = -6;
      impulse(render(g, 128, rate), {{149, float(14 + 2 * std::pow(10, -.3))}});
      g.buses[0].sends[0].preFader = false;
      impulse(render(g, 128, rate), {{149, float(14 + std::pow(10, -.3) + std::pow(10, -.6))}});
      g.buses[0].mute = true;
      impulse(render(g, 128, rate), {{149, 14}});
      g = graph(); g.buses[0].solo = true;
      impulse(render(g, 128, rate), {{149, float(9 + std::pow(10, -.3))}});
    }
    const auto reference = smooth(128);
    const auto interrupted = interruptedBalance(128);
    for (uint32_t block : {1, 17, 512, 4096})
      check(interruptedBalance(block) == interrupted, "Interrupted ramps remain exactly callback-size independent");
    for (auto block : {17u, 512u, 4096u}) {
      auto candidate = smooth(block);
      for (size_t i = 0; i < reference.size(); ++i) check(std::abs(candidate[i] - reference[i]) < 3e-7, "Smoothed controls are callback-size independent within float precision");
    }
    auto g = graph(); auto mixer = std::make_unique<MixerRuntime>(g, compileMixer(g, {1, 2}, processors(), 48000), 48000);
    std::vector<MixerControls> controls(5);
    controls[0].pan = std::numeric_limits<double>::quiet_NaN();
    check(!mixer->controls(controls), "Non-finite controls are rejected before the audio thread");
    controls[0].pan = 0;
    controls[0].prePan = std::numeric_limits<double>::quiet_NaN();
    check(!mixer->controls(controls), "Invalid input balance never reaches the audio queue");
    controls[0].prePan = 0;
    for (int i = 0; i < 8; ++i) check(mixer->controls(controls), "Bounded queue capacity accepted");
    check(!mixer->controls(controls), "Full queue provides explicit backpressure");
    mixer->begin(4097, 0); check(mixer->failed(), "Oversized blocks fail without writing beyond buffers");
    MixerGraph toxic; toxic.buses = {{1, 2, MixerBusKind::Track, "Track"}, {2, 0, MixerBusKind::Master, "Master"}};
    toxic.buses[1].gainDB = 24; toxic.buses[1].inserts = {"toxic"};
    auto guard = std::make_unique<MixerRuntime>(toxic, compileMixer(toxic, {1}, {{"toxic", 0, 0}}, 48000), 48000);
    guard->begin(1, 0); guard->process(0, nullptr, nullptr, nullptr, nullptr);
    const auto *silenced = guard->process(1, nullptr, nullptr,
      [](void *, size_t, float *data, uint32_t frames, uint64_t) noexcept { std::fill_n(data, frames * 2, std::numeric_limits<float>::max()); return true; }, nullptr);
    guard->complete();
    check(guard->failed() && silenced[0] == 0 && silenced[1] == 0, "Post-fader overflow is silenced before reaching the integer output mixer");
    std::cout << "PASS mixer runtime: exact routing/PDC, instrument and preview inputs, timing offsets, pre/post sends, mute/solo, smooth controls, meters and realtime audit\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
