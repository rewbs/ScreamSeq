#include "editor/MixerControlEdit.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <utility>
using namespace Tracker;
namespace {
void check(bool value, const char *message) { if(!value) throw std::runtime_error(message); }
void rejects(const std::function<void()> &operation) {
  try { operation(); } catch(const std::invalid_argument &) { return; }
  throw std::runtime_error("Invalid mixer control candidate was accepted");
}
MixerGraph fixture() {
  MixerGraph graph;
  graph.buses = {{7, 90, MixerBusKind::Track, "Track"}, {90, 0, MixerBusKind::Master, "Master"}};
  return graph;
}
void candidates() {
  auto graph=fixture();
  graph.buses[0].gainDB=-6.123456789;
  graph.buses[0].inserts={"retained-state"};
  graph.buses[0].sends={{90,-12,false,true}};
  graph.buses[0].timingMS=2.5;
  const auto before=graph;
  check(!applyMixerControls(graph,7,{}),"An empty patch is a no-op");
  MixerControlPatch unchanged;unchanged.gainDB=graph.buses[0].gainDB;
  check(!applyMixerControls(graph,7,unchanged) && graph==before,"Exact native values remain no-ops without display rounding");
  MixerControlPatch patch;patch.preGainDB=-96;patch.prePan=-1;patch.pan=1;patch.width=2;patch.mute=true;patch.solo=true;
  check(applyMixerControls(graph,7,patch),"An effective patch reports a change");
  auto expected=before;
  expected.buses[0].preGainDB=-96;expected.buses[0].prePan=-1;expected.buses[0].pan=1;
  expected.buses[0].width=2;expected.buses[0].mute=true;expected.buses[0].solo=true;
  check(graph==expected,"Only supplied controls change; other buses, precise gain, routes and insert identity survive");
  check(!applyMixerControls(graph,7,patch),"Repeated control patches do not create changes");
  patch={};patch.mute=false;patch.solo=false;patch.width=0;patch.preGainDB=24;
  check(applyMixerControls(graph,7,patch) && !graph.buses[0].mute && !graph.buses[0].solo &&
    graph.buses[0].width==0 && graph.buses[0].preGainDB==24,"Explicit false and zero values are not omitted");
  std::swap(graph.buses[0],graph.buses[1]);
  patch={};patch.gainDB=-9;
  check(applyMixerControls(graph,7,patch) && graph.buses[1].gainDB==-9 && graph.buses[0].gainDB==0,
    "Stable bus lookup survives reordering");
  const auto valid=graph;
  rejects([&]{applyMixerControls(graph,123,patch);});
  using Field=std::optional<double> MixerControlPatch::*;
  const std::vector<std::pair<Field,std::pair<double,double>>> ranges={
    {&MixerControlPatch::preGainDB,{-96,24}},{&MixerControlPatch::gainDB,{-96,24}},
    {&MixerControlPatch::prePan,{-1,1}},{&MixerControlPatch::pan,{-1,1}},{&MixerControlPatch::width,{0,2}}
  };
  for(const auto &[field,range]:ranges) {
    for(double value:{range.first-.001,range.second+.001,std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}) {
      patch={};patch.mute=true;patch.*field=value;
      rejects([&]{applyMixerControls(graph,7,patch);});
      check(graph==valid,"Invalid multi-field patch cannot partly modify its bus");
    }
    for(double value:{range.first,range.second}) {
      auto boundary=valid;patch={};patch.*field=value;
      applyMixerControls(boundary,7,patch);
    }
  }
}
void classification() {
  const auto original = fixture();
  const auto noop = classifyMixerControlEdit(original, original);
  check(!noop.changed && noop.controlsOnly, "No-op remains eligible to restore an auditioned frame");
  const std::vector<std::function<void(MixerGraph &)>> controls = {
    [](auto &g){g.buses[0].preGainDB=-12;}, [](auto &g){g.buses[0].prePan=-.3;},
    [](auto &g){g.buses[0].gainDB=-6;}, [](auto &g){g.buses[0].pan=.4;},
    [](auto &g){g.buses[0].width=.5;}, [](auto &g){g.buses[0].mute=true;},
    [](auto &g){g.buses[0].solo=true;}, [](auto &g){g.buses[0].name="Renamed";},
    [](auto &g){g.buses[0].color=0xaabbcc;}
  };
  for(const auto &change : controls) {
    auto next=original; change(next);
    const auto result=classifyMixerControlEdit(original,next);
    check(result.changed && result.controlsOnly,"Controls and presentation metadata retain existing live classification");
  }
  const std::vector<std::function<void(MixerGraph &)>> structural = {
    [](auto &g){g.buses[0].timingMS=2;}, [](auto &g){g.buses[0].output=0;},
    [](auto &g){g.buses[0].id=8;}, [](auto &g){std::swap(g.buses[0],g.buses[1]);},
    [](auto &g){g.buses[0].inserts={"effect"};},
    [](auto &g){g.buses[0].sends={{90,-12,false,true}};},
    [](auto &g){g.sidechains={{7,"effect",1,-3,false,true}};},
    [](auto &g){g.detachedChains={{55,{"effect"}}};},
    [](auto &g){g.masterOutputDisconnected=true;},
    [](auto &g){g.buses.push_back({91,90,MixerBusKind::Return,"Return"});}
  };
  for(const auto &change : structural) {
    auto next=original;change(next);next.buses[0].gainDB=-9;
    const auto result=classifyMixerControlEdit(original,next);
    check(result.changed && !result.controlsOnly,"A simultaneous control edit must not hide structural changes");
  }
  check(original==fixture(),"Classification does not modify either source graph");
}
void projectedFrames() {
  auto graph=fixture();auto &track=graph.buses[0];
  track.preGainDB=-12;track.gainDB=-6;track.prePan=-.3;track.pan=.4;track.width=.5;track.mute=true;
  graph.detachedChains={{55,{"orphan"}}};
  const auto saved=graph;
  const auto plan=compileMixer(graph,{7},{{"orphan"}},48000);
  const auto values=prepareMixerControlFrame(graph,plan);
  check(values.size()==3,"Detached chains contribute exactly one projected control slot");
  check(values[0].preGainDB==-12 && values[0].gainDB==-6 && values[0].prePan==-.3 &&
    values[0].pan==.4 && values[0].width==.5 && !values[0].audible,"Track controls and compiled audibility retain bus order");
  check(values[2].preGainDB==0 && values[2].gainDB==0 && values[2].prePan==0 &&
    values[2].pan==0 && values[2].width==1 && values[2].audible==plan.nodes[2].audible,
    "Synthetic roots retain neutral values and compiled audibility");
  check(graph==saved,"Frame preparation does not persist synthetic buses or change saved controls");
  auto shortPlan=plan;shortPlan.nodes.pop_back();rejects([&]{prepareMixerControlFrame(graph,shortPlan);});
  auto reordered=plan;std::swap(reordered.nodes[0],reordered.nodes[1]);rejects([&]{prepareMixerControlFrame(graph,reordered);});
}
}
int main() { try {
  candidates();classification();projectedFrames();
  std::cout << "PASS shared mixer control candidates, classification and detached control-frame preparation\n";
  return 0;
} catch(const std::exception &error) { std::cerr << error.what() << '\n'; return 1; } }
