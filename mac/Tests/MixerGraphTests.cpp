#include "editor/MixerGraph.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
using namespace Tracker;
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static void rejects(const std::function<void()> &test, const char *message) {
  try { test(); } catch (const std::invalid_argument &) { return; }
  throw std::runtime_error(message);
}
int main() {
  try {
    MixerGraph graph;
    graph.buses = {{1, 10, MixerBusKind::Track, "Drums"}, {2, 10, MixerBusKind::Track, "Bass"},
                   {10, 20, MixerBusKind::Group, "Rhythm"}, {11, 20, MixerBusKind::Return, "Space"},
                   {20, 0, MixerBusKind::Master, "Master"}};
    graph.buses[0].inserts = {"delay"};
    graph.buses[0].sends = {{11, -6, true, true}};
    graph.buses[3].inserts = {"reverb"};
    graph.instruments = {{"synth", 2, 0}};
    std::vector<MixerProcessorInfo> processors = {{"delay", 32, .1}, {"reverb", 16, 2}, {"synth", 8, .5, true}, {"master", 64, 0}};
    auto plan = compileMixer(graph, {1, 2}, processors, 48000);
    check(plan.order == std::vector<size_t>({0, 1, 2, 3, 4}), "Stable topological order respects tracks, group, return and master");
    check(plan.nodes[0].outputLatency == 32 && plan.nodes[1].inputLatency == 8 && plan.nodes[2].inputLatency == 32 &&
          plan.nodes[3].inputLatency == 32 && plan.nodes[4].inputLatency == 48 && plan.latency == 112,
          "Graph delay compensation follows the longest main/send/plugin path");
    check(plan.nodes[1].directDelay == 8 && plan.instruments[0].delay == 0, "Sample input aligns with a plugin sharing its track");
    check(plan.nodes[4].processors == std::vector<size_t>{3}, "Unrouted effects retain their master-rack behavior");
    check(std::abs(plan.tail - 2.1) < 1e-12, "Tail follows the longest effect path");
    auto busToGroup = plan.connections[plan.nodes[1].outputs[0]];
    check(busToGroup.delay == 24, "Shorter bus receives exact edge compensation");
    graph.buses[0].timingMS = -10; graph.buses[1].timingMS = 20;
    auto shifted = compileMixer(graph, {1, 2}, processors, 48000);
    check(shifted.latency == 592 && shifted.nodes[0].directDelay == 0 && shifted.nodes[1].directDelay == 1448,
          "Intentional track offsets survive compensation instead of being cancelled");
    check(shifted.instruments[0].delay == 1440 && shifted.connections[plan.nodes[1].outputs[0]].delay == 24,
          "Instrument and sample timing offset agree while processing latency remains compensated");
    graph.buses[0].timingMS = graph.buses[1].timingMS = 0;
    graph.buses[0].solo = true;
    auto solo = compileMixer(graph, {1, 2}, processors, 48000);
    check(solo.nodes[0].audible && !solo.nodes[1].audible && solo.nodes[2].audible && solo.nodes[3].audible && solo.nodes[4].audible,
          "Solo preserves the selected track's group and effect return without unmuting siblings");
    graph.buses[0].solo = false; graph.buses[2].solo = true;
    solo = compileMixer(graph, {1, 2}, processors, 48000);
    check(solo.nodes[0].audible && solo.nodes[1].audible && solo.nodes[3].audible, "Group solo includes its input tracks and their sends");
    graph.buses[0].mute = true;
    solo = compileMixer(graph, {1, 2}, processors, 48000);
    check(!solo.nodes[0].audible, "Explicit mute takes precedence over group solo");
    graph.buses[0].mute = false; graph.buses[2].solo = false;
    auto invalid = graph; invalid.buses[2].output = 11; invalid.buses[3].output = 10;
    rejects([&] { invalid.validate({1, 2}); }, "Reject group feedback cycle");
    invalid = graph; invalid.buses[3].sends = {{10, -12, false, false}}; invalid.buses[2].sends = {{11, -12, false, true}};
    rejects([&] { invalid.validate({1, 2}); }, "Disabled sends cannot hide a cycle");
    invalid = graph; invalid.buses[1].inserts = {"delay"};
    rejects([&] { invalid.validate({1, 2}); }, "A processor cannot run twice in one stream frame");
    invalid = graph; invalid.buses[1].gainDB = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { invalid.validate({1, 2}); }, "Reject non-finite mixer values");
    invalid = graph; invalid.buses[1].prePan = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { invalid.validate({1, 2}); }, "Reject non-finite input balance");
    invalid.buses[1].prePan = 1.001;
    rejects([&] { invalid.validate({1, 2}); }, "Reject out-of-range input balance");
    invalid = graph; invalid.buses.pop_back();
    rejects([&] { invalid.validate({1, 2}); }, "Reject missing master");
    invalid = graph; invalid.buses[1].id = 3;
    rejects([&] { invalid.validate({1, 2}); }, "Reject orphaned track bus");
    invalid = graph; invalid.buses[1].inserts = {"synth"}; invalid.instruments.clear();
    rejects([&] { compileMixer(invalid, {1, 2}, processors, 48000); }, "Instrument cannot be used as a serial effect");
    auto missing = processors; missing.erase(missing.begin());
    auto retained = compileMixer(graph, {1, 2}, missing, 48000);
    check(retained.nodes[0].processors.empty() && graph.buses[0].inserts[0] == "delay", "Missing plugins leave retained unresolved insert references");
    processors[2].outputBuses = 2; graph.instruments = {{"synth", 1, 1}};
    auto multi = compileMixer(graph, {1, 2}, processors, 48000);
    check(multi.instruments.size() == 2 && multi.instruments[1].output == 0 && multi.instruments[1].target == 4,
          "Routing an auxiliary output retains the default main output");
    graph.instruments[0].output = 2;
    check(compileMixer(graph, {1, 2}, processors, 48000).instruments.size() == 1, "Unavailable saved auxiliary route stays dormant");
    std::cout << "PASS mixer graph validation, deterministic topology, insert ownership, sends/groups, solo paths, latency and intentional timing offsets\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
