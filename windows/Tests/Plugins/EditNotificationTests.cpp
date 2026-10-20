#include "windows/Plugins/WindowsVST3.hpp"
#include "windows/Plugins/UiOwner.hpp"
#include "windows/Audio/HostClock.hpp"
#include "windows/Audio/RealtimeAudit.hpp"
#include "editor/hosted/HostedAudio.hpp"
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
  // exactly one native instance and overflow does not poison later checks.
  auto dll=LoadLibraryW(std::filesystem::u8path(state.descriptor.path).c_str());
  check(dll,"Native fixture loaded");
  struct Module {HMODULE handle;~Module(){FreeLibrary(handle);}} module{dll};
  auto gesture=reinterpret_cast<int(*)(double)>(GetProcAddress(dll,"ResonanceFixtureGesture"));
  check(gesture,"Native fixture gesture export");
  auto send=[&](double value){int accepted=-1;WindowsVST3::pluginMainCall([&]{accepted=gesture(value);});return accepted;};
  {
    PluginChain chain({state},48000,true);
    const auto before=ScreamSeq::hostTime100ns();
    check(send(.375)==1,"Fixture gesture accepted");
    const auto after=ScreamSeq::hostTime100ns();
    Sleep(2);const auto drain=ScreamSeq::hostTime100ns();
    PluginEdit event;
    check(chain.popEdit(0,event)&&event.id==7&&event.value==.375f,"Rack facade preserves accepted value");
    check(before&&event.hostTime>=before&&event.hostTime<=after&&event.hostTime<drain,"Rack facade preserves original callback time across delayed drain");
    check(!chain.popEdit(0,event)&&!chain.popEdit(1,event),"Notification consumed exactly once");
    check(chain.droppedEdits(0)==0,"Ordinary notification is not reported lost");
    std::array<float,256> pcm;pcm.fill(1);
    beginAudit();const auto okay=chain.process(pcm.data(),128);endAudit(okay);
    for(auto value:pcm)check(value==.375f,"Timestamped drain preserves audible parameter delivery");
    check(send(.625)==1,"Second gesture accepted");uint32_t id=0;float value=0;
    check(chain.popEdit(0,id,value)&&id==7&&value==.625f,"Old facade overload still consumes exact value");
  }
  {
    auto plugin=platformPluginBackendFactory().create(state,48000,true);
    const auto before=ScreamSeq::hostTime100ns();
    // Existing Windows policy faults when either notification/audio queue fills.
    // Count loss without changing capacity or pretending post-fault edits apply.
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
    check(send(std::numeric_limits<double>::quiet_NaN())==0&&plugin->droppedEdits()==1,"Invalid editor value is rejected and counted");
    PluginEdit event;check(!plugin->popEdit(event),"Invalid value never enters capture queue");
    check(plugin->failure().reason&&std::string(plugin->failure().reason)=="vst3.editor-value","Invalid value retains fault evidence");
  }
  std::cout<<"PASS native VST3 callback timestamps through rack, delayed polling, legacy overload, exact 8192-entry capacity/order/loss, invalid value, audible/fault PCM and host C++ allocation/free audit\n";
  return 0;
}catch(const std::exception &error){ScreamSeq::AudioAudit::active=false;std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
