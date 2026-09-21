#include "WindowsVST3.hpp"
#include "ScanProtocol.hpp"
#include <fstream>
#include <set>
namespace Tracker::WindowsVST3 {
std::unique_ptr<PluginBackend> createBackend(const PluginState &,double,bool,const std::string &hash);
namespace {
std::string scanner,cache;std::vector<Scan> records;bool loaded=false;
void defaults(){if(!scanner.empty())return;std::wstring exe(32768,0);auto n=GetModuleFileNameW(nullptr,exe.data(),DWORD(exe.size()));if(!n||n==exe.size())throw std::runtime_error("Cannot locate VST3 scanner");exe.resize(n);scanner=narrow((std::filesystem::path(exe).parent_path()/L"ScreamSeqVST3Scanner.exe").native());wchar_t local[32768]{};n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);if(!n||n>=32768)throw std::runtime_error("LOCALAPPDATA unavailable");cache=narrow((std::filesystem::path(local)/L"ScreamSeq"/L"vst3-arm64-cache.json").native());}
constexpr size_t maxCacheRecords=2048,maxCacheBytes=4*1024*1024;
std::vector<Scan> decodeCache(const JSON &v){
 if(!v.is_array()||v.size()>maxCacheRecords)throw std::runtime_error("VST3 cache count exceeds limit");
 std::vector<Scan> next;next.reserve(v.size());std::set<std::string> paths;
 for(const auto &j:v){auto s=decodeScan(j);if(!paths.insert(s.file.path).second)throw std::runtime_error("Ambiguous VST3 cache path");next.push_back(std::move(s));}
 return next;
}
void load(){if(loaded)return;defaults();std::vector<Scan> next;auto p=nativePath(cache);if(std::filesystem::exists(p)){if(std::filesystem::file_size(p)>maxCacheBytes)throw std::runtime_error("VST3 cache exceeds 4 MiB");std::ifstream f(p,std::ios::binary);auto v=JSON::parse(f,[](int depth,JSON::parse_event_t,JSON&){if(depth>16)throw std::runtime_error("VST3 cache nesting exceeds limit");return true;});next=decodeCache(v);}records=std::move(next);loaded=true;}
void save(const std::vector<Scan> &next){
 // Validate the entire prospective wire cache under the reader's invariants
 // before creating a staging file or replacing the last readable cache.
 if(next.size()>maxCacheRecords)throw std::runtime_error("VST3 cache count exceeds limit");
 JSON v=JSON::array();for(auto &r:next)v.push_back(encodeScan(r));auto text=v.dump();if(text.size()>maxCacheBytes)throw std::runtime_error("VST3 cache full");(void)decodeCache(v);
 auto p=nativePath(cache);std::filesystem::create_directories(p.parent_path());auto temp=p;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
 // Declare cleanup before the exclusive handle: unwinding closes the handle
 // before deletion. Do not remove an existing staging file we did not create.
 struct Remove{const std::filesystem::path &p;bool owned=false;~Remove(){if(owned)DeleteFileW(p.c_str());}}remove{temp};
 Handle file(CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));if(file.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot stage VST3 cache");remove.owned=true;
 DWORD n=0;if(!WriteFile(file.h,text.data(),DWORD(text.size()),&n,nullptr)||n!=text.size()||!FlushFileBuffers(file.h))throw std::runtime_error("Cannot write VST3 cache");CloseHandle(file.h);file.h=nullptr;if(!MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot publish VST3 cache");
}
}
void configure(std::string exe,std::string file){if(!nativePath(exe).is_absolute()||!nativePath(file).is_absolute())throw std::runtime_error("VST3 scanner/cache paths must be absolute");scanner=std::move(exe);cache=std::move(file);loaded=false;records.clear();}
std::vector<PluginDescriptor> rescan(const std::string &p,uint32_t timeout){load();auto s=scanChild(scanner,p,timeout);auto next=records;std::erase_if(next,[&](auto &x){return x.file.path==s.file.path;});next.push_back(s);save(next);records=std::move(next);return s.classes;}
std::vector<ScannedPlugin> scannedPlugins(const std::string &classID,bool instrument){
 if(!validClassID(classID))throw std::runtime_error("VST3 class ID must be exactly 32 hexadecimal characters");
 load();Steinberg::FUID uid;uid.fromString(classID.c_str());char canonical[33]{};uid.toString(canonical);
 std::vector<ScannedPlugin> out;for(const auto &record:records)for(const auto &d:record.classes)if(d.classID==canonical&&d.instrument==instrument)out.push_back({d,record.file.sha256});return out;
}
std::string verifyScannedPlugin(const std::string &path,const std::string &classID,bool instrument,const std::string &sha256){
 const auto candidates=scannedPlugins(classID,instrument);const auto f=fingerprint(path);
 if(!sha256.empty()&&f.sha256!=sha256)throw std::runtime_error("VST3 binary changed; rescan and choose the module again");
 const auto count=std::count_if(candidates.begin(),candidates.end(),[&](const auto &candidate){return candidate.descriptor.path==f.path&&candidate.sha256==f.sha256;});
 if(count!=1)throw std::runtime_error("Exact VST3 class/path/ARM64/hash not present in cache; rescan and choose a matching module");return f.path;
}
std::vector<std::string> defaultSearchRoots(){std::vector<std::string> r;for(auto key:{L"CommonProgramW6432",L"CommonProgramFiles",L"LOCALAPPDATA"}){wchar_t s[32768]{};auto n=GetEnvironmentVariableW(key,s,32768);if(n&&n<32768){auto p=std::filesystem::path(s)/(key==std::wstring(L"LOCALAPPDATA")?L"Programs/Common/VST3":L"VST3");auto u=narrow(p.native());if(std::find(r.begin(),r.end(),u)==r.end())r.push_back(u);}}return r;}
std::vector<std::string> candidates(const std::vector<std::string> &roots){std::set<std::string> out;size_t count=0;for(auto &root:roots){auto p=nativePath(root);if(!p.is_absolute())throw std::runtime_error("Absolute VST3 search root required");std::error_code ec;if(!std::filesystem::is_directory(p,ec))continue;for(auto it=std::filesystem::recursive_directory_iterator(p,std::filesystem::directory_options::skip_permission_denied);it!=std::filesystem::recursive_directory_iterator();++it){if(++count>100000||it.depth()>16)throw std::runtime_error("VST3 enumeration exceeds bound");if(it->is_symlink()){it.disable_recursion_pending();continue;}if(it->path().extension()==L".vst3"){out.insert(narrow(std::filesystem::canonical(it->path()).native()));if(it->is_directory())it.disable_recursion_pending();if(out.size()>2048)throw std::runtime_error("Too many VST3 candidates");}}}return {out.begin(),out.end()};}
class Factory final:public PluginBackendFactory{
 std::unique_ptr<PluginBackend> create(const PluginState &state,double rate,bool offline) override{
  if(state.descriptor.format!="VST3")throw std::runtime_error("Audio Units are unavailable on Windows; recipe and opaque state must be retained");
  if(!validClassID(state.descriptor.classID))throw std::runtime_error("VST3 class ID must be exactly 32 hexadecimal characters");
  load();auto f=fingerprint(state.descriptor.path);Steinberg::FUID uid;uid.fromString(state.descriptor.classID.c_str());char canonical[33]{};uid.toString(canonical);
  const Scan *match=nullptr;for(auto &r:records)if(r.file.path==f.path&&r.file.sha256==f.sha256&&r.file.machine==f.machine&&std::any_of(r.classes.begin(),r.classes.end(),[&](auto &d){return d.classID==canonical&&d.instrument==state.descriptor.instrument;})){if(match)throw std::runtime_error("Ambiguous VST3 identity");match=&r;}
  if(!match)throw std::runtime_error("Exact VST3 class/path/ARM64/hash not present in cache; explicit rescan or resolution required");
  return createBackend(state,rate,offline,f.sha256);
 }
 std::vector<PluginDescriptor> discover() override{load();std::vector<PluginDescriptor> out;for(auto &r:records)out.insert(out.end(),r.classes.begin(),r.classes.end());return out;}
 std::vector<PluginDescriptor> discoverVST3(const std::string &path) override{return rescan(path);}
};
PluginBackendFactory &factory(){static Factory f;return f;}
}
namespace Tracker {PluginBackendFactory &platformPluginBackendFactory(){return WindowsVST3::factory();}}
