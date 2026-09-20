#include "editor/MixerRuntime.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
using namespace Tracker;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) { *a = *f = *l = 0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
#endif
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static void rejects(const std::function<void()> &work) { try { work(); } catch (const std::invalid_argument &) { return; } throw std::runtime_error("Expected invalid sidechain graph"); }
static MixerGraph graph() {
  MixerGraph g;
  g.buses = {{1, 20, MixerBusKind::Track, "Program"}, {2, 20, MixerBusKind::Track, "Long key"},
             {3, 20, MixerBusKind::Track, "Short key"}, {20, 0, MixerBusKind::Master, "Master"}};
  g.buses[0].inserts = {"prefix", "target"}; g.buses[1].inserts = {"long"}; g.buses[2].inserts = {"short"};
  g.sidechains = {{2, "target", 1, 0, true}, {3, "target", 1, 0, false}};
  return g;
}
static std::vector<MixerProcessorInfo> processors() {
  std::vector<MixerProcessorInfo> p{{"prefix", 32, 0}, {"target", 16, 0}, {"long", 128, 0}, {"short", 8, 0}};
  p[1].activeInputs = 2; return p;
}
struct Effects {
  MixerRuntime *runtime;
  std::array<std::vector<float>, 4> delays;
  std::array<size_t, 4> cursors{};
  explicit Effects(MixerRuntime *r) : runtime(r) { const std::array<size_t, 4> latencies{32, 16, 128, 8}; for (size_t i = 0; i < 4; ++i) delays[i].resize(latencies[i] * 2); }
  static bool process(void *opaque, size_t plugin, float *data, uint32_t frames, uint64_t) noexcept {
    auto &s = *static_cast<Effects *>(opaque); auto &delay = s.delays[plugin]; auto &cursor = s.cursors[plugin];
    const auto inputs = s.runtime->inputs(plugin);
    for (uint32_t i = 0; i < frames * 2; ++i) {
      if (plugin == 1) data[i] *= inputs.empty() ? 0 : inputs[0].samples[i];
      std::swap(data[i], delay[cursor]); if (++cursor == delay.size()) cursor = 0;
    }
    return true;
  }
};
static std::vector<float> render(MixerGraph g, uint32_t rate, uint32_t block) {
  auto runtime = std::make_unique<MixerRuntime>(g, compileMixer(g, {1, 2, 3}, processors(), rate), rate);
  Effects effects(runtime.get()); std::array<float, 4096> input{}; std::vector<float> result(12000);
  for (uint32_t pos = 0; pos < 6000; pos += block) {
    const auto frames = std::min(block, 6000 - pos);
    uint64_t a, f, l; tracker_audit_begin(); runtime->begin(frames, pos);
    for (auto bus : runtime->plan().order) {
      input.fill(0);
      if (bus < 3 && pos <= 37 && pos + frames > 37) input[37 - pos] = bus == 0 ? 2 : bus == 1 ? 3 : 4;
      const auto *out = runtime->process(bus, input.data(), input.data(), Effects::process, &effects);
      if (bus == runtime->plan().master) std::copy_n(out, frames * 2, result.data() + pos * 2);
    }
    runtime->complete(); tracker_audit_end(&a, &f, &l);
    check(!runtime->failed() && a + f + l == 0, "Sidechain renders without faults, allocations, frees or locks");
  }
  return result;
}
static void impulse(const std::vector<float> &out, double value) {
  for (size_t i = 0; i < out.size(); ++i) check(std::abs(out[i] - (i / 2 == 181 ? value : 0)) < 2e-6, "Sidechain sum and program arrive at the independently calculated sample");
}
int main() { try {
  auto g = graph(); auto p = processors(); auto plan = compileMixer(g, {1, 2, 3}, p, 48000);
  check(plan.order == std::vector<size_t>({1, 2, 0, 3}), "Key source buses process before receiver, even if receiver is an earlier track");
  check(plan.nodes[0].inputLatency == 96 && plan.nodes[0].outputLatency == 144 && plan.latency == 144,
        "Program delay accounts for the prefix before the receiving insert");
  check(plan.sidechains[0].prefixLatency == 32 && plan.sidechains[0].delay == 0 && plan.sidechains[1].delay == 120,
        "Each key input aligns at the receiving insert, rather than at the bus output");
  for (auto rate : {44100u, 48000u, 96000u}) {
    auto reference = render(g, rate, 128); impulse(reference, 21);
    for (auto block : {17u, 512u, 4096u}) check(reference == render(g, rate, block), "Sidechain result is exact across callback partitions");
    auto pre = g; pre.buses[1].gainDB = -6; const double gain = std::pow(10, -.3);
    impulse(render(pre, rate, 17), 18 + 3 * gain);
    pre.sidechains[0].preFader = false; impulse(render(pre, rate, 512), 12 + 9 * gain);
    pre.buses[1].mute = true; impulse(render(pre, rate, 17), 12);
    auto solo = g; solo.buses[0].solo = true; impulse(render(solo, rate, 128), 0);
    solo.buses[1].solo = true; impulse(render(solo, rate, 17), 9);
  }
  auto bad = g; bad.sidechains[0].source = 1; rejects([&] { bad.validate({1, 2, 3}); });
  bad = g; bad.sidechains.push_back({1, "long", 1, 0, false, false}); rejects([&] { bad.validate({1, 2, 3}); });
  bad = g; bad.sidechains.push_back(bad.sidechains.front()); rejects([&] { bad.validate({1, 2, 3}); });
  bad = g; bad.sidechains[0].input = 0; rejects([&] { bad.validate({1, 2, 3}); });
  p[1].activeInputs = 0; check(compileMixer(g, {1, 2, 3}, p, 48000).sidechains.empty(), "Disabled auxiliary inputs retain dormant routes");
  p = processors(); p[1].bypass = true; check(compileMixer(g, {1, 2, 3}, p, 48000).sidechains.empty(), "Bypass removes sidechain processing and latency");
  p = processors(); p.erase(p.begin() + 1); check(compileMixer(g, {1, 2, 3}, p, 48000).sidechains.empty(), "Unavailable processors retain opaque sidechain routes");
  std::cout << "PASS sidechains: cycle/duplicate validation, deterministic topology, multiple sources, insert-position PDC, exact impulses, pre/post/mute/solo, dormant routes and RT audit\n";
  return 0;
} catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } }
