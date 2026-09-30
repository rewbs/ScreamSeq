#include "editor/SignalGraph.hpp"
#include "editor/SignalRuntime.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Tracker;
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>static void rejects(F f){bool rejected=false;try{f();}catch(const std::invalid_argument &){rejected=true;}check(rejected,"invalid graph accepted");}
static SignalDefinition graph(){SignalDefinition d;d.id=100;d.name="Test";d.nodes={{1,SignalNodeKind::Input,"Input"},{2,SignalNodeKind::Plugin,"Double"},{3,SignalNodeKind::Output,"Output"}};d.audio={{1,2},{2,3}};return d;}
struct Fixture {
  std::array<float,8192> aux{};
  std::array<double,8192> values{};
  size_t ramps=0,calls=0;
  static bool process(void *context,uint64_t,float *audio,uint32_t frames,uint64_t position,std::span<const MixerAudioInput> inputs) noexcept {
    auto &f=*static_cast<Fixture *>(context);++f.calls;
    for(size_t i=0;i<frames*2;++i){audio[i]*=2;for(auto &p:inputs)audio[i]+=p.samples[i];f.aux[i]=audio[i]*3;}
    (void)position;return true;
  }
  static const float *output(void *context,uint64_t,uint32_t)noexcept{return static_cast<Fixture *>(context)->aux.data();}
  static bool parameter(void *context,uint64_t,uint32_t,double a,double b,uint64_t frame,uint32_t duration)noexcept {
    auto &f=*static_cast<Fixture *>(context);++f.ramps;
    if(frame+duration>=f.values.size())return false;
    for(uint32_t i=0;i<=duration;++i)f.values[frame+i]=duration?a+(b-a)*i/duration:a;return true;
  }
  SignalCallbacks callbacks(){return {this,process,output,parameter};}
};
int main(){try{
  auto patch=graph();patch.nodes.push_back({4,SignalNodeKind::Plugin,"Insert A"});patch.nodes.push_back({5,SignalNodeKind::Plugin,"Insert B"});patch.nodes.push_back({6,SignalNodeKind::LFO,"Mod"});patch.modulation={{6,4,7,.1,.8,.2,true}};
  patch.audio.push_back({4,5,0,0,.7});auto originalPatch=patch;
  insertSignalNodes(patch,{5,4},0);
  check(patch.audio.size()==4&&patch.modulation==originalPatch.modulation,"Chain insertion retains internal gain and modulation");
  check(std::any_of(patch.audio.begin(),patch.audio.end(),[](const auto &e){return e.source==1&&e.target==4;})&&std::any_of(patch.audio.begin(),patch.audio.end(),[](const auto &e){return e.source==5&&e.target==2;}),"Selection order cannot reverse a serial chain");
  auto connected=patch;connected.audio.push_back({1,3,0,0,.25});insertSignalNodes(connected,{4,5},4);
  check(std::any_of(connected.audio.begin(),connected.audio.end(),[](const auto &e){return e.source==1&&e.target==2;}),"Moving a chain heals its old path");
  auto unchanged=patch;rejects([&]{insertSignalNodes(patch,{4,5},1);});check(patch==unchanged,"Invalid insertion is atomic");
  auto detached=patch;detachSignalNodes(detached,{5,4});
  check(detached.nodes==patch.nodes&&detached.modulation==patch.modulation,"Detachment preserves processor state, identities and modulation");
  check(std::any_of(detached.audio.begin(),detached.audio.end(),[](const auto &e){return e.source==1&&e.target==2;})&&std::any_of(detached.audio.begin(),detached.audio.end(),[](const auto &e){return e.source==4&&e.target==5&&e.gain==.7;}),"Detachment heals the main path and keeps the detached internal chain");
  auto deleted=patch;detachSignalNodes(deleted,{4,5},true);check(deleted.nodes.size()==patch.nodes.size()-2&&deleted.modulation.empty(),"Delete-and-heal also removes modulation targets");
  auto parallelHeal=patch;parallelHeal.audio.push_back({1,2});auto parallelBefore=parallelHeal;rejects([&]{detachSignalNodes(parallelHeal,{4,5});});check(parallelHeal==parallelBefore,"Healing never creates duplicate parallel routes or changes levels");
  auto sideDetach=graph();sideDetach.nodes[1].plugin.inputs={1};sideDetach.audio.push_back({1,2,0,1,.25});detachSignalNodes(sideDetach,{2});check(sideDetach.audio.size()==2&&sideDetach.audio[0].input==1,"Sidechain inputs are not mistaken for main predecessors");
  auto branched=patch;branched.audio.push_back({4,3});auto beforeBranch=branched;rejects([&]{insertSignalNodes(branched,{4,5},0);});check(branched==beforeBranch,"Ambiguous branches are never silently discarded");
  rejects([&]{detachSignalNodes(branched,{4,5});});check(branched==beforeBranch,"Ambiguous detach is atomic");
  auto grouped=patch;groupSignalNodes(grouped,{4,5},200,0,"Drive");
  check(grouped.nodes==patch.nodes&&grouped.audio==patch.audio&&grouped.modulation==patch.modulation,"Grouping preserves processor state, automation identities and every boundary cable");
  SignalGraph beforeGroup,afterGroup;beforeGroup.library={patch};afterGroup.library={grouped};
  beforeGroup.assignments={{10,patch.id}};afterGroup.assignments=beforeGroup.assignments;
  check(sameSignalProcessing(beforeGroup,afterGroup),"A processing boundary never restarts or recompiles DSP");
  groupSignalNodes(grouped,{4},201,200,"Nested");
  check(grouped.groups[0].nodes==std::vector<uint64_t>{5}&&grouped.groups[1].nodes==std::vector<uint64_t>{4}&&grouped.groups[1].parent==200,"Nested groups own only their immediate members");
  const auto groupedBefore=grouped;
  auto moved=grouped;moveSignalGroup(moved,200,300,120);
  check(moved.nodes[3].x==300&&moved.nodes[4].x==300&&moved.groups[1].x==300&&moved.groups[1].y==120,"Moving a parent translates all nested groups and processors once");
  auto movedBefore=moved;rejects([&]{moveSignalGroup(moved,200,100001,0);});check(moved==movedBefore,"Invalid group movement is atomic");
  rejects([&]{extractSignalGroup(grouped,200,400,401);});
  auto exportable=grouped;exportable.groups[0].nodes.push_back(6);
  const auto exported=extractSignalGroup(exportable,200,400,401);
  check(exported.nodes.size()==5&&exported.groups.size()==1&&exported.groups[0].parent==0&&exported.modulation==exportable.modulation,"Export includes nested processors and all enclosed modulation");
  check(exported.audio.size()==3&&exported.audio[0].gain==.7&&exported.audio.back().target==401,"Export keeps internal gain and creates independent audio boundaries");
  auto unused=afterGroup;auto unusedCopy=exported;unusedCopy.id=402;unused.library.push_back(unusedCopy);
  check(sameSignalProcessing(afterGroup,unused),"Saving an unused recipe cannot interrupt active playback");
  unused.library[0].nodes[1].plugin.state={std::byte{1}};
  check(!sameSignalProcessing(afterGroup,unused),"An active recipe processor change still invalidates processing");
  rejects([&]{groupSignalNodes(grouped,{4,5},202,200,"Mixed depths");});
  check(grouped==groupedBefore,"Invalid packaging cannot partially remove members from their owner");
  rejects([&]{groupSignalNodes(grouped,{1},202,0,"Input");});
  auto cyclic=grouped;cyclic.groups[0].parent=201;rejects([&]{compileSignal(cyclic);});
  auto duplicated=grouped;duplicated.groups[0].nodes.push_back(4);rejects([&]{compileSignal(duplicated);});
  auto missing=grouped;missing.groups[0].nodes.push_back(999);rejects([&]{compileSignal(missing);});
  ungroupSignalNodes(grouped,201);check(grouped.groups.size()==1&&grouped.groups[0].nodes.size()==2,"Ungroup moves contents back to their parent without changing wires");
  ungroupSignalNodes(grouped,200);check(grouped==patch,"Ungrouping restores the exact ungrouped definition");
  grouped=groupedBefore;std::erase_if(grouped.nodes,[](const auto &n){return n.id==4||n.id==5;});pruneSignalGroups(grouped);
  check(grouped.groups.empty(),"Deleting a final descendant retires empty nested boundaries");
  SignalGraph songGroups;songGroups.layout={{"plugin:a",{200,120}},{"plugin:b",{450,120}},{"plugin:c",{700,120}}};
  const auto ungroupedSong=songGroups;
  groupSongSignalNodes(songGroups,{"plugin:a","plugin:b"},{},501,0,"Rack pair");
  groupSongSignalNodes(songGroups,{"plugin:c"},{501},502,0,"Nested rack");
  check(songGroups.groups.size()==2&&songGroups.groups[0].parent==502,"Song groups keep rack identities and support nesting");
  check(sameSignalProcessing(songGroups,ungroupedSong),"Presentation grouping must not rebuild or change audio processing");
  const auto beforeBadGroup=songGroups;rejects([&]{groupSongSignalNodes(songGroups,{"plugin:a"},{},503,0,"Wrong depth");});check(songGroups==beforeBadGroup,"Rejected song grouping is atomic");
  rejects([&]{groupSongSignalNodes(songGroups,{"plugin:a","plugin:a"},{},503,501,"Duplicate");});
  moveSongSignalGroup(songGroups,502,250,150);check(songGroups.layout.at("plugin:a")==std::array<double,2>{250,150}&&songGroups.layout.at("plugin:c")==std::array<double,2>{750,150},"Song group drag moves every descendant");
  const auto beforeBadMove=songGroups;rejects([&]{moveSongSignalGroup(songGroups,502,-1,100);});check(songGroups==beforeBadMove,"Rejected group movement does not change positions");
  auto cyclicSong=songGroups;cyclicSong.groups[1].parent=501;rejects([&]{validateSongSignalGroups(cyclicSong);});
  ungroupSongSignalNodes(songGroups,501);check(songGroups.groups.size()==1&&songGroups.groups[0].nodes.size()==3,"Nested ungroup preserves direct owner");
  pruneSongSignalGroups(songGroups,{"plugin:c"});check(songGroups.groups[0].nodes==std::vector<std::string>{"plugin:c"},"Song group pruning retains surviving processors");
  pruneSongSignalGroups(songGroups,{});check(songGroups.groups.empty(),"Empty song groups retire");
  SignalGraph routedGroup;groupSongSignalNodes(routedGroup,{"plugin:a","plugin:b"},{},601,0,"Routed pair");
  MixerGraph routedMixer;routedMixer.buses={{10,20,MixerBusKind::Track,"Track"},{20,0,MixerBusKind::Master,"Master"}};routedMixer.buses[0].inserts={"a","b"};
  routedMixer.sidechains={{30,"b",1,-8,true,true},{40,"b",1,-3,false,true},{30,"b",0,-6,false,true},{40,"b",0,-9,true,true},{30,"b",3,0,false,false}};
  routedMixer.instruments={{"b",20,3},{"b",40,3},{"b",0,4}};
  GraphPluginRecipe recipeA,recipeB;recipeA.name="A";recipeA.inputs={2};recipeB.name="B";
  const auto originalMixer=routedMixer;const auto originalGroup=routedGroup;uint64_t exportedID=1000;
  auto routedExport=extractSongSignalGroup(routedGroup,routedMixer,601,{{"a",recipeA},{"b",recipeB}},[&]{return exportedID++;});
  const auto &exportA=routedExport.nodes[2],&exportB=routedExport.nodes[3];
  check(exportA.plugin.inputs==std::vector<uint32_t>{2}&&exportB.plugin.inputs==std::vector<uint32_t>{1}&&exportB.plugin.outputs==std::vector<uint32_t>{3},"Song export preserves explicitly and routing-enabled auxiliary buses, excluding disabled inputs");
  const auto exportedInput=routedExport.nodes[0].id;
  check(std::count_if(routedExport.audio.begin(),routedExport.audio.end(),[&](const auto &e){return e.source==exportedInput&&e.target==exportB.id&&e.input==0&&e.output!=0;})==1,"Additional main-input fan-in is exposed once at the correct downstream processor");
  check(routedMixer==originalMixer&&routedGroup==originalGroup&&std::all_of(routedExport.audio.begin(),routedExport.audio.end(),[](const auto &e){return e.gain==1;}),"Library export neither mutates external routing nor hides its gain/tap settings inside the copy");
  Fixture exportedFixture;SignalRuntime exportedRuntime(routedExport,compileSignal(routedExport),48000);
  std::array<float,16> exportedMain,extraMain,extraSide;exportedMain.fill(.1f);extraMain.fill(.2f);extraSide.fill(.3f);
  std::array<MixerAudioInput,2> exportedInputs{{{2,extraMain.data()},{3,extraSide.data()}}};
  check(exportedRuntime.render(exportedMain.data(),8,0,{},exportedFixture.callbacks(),exportedInputs)&&std::abs(exportedMain[0]-1.1f)<1e-6,"Exported extra main input bypasses the earlier insert while the sidechain reaches its original input");
  auto d=graph();auto plan=compileSignal(d);check(plan.order==std::vector<size_t>({0,1,2}),"wrong processing order");
  Fixture fixture;SignalRuntime runtime(d,plan,48000);std::array<float,256> samples{};samples[0]=samples[1]=.25;
  check(runtime.render(samples.data(),128,0,{},fixture.callbacks()),"render failed");check(samples[0]==.5&&samples[2]==0,"serial processing wrong");
  auto parallel=d;parallel.audio.push_back({1,3});SignalRuntime parallelRuntime(parallel,compileSignal(parallel),48000);samples.fill(0);samples[0]=samples[1]=.25;parallelRuntime.render(samples.data(),128,0,{},fixture.callbacks());check(samples[0]==.75,"fan-in did not sum");
  auto latency=compileSignal(parallel,{{2,7,1,1}});check(latency.totalLatency==7&&latency.edges[2].delay==7,"parallel PDC wrong");
  // A delayed dry branch must not drift at block boundaries. The fixture itself
  // reports no latency here, letting us distinguish each branch's impulse.
  SignalRuntime delayRuntime(parallel,latency,48000);samples.fill(0);samples[0]=samples[1]=1;delayRuntime.render(samples.data(),5,0,{},fixture.callbacks());check(samples[0]==2,"wet branch missing");samples.fill(0);delayRuntime.render(samples.data(),12,5,{},fixture.callbacks());check(samples[4]==1&&samples[6]==0,"PDC lost samples across block boundary");
  auto multi=d;multi.nodes[1].plugin.inputs={1};multi.nodes[1].plugin.outputs={1};multi.audio.push_back({1,2,1,1});multi.audio.push_back({2,3,1,1});
  SignalRuntime multiRuntime(multi,compileSignal(multi,{{2,0,3,3}}),48000);std::array<float,256> side{};side.fill(.1);samples.fill(.2);std::array<MixerAudioInput,1> external{{{1,side.data()}}};check(multiRuntime.render(samples.data(),128,0,{},fixture.callbacks(),external),"multiport failed");check(std::abs(samples[0]-.5)<1e-6&&std::abs(multiRuntime.output(1)[0]-1.5)<1e-6,"sidechain or aux output wrong");
  rejects([&]{compileSignal(multi,{{2,0,1,1}});});
  auto mod=d;mod.nodes.push_back({4,SignalNodeKind::LFO,"LFO"});mod.modulation.push_back({4,2,0,0,1,0});SignalRuntime modRuntime(mod,compileSignal(mod),48000);samples.fill(0);fixture.ramps=0;check(modRuntime.render(samples.data(),128,0,{},fixture.callbacks()),"modulation render failed");check(fixture.ramps==4,"modulation did not use ramp segments");for(size_t i=0;i<128;++i)check(std::abs(fixture.values[i]-(.5+.5*std::sin(i*2*3.141592653589793*2/48000)))<1e-5,"LFO ramp is discontinuous or inaccurate");
  auto amount=d;amount.nodes.push_back({4,SignalNodeKind::Amount,"Amount"});amount.modulation.push_back({4,2,0,1,0,0});SignalRuntime amountRuntime(amount,compileSignal(amount),48000);amountRuntime.amount(.2);check(amountRuntime.render(samples.data(),128,0,{},fixture.callbacks()),"macro render failed");check(std::abs(fixture.values[127]-.8)<1e-12,"inverse Amount mapping wrong");
  auto envelope=mod;envelope.nodes.back().kind=SignalNodeKind::NoteEnvelope;SignalRuntime envelopeRuntime(envelope,compileSignal(envelope),48000);
  envelopeRuntime.note(true,true);envelopeRuntime.render(samples.data(),128,0,{},fixture.callbacks());const double peak=fixture.values[127];check(std::abs(peak-(1-std::exp(-128./480)))<1e-12,"Note envelope attack is not sample timed");
  envelopeRuntime.render(samples.data(),128,128,{},fixture.callbacks());check(fixture.values[255]>peak,"Held note envelope restarted at callback boundary");
  envelopeRuntime.note(true,true);envelopeRuntime.render(samples.data(),128,256,{},fixture.callbacks());check(std::abs(fixture.values[383]-peak)<1e-12,"Repeated note did not retrigger envelope");
  envelopeRuntime.note(false);envelopeRuntime.render(samples.data(),128,384,{},fixture.callbacks());check(std::abs(fixture.values[511]-peak*std::exp(-128./4800))<1e-12,"Note-off release is not sample timed");
  auto midi=mod;midi.nodes.back().kind=SignalNodeKind::MIDI;midi.nodes.back().controller=74;SignalRuntime midiRuntime(midi,compileSignal(midi),48000);midiRuntime.controller(74,64./127);midiRuntime.render(samples.data(),128,0,{},fixture.callbacks());check(std::abs(fixture.values[127]-64./127)<1e-12,"MIDI CC source mapping wrong");
  auto curveGraph=mod;curveGraph.nodes.back().kind=SignalNodeKind::Automation;
  curveGraph.nodes.back().envelopes={{91,true,{{0,.1,AutomationCurve::Linear},{63,.8,AutomationCurve::Step},{79,.2,AutomationCurve::Linear},{127,.9,AutomationCurve::Linear}}}};
  for(uint32_t block:{1u,7u,17u,128u}){
    Fixture curveFixture;SignalRuntime curves(curveGraph,compileSignal(curveGraph),48000);
    for(uint32_t at=0;at<128;){const auto count=std::min(block,128-at);check(curves.render(samples.data(),count,at,{0,120,true,91,double(at),1,128,4},curveFixture.callbacks()),"Pattern curve render failed");at+=count;}
    for(size_t i=0;i<128;++i)check(std::abs(curveFixture.values[i]-automationValue(curveGraph.nodes.back().envelopes[0].points,i,128,4))<1e-12,"Graph curve timing or step interpolation differs with callback size");
    curves.render(samples.data(),128,0,{0,120,true,92,0,1,128,4},curveFixture.callbacks());check(curveFixture.values[100]==0,"Missing pattern curve did not output zero");
  }
  curveGraph.nodes.back().envelopes[0].points={{0,0,AutomationCurve::Scripted,CurveFormula("t")}};
  Fixture scripted;SignalRuntime scriptRuntime(curveGraph,compileSignal(curveGraph),48000);scriptRuntime.render(samples.data(),128,0,{0,120,true,91,0,1,128,4},scripted.callbacks());
  check(std::abs(scripted.values[127]-127./128)<1e-12,"Final scripted graph segment ignored pattern end");
  SignalGraph automated;automated.library={curveGraph};automated.validate({},{{91,1}});
  rejects([&]{automated.validate({},{{92,1}});});automated.library[0].nodes.back().envelopes[0].points[0].position=256;rejects([&]{automated.validate({},{{91,1}});});
  auto cycle=d;cycle.audio.push_back({2,2});rejects([&]{compileSignal(cycle);});
  cycle=d;cycle.nodes.push_back({4,SignalNodeKind::Follower,"Follower"});cycle.audio.push_back({2,4});cycle.modulation.push_back({4,2});rejects([&]{compileSignal(cycle);});
  // Smooth/scripted curves used to fit each truncated callback separately.
  // Include one-sample callbacks, fractional point boundaries, final scripts,
  // and a nonzero absolute start so the grid cannot accidentally restart.
  for(auto curve:{AutomationCurve::Smooth,AutomationCurve::Exponential,AutomationCurve::Logarithmic,AutomationCurve::ExponentialReverse,AutomationCurve::LogarithmicReverse,AutomationCurve::StepNext,AutomationCurve::Scripted}){
    auto nonlinear=curveGraph;nonlinear.nodes.back().envelopes={{91,true,{{0,.2,curve},{21,.9,curve},{71,.1,curve}}}};
    if(curve==AutomationCurve::Scripted)for(auto &p:nonlinear.nodes.back().envelopes[0].points)p.formula=CurveFormula("mix(start,end,t*t)");
    std::array<double,8192> reference{};bool haveReference=false;
    for(uint32_t block:{1u,7u,17u,128u}){Fixture f;SignalRuntime r(nonlinear,compileSignal(nonlinear),48000);
      for(uint32_t at=0;at<128;){auto count=std::min(block,128-at);check(r.render(samples.data(),count,at+13,{0,120,true,91,double(at)*.8,.8,128,4},f.callbacks()),"Nonlinear graph curve render failed");at+=count;}
      if(!haveReference){reference=f.values;haveReference=true;}else for(size_t i=13;i<141;++i)check(std::abs(f.values[i]-reference[i])<1e-12,"Nonlinear graph curve depends on callback partition");
    }
  }
  auto dangling=d;dangling.audio[0].source=999;rejects([&]{compileSignal(dangling);});
  SignalGraph library;library.library={d};library.lanes[5]=2;library.commands.push_back({6,5,100,0,0,SignalCommandKind::Start});library.validate({5},{{6,64}});library.commands.push_back(library.commands[0]);rejects([&]{library.validate({5},{{6,64}});});
  std::cout<<"Signal graph: DAG validation, serial/parallel audio, delay alignment, multiport sidechains, smooth modulation and macro mapping passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
