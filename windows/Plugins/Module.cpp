#include "Module.hpp"
#include "WindowsVST3.hpp"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
using namespace Steinberg;
namespace Tracker::WindowsVST3 {
namespace fs=std::filesystem;
static void check(bool ok,const char *s){if(!ok)throw std::runtime_error(s);}
std::wstring wide(const std::string &s){
 if(s.find('\0')!=std::string::npos||s.size()>32760)throw std::runtime_error("Invalid UTF-8 path");
 if(s.empty())return {};
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
 check(n>0,"Invalid UTF-8");std::wstring v(n,0);check(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),v.data(),n)==n,"Invalid UTF-8");return v;
}
std::string narrow(const std::wstring &s){
 if(s.empty())return {};
 int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
 check(n>0,"Invalid UTF-16");std::string v(n,0);check(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),v.data(),n,nullptr,nullptr)==n,"Invalid UTF-16");return v;
}
fs::path nativePath(const std::string &s){check(!s.empty(),"Empty Windows path");return fs::path(wide(s));}
bool validClassID(const std::string &s) noexcept {return s.size()==32&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');});}
std::string modulePath(const std::string &s){
 auto p=nativePath(s);check(p.is_absolute(),"VST3 requires an absolute Windows path; foreign paths are not retargeted");
 p=fs::canonical(p);
 if(fs::is_directory(p)){
  auto d=p/L"Contents"/L"arm64-win";check(fs::is_directory(d),"VST3 has no ARM64 Windows binary (x64 cannot load in this host)");
  std::vector<fs::path> matches;
  for(const auto &entry:fs::directory_iterator(d))if(entry.is_regular_file()&&entry.path().extension()==L".vst3")matches.push_back(entry.path());
  check(matches.size()==1,"VST3 ARM64 bundle binary is missing or ambiguous");p=fs::canonical(matches[0]);
 }
 check(fs::is_regular_file(p),"VST3 module missing");return narrow(p.native());
}
Fingerprint fingerprint(const std::string &s){
 Fingerprint f;f.path=modulePath(s);std::ifstream in(nativePath(f.path),std::ios::binary);
 check(bool(in),"Cannot read VST3 binary");in.seekg(0,std::ios::end);std::streamoff size=in.tellg();check(size>=64&&size<=1024LL*1024*1024,"VST3 binary size out of bounds");in.seekg(0);
 IMAGE_DOS_HEADER dos{};in.read(reinterpret_cast<char*>(&dos),sizeof dos);check(bool(in)&&dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>=64&&dos.e_lfanew<=size-24,"Not a valid Windows PE module");
 in.seekg(dos.e_lfanew);DWORD signature=0;IMAGE_FILE_HEADER head{};in.read(reinterpret_cast<char*>(&signature),4);in.read(reinterpret_cast<char*>(&head),sizeof head);
 check(bool(in)&&signature==IMAGE_NT_SIGNATURE&&(head.Characteristics&IMAGE_FILE_DLL),"Not a Windows DLL");f.machine=head.Machine;
 check(f.machine==IMAGE_FILE_MACHINE_ARM64,"Incompatible VST3 architecture: native ARM64 required; x64 is not supported");
 #if !defined(_M_ARM64)
 throw std::runtime_error("This VST3 provider requires a native ARM64 build");
 #endif
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
 check(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider failure");
 struct Close{BCRYPT_ALG_HANDLE &a;BCRYPT_HASH_HANDLE &h;~Close(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}}close{alg,hash};
 check(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0,"SHA256 setup failure");in.clear();in.seekg(0);
 std::array<unsigned char,65536> block{};while(in){in.read(reinterpret_cast<char*>(block.data()),block.size());auto n=in.gcount();if(n)check(BCryptHashData(hash,block.data(),ULONG(n),0)>=0,"SHA256 read failure");}
 check(in.eof(),"VST3 read failure");std::array<unsigned char,32> bytes{};check(BCryptFinishHash(hash,bytes.data(),ULONG(bytes.size()),0)>=0,"SHA256 finish failure");
 static constexpr char hex[]="0123456789abcdef";for(auto b:bytes){f.sha256+=hex[b>>4];f.sha256+=hex[b&15];}return f;
}
Module::Module(const std::string &p,const std::string &expected):pin(CreateFileW(wide(modulePath(p)).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)){
 check(pin.h!=INVALID_HANDLE_VALUE,"Cannot pin VST3 binary against replacement");auto f=fingerprint(p);check(expected.empty()||expected==f.sha256,"VST3 changed since scanning; explicit rescan required");
 dll=LoadLibraryExW(wide(f.path).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
 check(dll!=nullptr,"LoadLibraryExW failed loading ARM64 VST3 or its dependencies");
 try{
  auto init=reinterpret_cast<bool(*)()>(GetProcAddress(dll,"InitDll"));auto exit=GetProcAddress(dll,"ExitDll");
  auto get=reinterpret_cast<IPluginFactory*(*)()>(GetProcAddress(dll,"GetPluginFactory"));
  check(init&&exit&&get,"VST3 module exports incomplete");entered=init();check(entered,"VST3 InitDll failed");factory=get();check(factory!=nullptr,"VST3 factory unavailable");
 }catch(...){cleanup();throw;}
}
void Module::cleanup() noexcept{if(factory){factory->release();factory=nullptr;}if(dll){if(entered)reinterpret_cast<bool(*)()>(GetProcAddress(dll,"ExitDll"))();entered=false;FreeLibrary(dll);dll=nullptr;}}
Module::~Module(){cleanup();}
Scan scanInThisProcess(const std::string &p){
 Scan s;s.file=fingerprint(p);Module module(s.file.path,s.file.sha256);IPluginFactory2 *extended=nullptr;
 module.factory->queryInterface(IPluginFactory2::iid,reinterpret_cast<void**>(&extended));
 struct Release{IPluginFactory2 *p;~Release(){if(p)p->release();}}release{extended};
 auto n=module.factory->countClasses();check(n>=0&&n<=1024,"VST3 factory class count out of bounds");
 for(int i=0;i<n;++i){PClassInfo info{};check(module.factory->getClassInfo(i,&info)==kResultOk,"VST3 class read failed");
  check(memchr(info.category,0,sizeof info.category)&&memchr(info.name,0,sizeof info.name),"Unterminated VST3 metadata");
  if(strcmp(info.category,kVstAudioEffectClass))continue;
  PluginDescriptor d;d.format="VST3";d.path=s.file.path;d.name=info.name;wide(d.name);char id[33]{};FUID(info.cid).toString(id);d.classID=id;
  check(validClassID(d.classID),"Invalid VST3 class ID");check(std::none_of(s.classes.begin(),s.classes.end(),[&](auto &x){return x.classID==d.classID;}),"Ambiguous VST3 class ID");
  if(extended){PClassInfo2 e{};check(extended->getClassInfo2(i,&e)==kResultOk&&FUnknownPrivate::iidEqual(e.cid,info.cid),"VST3 factory class identity mismatch");check(memchr(e.subCategories,0,sizeof e.subCategories)!=nullptr,"Unterminated VST3 categories");d.instrument=strstr(e.subCategories,"Instrument")!=nullptr;}
  s.classes.push_back(std::move(d));
 }
 return s;
}
}
