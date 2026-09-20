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
  auto cycle=d;cycle.audio.push_back({2,2});rejects([&]{compileSignal(cycle);});
  cycle=d;cycle.nodes.push_back({4,SignalNodeKind::Follower,"Follower"});cycle.audio.push_back({2,4});cycle.modulation.push_back({4,2});rejects([&]{compileSignal(cycle);});
  auto dangling=d;dangling.audio[0].source=999;rejects([&]{compileSignal(dangling);});
  SignalGraph library;library.library={d};library.lanes[5]=2;library.commands.push_back({6,5,100,0,0,SignalCommandKind::Start});library.validate({5},{{6,64}});library.commands.push_back(library.commands[0]);rejects([&]{library.validate({5},{{6,64}});});
  std::cout<<"Signal graph: DAG validation, serial/parallel audio, delay alignment, multiport sidechains, smooth modulation and macro mapping passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
