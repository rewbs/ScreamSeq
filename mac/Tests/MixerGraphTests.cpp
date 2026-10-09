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
static void transitionDependencies() {
  MixerGraph g;
  g.buses={{1,4,MixerBusKind::Track,"Kick"},{2,4,MixerBusKind::Track,"Bass"},
           {3,4,MixerBusKind::Return,"Echo"},{4,0,MixerBusKind::Master,"Master"}};
  g.buses[0].inserts={"tone"};g.buses[1].inserts={"compressor"};g.buses[2].inserts={"echo"};g.buses[3].inserts={"limiter"};
  g.sidechains={{1,"compressor",1}};g.instruments={{"synth",2,0}};
  std::vector<MixerProcessorInfo> p={{"tone",0,0},{"compressor",0,0,false,false,1,1,2},
    {"echo",0,1},{"limiter",0,0},{"synth",0,0,true}};
  auto compare=[&](const MixerGraph &next,const std::vector<MixerProcessorInfo> &processors,const std::vector<std::string> &reset=std::vector<std::string>{}) {
    return reusableMixerProcessors(g,compileMixer(g,{1,2},p,48000),p,next,compileMixer(next,{1,2},processors,48000),processors,reset);
  };
  auto next=g;next.buses[0].name="Renamed";next.buses[1].color=0x123456;
  check(compare(next,p)==std::vector<size_t>({0,1,2,3,4}),"Labels and color never invalidate processor state reuse");
  next=g;next.sidechains[0].gainDB=-6;
  check(compare(next,p)==std::vector<size_t>({0,SIZE_MAX,2,SIZE_MAX,4}),"A changed detector invalidates the compressor and downstream limiter, but retains the source and unrelated return");
  auto histories=[&](const MixerGraph &next,const std::vector<MixerProcessorInfo> &processors) {
    return mixerTransitionReuse(g,compileMixer(g,{1,2},p,48000),p,next,compileMixer(next,{1,2},processors,48000),processors);
  };
  auto retained=histories(next,p);
  check(retained.sidechains==std::vector<size_t>{0},"Changing route gain preserves its pre-gain compensation history");
  check(retained.controls==std::vector<size_t>({0,1,2,3}),"A detector edit preserves all bus ramp histories");
  next=g;next.buses[0].gainDB=-6;
  check(compare(next,p)==std::vector<size_t>({0,SIZE_MAX,2,SIZE_MAX,4}),"A post-insert fader changes sidechain and downstream inputs without changing its own insert input");
  retained=histories(next,p);
  check(retained.controls==std::vector<size_t>({SIZE_MAX,1,2,3}) && retained.sidechains==std::vector<size_t>{SIZE_MAX},"A changed fader cannot inherit an incompatible target or a post-fader detector history");
  next=g;next.buses[0].prePan=.4;
  check(compare(next,p)==std::vector<size_t>({SIZE_MAX,SIZE_MAX,2,SIZE_MAX,4}),"Pre-insert balance propagates to every dependent processor");
  next=g;next.instruments[0].target=1;
  check(compare(next,p)==std::vector<size_t>({SIZE_MAX,SIZE_MAX,2,SIZE_MAX,4}),"Rerouting a held instrument retains the source while invalidating both affected processing paths");
  auto parameters=p;parameters[0].latency=73;
  check(compare(g,parameters)==std::vector<size_t>({SIZE_MAX,SIZE_MAX,2,SIZE_MAX,4}),"A changed latency invalidates newly compensated detector/main inputs as well as downstream processing");
  check(compare(g,p,{"compressor"})==std::vector<size_t>({0,SIZE_MAX,2,SIZE_MAX,4}),"Explicit recipe/state changes invalidate that processor and downstream dependencies");
  parameters=p;std::swap(parameters[0],parameters[2]);
  check(compare(g,parameters)==std::vector<size_t>({2,1,0,3,4}),"Reuse uses stable processor identity across slot reorder");
  next=g;next.buses[0].sends={{3,-12,true,true}};
  check(compare(next,p)==std::vector<size_t>({0,1,SIZE_MAX,SIZE_MAX,4}),"A new send retains the dry source and independent compressor while preparing changed return/downstream copies");
  next=g;next.sidechains[0].input=0;
  check(compare(next,p)==std::vector<size_t>({0,SIZE_MAX,2,SIZE_MAX,4}),"An extra main input is distinct from the detector input");
  next=g;std::swap(next.buses[0],next.buses[2]);retained=histories(next,p);
  check(retained.direct==std::vector<size_t>({2,1,0,3}) && retained.controls==std::vector<size_t>({2,1,0,3}),"Direct audio and controls follow stable buses after vector reorder");
  // Reversing a serial pair is legal in each plan, although a union of their
  // dependencies would cycle. Neither stateful processor may be shared.
  g.sidechains.clear();g.buses[0].inserts={"tone","compressor"};g.buses[1].inserts.clear();
  next=g;next.buses[0].inserts={"compressor","tone"};
  check(compare(next,p)==std::vector<size_t>({SIZE_MAX,SIZE_MAX,2,SIZE_MAX,4}),"Reversed processing order requires independent affected copies instead of an invalid union DAG");
}
int main() {
  try {
    transitionDependencies();
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
    {
      auto longTail = processors; longTail[1].tail = mixerMaximumTailSeconds;
      check(compileMixer(graph, {1, 2}, longTail, 48000).tail == 60, "The largest host tail compiles and the render tail stays bounded");
      longTail[1].tail = mixerMaximumTailSeconds + .001;
      std::string message;
      try { compileMixer(graph, {1, 2}, longTail, 48000); } catch (const std::invalid_argument &e) { message = e.what(); }
      check(message.find("Invalid mixer processor description") == 0 && message.find("reverb") != std::string::npos,
            "A rejected processor description names its instance");
    }
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
    auto sink=graph;sink.buses.back().inserts={"master"};sink.buses[3].output=0;sink.buses[3].inserts.clear();sink.instruments.push_back({"master",11,0});
    rejects([&]{compileMixer(sink,{1,2},processors,48000);},"Master must remain the final sink even when a downstream return is disconnected");
    sink=graph;sink.buses[3].output=0;sink.sidechains={{20,"reverb",1}};
    rejects([&]{sink.validate({1,2});},"Master detector sends cannot schedule a processor after the final sink");
    MixerGraph moved;
    moved.buses = {{1, 3, MixerBusKind::Track, "Track 1"}, {2, 3, MixerBusKind::Track, "Track 8"}, {3, 0, MixerBusKind::Master, "Master"}};
    const std::vector<std::string> rack = {"distortion", "compressor", "limiter"};
    auto untouched = moved;
    moveMixerInserts(moved, rack, rack, 3);
    check(moved == untouched, "No-op must not materialize implicit master inserts or add Undo");
    moveMixerInserts(moved, rack, {"distortion", "compressor"}, 2);
    check(moved.buses[0].inserts.empty() && moved.buses[1].inserts == std::vector<std::string>({"distortion", "compressor"}) && moved.buses[2].inserts == std::vector<std::string>({"limiter"}), "Moving a chain preserves order and leaves unrelated master effects");
    const auto routePlan = compileMixer(moved, {1, 2}, {{"distortion",0,0},{"compressor",0,0},{"limiter",0,0}},48000);
    check(routePlan.nodes[0].processors.empty() && routePlan.nodes[1].processors == std::vector<size_t>({0,1}) && routePlan.nodes[2].processors == std::vector<size_t>({2}), "Moved effects process only the target track, not the summed master");
    untouched=moved;
    rejects([&]{moveMixerInserts(moved,rack,{"compressor","distortion"},1);},"Reject reversed/nonconsecutive chain");
    rejects([&]{moveMixerInserts(moved,rack,{"distortion","compressor"},1,"missing");},"Reject invalid insertion anchor");
    rejects([&]{moveMixerInserts(moved,rack,{"distortion","distortion"},1);},"Reject duplicate plugin");
    rejects([&]{moveMixerInserts(moved,rack,{"distortion"},999);},"Reject unknown bus");
    check(moved==untouched,"Rejected moves must leave both source and destination intact");
    moveMixerInserts(moved,rack,{"compressor"},2,"distortion");
    check(moved.buses[1].inserts==std::vector<std::string>({"compressor","distortion"}),"Reorder within one bus without replacing processors");
    moveMixerInserts(moved,rack,{"compressor","distortion"},3,"limiter");
    check(moved.buses[1].inserts.empty() && moved.buses[2].inserts==std::vector<std::string>({"compressor","distortion","limiter"}),"Move the chain back before an existing master insert");
    auto pull=moved;
    detachMixerInsert(pull,rack,"distortion");
    check(pull.buses[2].inserts==std::vector<std::string>({"compressor","limiter"})&&pull.detached==std::vector<std::string>{"distortion"},"Pulling a middle insert heals only its serial neighbors and preserves unrelated order");
    const auto pulled=pull;detachMixerInsert(pull,rack,"distortion");check(pull==pulled,"Detaching an already loose effect is an exact no-op");
    auto pulledPlan=compileMixer(pull,{1,2},{{"distortion",0,0},{"compressor",0,0},{"limiter",0,0}},48000);
    check(pulledPlan.detached==std::vector<size_t>{0}&&pulledPlan.nodes[2].processors==std::vector<size_t>({1,2}),"Detached effect is clocked on silence outside audible path");
    rejects([&]{detachMixerInsert(pull,rack,"missing");},"Unknown detached target rejects");check(pull==pulled,"Rejected detach leaves graph intact");
    auto branched=moved;branched.sidechains.push_back({1,"compressor",1});const auto branchBefore=branched;
    rejects([&]{detachMixerInsert(branched,rack,"compressor");},"Explicit detector branch requires intentional disconnection first");check(branched==branchBefore,"Ambiguous detach cannot remove a detector cable");
    branched=moved;branched.instruments.push_back({"compressor",1,1});const auto outputBefore=branched;
    rejects([&]{detachMixerInsert(branched,rack,"compressor");},"Explicit auxiliary output cannot be silently cut");check(branched==outputBefore,"Auxiliary routing survives rejected detach");
    auto mutedOutput=moved;mutedOutput.instruments.push_back({"compressor",0,1});
    detachMixerInsert(mutedOutput,rack,"compressor");const auto marker=mutedOutput.instruments;
    auto mutedPlan=compileMixer(mutedOutput,{1,2},{{"distortion",0,0},{"compressor",0,0},{"limiter",0,0}},48000);
    check(mutedPlan.detached==std::vector<size_t>{1}&&mutedPlan.instruments.empty()&&marker.size()==1,"A previously cut auxiliary output does not prevent detachment or become audible");
    moveMixerInserts(mutedOutput,rack,{"compressor"},1);
    check(mutedOutput.instruments==marker&&compileMixer(mutedOutput,{1,2},{{"distortion",0,0},{"compressor",0,0},{"limiter",0,0}},48000).instruments.empty(),"Reinsert preserves the explicit disconnected-output marker");
    auto fallback=untouched;fallback.buses[1].inserts.clear();fallback.buses[2].inserts.clear();detachMixerInsert(fallback,rack,"compressor");
    check(fallback.buses[2].inserts==std::vector<std::string>({"distortion","limiter"}),"Detaching from implicit Master preserves surrounding fallback order");
    MixerGraph loose;loose.detached={"compressor"};loose.validate({1,2});
    loose.buses={{1,3,MixerBusKind::Track,"Track"},{2,3,MixerBusKind::Track,"Second"},{3,0,MixerBusKind::Master,"Master"}};
    const auto disconnected=loose;
    rejects([&]{moveMixerInserts(loose,rack,{"compressor"},1,"missing");},"Unknown destination anchor cannot connect a detached processor");
    check(loose==disconnected,"Failed detached insertion must preserve detached identity and every bus");
    moveMixerInserts(loose,rack,{"compressor"},1);
    check(loose.detached.empty()&&loose.buses[0].inserts==std::vector<std::string>{"compressor"}&&loose.buses.back().inserts==std::vector<std::string>({"distortion","limiter"}),"Detached insertion changes only its ownership and retains unrelated fallback effects");
    loose.detached={"compressor"};rejects([&]{loose.validate({1,2});},"Owned effects cannot also be marked unconnected");
    loose=disconnected;loose.detached.push_back("compressor");rejects([&]{loose.validate({1,2});},"Duplicate detached references reject before preparation");
    auto chains=moved;uint64_t freshID=90;
    detachMixerInserts(chains,rack,{"compressor","distortion"},[&]{return freshID++;});
    check(chains.buses[2].inserts==std::vector<std::string>{"limiter"}&&chains.detachedChains.size()==1&&chains.detachedChains[0].plugins==std::vector<std::string>({"compressor","distortion"}),"Detach retains a consecutive internal chain and heals its original neighbors");
    const auto chainSaved=chains;detachMixerInserts(chains,rack,{"compressor","distortion"},[&]{return freshID++;});check(chains==chainSaved&&freshID==91,"Whole detached chain detach is a no-op without identity churn");
    const auto projected=projectMixerDetachedChains(chains);check(projected.buses.size()==4&&projected.buses.back().id==90&&projected.buses.back().output==0&&chains.buses.size()==3,"Detached root projection is silent and never persists a fake user bus");
    disconnectMixerInsert(chains,rack,90,"distortion");check(chains.disconnectedMainInputs==std::vector<std::string>{"distortion"}&&chains.detachedChains==chainSaved.detachedChains,"Cut preserves processor ownership/order and marks only the exact implicit input");
    const auto cutChain=chains;rejects([&]{disconnectMixerInsert(chains,rack,90,"compressor");},"A silent detached root has no cable to cut");check(chains==cutChain,"Invalid silent-head cut is atomic");
    moveMixerInserts(chains,rack,{"distortion"},90,"distortion");check(chains==chainSaved,"Repatch same input restores only that cable");
    moveMixerInserts(chains,rack,{"compressor","distortion"},1);check(chains.detachedChains.empty()&&chains.buses[0].inserts==std::vector<std::string>({"compressor","distortion"}),"Moving the complete loose chain adopts one bus and prunes its empty internal root");
    auto routed=moved;routed.sidechains={{1,"compressor",1}};routed.instruments={{"compressor",2,1}};
    const auto routes=routed;detachMixerInserts(routed,rack,{"compressor","distortion"},[&]{return freshID++;});
    check(routed.sidechains==routes.sidechains&&routed.instruments==routes.instruments&&routed.detachedChains.size()==1,"Detach preserves exact incoming detector and outgoing auxiliary branches");routed.validate({1,2});
    std::cout << "PASS mixer graph validation, deterministic topology, insert ownership, sends/groups, solo paths, latency and intentional timing offsets\n";
    return 0;
  } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
