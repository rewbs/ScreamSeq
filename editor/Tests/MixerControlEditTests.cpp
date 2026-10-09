#include "editor/MixerControlEdit.hpp"
#include <functional>
#include <iostream>
#include <utility>
using namespace Tracker;
namespace {
void check(bool value, const char *message) { if(!value) throw std::runtime_error(message); }
void rejects(const std::function<void()> &operation) {
  try { operation(); } catch(const std::invalid_argument &) { return; }
  throw std::runtime_error("Mismatched control frame plan was accepted");
}
MixerGraph fixture() {
  MixerGraph graph;
  graph.buses = {{7, 90, MixerBusKind::Track, "Track"}, {90, 0, MixerBusKind::Master, "Master"}};
  return graph;
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
  classification();projectedFrames();
  std::cout << "PASS shared mixer edit classification and detached control-frame preparation\n";
  return 0;
} catch(const std::exception &error) { std::cerr << error.what() << '\n'; return 1; } }
