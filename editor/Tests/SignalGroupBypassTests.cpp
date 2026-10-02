#include "editor/SignalGraph.hpp"
#include "editor/SignalGroupBypass.hpp"
#include <iostream>
#include <algorithm>
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
  SignalSongSource control;control.node.id=80;control.node.kind=SignalNodeKind::LFO;control.node.x=20;control.node.y=40;song.songSources.push_back(control);
  groupSongSignalNodes(song,{"source:n80"},{},81,0,"Control boundary");validateSongSignalGroups(song);CHECK(song.groups.back().nodes==std::vector<std::string>{"source:n80"});
  moveSongSignalGroup(song,81,120,140);CHECK((song.layout.at("source:n80")==std::array<double,2>{120,140}));
  CHECK(resolvedSongGroupDryRoutes(song,mixer,{"a","b"},81).empty());
  rejects([&]{groupSongSignalNodes(song,{"source:n999"},{},82,0,"Invalid");});
  rejects([&]{extractSongSignalGroup(song,mixer,81,{{"a",gain},{"b",gain}},[&]{return freshID++;});});
  auto membership=song;membership.songSources.clear();rejects([&]{validateSongSignalGroups(membership);});
  SignalGraph a;a.library={recipe()};a.assignments={{50,1,1,1}};auto c=a;c.library[0].groups[0].name="Label only";c.library[0].groups[0].x=50;CHECK(sameSignalProcessing(a,c));c.library[0].groups[0].bypass=true;CHECK(!sameSignalProcessing(a,c)&&sameSignalControlLayout(a,c));c.library[0].groups[0].nodes={3};CHECK(!sameSignalControlLayout(a,c)&&!sameSignalSourceLayout(a,c));
  {
    MixerGraph beforeMixer;MixerBus track;track.id=10;track.output=50;track.inserts={"compressor"};MixerBus detector;detector.id=11;detector.output=50;MixerBus master;master.id=50;master.kind=MixerBusKind::Master;beforeMixer.buses={track,detector,master};beforeMixer.sidechains={{11,"compressor",1,-9,false,true}};
    SignalGraph beforeGraph;beforeGraph.groups={{100,0,"Outer",0,0,{}},{101,100,"Dynamics",0,0,{"plugin:compressor"}}};
    for(auto &g:beforeGraph.groups){const auto boundary=signalSongGroupBoundary(beforeGraph,beforeMixer,{"compressor"},g.id);CHECK(boundary.inputs.size()==2&&boundary.outputs.size()==1);const auto main=*std::find_if(boundary.inputs.begin(),boundary.inputs.end(),[](const auto &r){return r.kind=="insert";});g.dryRoutes={{main,boundary.outputs[0]}};g.bypass=true;}
    auto afterMixer=beforeMixer;afterMixer.buses[0].inserts.push_back("reverb");auto afterGraph=beforeGraph;
    preserveSongGroupInsertion(afterGraph,beforeGraph,beforeMixer,afterMixer,{"compressor"},{"compressor","reverb"},"reverb");
    for(size_t i=0;i<2;++i){CHECK(afterGraph.groups[i].dryRoutes[0].input==beforeGraph.groups[i].dryRoutes[0].input);CHECK((afterGraph.groups[i].dryRoutes[0].output==SignalRouteIdentity{"insert","n10",{},"reverb","main-path"}));CHECK(afterGraph.groups[i].nodes==beforeGraph.groups[i].nodes&&afterGraph.groups[i].bypass);CHECK(resolvedSongGroupDryRoutes(afterGraph,afterMixer,{"compressor","reverb"},afterGraph.groups[i].id).size()==1);}
    CHECK(afterMixer.sidechains==beforeMixer.sidechains);
    auto inner=beforeGraph;inner.groups[1].nodes.push_back("plugin:reverb");preserveSongGroupInsertion(inner,beforeGraph,beforeMixer,afterMixer,{"compressor"},{"compressor","reverb"},"reverb");CHECK(inner.groups[0].dryRoutes==beforeGraph.groups[0].dryRoutes&&inner.groups[1].dryRoutes==beforeGraph.groups[1].dryRoutes);
    auto prepend=beforeMixer;prepend.buses[0].inserts.insert(prepend.buses[0].inserts.begin(),"reverb");auto extended=beforeGraph;extended.groups[1].nodes.push_back("plugin:reverb");preserveSongGroupInsertion(extended,beforeGraph,beforeMixer,prepend,{"compressor"},{"compressor","reverb"},"reverb");for(const auto &g:extended.groups)CHECK(g.dryRoutes[0].input.plugin=="reverb"&&g.dryRoutes[0].output.kind=="output");
    auto fanMixer=beforeMixer;MixerBus returnBus;returnBus.id=12;returnBus.output=50;returnBus.kind=MixerBusKind::Return;fanMixer.buses.push_back(returnBus);fanMixer.buses[0].sends={{12,-12,false,true}};auto fan=beforeGraph;
    for(auto &g:fan.groups){auto out=signalSongGroupBoundary(fan,fanMixer,{"compressor"},g.id).outputs;g.dryRoutes={{g.dryRoutes[0].input,out[0]},{g.dryRoutes[0].input,out[1]}};}
    auto appended=fanMixer;appended.buses[0].inserts.push_back("reverb");auto merged=fan;preserveSongGroupInsertion(merged,fan,fanMixer,appended,{"compressor"},{"compressor","reverb"},"reverb");CHECK(merged.groups[0].dryRoutes.size()==1);
    auto conflict=fan;const auto boundary=signalSongGroupBoundary(conflict,fanMixer,{"compressor"},101);conflict.groups[1].dryRoutes[1].input=*std::find_if(boundary.inputs.begin(),boundary.inputs.end(),[](const auto &r){return r.kind=="plugin-input";});auto unchanged=conflict;rejects([&]{preserveSongGroupInsertion(conflict,unchanged,fanMixer,appended,{"compressor"},{"compressor","reverb"},"reverb");});CHECK(conflict==unchanged);
    auto stale=beforeGraph;stale.groups[0].dryRoutes[0].output.target="n999";const auto priorStale=stale;rejects([&]{preserveSongGroupInsertion(stale,priorStale,beforeMixer,afterMixer,{"compressor"},{"compressor","reverb"},"reverb");});CHECK(stale==priorStale);
    auto undecided=beforeGraph;for(auto &g:undecided.groups){g.bypass=false;g.dryRoutes.clear();}auto undecidedBefore=undecided;preserveSongGroupInsertion(undecided,undecidedBefore,beforeMixer,afterMixer,{"compressor"},{"compressor","reverb"},"reverb");CHECK(undecided==undecidedBefore);
  }
  std::cout<<"PASS group dry boundaries: exact ingress/egress, explicit branches, nested members, silence, dry-cycle rejection and live-control classification\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
