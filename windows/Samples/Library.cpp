#include "Library.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/ProjectIO.hpp"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>

namespace ScreamSeq::Samples {
namespace {
void need(bool ok,const char *message){if(!ok)throw Api::ApiError(-32602,message);}
void keys(const Json &p,std::initializer_list<const char *> allowed){need(p.is_object(),"Expected sample library parameters");for(auto i=p.begin();i!=p.end();++i)need(std::any_of(allowed.begin(),allowed.end(),[&](auto k){return i.key()==k;}),"Unknown sample library field");}
std::string text(const Json &v){need(v.is_string(),"Expected sample library text");const auto &s=v.get_ref<const std::string &>();need(s.size()<=16384&&s.find('\0')==s.npos,"Invalid sample library text");if(!s.empty()){const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);need(n>0&&n<=4096,"Invalid or oversized sample library text");}return s;}
std::vector<std::string> strings(const Json &v){need(v.is_array()&&v.size()<=32,"Use at most 32 sample library paths/tags");std::vector<std::string> out;for(const auto &s:v)out.push_back(text(s));return out;}
size_t integer(const Json &v,size_t max){need(v.is_number()&&!v.is_boolean(),"Expected a sample library integer");const auto n=v.get<double>();need(std::isfinite(n)&&n>=0&&n<=double(max)&&std::floor(n)==n,"Invalid sample library integer");return size_t(n);}
bool samePath(const std::string &a,const std::string &b){const auto x=std::filesystem::u8path(a),y=std::filesystem::u8path(b);return CompareStringOrdinal(x.c_str(),-1,y.c_str(),-1,TRUE)==CSTR_EQUAL;}
std::vector<std::string> roots(const std::vector<std::string> &paths,bool existing){
  need(paths.size()<=32,"Use at most 32 sample folders");std::vector<std::string> result;
  for(const auto &path:paths){(void)text(path);std::string canonical;try{canonical=LibraryIndex::canonicalPath(path);}catch(const std::exception &e){throw Api::ApiError(-32602,e.what());}
    if(existing){std::error_code error;need(std::filesystem::is_directory(std::filesystem::u8path(canonical),error)&&!error,"Sample folder is unavailable");}
    if(std::none_of(result.begin(),result.end(),[&](const auto &old){return samePath(old,canonical);}))result.push_back(std::move(canonical));
  }return result;
}
std::string revision(){GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Cannot create sample library revision");wchar_t value[39]{};StringFromGUID2(id,value,39);std::string result;for(size_t i=1;i<37;++i)result+=char(value[i]);return result;}
Json readJson(const std::filesystem::path &path,size_t bytes,size_t events){
  const auto attrs=GetFileAttributesW(path.c_str());
  if(attrs==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)return nullptr;throw std::runtime_error("Cannot read sample library file");}
  need(!(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_DEVICE|FILE_ATTRIBUTE_REPARSE_POINT)),"Sample library cache must be a regular file");
  auto data=Project::readProjectBytes(path,bytes);size_t count=0;std::vector<std::set<std::string>> names;
  auto validate=[&](int depth,Json::parse_event_t event,Json &value){
    need(++count<=events&&depth<=12,"Sample library file exceeds structural limits");
    if(event==Json::parse_event_t::object_start)names.emplace_back();
    else if(event==Json::parse_event_t::object_end)names.pop_back();
    else if(event==Json::parse_event_t::key)need(!names.empty()&&names.back().insert(text(value)).second,"Duplicate sample library key");
    else if(event==Json::parse_event_t::value&&value.is_string())(void)text(value);
    return true;
  };
  return Json::parse(data.begin(),data.end(),validate);
}
void writeJson(const std::filesystem::path &path,const Json &value,size_t maximum){const auto bytes=value.dump();need(bytes.size()<=maximum,"Sample library file exceeds size limit");std::filesystem::create_directories(path.parent_path());Project::writeProjectFile(path,std::as_bytes(std::span(bytes.data(),bytes.size())),true);}
}
struct Library::Impl {
  struct Scan {uint64_t generation;bool load;std::vector<std::string> roots;};
  const std::optional<std::filesystem::path> directory;
  const std::function<void()> beforeScan;
  std::vector<std::string> currentRoots;
  std::shared_ptr<const LibraryIndex> index;
  std::string version=revision(),error,preferencesWarning;
  bool indexing=true;
  std::atomic<std::shared_ptr<const Json>> published;
  std::atomic<bool> closing=false;
  std::atomic<uint64_t> generation=1;
  std::mutex ownerMutex,scanMutex;
  std::condition_variable ownerWake,scanWake;
  std::deque<std::function<void()>> jobs;
  std::optional<Scan> nextScan;
  std::thread ownerThread,scanThread;

