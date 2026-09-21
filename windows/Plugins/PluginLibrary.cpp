#include "PluginLibrary.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/ProjectIO.hpp"
#include <bcrypt.h>
#include <set>
namespace ScreamSeq::Plugins {
namespace {
using Json=nlohmann::json;
constexpr size_t maximumBytes=2u*1024u*1024u;
void need(bool value,const char *message){if(!value)throw Api::ApiError(-32602,message);}
void keys(const Json &v,std::initializer_list<const char *> allowed){need(v.is_object(),"Expected plugin library object");for(auto i=v.begin();i!=v.end();++i)need(std::any_of(allowed.begin(),allowed.end(),[&](auto k){return i.key()==k;}),"Unknown plugin library field");}
const Json &field(const Json &v,const char *name){need(v.contains(name),"Missing plugin library field");return v.at(name);}
std::wstring wide(const Json &v,size_t maximum){need(v.is_string(),"Expected library text");const auto &s=v.get_ref<const std::string &>();need(s.size()<=maximum*4&&s.find('\0')==s.npos,"Invalid library text");if(s.empty())return {};const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);need(size>0&&size_t(size)<=maximum,"Invalid or oversized library text");std::wstring result(size,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),result.data(),size);return result;}
std::string utf8(const std::wstring &v){if(v.empty())return {};const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v.data(),int(v.size()),nullptr,0,nullptr,nullptr);need(size>0,"Invalid library Unicode");std::string out(size,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v.data(),int(v.size()),out.data(),size,nullptr,nullptr);return out;}
std::string text(const Json &v,size_t maximum){(void)wide(v,maximum);return v.get<std::string>();}
bool boolean(const Json &v){need(v.is_boolean(),"Expected library boolean");return v.get<bool>();}
void validID(const std::string &id){need(id.size()==65&&id[0]=='p'&&std::all_of(id.begin()+1,id.end(),[](auto c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),"Invalid plugin catalog identity");}
std::string hash(std::span<const std::byte> bytes){std::array<UCHAR,32> digest{};if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<std::byte *>(bytes.data())),ULONG(bytes.size()),digest.data(),ULONG(digest.size()))<0)throw std::runtime_error("Cannot hash plugin library identity");std::string out;for(auto b:digest){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;}
Json defaults(){return {{"favorite",false},{"hidden",false},{"category",""}};}
void preferences(const Json &v){keys(v,{"favorite","hidden","category"});boolean(field(v,"favorite"));boolean(field(v,"hidden"));text(field(v,"category"),80);}
std::string trim(const Json &v){auto s=wide(v,80);auto space=[](wchar_t c){WORD kind=0;return GetStringTypeW(CT_CTYPE1,&c,1,&kind)&&(kind&C1_SPACE);};size_t first=0,last=s.size();while(first<last&&space(s[first]))++first;while(last>first&&space(s[last-1]))--last;return utf8(s.substr(first,last-first));}
void pathCheck(const std::optional<std::filesystem::path> &path){need(path&&path->is_absolute()&&!path->filename().empty(),"Plugin library preferences are not configured for this session");}
class Lock {
  HANDLE mutex_=nullptr;
public:
  explicit Lock(const std::filesystem::path &path){
    const auto canonical=std::filesystem::weakly_canonical(path).native();std::wstring folded(canonical.size(),0);
    need(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,canonical.data(),int(canonical.size()),folded.data(),int(folded.size()),nullptr,nullptr,0)>0,"Cannot canonicalize plugin library path");
    const auto digest=hash(std::as_bytes(std::span(folded.data(),folded.size())));
    const auto name=L"Global\\org.resonance.tracker.plugin-library-v1."+std::wstring(digest.begin(),digest.end());
    mutex_=CreateMutexW(nullptr,FALSE,name.c_str());if(!mutex_)Project::FileDetail::fail("Cannot open plugin library lock");
    const auto result=WaitForSingleObject(mutex_,0);if(result==WAIT_OBJECT_0||result==WAIT_ABANDONED)return;
    const auto error=GetLastError();CloseHandle(mutex_);mutex_=nullptr;if(result==WAIT_TIMEOUT)throw Api::ApiError(-32002,"Plugin library is busy; retry shortly");throw std::system_error(error,std::system_category(),"Cannot lock plugin library");
  }
  ~Lock(){if(mutex_){ReleaseMutex(mutex_);CloseHandle(mutex_);}}
  Lock(const Lock &)=delete;Lock &operator=(const Lock &)=delete;
};
Json readFile(const std::filesystem::path &path){
  const auto attr=GetFileAttributesW(path.c_str());
  if(attr==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();need(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND,"Cannot read plugin library preferences");return {{"version",1},{"revision","library:0"},{"entries",Json::object()}};}
  need(!(attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_DEVICE|FILE_ATTRIBUTE_REPARSE_POINT)),"Plugin library preferences must be a regular file");
  Json root;
  try{const auto bytes=Project::readProjectBytes(path,maximumBytes);std::vector<std::set<std::string>> names;size_t count=0;
    auto check=[&](int depth,Json::parse_event_t event,Json &value){
      need(++count<=65536,"Plugin library exceeds structural limits");
      if(event==Json::parse_event_t::object_start){need(depth<=2,"Plugin library exceeds nesting limits");names.emplace_back();}
      else if(event==Json::parse_event_t::object_end)names.pop_back();
      else if(event==Json::parse_event_t::array_start)need(false,"Unexpected plugin library array");
      else if(event==Json::parse_event_t::key){need(!names.empty()&&value.get_ref<const std::string &>().size()<=320&&names.back().insert(value.get<std::string>()).second,"Duplicate or oversized plugin library key");}
      else if(event==Json::parse_event_t::value&&value.is_string())need(value.get_ref<const std::string &>().size()<=320,"Oversized plugin library text");return true;
    };
    root=Json::parse(bytes.begin(),bytes.end(),check);
  }catch(const Api::ApiError &){throw;}catch(const std::exception &e){throw Api::ApiError(-32602,e.what());}
  keys(root,{"version","revision","entries"});need(field(root,"version").is_number()&&root.at("version")==1,"Unsupported plugin library version");need(!text(field(root,"revision"),80).empty(),"Invalid plugin library revision");
  const auto &entries=field(root,"entries");need(entries.is_object()&&entries.size()<=4096,"Plugin library exceeds 4096 entries");for(auto i=entries.begin();i!=entries.end();++i){validID(i.key());preferences(i.value());}return root;
}
std::string revision(){GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Cannot create plugin library revision");wchar_t value[39]{};StringFromGUID2(guid,value,39);return "library:"+utf8(std::wstring(value+1,36));}
}
std::string PluginLibrary::identifier(const Json &d){
  const auto format=text(field(d,"format"),16);Json identity;
  if(format=="VST3"){
    auto path=std::filesystem::u8path(text(field(d,"path"),8192));need(path.is_absolute(),"VST3 library path must be absolute");auto id=text(field(d,"classID"),32);need(id.size()==32&&std::all_of(id.begin(),id.end(),[](auto c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}),"Invalid VST3 library class");for(auto &c:id)if(c>='a'&&c<='f')c-=32;
    identity=Json::array({format,utf8(path.lexically_normal().make_preferred().native()),id});
  }else if(format=="Built-in"){auto id=text(field(d,"classID"),128);need(!id.empty(),"Missing built-in library identity");identity=Json::array({format,id});}
  else{need(format=="AU","Unknown plugin library format");identity=Json::array({format});for(auto k:{"type","subtype","manufacturer"}){const auto &v=field(d,k);need(v.is_number_integer()&&v>=0&&v<=UINT32_MAX,"Invalid AU library identity");identity.push_back(v);}}
  const auto bytes=identity.dump();return "p"+hash(std::as_bytes(std::span(bytes.data(),bytes.size())));
}
PluginLibrary::Json PluginLibrary::read() const {pathCheck(path_);Lock lock(*path_);return readFile(*path_);}
PluginLibrary::Json PluginLibrary::set(const std::string &expected,const std::string &id,const Json &patch,bool dry) const {
  need(!text(Json(expected),80).empty(),"Provide the current library revision");validID(id);keys(patch,{"favorite","hidden","category"});need(!patch.empty(),"Provide a library preference");pathCheck(path_);Lock lock(*path_);auto root=readFile(*path_);
  if(root.at("revision").get<std::string>()!=expected)throw Api::ApiError(-32001,"Plugin library changed; reload before editing preferences");
  auto &entries=root["entries"];const auto old=entries.value(id,defaults());auto next=old;
  for(auto k:{"favorite","hidden"})if(patch.contains(k))next[k]=boolean(patch.at(k));if(patch.contains("category"))next["category"]=trim(patch.at("category"));
  const bool changed=next!=old;if(next==defaults())entries.erase(id);else entries[id]=next;need(entries.size()<=4096,"Plugin library exceeds 4096 customized entries");
  if(changed&&!dry){root["revision"]=revision();const auto bytes=root.dump();need(bytes.size()<=maximumBytes,"Plugin library exceeds 2 MiB");std::filesystem::create_directories(path_->parent_path());Project::writeProjectFile(*path_,std::as_bytes(std::span(bytes.data(),bytes.size())),true);}
  return {{"libraryRevision",root.at("revision")},{"wouldChange",changed},{"written",changed&&!dry},{"catalogID",id},{"preferences",next}};
}
PluginLibrary::Json PluginLibrary::decorate(const Json &plugins,const Json &library){
  Json result=Json::array();for(const auto &plugin:plugins){const auto id=identifier(plugin);const auto p=library.at("entries").value(id,defaults());auto entry=plugin;entry["descriptor"]=plugin;entry["catalogID"]=id;entry["favorite"]=p.at("favorite");entry["hidden"]=p.at("hidden");entry["customCategory"]=p.at("category");entry["category"]=p.at("category").get<std::string>().empty()?Json(plugin.value("isInstrument",false)?"Instruments":"Effects"):p.at("category");result.push_back(std::move(entry));}return result;
}
}
