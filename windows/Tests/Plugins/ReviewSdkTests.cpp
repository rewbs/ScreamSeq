#include "windows/Plugins/WindowsVST3.hpp"
#include "windows/Plugins/UiOwner.hpp"
#include "windows/Project/BinaryPlist.hpp"
#include <windows.h>
#include <iostream>
#include <stdexcept>
using namespace Tracker;
static void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
template<class F>void rejects(F f,const char *s){bool threw=false;try{f();}catch(const std::exception&){threw=true;}check(threw,s);}
int main(int argc,char **argv){try{
 check(argc==5,"args");USHORT emulated=0,native=0;check(IsWow64Process2(GetCurrentProcess(),&emulated,&native)&&native==IMAGE_FILE_MACHINE_ARM64&&!emulated,"native ARM64");
 WindowsVST3::configure(argv[1],argv[3]);auto ds=WindowsVST3::rescan(argv[2]);auto &f=platformPluginBackendFactory();PluginState recipe{ds[0]};auto keep=f.create(recipe,48000,true);
 auto dll=GetModuleHandleW(std::filesystem::u8path(ds[0].path).c_str());auto mode=reinterpret_cast<void(*)(int)>(GetProcAddress(dll,"FixtureReviewMode"));auto separate=reinterpret_cast<int(*)()>(GetProcAddress(dll,"FixtureIsSeparate"));check(mode&&separate,"exports");
 auto set=[&](int n){WindowsVST3::pluginMainCall([&]{mode(n);});};auto pcm=[](PluginBackend &p,float gain){std::array<float,128> b;b.fill(1);check(p.process(b.data(),64,0,nullptr,0,{})&&std::abs(b[0]-gain)<1e-6,"restored real DLL PCM");};
 std::string scenario=argv[4];
 if(scenario=="state"){
  check(keep->parameter(7,.25,0),"set gain");auto saved=keep->state();auto root=ScreamSeq::Project::decodePlist(saved.state);root["unknown"]=ScreamSeq::Project::Value{{"nested",ScreamSeq::Project::Value::binary({0,255,3})}};saved.state=ScreamSeq::Project::encodePlist(root);
  {auto p=f.create(saved,48000,true);pcm(*p,.25f);check(ScreamSeq::Project::decodePlist(p->state().state)==root,"state/unknown field roundtrip");}
  set(1);if(separate()){auto p=f.create(recipe,48000,true);auto empty=p->state();check(ScreamSeq::Project::decodePlist(empty.state)["controller"].get_binary().empty(),"no private controller state must be empty DATA");auto q=f.create(empty,48000,true);pcm(*q,.5f);}set(0);
  set(2);{auto p=f.create(recipe,48000,true);pcm(*p,.5f);auto q=f.create(saved,48000,true);pcm(*q,.25f);}set(0);
  set(3);rejects([&]{keep->state();},"required component getState failure ignored");rejects([&]{f.create(recipe,48000,true);},"initial required component getState failure ignored");set(0);
  set(7);rejects([&]{f.create(saved,48000,true);},"required nonempty component setState failure ignored");set(0);
  set(8);rejects([&]{f.create(recipe,48000,true);},"unexpected controller sync failure ignored");set(0);
  if(separate()){
   set(4);rejects([&]{keep->state();},"unexpected controller getState failure ignored");set(0);
   set(6);rejects([&]{f.create(saved,48000,true);},"unexpected controller setState failure ignored");set(0);
   set(5);auto p=f.create(saved,48000,true);pcm(*p,.25f);check(ScreamSeq::Project::decodePlist(p->state().state)==root,"opaque unsupported controller bytes lost");p.reset();set(0);
  }
 }
 else if(scenario=="lifecycle"){
  auto metric=reinterpret_cast<int(*)(int)>(GetProcAddress(dll,"FixtureReviewMetric"));auto count=reinterpret_cast<int(*)(int)>(GetProcAddress(dll,"FixtureCount"));check(metric&&count,"metrics exports");
  for(int fault:{11,10}){
   if(fault==10&&!separate())continue;
   std::array<int,10> before{};std::array<int,4> live{};for(int i=0;i<10;++i)before[i]=metric(i);for(int i=0;i<4;++i)live[i]=count(i);
   set(fault);rejects([&]{f.create(recipe,48000,true);},"failed initialization accepted");set(0);
   for(int i=0;i<4;++i)check(live[i]==count(i),"failed initialization leaked references");
   check(metric(0)==before[0]&&metric(7)==before[7]&&metric(8)==before[8],"handler called after failed controller initialize");
   check(metric(1)==before[1]+1&&metric(9)==before[9]+1&&metric(2)==before[2],"failed initialize must release exactly once without terminate");
  }
 }
 else if(scenario=="phase"){
  auto metric=reinterpret_cast<int(*)(int)>(GetProcAddress(dll,"FixtureReviewMetric"));check(metric!=nullptr,"phase metrics");
  std::array<int,4> before{};for(int i=0;i<4;++i)before[i]=metric(i+3);
  set(20);{auto p=f.create(recipe,48000,true);check(p->parameters().size()==1,"Created-phase query silently lost separate controller");pcm(*p,.5f);}set(0);
  check(metric(3)==before[0]&&metric(4)==before[1]&&metric(5)==before[2]+1&&metric(6)==before[3]+1,"Created-only SDK methods called after initialize or missing");
  for(int fault:{21,22}){set(fault);rejects([&]{f.create(recipe,48000,true);},"unexpected Created-phase error ignored");set(0);}
  for(int optional:{23,24,25}){set(optional);auto p=f.create(recipe,48000,true);pcm(*p,.5f);if(optional==23)check(p->parameters().size()==1,"optional IoMode lost controller");p.reset();set(0);}
 }
 else if(scenario=="catalog"){
  set(30);rejects([&]{f.create(recipe,48000,true);},"advertised parameter metadata failure accepted as partial catalog");set(0);
  set(31);{auto p=f.create(recipe,48000,true);check(p->parameters().empty(),"legitimate zero-parameter controller rejected");pcm(*p,.5f);}set(0);
  if(!separate()){
   auto p=f.create(PluginState{ds[3]},48000,true);check(p->programs().size()==6,"program baseline");
   for(int fault:{30,32,33,34}){set(fault);rejects([&]{p->programs();},"advertised program metadata error silently skipped");set(0);}
  }
 }
 else throw std::runtime_error("unknown scenario");
 std::cout<<"PASS native ARM64 real DLL "<<scenario<<" separate="<<separate()<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
