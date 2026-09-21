#include "windows/Plugins/WindowsVST3.hpp"
#include "windows/Plugins/UiOwner.hpp"
#include "windows/Project/BinaryPlist.hpp"
#include "editor/hosted/HostedAudio.hpp"
#include "mac/Audio/NativeSignalGraph.hpp"
#include <iostream>
#include <stdexcept>
#include <windows.h>
using namespace Tracker;
static void check(bool b,const char *why){if(!b)throw std::runtime_error(why);}
template<class F>static void rejects(F f,const char *why){bool threw=false;try{f();}catch(const std::exception &){threw=true;}check(threw,why);}
int main(int argc,char **argv){try{
 check(argc==4,"args");WindowsVST3::configure(argv[1],argv[3]);auto ds=WindowsVST3::rescan(argv[2]);auto &f=platformPluginBackendFactory();PluginState effect{ds[0]};
 auto keep=f.create(effect,48000,true);auto path=std::filesystem::u8path(ds[0].path);HMODULE dll=GetModuleHandleW(path.c_str());check(dll!=nullptr,"DLL loaded");
 auto mode=reinterpret_cast<void(*)(int)>(GetProcAddress(dll,"FixtureFailure"));auto count=reinterpret_cast<int(*)(int)>(GetProcAddress(dll,"FixtureCount"));auto bounds=reinterpret_cast<void(*)(bool)>(GetProcAddress(dll,"FixtureStreamBounds"));check(mode&&count&&bounds,"exports");
 std::array<int,4> baseline{};for(int i=0;i<4;++i)baseline[i]=count(i);
 for(int failure=1;failure<=4;++failure){WindowsVST3::pluginMainCall([&]{mode(failure);});rejects([&]{f.create(effect,48000,true);},"initialization failure accepted");WindowsVST3::pluginMainCall([&]{mode(0);});for(int i=0;i<4;++i)check(count(i)==baseline[i],"partial initialization leaked active/object refs");check(count(5)==0,"wrong lifecycle unwind order");}
 std::cout<<"PASS reverse-order initialization failure cleanup\n";
 WindowsVST3::pluginMainCall([&]{bounds(true);});auto state=keep->state();WindowsVST3::pluginMainCall([&]{bounds(false);});check(!state.state.empty(),"bounded state streams");
 std::cout<<"PASS retained stream refs, extreme seeks and invalid seek modes\n";
 int before=count(4);
 {auto rack=std::make_unique<PluginChain>(std::vector{effect},48000,true);
 NativeSong native;native.patterns[0].id=3;native.tracks[0].id=1;native.tracks[1].id=4;
 native.mixer.buses={{1,2,MixerBusKind::Track,"One"},{4,2,MixerBusKind::Track,"Two"},{2,0,MixerBusKind::Master,"Master"}};
 SignalDefinition graph;graph.id=100;graph.number=1;graph.name="Native DLL";
 graph.nodes={{101,SignalNodeKind::Input,"Input"},{102,SignalNodeKind::Plugin,"Effect"},{103,SignalNodeKind::Output,"Output"}};
 const auto &p=ds[0];graph.nodes[1].plugin={p.format,p.name,p.path,p.classID,p.type,p.subtype,p.manufacturer};graph.audio={{101,102},{102,103}};
 native.signal.library={graph};native.signal.assignments={{1,100,1,1},{4,100,1,1}};
 auto instances=std::make_unique<NativeSignalGraph>(native,48000,true);check(count(4)==before+3,"rack/graph did not create independent real DLL objects");
 auto entries=reinterpret_cast<int(*)()>(GetProcAddress(dll,"FixtureModuleEntries"));check(entries&&entries()==1,"InitDll repeated while shared module still loaded");
 check(rack->parameter(0,7,.25f),"rack edit");std::array<float,128> pcm;pcm.fill(1);check(rack->process(pcm.data(),64),"rack process");check(pcm[0]==.25f,"rack PCM");
 for(size_t target=0;target<2;++target){pcm.fill(1);check(instances->process(target,pcm.data(),64,0,{}),"real graph target render");check(pcm[0]==.5f,"rack edit leaked into graph target PCM");}
 auto separate=f.create(effect,48000,true);pcm.fill(1);check(separate->process(pcm.data(),64,0,nullptr,0,{}),"independent PCM");check(pcm[0]==.5f,"processor state leaked across instances");}
 for(int i=0;i<4;++i)check(count(i)==baseline[i],"rack/graph teardown leaked refs");
 std::cout<<"PASS real rack plus two graph target constructors and independent DSP state\n";
 auto restart=reinterpret_cast<int(*)(int)>(GetProcAddress(dll,"FixtureRestart"));WindowsVST3::pluginMainCall([&]{check(restart(1<<3)==1,"latency restart dispatch");});
 check(keep->latencyChangePending(),"latency maintenance was not requested");
 keep->refreshLatency();check(!keep->latencyChangePending(),"latency maintenance not cleared");
 std::array<float,128> pcm;pcm.fill(1);check(keep->process(pcm.data(),64,0,nullptr,0,{}),"latency notification poisoned processing");
 WindowsVST3::pluginMainCall([&]{restart(1<<1);});pcm.fill(1);
 check(!keep->process(pcm.data(),64,64,nullptr,0,{}),"I/O change continued with stale buffers");for(auto v:pcm)check(v==0,"I/O restart fault did not silence");
 std::cout<<"PASS latency maintenance and unsupported I/O restart fault/silence\n";
 auto pin=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);check(pin!=nullptr,"test DLL pin");auto entries=reinterpret_cast<int(*)()>(GetProcAddress(dll,"FixtureModuleEntries"));keep.reset();for(int i=0;i<4;++i)check(count(i)==0,"last instance teardown leak");check(entries()==0&&count(5)==0,"ExitDll/lifecycle imbalance");FreeLibrary(pin);
 std::cout<<"PASS last-instance ExitDll and full object teardown\n";
 return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
