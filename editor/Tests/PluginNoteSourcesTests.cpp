#include "editor/PluginNoteSources.hpp"
#include <iostream>
#include <stdexcept>
using namespace Tracker;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main(){try{
  NativeSong native;for(uint16_t i=1;i<=3;++i)native.instruments[i]=native.makeEntity();native.tracks[0]=native.makeEntity();
  const auto one=native.instruments.at(1).id,three=native.instruments.at(3).id,track=native.tracks.at(0).id;
  PluginState a,b;a.instanceID="a";b.instanceID="b";a.descriptor.instrument=b.descriptor.instrument=true;
  setPluginAssignments(a,{{1,5},{3,2}});setPluginAssignments(b,{{2,1}});std::vector<PluginState> before{a,b},after{b};
  native.signal.noteRouting.routes={{native.makeEntity().id,NoteSourceKind::Instrument,one,"b",0,true}};
  const auto initial=native;reconcilePluginNoteSources(native,before,after);
  check(native.signal.noteRouting.triggerSources==std::vector<NoteTriggerSource>{{one,5}},"Removal failed to preserve routed source and MIDI channel");
  auto second=native;reconcilePluginNoteSources(second,after,after);check(second==native,"Unrelated rack edit changed retained trigger");
  reconcilePluginNoteSources(second,after,after,one);check(second.signal.noteRouting.triggerSources.empty(),"Explicit unassign did not restore sample mode");
  auto channel=initial;channel.signal.noteRouting.routes={{channel.makeEntity().id,NoteSourceKind::Channel,track,"b",0,false}};
  reconcilePluginNoteSources(channel,before,after);check(channel.signal.noteRouting.triggerSources.size()==2,"Muted channel cable lost formerly assigned alias sources");
  reconcilePluginNoteSources(channel,after,after,one);check(channel.signal.noteRouting.triggerSources==std::vector<NoteTriggerSource>{{three,2}},"Unassign erased sibling alias source");
  auto none=initial;none.signal.noteRouting.routes.clear();reconcilePluginNoteSources(none,before,after);check(none.signal.noteRouting.triggerSources.empty(),"Removal created an orphan source with no surviving destination");
  auto unassigned=initial;auto cleared=a;setPluginAssignments(cleared,{});std::vector<PluginState> unassignedRack{cleared,b};
  reconcilePluginNoteSources(unassigned,before,unassignedRack);check(unassigned.signal.noteRouting.triggerSources.empty(),"Explicit unassign preserved plugin mode instead of sample mode");
  auto reassigned=b;setPluginAssignments(reassigned,{{2,1},{1,9}});std::vector<PluginState> rebound{reassigned};
  reconcilePluginNoteSources(native,after,rebound);check(native.signal.noteRouting.triggerSources.empty(),"A new default assignment retained redundant orphan state");
  NoteRouting routing;routing.triggerSources={{one,5}};routing.validate(std::vector<uint64_t>{track},std::vector<uint64_t>{one,three});
  routing.triggerSources.push_back({one,7});bool rejected=false;try{routing.validate(std::vector<uint64_t>{track},std::vector<uint64_t>{one,three});}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Duplicate trigger source accepted");
  std::cout<<"PASS durable plugin-note generators across removal, aliases, disabled routes, unassign and reassignment\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
