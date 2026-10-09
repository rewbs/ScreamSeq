#include "editor/SignalRuntime.hpp"
#include "editor/hosted/GraphSignalObservation.hpp"
#include <stdexcept>
#include <limits>
#define REQUIRE(condition) do { if(!(condition))throw std::runtime_error(#condition); } while(false)
#include <cmath>
#include <iostream>

using namespace Tracker;
static void noteGateHistory() {
  for(uint32_t rate:{44100u,48000u,96000u})for(uint32_t block:{17u,128u,4096u}){
    SignalDefinition graph;graph.id=40;graph.name="Gate observations";
    graph.nodes={{41,SignalNodeKind::Input,"Input"},{42,SignalNodeKind::Output,"Output"},{43,SignalNodeKind::NoteEnvelope,"Gate"}};
    graph.audio={{41,42,0,0,1}};const auto plan=compileSignal(graph);
    auto o=std::make_unique<SignalObservation>(rate);std::vector<SignalPortIdentity> pending;
    const auto domain=o->newDomain(),otherDomain=o->newDomain();
    SignalRuntime runtime(graph,plan,rate),other(graph,plan,rate);
    runtime.observer(std::make_shared<GraphSignalObservation>(*o,domain,SignalCopyIdentity{40,50,2},graph,plan,pending));
    other.observer(std::make_shared<GraphSignalObservation>(*o,otherDomain,SignalCopyIdentity{40,51,2},graph,plan,pending));
    auto batch=o->preparePorts(std::move(pending));o->publishPorts(batch);
    uint32_t token=0,otherToken=0;for(size_t i=0;i<o->ports.size();++i)if(o->ports[i].kind=="control"){
      (o->ports[i].copy->target==50?token:otherToken)=uint32_t(i+1);
    }
    REQUIRE(token&&otherToken);std::array<float,8192> samples{};uint64_t at=0;
    auto render=[&](uint32_t frames){for(uint32_t offset=0;offset<frames;){const auto count=std::min(block,frames-offset);
      REQUIRE(runtime.render(samples.data(),count,at,{},{}));REQUIRE(other.render(samples.data(),count,at,{},{}));offset+=count;at+=count;}};
    render(127);auto g=*o->read(token).noteGate;REQUIRE(!g.held&&!g.hasEvent&&g.on+g.off+g.retrigger==0);
    runtime.note(true,true);render(4096);g=*o->read(token).noteGate;
    REQUIRE(g.held&&g.on==1&&g.off==0&&g.retrigger==1&&g.hasEvent&&g.lastFrame==127);
    runtime.note(true);render(128);g=*o->read(token).noteGate;REQUIRE(g.on==1&&g.retrigger==1&&g.lastFrame==127);
    const auto retriggerAt=at;runtime.note(true,true);render(33);g=*o->read(token).noteGate;
    REQUIRE(g.on==1&&g.retrigger==2&&g.lastFrame==retriggerAt);
    const auto releaseAt=at;runtime.note(false);runtime.note(false);render(50);g=*o->read(token).noteGate;
    REQUIRE(!g.held&&g.off==1&&g.lastFrame==releaseAt);
    const auto untouched=*o->read(otherToken).noteGate;REQUIRE(!untouched.held&&!untouched.hasEvent&&untouched.on+untouched.off+untouched.retrigger==0);
    const auto generation=o->read(token).generation;o->activateDomain(domain,{});REQUIRE(!o->read(token).noteGate);
    render(1);REQUIRE(o->read(token).generation>generation&&o->read(token).noteGate->off==1);
    // Rebuild transfers the live history for a retained source; a new source
    // begins with the current aggregate gate but no invented historical note-on.
    runtime.note(true,true);auto updated=graph;updated.nodes.push_back({44,SignalNodeKind::NoteEnvelope,"New gate"});
    const auto updatedPlan=compileSignal(updated);SignalRuntime replacement(updated,updatedPlan,rate);pending.clear();
    replacement.observer(std::make_shared<GraphSignalObservation>(*o,domain,SignalCopyIdentity{40,50,2},updated,updatedPlan,pending));
    batch=o->preparePorts(std::move(pending));o->publishPorts(batch);replacement.inheritState(runtime);
    REQUIRE(replacement.render(samples.data(),1,at,{},{}));g=*o->read(token).noteGate;
    REQUIRE(g.held&&g.on==2&&g.off==1&&g.retrigger==3&&g.lastFrame==at);
    for(size_t i=0;i<o->ports.size();++i)if(o->ports[i].node=="node:n44"){
      const auto fresh=*o->read(uint32_t(i+1)).noteGate;REQUIRE(fresh.held&&!fresh.hasEvent&&fresh.on+fresh.off+fresh.retrigger==0);
    }
  }
}
static void generationHistory() {
  auto o=std::make_unique<SignalObservation>(48000);
  SignalPortIdentity audio{"generation/audio","test","Audio",true};audio.copy=SignalCopyIdentity{1,2,2};
  SignalPortIdentity control{"generation/control","test","Control",true};control.copy=audio.copy;control.kind="control";
  auto batch=o->preparePorts({audio,control});o->publishPorts(batch);
  REQUIRE(!o->available(1)&&!o->read(1).available&&!o->available(2));
  const auto domain=o->newDomain();const std::array configs{SignalPortConfiguration{1,0,0},SignalPortConfiguration{2,0,0}};
  o->activateDomain(domain,configs);REQUIRE(o->available(1)&&!o->read(1).measured);
  const std::array<float,2> invalid{1.25f,std::numeric_limits<float>::quiet_NaN()};
  o->observe(1,invalid.data(),1,0);o->observeControl(2,.2,.7,1,0);
  REQUIRE(o->read(1).clipped&&o->read(1).nonFinite&&o->read(1).lastSignal==1);
  REQUIRE(o->read(2).value==.7);
  o->activateDomain(domain,configs);
  auto empty=o->read(1);REQUIRE(empty.available&&!empty.measured&&!empty.clipped&&!empty.nonFinite&&empty.lastSignal==0&&empty.peakLeft==0);
  REQUIRE(o->read(2).value==0&&!o->read(2).measured);
  o->observe(1,nullptr,1,1);o->observeControl(2,0,0,1,1);
  auto silent=o->read(1);REQUIRE(silent.measured&&silent.peakLeft==0&&silent.rmsLeft==0&&silent.lastSignal==0&&!silent.clipped&&!silent.nonFinite);
  o->observeControl(2,std::numeric_limits<double>::infinity(),.5,1,2);
  REQUIRE(o->read(2).nonFinite&&o->read(2).measured&&o->read(2).through==3&&std::isfinite(o->read(2).value));
  o->clear(2);o->observeControl(2,.1,.4,1,3);REQUIRE(!o->read(2).nonFinite&&o->read(2).value==.4);
  o->observeControl(2,std::numeric_limits<double>::infinity(),0,1,4);
  o->activateDomain(domain,configs);o->observeControl(2,.3,.6,1,5);
  REQUIRE(!o->read(2).nonFinite&&o->read(2).value==.6);
  // Reusing the original copy domain after an unrelated mixer adoption resets
  // only its own history; Clear retains the ordinary same-generation decay.
  o->observe(1,invalid.data(),1,6);o->clear(1);o->observe(1,nullptr,1,7);
  REQUIRE(!o->read(1).clipped&&!o->read(1).nonFinite&&o->read(1).peakLeft>1&&o->read(1).lastSignal==7);
}
int main(){
  noteGateHistory();
  generationHistory();
  auto observation=std::make_unique<SignalObservation>(48000);
  const auto rack=observation->add({"rack","plugin:rack","Rack",true,0,2,0,0});
  observation->activate(std::array{SignalPortConfiguration{rack,0,0}});
  SignalDefinition graph;graph.id=10;graph.name="Observed";
  SignalNode input;input.id=11;input.kind=SignalNodeKind::Input;input.name="Input";
  SignalNode output;output.id=12;output.kind=SignalNodeKind::Output;output.name="Output";
  SignalNode effect;effect.id=13;effect.kind=SignalNodeKind::Plugin;effect.name="Gain";
  effect.plugin.format="Built-in";effect.plugin.classID="org.resonance.builtin.gain";
  SignalNode amount;amount.id=14;amount.kind=SignalNodeKind::Amount;amount.name="Amount";
  graph.nodes={input,output,effect,amount};graph.audio={{11,13,0,0,.5},{13,12,0,0,1}};
  graph.modulation={{14,13,0,0,1,0,true}};
  const auto plan=compileSignal(graph,{{13,0,1,1}});
  SignalCallbacks callbacks;
  callbacks.process=[](void*,uint64_t,float *samples,uint32_t count,uint64_t,std::span<const MixerAudioInput>)noexcept {for(uint32_t i=0;i<count*2;++i)samples[i]*=2;return true;};
  callbacks.parameter=[](void*,uint64_t,uint32_t,double,double,uint64_t,uint32_t)noexcept {return true;};
  std::vector<SignalPortIdentity> pending;
  const auto firstDomain=observation->newDomain(),secondDomain=observation->newDomain();
  auto first=std::make_unique<SignalRuntime>(graph,plan,48000);
  first->observer(std::make_shared<GraphSignalObservation>(*observation,firstDomain,SignalCopyIdentity{10,100,2,0,UINT16_MAX},graph,plan,pending));
  auto second=std::make_unique<SignalRuntime>(graph,plan,48000);
  second->observer(std::make_shared<GraphSignalObservation>(*observation,secondDomain,SignalCopyIdentity{10,101,2,0,UINT16_MAX},graph,plan,pending));
  auto ports=observation->preparePorts(std::move(pending));observation->publishPorts(ports);
  const auto find=[&](uint64_t target,const std::string &node,bool output,const std::string &kind,bool route=false){for(size_t i=0;i<observation->ports.size();++i){const auto &p=observation->ports[i];if(p.copy&&p.copy->target==target&&p.node==node&&p.output==output&&p.kind==kind&&bool(p.route)==route)return uint32_t(i+1);}return 0u;};
  const auto outputA=find(100,"node:n13",true,"audio"),outputB=find(101,"node:n13",true,"audio"),controlA=find(100,"node:n14",true,"control");
  REQUIRE(outputA&&outputB&&controlA&&outputA!=outputB);
  std::array<float,128> a,b;a.fill(.25f);b.fill(.75f);first->amount(.2);second->amount(.8);
  observation->scope.watch(outputA);
  REQUIRE(first->render(a.data(),64,0,{},callbacks));REQUIRE(second->render(b.data(),64,0,{},callbacks));
  REQUIRE(std::abs(observation->read(outputA).rmsLeft-.25)<1e-6);REQUIRE(std::abs(observation->read(outputB).rmsLeft-.75)<1e-6);
  REQUIRE(std::abs(observation->read(controlA).value-.2)<1e-8);
  auto scope=observation->scope.snapshot();REQUIRE(scope.frames==64&&scope.routeGeneration==observation->read(outputA).generation);
  // A separate mixer adoption must not retire recipe ports or invalidate the
  // selected copy's scope. Nor can another copy make these readings available.
  observation->activate(std::array{SignalPortConfiguration{rack,0,32}});
  REQUIRE(observation->available(outputA)&&observation->available(outputB));
  REQUIRE(observation->scope.snapshot().routeGeneration==scope.routeGeneration);
  // Preparation has no observable side effects; membership changes at render.
  auto changed=graph;changed.nodes.erase(changed.nodes.begin()+2);changed.audio={{11,12,0,0,1}};changed.modulation.clear();
  const auto changedPlan=compileSignal(changed);pending.clear();
  auto replacement=std::make_unique<SignalRuntime>(changed,changedPlan,48000);
  replacement->observer(std::make_shared<GraphSignalObservation>(*observation,firstDomain,SignalCopyIdentity{10,100,2,0,UINT16_MAX},changed,changedPlan,pending));
  REQUIRE(observation->available(outputA));ports=observation->preparePorts(std::move(pending));observation->publishPorts(ports);
  REQUIRE(replacement->render(a.data(),64,64,{},callbacks));REQUIRE(!observation->available(outputA)&&observation->available(outputB));
  REQUIRE(!observation->read(outputA).fresh);
  // A changed physical channel count needs a different immutable token. The
  // cable follows the source pair's channel count, including explicit mono.
  pending.clear();const auto before=observation->ports.size();
  const std::array monoBus{SignalObservedBus{13,true,0,1}};
  auto mono=std::make_unique<SignalRuntime>(graph,plan,48000);
  mono->observer(std::make_shared<GraphSignalObservation>(*observation,firstDomain,SignalCopyIdentity{10,100,2,0,UINT16_MAX},graph,plan,pending,monoBus));
  REQUIRE(!pending.empty());ports=observation->preparePorts(std::move(pending));observation->publishPorts(ports);
  uint32_t monoOutput=0,monoCable=0;
  for(size_t i=before;i<observation->ports.size();++i){const auto &p=observation->ports[i];if(p.node=="node:n13"&&p.output&&p.channels==1){if(p.route)monoCable=uint32_t(i+1);else monoOutput=uint32_t(i+1);}}
  REQUIRE(monoOutput&&monoCable&&monoOutput!=outputA&&!observation->available(monoOutput));
  REQUIRE(mono->render(a.data(),64,128,{},callbacks));
  REQUIRE(!observation->available(outputA)&&observation->available(monoOutput)&&observation->available(monoCable));
  auto lfoGraph=graph;lfoGraph.nodes.back().kind=SignalNodeKind::LFO;
  pending.clear();auto lfoPlan=compileSignal(lfoGraph,{{13,0,1,1}});
  auto lfo=std::make_unique<SignalRuntime>(lfoGraph,lfoPlan,48000);
  lfo->observer(std::make_shared<GraphSignalObservation>(*observation,firstDomain,SignalCopyIdentity{10,100,2,0,UINT16_MAX},lfoGraph,lfoPlan,pending,monoBus));
  REQUIRE(pending.size()==1&&pending.front().kind=="control");
  const auto lfoControl=uint32_t(observation->ports.size()+1);ports=observation->preparePorts(std::move(pending));observation->publishPorts(ports);
  REQUIRE(lfo->render(a.data(),64,192,{},callbacks));
  REQUIRE(!observation->available(controlA)&&observation->available(lfoControl));
  const auto activeGeneration=observation->read(lfoControl).generation;
  observation->activateDomain(firstDomain,{});
  REQUIRE(!observation->available(lfoControl)&&observation->available(outputB));
  REQUIRE(lfo->render(a.data(),64,256,{},callbacks));
  REQUIRE(observation->available(lfoControl)&&observation->read(lfoControl).fresh&&observation->read(lfoControl).generation>activeGeneration);
  std::cout<<"Exact graph-copy ports, control values, generation history, immutable layouts and scope isolation passed\n";
}
