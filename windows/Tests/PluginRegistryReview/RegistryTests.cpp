// Actual provider/SDK/fixture regression; no production fault-injection switches.
#include "windows/Plugins/WindowsVST3.hpp"
#include "windows/Plugins/ScanProtocol.hpp"
#include <fstream>
#include <iostream>
#include <set>
#include <functional>
#include "RegistryIO.hpp"
#define WriteFile ReviewIO::write
#define FlushFileBuffers ReviewIO::flush
#define MoveFileExW ReviewIO::move
#include "windows/Plugins/Registry.cpp"
#undef MoveFileExW
#undef FlushFileBuffers
#undef WriteFile
using namespace Tracker;
using namespace Tracker::WindowsVST3;
namespace fs=std::filesystem;
static void need(bool ok,const std::string &what){if(!ok)throw std::runtime_error(what);}
static std::string bytes(const fs::path &p){std::ifstream f(p,std::ios::binary);need(bool(f),"read failed");return {std::istreambuf_iterator<char>(f),{}};}
static void put(const fs::path &p,const std::string &s){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(s.data(),s.size());need(bool(f),"write failed");}
static void reject(const std::function<void()> &f,const std::string &what){try{f();}catch(const std::exception &e){std::cout<<what<<": "<<e.what()<<'\n';return;}throw std::runtime_error("DID NOT REJECT: "+what);}
static std::string u8(const fs::path &p){return narrow(p.native());}
static std::wstring quote(const std::wstring &s){std::wstring out=L"\"";size_t back=0;for(auto c:s){if(c==L'\\'){++back;continue;}out.append(back*(c==L'"'?2:1),L'\\');back=0;if(c==L'"')out+=L'\\';out+=c;}out.append(back*2,L'\\');return out+L"\"";}
static void child(const std::vector<std::wstring> &args){
 wchar_t exe[32768]{};need(GetModuleFileNameW(nullptr,exe,32768)!=0,"exe path");std::wstring command=quote(exe);for(auto &a:args)command+=L" "+quote(a);
 STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION pi{};
 need(CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&pi),"child create");Handle process(pi.hProcess),thread(pi.hThread);
 if(WaitForSingleObject(pi.hProcess,10000)!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,98);WaitForSingleObject(pi.hProcess,1000);throw std::runtime_error("bounded child timed out");}
 DWORD code=0;need(GetExitCodeProcess(pi.hProcess,&code)&&code==0,"fresh process discover failed: "+std::to_string(code));
}
static JSON descriptors(const std::vector<PluginDescriptor> &ds){JSON a=JSON::array();for(auto &d:ds)a.push_back({d.format,d.path,d.classID,d.name,d.instrument});return a;}
static JSON recordSnapshot(){JSON a=JSON::array();for(const auto &s:records)a.push_back(encodeScan(s));return a;}
static void restart(const fs::path &cacheFile,const JSON &expected){auto expectedFile=cacheFile;expectedFile+=L".expected";put(expectedFile,expected.dump());child({L"discover",cacheFile.native(),expectedFile.native()});}
static void noTemps(const fs::path &p){for(auto &e:fs::directory_iterator(p))need(e.path().extension()!=L".tmp","orphan staging file: "+u8(e.path()));}
static JSON retired(const Scan &actual,size_t count,const fs::path &dir){JSON a=JSON::array();for(size_t i=0;i<count;++i){auto r=encodeScan(actual);r["path"]=u8(dir/(L"retired-"+std::to_wstring(i)+L".vst3"));r["classes"]=JSON::array({r["classes"][0]});r["classes"][0]["name"]="Retired "+std::to_string(i);a.push_back(r);}return a;}
static void capacity(const std::string &scannerExe,const std::string &fixture,const fs::path &dir){
 auto s=scanChild(scannerExe,fixture,5000);need(!s.classes.empty(),"real fixture classes");auto cacheFile=dir/L"cache.json";
 auto old=retired(s,2048,dir);put(cacheFile,old.dump());configure(scannerExe,u8(cacheFile));auto before=descriptors(platformPluginBackendFactory().discover());need(before.size()==2048,"load at capacity");auto memory=recordSnapshot();auto original=bytes(cacheFile);
 bool rejected=false;try{rescan(fixture);}catch(const std::exception &e){rejected=true;std::cout<<"append rejected: "<<e.what()<<'\n';}
 if(!rejected){
  // On RED report the exact unreadable-publication defect as well.
  try{configure(scannerExe,u8(cacheFile));platformPluginBackendFactory().discover();}catch(const std::exception &e){std::cout<<"restart parse: "<<e.what()<<'\n';}
 }
 need(rejected,"DID NOT REJECT: 2049th record");need(recordSnapshot()==memory,"rejection changed complete records");need(bytes(cacheFile)==original,"rejection changed old bytes");need(descriptors(platformPluginBackendFactory().discover())==before,"rejection changed records");restart(cacheFile,before);noTemps(dir);
 old[2047]=encodeScan(s);old[2047]["classes"][0]["name"]="Stale fixture name";put(cacheFile,old.dump());configure(scannerExe,u8(cacheFile));platformPluginBackendFactory().discover();
 auto ds=rescan(fixture);need(descriptors(ds)==descriptors(s.classes),"replacement returned wrong classes");auto after=JSON::parse(bytes(cacheFile));need(after.size()==2048,"replacement count changed");need(after.back()==encodeScan(s),"replacement metadata not updated");old[2047]=encodeScan(s);need(after==old,"replacement changed retired records");
 auto visible=descriptors(platformPluginBackendFactory().discover());restart(cacheFile,visible);noTemps(dir);std::cout<<"capacity append/replacement/fresh-reader PASS\n";
}
static std::string lower(std::string s){for(auto &c:s)if(c>='A'&&c<='F')c+=('a'-'A');return s;}
static void identity(const std::string &scannerExe,const std::string &fixture,const fs::path &dir){
 auto s=scanChild(scannerExe,fixture,5000);need(!s.classes.empty(),"real fixture classes");auto cacheFile=dir/L"cache.json";auto canonical=encodeScan(s);
 const auto id=s.classes[0].classID;need(lower(id)!=id,"fixture ID must include uppercase hex letters");
 auto bad=canonical;bad["classes"][0]["id"]=lower(id);
 put(cacheFile,JSON::array({bad}).dump());configure(scannerExe,u8(cacheFile));
 reject([&]{platformPluginBackendFactory().discover();},"lowercase cache ID");
 bad=canonical;bad["classes"].push_back(bad["classes"][0]);bad["classes"].back()["id"]=lower(id);
 put(cacheFile,JSON::array({bad}).dump());configure(scannerExe,u8(cacheFile));reject([&]{platformPluginBackendFactory().discover();},"case-collision cache IDs");
 for(const auto &invalid:std::vector<std::string>{"",std::string(31,'A'),std::string(33,'A'),std::string(32,'G'),std::string(31,'A')+std::string(1,'\0'),"{"+id+"}"}){
  bad=canonical;bad["classes"][0]["id"]=invalid;put(cacheFile,JSON::array({bad}).dump());configure(scannerExe,u8(cacheFile));reject([&]{platformPluginBackendFactory().discover();},"invalid cache ID");
  PluginState source;source.descriptor=s.classes[0];source.descriptor.classID=invalid;reject([&]{platformPluginBackendFactory().create(source,48000,true);},"invalid source ID");
 }
 bad=canonical;bad["classes"].push_back(bad["classes"][0]);put(cacheFile,JSON::array({bad}).dump());configure(scannerExe,u8(cacheFile));reject([&]{platformPluginBackendFactory().discover();},"exact duplicate cache IDs");
 put(cacheFile,JSON::array({canonical}).dump());configure(scannerExe,u8(cacheFile));need(descriptors(platformPluginBackendFactory().discover())==descriptors(s.classes),"canonical cache discover");
 PluginState source;source.descriptor=s.classes[0];auto original=descriptors({source.descriptor});{auto plugin=platformPluginBackendFactory().create(source,48000,true);need(bool(plugin),"canonical fixture creation");}need(descriptors({source.descriptor})==original,"canonical source mutated");
 // Project descriptors are separate: prevalidate then compare SDK canonical ID,
 // without rewriting the original descriptor, name or path.
 source.descriptor.classID=lower(id);source.descriptor.name="Retained source recipe name";original=descriptors({source.descriptor});{auto plugin=platformPluginBackendFactory().create(source,48000,true);need(bool(plugin),"lowercase source descriptor creation");}need(descriptors({source.descriptor})==original,"source descriptor silently repaired");
 restart(cacheFile,descriptors(s.classes));noTemps(dir);std::cout<<"canonical cache/source identity PASS\n";
}
#include "AdditionalCases.hpp"
#include "RaceCases.hpp"
int wmain(int argc,wchar_t **argv){try{
 if(argc==4&&std::wstring(argv[1])==L"replace-cache"){need(MoveFileExW(argv[2],argv[3],MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"external atomic cache replacement");return 0;}
 if(argc==4&&std::wstring(argv[1])==L"discover"){
  auto cacheFile=fs::path(argv[2]);configure(u8(cacheFile.parent_path()/L"absent-scanner.exe"),u8(cacheFile));auto expected=JSON::parse(bytes(argv[3]));need(descriptors(platformPluginBackendFactory().discover())==expected,"fresh discover content mismatch");return 0;
 }
 need(argc==5,"case scanner fixture private-directory required");USHORT process=0,native=0;need(IsWow64Process2(GetCurrentProcess(),&process,&native)&&process==0&&native==IMAGE_FILE_MACHINE_ARM64,"native ARM64 required");
 fs::path dir=argv[4];need(dir.is_absolute(),"private absolute test directory required");dir/=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());fs::create_directories(dir);std::string mode=narrow(argv[1]);
 if(mode=="capacity")capacity(narrow(argv[2]),narrow(argv[3]),dir);
 else if(mode=="identity")identity(narrow(argv[2]),narrow(argv[3]),dir);
 else if(mode=="invariants")invariants(narrow(argv[2]),narrow(argv[3]),dir);
 else if(mode=="concurrent")concurrentCache(narrow(argv[2]),narrow(argv[3]),dir);
 else if(mode=="retarget")retarget(narrow(argv[2]),narrow(argv[3]),dir);
 else if(mode=="write"||mode=="short-write"||mode=="flush"||mode=="rename"||mode=="locked"||mode=="stage")ioFailure(mode,narrow(argv[2]),narrow(argv[3]),dir);
 else throw std::runtime_error("unknown case");
 return 0;
}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
