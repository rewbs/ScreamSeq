#include "editor/ParameterProvenance.hpp"
#include "editor/ParameterBaseline.hpp"
#include "editor/TrackerDocument.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace Tracker;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::invalid_argument &){rejected=true;}CHECK(rejected);}
int main(){try{
  Document doc(MOD_TYPE_MPT,4);doc.song().Order().assign(2,0);auto native=doc.native();const auto pattern=native.patterns.at(0).id,track=native.tracks.at(2).id;
  MusicalAutomationLane lane;lane.id=native.makeEntity().id;lane.pattern=pattern;lane.plugin="unloaded";lane.parameter=UINT32_MAX;lane.enabled=false;lane.points={{16*256,.2},{32*256,.8}};native.automation.push_back(lane);
  native.performance.bindings[3]={"unloaded",UINT32_MAX,"Frequency"};native.performance.bindings[4]={"other",UINT32_MAX,"Other plugin"};
  native.performance.commands={{pattern,track,20*65536,2*65536,1,PatternCommandKind::ParameterSlide,3,.7},{pattern,track,16*65536,0,1,PatternCommandKind::ParameterSet,3,.2},{pattern,track,17*65536,0,1,PatternCommandKind::ParameterSet,4,.5}};
  const auto before=native;const auto revision=doc.revision;
  auto page=parameterProvenance(native,doc.song(),"unloaded",UINT32_MAX,{}, {4,48000,144000});
  CHECK(page.total==3&&page.sources.size()==3&&page.offset==0);CHECK(native==before&&doc.revision==revision&&!doc.canUndo());
  CHECK(page.sources[0].kind==ParameterProvenanceKind::Envelope&&!page.sources[0].enabled&&page.sources[0].position==16*65536&&page.sources[0].endPosition==32*65536);
  const auto &fx=page.sources[1];CHECK(fx.kind==ParameterProvenanceKind::PatternCommands&&fx.count==2&&fx.pattern==pattern&&fx.track==track&&fx.channel==2&&fx.column==1&&fx.binding==3);
  CHECK(fx.orders.size()==2&&fx.orders[0].second==0&&fx.orders[1].second==1);
  CHECK(fx.commands[0].kind==PatternCommandKind::ParameterSet&&fx.commands[1].kind==PatternCommandKind::ParameterSlide&&fx.commands[1].duration==2*65536&&fx.commands[1].value==.7);
  CHECK(page.sources[2].kind==ParameterProvenanceKind::Recorded&&page.sources[2].count==4&&page.sources[2].firstFrame==48000);
  const auto key=fx.key;std::reverse(native.performance.commands.begin(),native.performance.commands.end());native.performance.commands[1].position=40*65536;
  CHECK(parameterProvenance(native,doc.song(),"unloaded",UINT32_MAX).sources[1].key==key);
  const auto next=parameterProvenance(native,doc.song(),"unloaded",UINT32_MAX,{}, {4,48000,144000},1,1);CHECK(next.total==3&&next.sources.size()==1&&next.sources[0].key==key);
  CHECK(parameterProvenance(native,doc.song(),"unloaded",UINT32_MAX,{}, {},900,1).sources.empty());
  CHECK(parameterProvenance(native,doc.song(),"other",7).sources.empty());
  native.automation[0].points.clear();CHECK(parameterProvenance(native,doc.song(),"unloaded",UINT32_MAX).sources[0].count==0);
  native.performance.commands.clear();for(uint32_t i=0;i<200;++i)native.performance.commands.push_back({pattern,track,i*256,0,1,PatternCommandKind::ParameterSet,3,.5});
  const auto bounded=parameterProvenance(native,doc.song(),"unloaded",UINT32_MAX).sources[1];CHECK(bounded.count==200&&bounded.commands.size()==128&&bounded.omittedCommands==72);
  rejects([&]{parameterProvenance(native,doc.song(),"",7);});rejects([&]{parameterProvenance(native,doc.song(),"unloaded",7,65535);});rejects([&]{parameterProvenance(native,doc.song(),"unloaded",7,{}, {},0,257);});rejects([&]{parameterProvenance(native,doc.song(),"unloaded",7,{}, {1,3,2});});
  SignalDefinition recipe;SignalNode processor;processor.id=7;processor.kind=SignalNodeKind::Plugin;processor.plugin.parameters[42]=80;recipe.nodes.push_back(processor);
  CHECK(signalManualParameterValue(recipe,7,42,20,20000,900)==80);
  recipe.modulation.push_back({8,7,42,-.2,.3,.25,true});
  CHECK(signalManualParameterValue(recipe,7,42,20,20000,900)==5015);
  recipe.modulation[0].enabled=false;CHECK(signalManualParameterValue(recipe,7,42,20,20000,900)==80);
  CHECK(signalManualParameterValue(recipe,7,43,20,20000,900)==900);
  struct Parameter {uint32_t id;double min,max,value;};
  recipe.modulation.push_back({9,7,42,-.1,.1,.2,true});recipe.modulation.push_back({10,99,42,0,.4,.8,true});
  captureSignalParameterBases(recipe,7,std::vector<Parameter>{{42,20,20000,10010}});
  CHECK(recipe.modulation[0].base==.5&&recipe.modulation[1].base==.5&&recipe.modulation[2].base==.8);
  recipe.nodes[0].plugin.parameters.clear();CHECK(signalManualParameterValue(recipe,7,42,20,20000,900)==10010);
  const auto captured=recipe;
  rejects([&]{captureSignalParameterBases(recipe,7,std::vector<Parameter>{{42,20,20,20}});});CHECK(recipe==captured);
  rejects([&]{captureSignalParameterBases(recipe,7,std::vector<Parameter>{});});CHECK(recipe==captured);
  rejects([&]{captureSignalParameterBases(recipe,7,std::vector<Parameter>{{42,20,20000,std::numeric_limits<double>::quiet_NaN()}});});CHECK(recipe==captured);
  rejects([&]{captureSignalParameterBases(recipe,7,std::vector<Parameter>{{42,-1e308,1e308,0}});});CHECK(recipe==captured);
  struct WideFloatParameter {uint32_t id;float min,max,value;};
  captureSignalParameterBases(recipe,7,std::vector<WideFloatParameter>{{42,-3e38f,3e38f,0}});
  CHECK(recipe.modulation[0].base==.5&&recipe.modulation[1].base==.5);
  SignalDefinition modes;modes.id=10;modes.number=1;modes.name="Discrete mode validation";modes.nodes={{1,SignalNodeKind::Input,"Input"},{2,SignalNodeKind::Plugin,"Effect"},{3,SignalNodeKind::Output,"Output"},{4,SignalNodeKind::LFO,"LFO"},{5,SignalNodeKind::Random,"Random"}};
  modes.nodes[1].plugin.format="Built-in";modes.nodes[1].plugin.classID="resonance.gainer.v1";modes.audio={{1,2},{2,3}};
  modes.modulation={{4,2,3,0,.2,.5,true,true},{5,2,3,-.1,.1,.5,true,false}};
  rejects([&]{compileSignal(modes);});modes.modulation.back().enabled=false;compileSignal(modes);
  auto otherModes=modes;otherModes.modulation[0].quantized=false;
  SignalGraph originalModes,changedModes;originalModes.library={modes};originalModes.assignments={{100,modes.id}};changedModes=originalModes;changedModes.library={otherModes};
  CHECK(!sameSignalProcessing(originalModes,changedModes)&&sameSignalControlLayout(originalModes,changedModes));
  std::cout<<"PASS bounded parameter provenance: stopped/unloaded metadata, stable references, exact command semantics, pagination and no mutation\n";
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
