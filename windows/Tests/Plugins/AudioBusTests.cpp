#include "windows/Plugins/WindowsVST3.hpp"
#include "windows/Plugins/UiOwner.hpp"
#include "windows/Plugins/NativeArchitecture.hpp"
#include <fstream>
#include <filesystem>
#include "editor/hosted/HostedAudio.hpp"
#include "windows/Audio/RealtimeAudit.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include "soundlib/plugins/PlugInterface.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace Tracker;
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static void tracker_audit_begin(){ScreamSeq::AudioAudit::allocations=0;ScreamSeq::AudioAudit::deallocations=0;ScreamSeq::AudioAudit::active=true;}
static void auditEnd(bool ok){ScreamSeq::AudioAudit::active=false;check(ok,"Native VST3 channel-pair processing succeeds");check(!ScreamSeq::AudioAudit::allocations&&!ScreamSeq::AudioAudit::deallocations,"No host C++ allocation/free in native VST3 bus processing");}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *locks){ScreamSeq::AudioAudit::active=false;*a=ScreamSeq::AudioAudit::allocations;*f=ScreamSeq::AudioAudit::deallocations;*locks=0;}
#include "editor/Tests/HostedCopyMigrationChecks.hpp"
#include "editor/Tests/HostedStageRoutingChecks.hpp"
#include "editor/Tests/NativeWideBusChecks.hpp"
#include "editor/Tests/ProviderLatencyChecks.hpp"
#include "editor/Tests/LiveLatencyChecks.hpp"
#include "editor/Tests/HostedRackPresetChecks.hpp"
#include "editor/Tests/ColumnMuteOwnershipChecks.hpp"
int main(int argc,char **argv){try{
  check(argc==4,"Scanner, fixture and cache paths required");
  WindowsVST3::configure(argv[1],argv[3]);const auto descriptors=WindowsVST3::rescan(argv[2]);check(!descriptors.empty(),"Actual VST3 fixture discovered");
  check(descriptors.size()>1&&descriptors[1].instrument,"Queued mute fixture instrument is missing");
  for(auto rate:{44100u,48000u,96000u})for(auto block:{17u,128u,511u})for(bool sample:{false,true})for(bool samePitch:{false,true})
    columnMuteOwnershipChecks(descriptors[1],rate,block,sample,samePitch);
  std::cout<<"PASS queued column mute: sample/plugin NNA, independent equal-pitch ownership and audition at 3 rates/3 callback partitions; host C++ allocation/free audit\n";
  const auto wrongMachine=std::filesystem::u8path(argv[3]).parent_path()/L"audio-bus-wrong-machine.vst3";
  std::filesystem::copy_file(std::filesystem::u8path(argv[2]),wrongMachine,std::filesystem::copy_options::overwrite_existing);
  {std::fstream file(wrongMachine,std::ios::in|std::ios::out|std::ios::binary);file.seekg(0x3c);int32_t pe=0;file.read(reinterpret_cast<char*>(&pe),4);file.seekp(pe+4);const uint16_t machine=WindowsVST3::nativeMachine==IMAGE_FILE_MACHINE_ARM64?IMAGE_FILE_MACHINE_AMD64:IMAGE_FILE_MACHINE_ARM64;file.write(reinterpret_cast<const char*>(&machine),2);check(bool(file),"Prepare incompatible PE fixture");}
  bool wrongRejected=false;try{const auto path=wrongMachine.u8string();WindowsVST3::rescan(std::string(reinterpret_cast<const char*>(path.data()),path.size()));}catch(const std::exception &e){wrongRejected=std::string(e.what()).find("Incompatible VST3 architecture")!=std::string::npos;}
  std::filesystem::remove(wrongMachine);check(wrongRejected,"Reject incompatible PE architecture before loading vendor code");
  auto keep=std::make_unique<NativePlugin>(PluginState{descriptors[0]},48000,true);
  auto dll=LoadLibraryW(std::filesystem::u8path(descriptors[0].path).c_str());check(dll,"Actual VST3 fixture DLL loaded");struct FixtureModule {HMODULE handle;~FixtureModule(){FreeLibrary(handle);}} module{dll};
  auto wide=reinterpret_cast<void(*)(bool)>(GetProcAddress(dll,"ResonanceFixtureWideBuses"));check(wide,"Wide native fixture export available");
  WindowsVST3::pluginMainCall([&]{wide(true);});
  for(uint32_t rate:{44100u,48000u,96000u})for(uint32_t block:{17u,128u,4096u})wideBusChecks(descriptors[0],rate,block);
  WindowsVST3::pluginMainCall([&]{wide(false);});
  auto announce=reinterpret_cast<int(*)(uint32_t)>(GetProcAddress(dll,"ResonanceFixtureLatency"));
  auto activations=reinterpret_cast<uint64_t(*)()>(GetProcAddress(dll,"ResonanceFixtureActivationCalls"));check(announce&&activations,"Latency fixture exports");
  providerLatencyChecks(descriptors[0],[&](uint32_t frames){WindowsVST3::pluginMainCall([&]{announce(frames);});},[&]{uint64_t count=0;WindowsVST3::pluginMainCall([&]{count=activations();});return count;});
  keep.reset();
  PluginState effect{descriptors[0]};effect.instanceID="windows-live-latency";
  liveLatencyChecks(effect,[&](uint32_t frames){int count=0;WindowsVST3::pluginMainCall([&]{count=announce(frames);});return count;},[&]{uint64_t count=0;WindowsVST3::pluginMainCall([&]{count=activations();});return count;});
  WindowsVST3::pluginMainCall([&]{announce(0);});
  for(auto rate:{44100u,48000u,96000u})for(auto block:{17u,128u,4096u}){hostedStageRouting(descriptors[0],rate,block);hostedStageFollower(descriptors[0],rate,block);}
  for(auto rate:{44100u,48000u,96000u})for(auto block:{17u,128u,4096u})for(bool sample:{false,true})hostedCopyMigration(descriptors[0],rate,block,sample);
  for(auto rate:{44100u,48000u,96000u})for(auto block:{17u,512u,4096u}){hostedRackPreset(descriptors[0],rate,block);hostedRackPreset(descriptors[0],rate,block,true);hostedPresetRamp(descriptors[0],rate,block);}
  std::cout<<"PASS actual Windows VST3 5/3-channel buses: all pairs, odd mono, inactive capacity, state identity, timed automation; 44.1/48/96 kHz, 17/128/4096 frames, host C++ allocation/free audit; live ordinary/sample graph add, Amount, remove/Undo, rejected publication and render clock; live rack/recipe 0/13/3-sample PDC without vendor activation; live rack preset/restore, subsequent routing and manual fence, ongoing ramp preservation; typed aggregate stage cables and exact auxiliary-output follower PCM\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