  Impl(std::optional<std::filesystem::path> path,std::vector<std::string> defaults,std::function<void()> hook):directory(std::move(path)),beforeScan(std::move(hook)),currentRoots(roots(defaults,false)) {
    if(directory)need(directory->is_absolute(),"Sample library storage directory must be absolute");
    publish();nextScan=Scan{1,true,currentRoots};
    ownerThread=std::thread([this]{ownerLoop();});
    try{scanThread=std::thread([this]{scanLoop();});}
    catch(...){closing=true;ownerWake.notify_all();ownerThread.join();throw;}
  }
  ~Impl(){closing=true;generation.fetch_add(1);ownerWake.notify_all();scanWake.notify_all();if(scanThread.joinable())scanThread.join();if(ownerThread.joinable())ownerThread.join();}
  void publish(){
    Json warnings=index?Json(index->warnings()):Json::array();if(!preferencesWarning.empty())warnings.push_back(preferencesWarning);
    published.store(std::make_shared<const Json>(Json{{"roots",currentRoots},{"count",index?index->count():0},{"indexing",indexing},{"ready",bool(index)},{"libraryRevision",version},{"error",error.empty()?Json():Json(error)},{"indexedAt",index?Json(index->indexedAt()):Json()},{"warnings",std::move(warnings)},{"extensions",LibraryIndex::extensions()}}),std::memory_order_release);
  }
  Json reply(Json data)const{return {{"revision","library:"+version},{"changed",false},{"playbackStopped",false},{"data",std::move(data)}};}
  void ownerLoop(){
    for(;;){std::function<void()> job;{std::unique_lock lock(ownerMutex);ownerWake.wait(lock,[&]{return closing||!jobs.empty();});if(closing)return;job=std::move(jobs.front());jobs.pop_front();}
      try{job();}catch(const std::exception &e){error=e.what();indexing=false;try{publish();}catch(...){}}
    }
  }
  void complete(std::function<void()> task){std::lock_guard lock(ownerMutex);if(closing)return;jobs.push_back(std::move(task));ownerWake.notify_one();}
  void schedule(bool load=false){
    const auto token=generation.fetch_add(1)+1;indexing=true;error.clear();publish();
    {std::lock_guard lock(scanMutex);nextScan=Scan{token,load,currentRoots};}scanWake.notify_one();
  }
  void scanLoop(){
    for(;;){Scan job;{std::unique_lock lock(scanMutex);scanWake.wait(lock,[&]{return closing||nextScan.has_value();});if(closing)return;job=std::move(*nextScan);nextScan.reset();}
      auto cancelled=[&]{return closing||generation.load()!=job.generation;};
      try{
        std::shared_ptr<const LibraryIndex> loaded;std::string warning;
        if(job.load&&directory){
          try{auto configured=readJson(*directory/L"roots.json",1024u*1024u,512);if(!configured.is_null())job.roots=roots(strings(configured),false);}catch(const std::exception &e){warning=std::string("Sample folder preferences could not be loaded: ")+e.what();}
          if(!cancelled())try{auto cached=readJson(*directory/L"index.json",256u*1024u*1024u,12000000);if(!cached.is_null()){auto candidate=LibraryIndex::fromSnapshot(cached);if(candidate->roots()==job.roots)loaded=std::move(candidate);}}catch(const std::exception &){/* A disposable index can be rebuilt from its roots. */}
        }
        if(cancelled())continue;
        if(!job.load){if(beforeScan)beforeScan();loaded=LibraryIndex::scan(job.roots,cancelled);if(cancelled())continue;if(directory)writeJson(*directory/L"index.json",loaded->snapshot(),256u*1024u*1024u);}
        if(cancelled())continue;
        complete([this,job=std::move(job),loaded=std::move(loaded),warning=std::move(warning)]()mutable{
          if(closing||job.generation!=generation.load())return;
          if(job.load){currentRoots=std::move(job.roots);preferencesWarning=std::move(warning);}
          index=std::move(loaded);version=revision();indexing=false;error.clear();publish();if(!index)schedule();
        });
      }catch(const std::exception &e){const auto token=job.generation;const std::string message=e.what();complete([this,token,message]{if(!closing&&token==generation.load()){indexing=false;error=message;publish();}});}
    }
  }
  void checkRevision(const Json &p,bool required){
    if(!required&&!p.contains("expectedLibraryRevision"))return;
    need(p.contains("expectedLibraryRevision"),"expectedLibraryRevision is required");
    if(text(p.at("expectedLibraryRevision"))!=version)throw Api::ApiError(-32001,"Sample library changed; read sample.library.get again");
  }
  Json operation(const std::string &method,const Json &p){
    if(method=="sample.library.get"){keys(p,{});return reply(*published.load(std::memory_order_acquire));}
    if(method=="sample.library.roots.set"){
      keys(p,{"roots","expectedLibraryRevision"});checkRevision(p,true);need(p.contains("roots"),"roots is required");auto next=roots(strings(p.at("roots")),true);
      // Commit small preferences before changing the visible generation. Sample
      // packs are never edited; removing a root only removes it from this list.
      if(directory)writeJson(*directory/L"roots.json",next,1024u*1024u);
      currentRoots=std::move(next);index.reset();version=revision();preferencesWarning.clear();schedule();return reply(*published.load(std::memory_order_acquire));
    }
    if(method=="sample.library.rescan"){
      keys(p,{"expectedLibraryRevision"});checkRevision(p,true);if(indexing)throw Api::ApiError(-32002,"Sample library is already indexing");schedule();return reply(*published.load(std::memory_order_acquire));
    }
    if(method=="sample.library.search"){
      keys(p,{"query","tags","root","tagQuery","offset","limit","expectedLibraryRevision"});checkRevision(p,false);
      LibraryQuery query;query.text=text(p.value("query",Json("")));query.tagText=text(p.value("tagQuery",Json("")));if(p.contains("tags"))query.tags=strings(p.at("tags"));query.offset=integer(p.value("offset",Json(0)),250000);query.limit=integer(p.value("limit",Json(100)),1000);need(query.limit>0,"limit must be positive");
      if(p.contains("root")){auto path=LibraryIndex::canonicalPath(text(p.at("root")));auto it=std::find_if(currentRoots.begin(),currentRoots.end(),[&](const auto &root){return samePath(root,path);});need(it!=currentRoots.end(),"Root is not in this library");query.root=*it;}
      auto result=index?index->search(query):Json{{"items",Json::array()},{"total",0},{"offset",query.offset},{"tags",Json::array()}};result["libraryRevision"]=version;result["indexing"]=indexing;return reply(std::move(result));
    }
    if(method=="sample.library.multisample.get"){
      keys(p,{"path","expectedLibraryRevision"});checkRevision(p,false);need(p.contains("path"),"path is required");const auto path=LibraryIndex::canonicalPath(text(p.at("path")));return reply({{"group",index?index->multisample(path):Json()},{"libraryRevision",version}});
    }
    throw Api::ApiError(-32601,"Unknown sample library method");
  }
  std::future<Json> invoke(std::string method,Json params){
    auto promise=std::make_shared<std::promise<Json>>();auto future=promise->get_future();
    {std::lock_guard lock(ownerMutex);if(closing||jobs.size()>=64){promise->set_exception(std::make_exception_ptr(Api::ApiError(-32002,"Sample library is busy or closing")));return future;}
      jobs.emplace_back([this,promise,method=std::move(method),params=std::move(params)]{try{promise->set_value(operation(method,params));}catch(const std::invalid_argument &e){promise->set_exception(std::make_exception_ptr(Api::ApiError(-32602,e.what())));}catch(...){promise->set_exception(std::current_exception());}});
    }ownerWake.notify_one();return future;
  }
};
Library::Library(std::optional<std::filesystem::path> directory,std::vector<std::string> defaults,std::function<void()> hook):impl_(std::make_unique<Impl>(std::move(directory),std::move(defaults),std::move(hook))){}
Library::~Library()=default;
std::future<Json> Library::invoke(std::string method,Json params){return impl_->invoke(std::move(method),std::move(params));}
Json Library::status()const{return *impl_->published.load(std::memory_order_acquire);}
std::vector<std::string> Library::reads(){return {"sample.library.get","sample.library.search","sample.library.multisample.get"};}
std::vector<std::string> Library::writes(){return {"sample.library.roots.set","sample.library.rescan"};}
}
