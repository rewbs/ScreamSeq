#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "pluginterfaces/base/ipluginbase.h"
#include "editor/hosted/PluginTypes.hpp"
#include <filesystem>
#include <string>
#include <vector>
namespace Tracker::WindowsVST3 {
struct Handle {
 HANDLE h=nullptr;
 explicit Handle(HANDLE v=nullptr):h(v){}
 ~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
 Handle(const Handle&)=delete; Handle &operator=(const Handle&)=delete;
};
std::wstring wide(const std::string &);
std::string narrow(const std::wstring &);
std::filesystem::path nativePath(const std::string &);
std::string modulePath(const std::string &);
struct Fingerprint {std::string path,sha256;uint16_t machine=0;};
Fingerprint fingerprint(const std::string &);
class Module {
 Handle pin;
 HMODULE dll=nullptr;
 bool entered=false;
public:
 Steinberg::IPluginFactory *factory=nullptr;
 explicit Module(const std::string &path,const std::string &expectedHash={});
 ~Module();
 Module(const Module&)=delete;
 void cleanup() noexcept;
};
struct Scan {Fingerprint file;std::vector<PluginDescriptor> classes;};
Scan scanInThisProcess(const std::string &); // Only called by the isolated scanner.
Scan scanChild(const std::string &exe,const std::string &,uint32_t timeoutMs);
}
