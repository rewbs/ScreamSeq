#include "windows/Plugins/WindowsVST3.hpp"
#include "windows/Plugins/UiOwner.hpp"
#include "windows/Project/BinaryPlist.hpp"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace Tracker;
static void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
int main(int argc,char **argv){try{
 check(argc==4,"args");WindowsVST3::configure(argv[1],argv[3]);auto ds=WindowsVST3::rescan(argv[2]);check(ds.size()==4,"separate factory controller must not be a discovered effect");auto &factory=platformPluginBackendFactory();
 auto keep=factory.create(PluginState{ds[0]},48000,true);HMODULE dll=GetModuleHandleW(std::filesystem::u8path(ds[0].path).c_str());auto connections=reinterpret_cast<int(*)()>(GetProcAddress(dll,"FixtureConnections"));auto mode=reinterpret_cast<void(*)(int)>(GetProcAddress(dll,"FixtureFailure"));auto count=reinterpret_cast<int(*)(int)>(GetProcAddress(dll,"FixtureCount"));check(connections&&mode&&count,"exports");check(connections()==2&&count(0)==2,"separate component/controller connection");
 auto s=keep->state();auto root=ScreamSeq::Project::decodePlist(s.state);check(root["component"].get_binary().size()==4&&root["controller"].get_binary().size()==8,"separate state stream distinction");
 double marker=.9375;std::vector<uint8_t> bytes(8);memcpy(bytes.data(),&marker,8);root["controller"]=ScreamSeq::Project::Value::binary(bytes);root["unknown"]=ScreamSeq::Project::Value::array({"retain",42});s.state=ScreamSeq::Project::encodePlist(root);
 {auto copy=factory.create(s,48000,true);auto read=ScreamSeq::Project::decodePlist(copy->state().state);check(read["controller"]==root["controller"]&&read["unknown"]==root["unknown"],"controller/unknown state roundtrip");std::array<float,34> pcm;pcm.fill(1);check(copy->process(pcm.data(),17,0,nullptr,0,{}),"separate controller processing");check(pcm[0]==.5f,"separate component PCM");check(connections()==4,"independent connections");}
 check(connections()==2&&count(0)==2,"disconnect/releases");
 WindowsVST3::pluginMainCall([&]{mode(5);});bool rejected=false;try{factory.create(PluginState{ds[0]},48000,true);}catch(const std::exception&){rejected=true;}WindowsVST3::pluginMainCall([&]{mode(0);});check(rejected,"failed controller connection accepted");check(connections()==2&&count(0)==2,"failed connection leaked partial objects");
 std::cout<<"PASS separate controller ABI, bidirectional connection, component/controller DATA, unknown-state retention, failed connection cleanup\n";
 return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
