#include "windows/Samples/Library.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/ProjectIO.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace ScreamSeq::Samples;
using namespace std::chrono_literals;
namespace fs=std::filesystem;
static void check(bool value,const char *what){if(!value)throw std::runtime_error(what);}
static std::string text(const fs::path &p){const auto s=p.u8string();return {reinterpret_cast<const char *>(s.data()),s.size()};}
static void file(const fs::path &path,const std::string &contents="Sample pack contents are not modified"){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<contents;check(bool(out),"write owned fixture");}
static Json invoke(Library &library,const std::string &method,Json p=Json::object()){auto result=library.invoke(method,std::move(p)).get();check(result["revision"].get<std::string>().starts_with("library:")&&!result["changed"].get<bool>()&&!result["playbackStopped"].get<bool>(),"library work acquired musical revision/history semantics");return result;}
static Json ready(Library &library,bool success=true){const auto end=std::chrono::steady_clock::now()+5s;while(std::chrono::steady_clock::now()<end){auto state=library.status();if(!state["indexing"].get<bool>()){if(success)check(state["ready"].get<bool>()&&state["error"].is_null(),state.dump().c_str());return state;}std::this_thread::sleep_for(1ms);}throw std::runtime_error("index did not settle");}
template<class F>static void code(F f,int expected){try{f();}catch(const ScreamSeq::Api::ApiError &e){check(e.code==expected,"wrong API rejection code");return;}throw std::runtime_error("invalid library operation accepted");}
struct Folder{fs::path base,path;explicit Folder(fs::path p):base(fs::canonical(p)),path(base/("sample-library-"+std::to_string(GetCurrentProcessId()))){check(path.parent_path()==base&&fs::create_directory(path),"create owned root");}~Folder(){if(path.parent_path()==base&&path.filename()=="sample-library-"+std::to_string(GetCurrentProcessId())){std::error_code error;fs::remove_all(path,error);}}};
struct Gate{std::atomic<bool> armed=false;std::promise<void> entered,release;std::shared_future<void> proceed=release.get_future();void wait(){if(armed.exchange(false)){entered.set_value();proceed.wait();}}void open(){try{release.set_value();}catch(const std::future_error &){}}};
struct Release{std::shared_ptr<Gate> gate;~Release(){gate->open();}};
int main(int argc,char **argv){try{
  check(argc==2,"fixture parent argument");Folder owned(fs::u8path(argv[1]));const auto pack=owned.path/L"Éléctric Pack",other=owned.path/L"Other",storage=owned.path/L"Settings";fs::create_directory(other);
  file(pack/L"Drums/Kick.wav");file(pack/L"Drums/Snare.wav");file(pack/L"Keys/Soft Piano C2.wav");file(pack/L"Keys/Soft Piano G2.wav");
  auto gate=std::make_shared<Gate>();Json persisted;
  {
    Library library(storage,{},[gate]{gate->wait();});Release release{gate};
    auto initial=ready(library);check(initial["roots"].empty()&&initial["count"]==0,"empty initial library");
    auto get=invoke(library,"sample.library.get");check(get["data"]==initial&&get["revision"]=="library:"+initial["libraryRevision"].get<std::string>(),"independent reply revision");
    auto changed=invoke(library,"sample.library.roots.set",{{"expectedLibraryRevision",initial["libraryRevision"]},{"roots",{text(pack),text(pack)}}});
    check(changed["data"]["indexing"]&&!changed["data"]["ready"].get<bool>()&&changed["data"]["roots"].size()==1,"root commit must reset index and deduplicate paths");
    auto current=ready(library);check(current["count"]==4,"real directory scan");
    auto query=invoke(library,"sample.library.search",{{"query","electric kick"},{"expectedLibraryRevision",current["libraryRevision"]},{"limit",1.0}});
    check(query["data"]["total"]==1&&query["data"]["libraryRevision"]==current["libraryRevision"],"worker search and numeric integer compatibility");
    auto group=invoke(library,"sample.library.multisample.get",{{"path",text(pack/L"Keys/Soft Piano C2.wav")}});check(group["data"]["group"]["count"]==2,"worker multisample lookup");
    code([&]{invoke(library,"sample.library.rescan",{{"expectedLibraryRevision",initial["libraryRevision"]}});},-32001);
    for(auto p:{Json{{"limit",true}},Json{{"limit",0}},Json{{"offset",250001}},Json{{"unknown",1}},Json{{"tags",Json::array({true})}},Json{{"root",text(other)}}})code([&]{invoke(library,"sample.library.search",p);},-32602);
    code([&]{invoke(library,"sample.library.roots.set",{{"roots",{text(other)}}});},-32602);
    code([&]{invoke(library,"sample.library.roots.set",{{"expectedLibraryRevision",current["libraryRevision"]},{"roots",{"relative"}}});},-32602);
    code([&]{invoke(library,"sample.library.preview",{{"path",text(pack/L"Drums/Kick.wav")}});},-32601); // Preview is a separate, not-yet-wired service.
    check(library.status()==current,"rejected operations changed the index/revision");
    // Atomic preferences cannot publish in-memory roots if replacement fails.
    const auto before=ScreamSeq::Project::readProjectBytes(storage/L"roots.json");
    HANDLE locked=CreateFileW((storage/L"roots.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(locked!=INVALID_HANDLE_VALUE,"lock fixture preferences");
    bool failed=false;try{invoke(library,"sample.library.roots.set",{{"expectedLibraryRevision",current["libraryRevision"]},{"roots",{text(other)}}});}catch(const std::exception &){failed=true;}CloseHandle(locked);
    check(failed&&library.status()==current&&ScreamSeq::Project::readProjectBytes(storage/L"roots.json")==before,"failed replacement changed roots/file/revision");
    // Hold a real rescan before enumeration; query and preference ownership must
    // remain independent, and changing roots must retire the old generation.
    file(pack/L"Drums/New.wav");gate->armed=true;auto entered=gate->entered.get_future();
    invoke(library,"sample.library.rescan",{{"expectedLibraryRevision",current["libraryRevision"]}});check(entered.wait_for(2s)==std::future_status::ready,"scanner did not enter gate");
    auto scanning=library.status();check(scanning["indexing"]&&scanning["ready"]&&scanning["count"]==4,"rescan hid the immutable previous index");
    code([&]{invoke(library,"sample.library.rescan",{{"expectedLibraryRevision",scanning["libraryRevision"]}});},-32002);
    const auto start=std::chrono::steady_clock::now();query=invoke(library,"sample.library.search");check(std::chrono::steady_clock::now()-start<500ms&&query["data"]["total"]==4&&query["data"]["indexing"],"search blocked on a scan or exposed partial results");
    invoke(library,"sample.library.roots.set",{{"expectedLibraryRevision",scanning["libraryRevision"]},{"roots",{text(other)}}});gate->open();persisted=ready(library);
    check(persisted["roots"]==Json::array({LibraryIndex::canonicalPath(text(other))})&&persisted["count"]==0,"cancelled old scan overwrote new roots");
    check(fs::exists(pack/L"Drums/Kick.wav")&&fs::exists(pack/L"Drums/New.wav"),"removing a root removed sample files");
  }
  {Library reopened(storage);const auto state=ready(reopened);check(state["roots"]==persisted["roots"]&&state["count"]==0&&state["indexedAt"]==persisted["indexedAt"],"persistent roots/index did not reopen");check(state["libraryRevision"]!=persisted["libraryRevision"],"a new session accepted an old revision");}
  file(storage/L"index.json","{ corrupt cache");
  {Library recovered(storage);check(ready(recovered)["roots"]==persisted["roots"],"damaged disposable cache lost configured roots");}
  file(storage/L"roots.json","{ corrupt preferences");
  {Library recovered(storage,{text(pack)});const auto state=ready(recovered);check(state["count"]==5&&!state["warnings"].empty(),"damaged preferences did not use explicit defaults with a warning");}
  {Library ephemeral;auto initial=ready(ephemeral);invoke(ephemeral,"sample.library.roots.set",{{"expectedLibraryRevision",initial["libraryRevision"]},{"roots",{text(pack)}}});check(ready(ephemeral)["count"]==5,"inspection-only in-memory roots failed");}
  std::cout<<"PASS independent library revisions, guarded preferences, async scans/search, generation cancellation, atomic failure, cache reopen/recovery and in-memory operation\n";
  return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
