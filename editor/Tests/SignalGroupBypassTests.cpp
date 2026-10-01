#include "editor/SignalGraph.hpp"
#include "editor/SignalGroupBypass.hpp"
#include <iostream>
#include <stdexcept>
using namespace Tracker;
#define CHECK(x) do{if(!(x))throw std::runtime_error("Check failed: " #x);}while(false)
template<class F> void rejects(F &&f){try{f();}catch(const std::invalid_argument &){return;}throw std::runtime_error("Expected invalid group mapping");}
SignalDefinition recipe(){SignalDefinition d;d.id=1;d.number=1;d.name="Boundary";for(uint64_t i=2;i<=6;++i){SignalNode n;n.id=i;n.kind=i==2?SignalNodeKind::Input:i==6?SignalNodeKind::Output:SignalNodeKind::Plugin;n.plugin.classID="resonance.gainer.v1";d.nodes.push_back(n);}d.audio={{2,3,0,0,.5},{3,4,0,0,1},{4,5,0,0,.75},{5,6,0,0,1}};d.groups={{20,0,"Group",0,0,{3,4}}};return d;}
int main(){try{
  auto d=recipe();auto b=signalGroupBoundary(d,20);CHECK(b.inputs.size()==1&&b.outputs.size()==1);auto r=resolvedSignalGroupDryRoutes(d,20);CHECK((r[0].input==SignalGroupInput{2,3,0,0}));CHECK((r[0].output==SignalGroupOutput{4,0}));d.groups[0].bypass=true;compileSignal(d);
  d.audio.push_back({3,6,0,0,.25});b=signalGroupBoundary(d,20);CHECK(b.outputs.size()==2);rejects([&]{compileSignal(d);});d.groups[0].bypass=false;compileSignal(d);d.groups[0].dryRoutes={{{2,3,0,0},{3,0}},{{2,3,0,0},{4,0}}};d.groups[0].bypass=true;compileSignal(d);CHECK(resolvedSignalGroupDryRoutes(d,20).size()==2);
  auto invalid=d;invalid.groups[0].dryRoutes[0].input.target=4;rejects([&]{compileSignal(invalid);});invalid=d;invalid.groups[0].dryRoutes[1].output={3,0};rejects([&]{compileSignal(invalid);});
  auto nested=recipe();nested.groups={{20,0,"Outer",0,0,{}},{21,20,"Inner",0,0,{3,4}}};CHECK(signalGroupMembers(nested,20)==std::set<uint64_t>({3,4}));CHECK(resolvedSignalGroupDryRoutes(nested,20)==resolvedSignalGroupDryRoutes(nested,21));
  auto silent=recipe();silent.audio.erase(silent.audio.begin());silent.groups[0].bypass=true;CHECK(resolvedSignalGroupDryRoutes(silent,20)[0].input==SignalGroupInput{});compileSignal(silent);
  // Existing wet DAG is 4→5→3; selecting 3 and4 is legal, but choosing 5→3
  // as the dry source for output4 would create a separate dry dependency cycle.
  auto cycle=recipe();cycle.audio={{2,4,0,0,1},{4,5,0,0,1},{5,3,0,0,1},{3,6,0,0,1}};cycle.groups[0].dryRoutes={{{5,3,0,0},{4,0}},{{2,4,0,0},{3,0}}};cycle.groups[0].bypass=true;rejects([&]{compileSignal(cycle);});
  auto exportedSource=recipe();exportedSource.groups[0].bypass=true;exportedSource.groups[0].dryRoutes=resolvedSignalGroupDryRoutes(exportedSource,20);
  auto exported=extractSignalGroup(exportedSource,20,100,101);CHECK(exported.groups.size()==1&&exported.groups[0].bypass);CHECK(exported.groups[0].dryRoutes[0].input.source==100);compileSignal(exported);
  MixerGraph mixer;MixerBus main;main.id=50;main.kind=MixerBusKind::Master;main.inserts={"a","b"};mixer.buses={main};SignalGraph song;song.groups={{70,0,"Rack",0,0,{"plugin:a","plugin:b"}}};song.groups[0].bypass=true;
  const auto songBoundary=signalSongGroupBoundary(song,mixer,{"a","b"},70);CHECK(songBoundary.inputs.size()==1&&songBoundary.outputs.size()==1);CHECK(resolvedSongGroupDryRoutes(song,mixer,{"a","b"},70).size()==1);
  uint64_t freshID=200;GraphPluginRecipe gain;gain.classID="resonance.gainer.v1";auto songExport=extractSongSignalGroup(song,mixer,70,{{"a",gain},{"b",gain}},[&]{return freshID++;});songExport.number=1;CHECK(songExport.groups.size()==1&&songExport.groups[0].bypass);compileSignal(songExport);
  SignalGraph a;a.library={recipe()};a.assignments={{50,1,1,1}};auto c=a;c.library[0].groups[0].name="Label only";c.library[0].groups[0].x=50;CHECK(sameSignalProcessing(a,c));c.library[0].groups[0].bypass=true;CHECK(!sameSignalProcessing(a,c)&&sameSignalControlLayout(a,c));c.library[0].groups[0].nodes={3};CHECK(!sameSignalControlLayout(a,c)&&!sameSignalSourceLayout(a,c));
  std::cout<<"PASS group dry boundaries: exact ingress/egress, explicit branches, nested members, silence, dry-cycle rejection and live-control classification\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
