#include "windows/Plugins/WindowsVST3.hpp"
#include "windows/Plugins/UiOwner.hpp"
#include "windows/Project/BinaryPlist.hpp"
#include "editor/hosted/HostedAudio.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <cmath>
using namespace Tracker;
static void check(bool ok,const char *what){if(!ok)throw std::runtime_error(what);}
template<class F>static void rejects(F f,const char *what){bool did=false;try{f();}catch(const std::exception&){did=true;}check(did,what);}
static void expectPCM(float x,double y){check(std::abs(x-y)<1e-6,"PCM differs");}
static std::wstring wide(const std::string &s){auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),w.data(),n);return w;}
static std::string utf(const std::filesystem::path &p){auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
template<class T>static T symbol(HMODULE dll,const char *name){auto p=reinterpret_cast<T>(GetProcAddress(dll,name));check(p!=nullptr,"fixture export missing");return p;}
int main(int argc,char **argv){try{
 check(argc==4,"arguments");USHORT emulated=0,native=0;check(IsWow64Process2(GetCurrentProcess(),&emulated,&native)&&native==IMAGE_FILE_MACHINE_ARM64&&emulated==IMAGE_FILE_MACHINE_UNKNOWN,"not running native ARM64");std::cout<<"native-machine=ARM64, process=not-emulated\n";WindowsVST3::configure(argv[1],argv[3]);
 auto descriptors=WindowsVST3::rescan(argv[2]);check(descriptors.size()==4,"real fixture DLL must expose four exact classes");
 check(descriptors[0].classID=="5245534F4E414E434546464543540001","canonical FUID class identity");
 PluginState effect{descriptors[0]};auto &factory=platformPluginBackendFactory();
 for(const auto &bad:{std::string(31,'0'),std::string(33,'0'),std::string(32,'z'),std::string(" ")+effect.descriptor.classID,effect.descriptor.classID+"-"}){auto s=effect;s.descriptor.classID=bad;rejects([&]{factory.create(s,48000,true);},"malformed ID accepted");}
 auto wrong=effect;wrong.descriptor.classID=std::string(32,'0');rejects([&]{factory.create(wrong,48000,true);},"class mismatch accepted");wrong=effect;wrong.descriptor.path="/Library/Audio/Plug-Ins/VST3/missing.vst3";rejects([&]{factory.create(wrong,48000,true);},"foreign Mac path retargeted");wrong=effect;wrong.descriptor.format="AU";wrong.state={std::byte{3}};rejects([&]{factory.create(wrong,48000,true);},"AU unexpectedly available");check(wrong.state.size()==1,"AU state changed");
 auto plugin=factory.create(effect,48000,true);std::array<float,8192> pcm{};std::array<const float*,64> inputs{};
 pcm.fill(1);check(plugin->process(pcm.data(),17,0,inputs.data(),0,{}),"real effect processing");for(int i=0;i<34;++i)expectPCM(pcm[i],.5);
 HMODULE dll=GetModuleHandleW(wide(descriptors[0].path).c_str());check(dll!=nullptr,"real DLL is not loaded");
 std::cout<<"PASS ARM64 loader, cache identity, malformed IDs, missing/AU rejection, effect PCM\n";
 check(plugin->supportsSampleOffsetParameters(),"sample-offset capability");
 constexpr double lo=.123456789012345,hi=.987654321098765;
 check(plugin->parameter(7,lo,0)&&plugin->parameter(7,hi,16),"double ramp endpoints rejected");pcm.fill(1);check(plugin->process(pcm.data(),17,17,inputs.data(),0,{}),"ramp processing");for(int i=0;i<17;++i)expectPCM(pcm[i*2],lo+(hi-lo)*i/16);
 auto q=symbol<double(*)(int)>(dll,"FixtureQueueValue");check(q(0)==lo&&q(1)==hi,"VST3 lost double precision before queue");
 pcm.fill(1);check(plugin->process(pcm.data(),17,34,inputs.data(),0,{}),"queue reset");for(int i=0;i<34;++i)expectPCM(pcm[i],hi);
 check(!plugin->parameter(7,.5,4096),"parameter offset outside prepared block accepted");
 std::cout<<"PASS precise double endpoints, queue consumption and offset bounds\n";
 auto output=symbol<void(*)(bool)>(dll,"FixtureRequireOutput");output(true);pcm.fill(1);check(plugin->process(pcm.data(),17,51,inputs.data(),0,{}),"output event sink missing");output(false);
 auto saved=plugin->state();auto root=ScreamSeq::Project::decodePlist(saved.state);check(root["component"].is_binary()&&!root["component"].get_binary().has_subtype(),"component is not ordinary plist data");check(root["controller"].is_binary(),"controller stream missing");root["future"]=ScreamSeq::Project::Value{{"nested",ScreamSeq::Project::Value::array({1,"opaque"})}};saved.state=ScreamSeq::Project::encodePlist(root);auto restored=factory.create(saved,48000,true);check(ScreamSeq::Project::decodePlist(restored->state().state)["future"]==root["future"],"unknown inner state lost");pcm.fill(1);check(restored->process(pcm.data(),17,0,inputs.data(),0,{}),"restored processing");expectPCM(pcm[0],hi);
 auto typed=root;typed["component"]=ScreamSeq::Project::Value::binary({0,0,0,0},uint64_t(ScreamSeq::Project::OpaqueType::UID));saved.state=ScreamSeq::Project::encodePlist(typed);rejects([&]{factory.create(saved,48000,true);},"opaque subtype accepted as DATA");
 std::cout<<"PASS output events, binary-plist state and unknown values\n";
 PluginState side=effect;side.auxiliaryInputs={1};auto sidechain=factory.create(side,48000,true);std::array<float,8192> aux{};aux.fill(99);for(int i=7;i<24;++i){aux[i*2]=.25;aux[i*2+1]=.75;}inputs[1]=aux.data();pcm.fill(1);check(sidechain->process(pcm.data(),17,0,inputs.data(),7,{}),"sidechain processing");for(int i=0;i<34;++i)expectPCM(pcm[i],.75);inputs[1]=nullptr;
 PluginState inst{descriptors[1]};inst.auxiliaryOutputs={1,2,31};auto instrument=factory.create(inst,48000,true);check(instrument->midi(0x91,60,100),"instrument note");pcm.fill(0);check(instrument->process(pcm.data(),17,0,inputs.data(),7,{}),"instrument processing");expectPCM(pcm[0],.1);expectPCM(instrument->auxiliaryOutput(1)[0],.2);expectPCM(instrument->auxiliaryOutput(1)[1],.2);expectPCM(instrument->auxiliaryOutput(2)[1],-.15);expectPCM(instrument->auxiliaryOutput(31)[0],3.2);
 check(instrument->midi(0xb1,123,0),"all notes off");check(instrument->process(pcm.data(),17,17,inputs.data(),0,{}),"note off processing");expectPCM(pcm[0],0);check(!instrument->midi(0x90,255,127),"malformed MIDI accepted");
 std::cout<<"PASS sidechain whole-block offset, mono/stereo auxiliary slice outputs, instrument and channel note-off\n";
 NativePlugin precise(inst,48000,true);precise.prepareMusicalMIDI();precise.prepareMusicalAutomation();check(precise.scheduleMIDI(0x90,60,127,7)&&precise.scheduleMIDI(0x80,60,0,23),"precise MIDI schedule");check(precise.scheduleRamp(7,.25,.75,7,16),"shared ramp schedule");pcm.fill(0);check(precise.process(pcm.data(),41,0),"shared precise processing");for(int i=0;i<41;++i)expectPCM(pcm[i*2],i>=7&&i<23?.2*(.25+.5*(i-7)/16):0);
 auto observe=symbol<void(*)(bool)>(dll,"ResonanceFixtureObserve");auto errors=symbol<uint64_t(*)()>(dll,"ResonanceFixtureClockErrors");observe(true);PluginTransport t;t.beat=123*2./48000;plugin->transport(t);pcm.fill(0);check(plugin->process(pcm.data(),17,123,inputs.data(),0,t),"current transport process");check(errors()==0,"stale transport");observe(false);
 PluginState prog{descriptors[3]};auto programs=factory.create(prog,48000,true);check(programs->programs().size()==6,"program catalog");programs->loadProgram("vst3:0:17:2");programs->loadProgram("vst3:7:18:2");pcm.fill(1);check(programs->process(pcm.data(),17,0,inputs.data(),0,{}),"program processing");expectPCM(pcm[0],.75);
 auto delayed=factory.create(PluginState{descriptors[2]},48000,true);check(delayed->latency()==32./48000,"reported latency");
 std::cout<<"PASS shared precise MIDI/ramp, current transport, programs and latency\n";
 auto gesture=symbol<int(*)(double)>(dll,"ResonanceFixtureGesture");WindowsVST3::pluginMainCall([&]{check(gesture(.375)>0,"handler edit rejected");});uint32_t id=0;float value=0;check(plugin->popEdit(id,value)&&id==7&&value==.375f,"commit-value edit queue");
 auto overflow=factory.create(effect,48000,true);for(int i=0;i<32;++i)check(overflow->parameter(7,.5,i),"parameter queue fills");check(!overflow->parameter(7,.5,32),"parameter overflow accepted");pcm.fill(1);check(!overflow->process(pcm.data(),64,0,inputs.data(),0,{}),"queue overflow failed to fault");for(int i=0;i<128;++i)expectPCM(pcm[i],0);
 std::cout<<"PASS editor commit queue and overflow fault/silence\n";
 auto outputMode=symbol<void(*)(int)>(dll,"FixtureOutputMode");
 for(int mode=1;mode<=3;++mode){auto fresh=factory.create(effect,48000,true);outputMode(mode);pcm.fill(1);check(!fresh->process(pcm.data(),64,0,nullptr,0,{}),"vendor output overflow not faulted");for(int i=0;i<128;++i)expectPCM(pcm[i],0);outputMode(0);}
 auto fresh=factory.create(effect,48000,true);outputMode(4);check(fresh->process(pcm.data(),64,0,nullptr,0,{}),"output parameter process");check(fresh->parameters()[0].value==.875f,"output parameters not consumed");outputMode(0);
 std::cout<<"PASS bounded output event/parameter sinks and output values\n";
 auto pitch=symbol<void(*)(bool)>(dll,"ResonanceFixturePitchMode");pitch(true);{auto mapped=factory.create(inst,48000,true);check(mapped->midi(0x90,60,100)&&mapped->midi(0xe0,0,0),"mapped MIDI bend");check(mapped->process(pcm.data(),17,0,nullptr,0,{}),"mapped MIDI process");expectPCM(pcm[0],0);check(mapped->midi(0xe0,0,64),"center pitch bend");check(mapped->process(pcm.data(),17,17,nullptr,0,{}),"center bend process");expectPCM(pcm[0],.1);}pitch(false);
 for(uint32_t rate:{44100u,48000u,96000u})for(uint32_t frames:{17u,128u,4096u}){auto p=factory.create(effect,rate,true);check(p->parameter(7,lo,0)&&p->parameter(7,hi,frames-1),"rate ramp setup");pcm.fill(1);check(p->process(pcm.data(),frames,0,nullptr,0,{}),"rate block process");for(uint32_t i=0;i<frames;++i)expectPCM(pcm[i*2],lo+(hi-lo)*i/(frames-1));}
 auto duplicate=effect;duplicate.auxiliaryInputs={1,1};rejects([&]{factory.create(duplicate,48000,true);},"duplicate aux bus");duplicate.auxiliaryInputs={64};rejects([&]{factory.create(duplicate,48000,true);},"bus index 64");
 auto readOnlyMode=symbol<void(*)(int)>(dll,"ResonanceFixtureProgramMode");readOnlyMode(2);{auto readonly=factory.create(prog,48000,true);check(!readonly->parameter(100,.5,0),"read-only parameter accepted");check(readonly->parameters()[1].step==.5f,"step metadata lost");}readOnlyMode(0);
 std::cout<<"PASS actual MIDI mapping, 44.1/48/96 kHz at 17/128/4096 frames, aux validation and read-only metadata\n";
 for(uint32_t block:{17u,128u,4096u}){NativePlugin ramped(effect,48000,true);ramped.prepareMusicalAutomation();check(ramped.scheduleRamp(7,lo,hi,0,4095),"partition ramp schedule");for(uint32_t pos=0;pos<4096;){const auto n=std::min(block,4096-pos);pcm.fill(1);check(ramped.process(pcm.data(),n,pos),"partition shared process");for(uint32_t i=0;i<n;++i)expectPCM(pcm[i*2],lo+(hi-lo)*(pos+i)/4095);pos+=n;}}
 {auto same=factory.create(effect,48000,true);check(same->parameter(7,.25,0)&&same->parameter(7,.75,0),"same-offset replacement");pcm.fill(1);check(same->process(pcm.data(),17,0,nullptr,0,{}),"replacement process");expectPCM(pcm[0],.75);}
 std::cout<<"PASS shared callback partition ramp and same-offset replacement\n";
 auto effectDelay=symbol<void(*)(bool)>(dll,"ResonanceFixtureEffectDelay");effectDelay(true);{auto delay=factory.create(effect,48000,true);check(delay->latency()==32./48000&&delay->tail()==32./48000,"effect latency/tail seconds");pcm.fill(0);pcm[0]=pcm[1]=1;check(delay->process(pcm.data(),64,0,nullptr,0,{}),"latency PCM process");for(int i=0;i<64;++i)expectPCM(pcm[i*2],i==32?.5:0);}effectDelay(false);
 std::cout<<"PASS actual 32-frame effect delay PCM and latency/tail reporting\n";
 return 0;
 }catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
