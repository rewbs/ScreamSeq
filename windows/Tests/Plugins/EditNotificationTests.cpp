#include "windows/Plugins/WindowsVST3.hpp"
#include "windows/Plugins/UiOwner.hpp"
#include "windows/Audio/HostClock.hpp"
#include "windows/Audio/RealtimeAudit.hpp"
#include "editor/hosted/HostedAudio.hpp"
#include "editor/hosted/RecordedGestureTake.hpp"
#include <windows.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace Tracker;
static void check(bool okay,const char *message){if(!okay)throw std::runtime_error(message);}
static void beginAudit(){ScreamSeq::AudioAudit::allocations=0;ScreamSeq::AudioAudit::deallocations=0;ScreamSeq::AudioAudit::active=true;}
static void endAudit(bool okay){
  ScreamSeq::AudioAudit::active=false;
  check(okay,"Accepted editor gesture must remain audible");
  check(!ScreamSeq::AudioAudit::allocations&&!ScreamSeq::AudioAudit::deallocations,"Editor delivery must not allocate/free host C++ storage in process");
}
int main(int argc,char **argv){try{
  check(argc==4,"Scanner, fixture and cache paths required");
  WindowsVST3::configure(argv[1],argv[3]);
  const auto descriptors=WindowsVST3::rescan(argv[2]);
  check(!descriptors.empty(),"Native VST3 fixture discovered");
  PluginState state{descriptors[0]};state.instanceID="timestamped-editor";
  // Keep only the module loaded between scopes, so each broadcast reaches
  // exactly one native instance; each scope owns independent queue/loss state.
  auto dll=LoadLibraryW(std::filesystem::u8path(state.descriptor.path).c_str());
  check(dll,"Native fixture loaded");
  struct Module {HMODULE handle;~Module(){FreeLibrary(handle);}} module{dll};
  auto gesture=reinterpret_cast<int(*)(double)>(GetProcAddress(dll,"ResonanceFixtureGesture"));
  check(gesture,"Native fixture gesture export");
  auto send=[&](double value){int accepted=-1;WindowsVST3::pluginMainCall([&]{accepted=gesture(value);});return accepted;};
  {
    NativePlugin plugin(state,48000,true);
    check(send(.25)==1,"Pre-boundary native gesture failed");
    const auto boundary=plugin.editBoundary();
    check(boundary&&boundary->fence.hostTime&&boundary->source==plugin.editSource(),
      "Native owner did not capture an identified timestamped boundary");
    check(send(.75)==1,"Post-boundary native gesture failed");
    PluginEdit event;PluginEditSource source;
    check(plugin.popEditBefore(*boundary,event,source)==PluginEditRead::Event&&event.id==7&&event.value==.25f&&
      event.hostTime<=boundary->fence.hostTime&&source==boundary->source,"Native boundary lost its original input/provider");
    check(plugin.popEditBefore(*boundary,event,source)==PluginEditRead::Complete,
      "Native boundary consumed a later gesture");
    check(plugin.popEdit(event,source)&&event.value==.75f&&event.hostTime>=boundary->fence.hostTime,
      "Later native input was discarded by the recording drain");
    check(plugin.popEditBefore(*boundary,event,source)==PluginEditRead::InvalidFence,
      "An already passed native boundary was silently accepted");
    NativePlugin replacement(state,48000,true);
    check(replacement.popEditBefore(*boundary,event,source)==PluginEditRead::InvalidFence,
      "A replaced provider reused another incarnation's recording boundary");
  }
  {
    PluginChain chain({state},48000,true);
    const auto originalSource=chain.editSource(0);
    check(originalSource.incarnation&&!originalSource.dropped&&!chain.editSource(1).incarnation,
      "Rack notification source needs a real incarnation and invalid-slot sentinel");
    const auto before=ScreamSeq::hostTime100ns();
    check(send(.375)==1,"Fixture gesture accepted");
    const auto after=ScreamSeq::hostTime100ns();
    Sleep(2);const auto drain=ScreamSeq::hostTime100ns();
    PluginEdit event;PluginEditSource source;
    check(chain.popEdit(0,event,source)&&event.id==7&&event.value==.375f,"Rack facade preserves accepted value");
    check(source==originalSource,"Timestamped drain lost its actual notification provider");
    check(before&&event.hostTime>=before&&event.hostTime<=after&&event.hostTime<drain,"Rack facade preserves original callback time across delayed drain");
    check(!chain.popEdit(0,event,source)&&source==originalSource,"Empty queue changed its provider or consumed an event twice");
    check(!chain.popEdit(1,event,source)&&!source.incarnation,"Missing slot retained another slot's provider identity");
    check(chain.droppedEdits(0)==0,"Ordinary notification is not reported lost");
    std::array<float,256> pcm;pcm.fill(1);
    beginAudit();const auto okay=chain.process(pcm.data(),128);endAudit(okay);
    for(auto value:pcm)check(value==.375f,"Timestamped drain preserves audible parameter delivery");
    check(send(.625)==1,"Second gesture accepted");uint32_t id=0;float value=0;
    check(chain.popEdit(0,id,value)&&id==7&&value==.625f,"Old facade overload still consumes exact value");
  }
  {
    // Real vendor callbacks enter the shared take before any coalescing.
    // The clock coverage is controlled: this checks notification/capture
    // integration and original-time mapping, not device presentation latency.
    NativePlugin plugin(state,96000,true);
    const auto catalog=plugin.parameters();
    const std::array<PluginState,1> rack{state};
    const std::array<RecordedGestureTake::Provider,1> providers{{{state.instanceID,plugin.editSource()}}};
    NativeSong native;
    check(send(.125)==1,"Pre-arm fixture gesture failed");
    Sleep(2);
    const auto arm=ScreamSeq::hostTime100ns();
    auto timeline=std::make_shared<RecordingFrameTimeline>();
    check(timeline->publish(arm,arm+1,0,1,96000,29),"Native take clock failed to start");
    RecordedGestureTake take("vendor-take","revision",arm,timeline,rack,{},providers);
    check(send(.25)==1,"First recorded fixture gesture failed");
    Sleep(2);
    check(send(.75)==1,"Second recorded fixture gesture failed");
    uint64_t release=0;
    // This synchronous fixture's notifications all originate on the vendor
    // UI owner. Its FIFO barrier precedes seal; an empty poll alone is not it.
    WindowsVST3::pluginMainCall([&]{release=ScreamSeq::hostTime100ns();});
    check(release>arm+1,"Vendor release did not advance the host clock");
    take.seal(release);
    check(timeline->publish(arm+1,release,1,959,96000,29),"Native take coverage did not advance");
    timeline->close(29,release);
    Sleep(2);
    check(send(.5)==1,"Post-release fixture gesture failed");
    const auto delivery=ScreamSeq::hostTime100ns();
    check(!timeline->locate(delivery,29),"Delayed delivery accidentally remained in recording coverage");
    PluginEdit event;PluginEditSource source;unsigned captured=0,outside=0;
    while(plugin.popEdit(event,source)) {
      check(source==providers[0].source,"Native take changed notification provider");
      const auto accepted=take.append(state.instanceID,event,source,"revision",29,native,catalog);
      if(accepted==RecordedGestureTake::Intake::Captured)++captured;
      else {check(accepted==RecordedGestureTake::Intake::Outside,"Native take refused valid vendor input");++outside;}
    }
    check(captured==2&&outside==2&&take.eventCount()==2&&take.targetCount()==1,
      "Native take lost delayed input or included pre-arm/post-release input");
    check(source==providers[0].source&&take.observeProvider(state.instanceID,source,"revision",29),
      "Empty paired pop lost provider evidence");
    bool refused=false;
    try{(void)take.preparedOwners("revision",29);}catch(const std::invalid_argument &){refused=true;}
    check(refused,"An ordinary empty poll was mistaken for the native release barrier");
    check(take.drained(state.instanceID,source,"revision",29),"Native release drain was refused");
    const auto events=take.target(0).events();
    check(events[0].value==.25f&&events[1].value==.75f&&
      events[0].hostTime<events[1].hostTime&&events[1].hostTime<release&&events[0].frame&&events[1].frame,
      "Delayed native callbacks lost their original order, values or clock mapping");
    for(const auto &input:events) {
      const auto expected=uint64_t((1.L+static_cast<long double>(input.hostTime-(arm+1))*959/(release-(arm+1)))/2);
      check(*input.frame==expected,"Native take used delivery time or failed 96k-to-48k canonical conversion");
    }
    check(events[0].frame!=events[1].frame,"Fixture failed to separate its two recorded times");
    std::vector<ParameterChange> saved;
    unsigned publications=0;
    check(take.commit("revision",29,false,[&](auto owners,bool loss){
      const std::array<RecordedGestureCapture::BatchInput,1> inputs{{{owners[0],source.incarnation,catalog}}};
      auto candidate=RecordedGestureCapture::prepareBatch(inputs,"revision",29,rack,native,loss);
      return RecordedGestureCapture::publishBatch(candidate,inputs,[&](const auto &points,bool changed){
        check(changed,"Native take was unexpectedly a no-op");saved=points;++publications;return true;
      });
    })&&take.completed()&&publications==1&&saved==std::vector<ParameterChange>{
      {0,7,.25f,*events[0].frame},{0,7,.75f,*events[1].frame}},
      "Real vendor input did not publish the complete original-time take once");
  }
  {
    auto plugin=platformPluginBackendFactory().create(state,48000,true);
    const auto before=ScreamSeq::hostTime100ns();
    // Preserve Windows' strict original-time input-loss policy. Only the
    // redundant audible FIFO is removed; notification capacity is unchanged.
    WindowsVST3::pluginMainCall([&]{
      for(size_t n=0;n<8192;++n)check(gesture(n%2?.75:.25)==1,"Bounded queue admission changed");
      check(gesture(.5)==0,"Overflow must keep the existing Windows rejection");
    });
    const auto after=ScreamSeq::hostTime100ns();
    check(plugin->droppedEdits()==1,"First queue overflow must be observable");
    check(plugin->failure().reason&&std::string(plugin->failure().reason)=="vst3.editor-queue","Queue overflow must retain the existing fault");
    PluginEdit event;size_t count=0;uint64_t previous=before;
    while(plugin->popEdit(event)){
      check(event.id==7&&event.value==(count%2?.75f:.25f),"Queue preserves accepted order/values on overflow");
      check(before&&event.hostTime>=previous&&event.hostTime<=after,"Queue preserves original ordered timestamps on overflow");
      previous=event.hostTime;++count;
    }
    check(count==8192&&plugin->droppedEdits()==1,"Draining must not reset bounded loss evidence");
    std::array<float,256> pcm;pcm.fill(1);
    beginAudit();const auto okay=plugin->process(pcm.data(),128,0,nullptr,0,{});
    ScreamSeq::AudioAudit::active=false;
    check(!okay&&!ScreamSeq::AudioAudit::allocations&&!ScreamSeq::AudioAudit::deallocations,"Faulted processing remains allocation/free safe");
    for(auto value:pcm)check(value==0,"Overflow retains existing silence-on-fault behavior");
  }
  {
    auto plugin=platformPluginBackendFactory().create(state,48000,true);
    // Drain history notifications while audio is stopped: no second fixed FIFO
    // may accumulate 8192 obsolete values and make an ordinary gesture fatal.
    WindowsVST3::pluginMainCall([&]{
      for(unsigned i=0;i<20000;++i){check(gesture(i%2?.75:.25)==1,"Long editor gesture faulted");
        PluginEdit event;check(plugin->popEdit(event)&&event.value==(i%2?.75f:.25f),"Original editor events were coalesced");}
    });
    std::array<float,256> pcm;pcm.fill(1);
    beginAudit();const auto okay=plugin->process(pcm.data(),128,0,nullptr,0,{});endAudit(okay);
    for(auto value:pcm)check(value==.75f,"Long gesture replayed old audible values");
    check(!plugin->droppedEdits()&&!plugin->failure().reason,"Ordinary stopped gesture created artificial loss or failure");
    const auto saved=plugin->state();auto reopened=platformPluginBackendFactory().create(saved,48000,true);
    pcm.fill(1);check(reopened->process(pcm.data(),128,0,nullptr,0,{}),"Long gesture state did not reopen");
    for(auto value:pcm)check(value==.75f,"Latest editor state did not persist exactly");
  }
  {
    auto plugin=platformPluginBackendFactory().create(state,48000,true);
    // Fill this parameter's 32 nonzero sample-offset points. The editor value
    // waits for space without masking a musical scheduling capacity violation.
    for(unsigned frame=1;frame<=32;++frame)check(plugin->parameter(7,.5,frame),"Full musical queue fixture failed");
    check(send(.375)==1,"Editor value beside musical automation refused");
    std::array<float,128> pcm;pcm.fill(1);
    beginAudit();const auto first=plugin->process(pcm.data(),64,0,nullptr,0,{});endAudit(first);
    check(!plugin->failure().reason&&pcm.back()==.5f,"Deferred editor input displaced the scheduled points or faulted processing");
    pcm.fill(1);beginAudit();const auto second=plugin->process(pcm.data(),64,64,nullptr,0,{});endAudit(second);
    for(auto value:pcm)check(value==.375f,"Deferred editor value never reached the next block");
  }
  {
    auto plugin=platformPluginBackendFactory().create(state,48000,true);
    check(send(std::numeric_limits<double>::quiet_NaN())==0&&plugin->droppedEdits()==1,"Invalid editor value is rejected and counted");
    PluginEdit event;check(!plugin->popEdit(event),"Invalid value never enters capture queue");
    check(plugin->failure().reason&&std::string(plugin->failure().reason)=="vst3.editor-value","Invalid value retains fault evidence");
  }
  std::cout<<"PASS native VST3 original timestamps/order, exact bounded loss, long stopped gestures, latest audible delivery, scheduled-capacity deferral, persistence, invalid-value failure and host C++ allocation/free audit\n";
  return 0;
}catch(const std::exception &error){ScreamSeq::AudioAudit::active=false;std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
