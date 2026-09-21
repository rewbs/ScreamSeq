#include "windows/Plugins/WindowsVST3.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace Tracker;namespace fs=std::filesystem;
static void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
static std::string utf(const fs::path &p){auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
template<class F>static void rejected(F f,const char *message){bool caught=false;try{f();}catch(const std::exception &e){caught=std::string(e.what()).find(message)!=std::string::npos;std::cout<<"rejection: "<<e.what()<<'\n';}check(caught,message);}
int main(int argc,char **argv){try{
 check(argc==7,"args");auto folder=fs::u8path(argv[3]);fs::create_directories(folder);auto cache=folder/L"isolated-cache.json";fs::remove(cache);WindowsVST3::configure(argv[1],utf(cache));auto &factory=platformPluginBackendFactory();check(factory.discover().empty(),"first startup must not execute or scan plugins");
 const auto unicode=folder/L"音源-é-테스트.vst3";fs::copy_file(fs::u8path(argv[2]),unicode,fs::copy_options::overwrite_existing);
 auto ds=WindowsVST3::rescan(utf(unicode));check(ds.size()==4,"UTF-8 native scanner path");check(GetModuleHandleW(unicode.c_str())==nullptr,"scanner loaded DLL into calling process");
 WindowsVST3::configure(utf(folder/L"intentionally-missing-scanner.exe"),utf(cache));check(factory.discover().size()==4,"cached discovery tried to execute scanner");
 {auto p=factory.create(PluginState{ds[0]},48000,true);std::array<float,16> audio;audio.fill(1);check(p->process(audio.data(),8,0,nullptr,0,{}),"Unicode DLL DSP");check(audio[0]==.5f,"Unicode DLL PCM");}
 {std::ofstream append(unicode,std::ios::app|std::ios::binary);append.put('x');}
 rejected([&]{factory.create(PluginState{ds[0]},48000,true);},"hash");WindowsVST3::configure(argv[1],utf(cache));ds=WindowsVST3::rescan(utf(unicode));
 auto wrong=PluginState{ds[0]};wrong.descriptor.classID="00000000000000000000000000000000";rejected([&]{factory.create(wrong,48000,true);},"Exact");
 fs::remove(unicode);rejected([&]{factory.create(PluginState{ds[0]},48000,true);},"canonical");
 for(int fault=0;fault<3;++fault){auto start=GetTickCount64();rejected([&]{WindowsVST3::rescan(argv[4+fault],fault==1?150:3000);},fault==0?"crashed":fault==1?"timeout":"output");check(GetTickCount64()-start<5000,"scanner fault exceeded wall clock bound");}
 check(factory.discover().size()==4,"failed scans destroyed cache");
 // A real ARM64 PE copied then marked AMD64 must reject before LoadLibrary.
 auto x64=folder/L"wrong-architecture.vst3";fs::copy_file(fs::u8path(argv[2]),x64,fs::copy_options::overwrite_existing);
 {std::fstream f(x64,std::ios::in|std::ios::out|std::ios::binary);f.seekg(0x3c);int32_t pe=0;f.read(reinterpret_cast<char*>(&pe),4);f.seekp(pe+4);uint16_t machine=IMAGE_FILE_MACHINE_AMD64;f.write(reinterpret_cast<char*>(&machine),2);}
 rejected([&]{WindowsVST3::rescan(utf(x64));},"ARM64");fs::remove(x64);
 auto bundle=folder/L"Bundle.vst3";auto binary=bundle/L"Contents"/L"arm64-win"/L"Bundle.vst3";fs::create_directories(binary.parent_path());fs::copy_file(fs::u8path(argv[2]),binary,fs::copy_options::overwrite_existing);auto bundleClasses=WindowsVST3::rescan(utf(bundle));check(bundleClasses.size()==4,"ARM64 VST3 bundle discovery");
 auto other=binary.parent_path()/L"Other.vst3";fs::copy_file(binary,other,fs::copy_options::overwrite_existing);rejected([&]{WindowsVST3::rescan(utf(bundle));},"ambiguous");fs::remove(other);fs::remove_all(bundle);
 std::cout<<"PASS cached/no-execution startup, Unicode, fingerprint mismatch, missing/ambiguous/wrong-PE, isolated crash/timeout/bounded-output\n";
 return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
