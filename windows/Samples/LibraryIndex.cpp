#include "LibraryIndex.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace ScreamSeq::Samples {
namespace {
void need(bool value,const char *message){if(!value)throw std::invalid_argument(message);}
std::wstring wide(const std::string &s){
  need(s.size()<=16384&&s.find('\0')==s.npos,"Invalid or oversized sample library text");
  if(s.empty())return {};
  const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
  need(n>0&&n<=4096,"Invalid sample library Unicode");std::wstring out(n,0);
  MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n);return out;
}
std::string utf8(const std::wstring &s){
  if(s.empty())return {};
  const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
  need(n>0,"Invalid sample library Unicode");std::string out(n,0);
  WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;
}
std::string pathText(const std::filesystem::path &p){return utf8(p.native());}
std::string rootLabel(const std::filesystem::path &p){return pathText(p.filename().empty()?p.root_name():p.filename());}
std::string pathKey(const std::string &s){
  auto w=wide(s);for(auto &c:w)if(c==L'/')c=L'\\';
  if(w.empty())return {};
  const auto n=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,w.data(),int(w.size()),nullptr,0,nullptr,nullptr,0);
  need(n>0,"Cannot normalize sample library path");std::wstring out(n,0);
  LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,w.data(),int(w.size()),out.data(),n,nullptr,nullptr,0);return utf8(out);
}
bool within(const std::string &path,const std::string &root){auto prefix=pathKey(root);if(!prefix.ends_with('\\'))prefix+='\\';return pathKey(path).starts_with(prefix);}
bool space(wchar_t c){WORD kind=0;return GetStringTypeW(CT_CTYPE1,&c,1,&kind)&&(kind&C1_SPACE);}
bool alphanumeric(wchar_t c){WORD kind=0;return GetStringTypeW(CT_CTYPE1,&c,1,&kind)&&(kind&(C1_ALPHA|C1_DIGIT));}
bool digit(wchar_t c){return c>=L'0'&&c<=L'9';}
bool naturalLess(const std::string &a,const std::string &b){
  const auto x=wide(a),y=wide(b);const auto order=CompareStringEx(LOCALE_NAME_INVARIANT,NORM_IGNORECASE|SORT_DIGITSASNUMBERS,x.data(),int(x.size()),y.data(),int(y.size()),nullptr,nullptr,0);
  return order==CSTR_LESS_THAN||(order==CSTR_EQUAL&&a<b);
}
std::string timestamp(){SYSTEMTIME t{};GetSystemTime(&t);char out[32]{};std::snprintf(out,sizeof(out),"%04u-%02u-%02uT%02u:%02u:%02uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);return out;}
std::string hash(const std::string &s){
  std::array<UCHAR,32> digest{};
  if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char *>(s.data())),ULONG(s.size()),digest.data(),ULONG(digest.size()))<0)throw std::runtime_error("Cannot hash sample family");
  std::string out;out.reserve(64);for(auto b:digest){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;
}
std::vector<std::pair<std::string,bool>> terms(const std::string &text){
  std::vector<std::pair<std::string,bool>> out;std::wstring current;bool quoted=false;
  auto finish=[&]{if(current.empty())return;const bool excluded=current.size()>1&&current[0]==L'-';out.emplace_back(LibraryIndex::fold(utf8(current.substr(excluded?1:0))),excluded);current.clear();};
  for(auto c:wide(text)){if(c==L'"')quoted=!quoted;else if(space(c)&&!quoted)finish();else current+=c;}finish();return out;
}
const std::string &stringField(const Json &j,const char *key){need(j.contains(key)&&j.at(key).is_string(),"Invalid sample index text field");const auto &v=j.at(key).get_ref<const std::string &>();(void)wide(v);return v;}
std::vector<std::string> strings(const Json &j,size_t maximum){need(j.is_array()&&j.size()<=maximum,"Invalid sample index string list");std::vector<std::string> out;for(const auto &v:j){need(v.is_string(),"Invalid sample index string");auto s=v.get<std::string>();(void)wide(s);out.push_back(std::move(s));}return out;}
}

Json LibraryEntry::dictionary() const{return {{"path",path},{"root",root},{"name",name},{"folders",folders},{"bytes",bytes},{"modified",modified}};}
const std::vector<std::string> &LibraryIndex::extensions(){static const std::vector<std::string> value={"8svx","aif","aifc","aiff","au","brr","caf","flac","iff","its","mp3","ogg","s3i","snd","w64","wav","wave"};return value;}
std::string LibraryIndex::canonicalPath(const std::string &path){
  const std::filesystem::path p(wide(path));need(p.is_absolute(),"Use an absolute sample path");
  std::error_code error;auto canonical=std::filesystem::canonical(p,error);if(error)canonical=p.lexically_normal();return pathText(canonical.make_preferred());
}
std::string LibraryIndex::fold(const std::string &s){
  auto w=wide(s);if(w.empty())return {};
  int size=NormalizeString(NormalizationD,w.data(),int(w.size()),nullptr,0);need(size>0,"Cannot normalize sample library text");
  std::wstring normalized(size,0);size=NormalizeString(NormalizationD,w.data(),int(w.size()),normalized.data(),int(normalized.size()));need(size>0,"Cannot normalize sample library text");normalized.resize(size);
  std::wstring stripped;stripped.reserve(normalized.size());for(auto c:normalized){WORD kind=0;GetStringTypeW(CT_CTYPE3,&c,1,&kind);if(!(kind&(C3_NONSPACING|C3_DIACRITIC)))stripped+=c;}
  if(stripped.empty())return {};
  size=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,stripped.data(),int(stripped.size()),nullptr,0,nullptr,nullptr,0);need(size>0,"Cannot fold sample library text");std::wstring lowered(size,0);
  LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,stripped.data(),int(stripped.size()),lowered.data(),size,nullptr,nullptr,0);return utf8(lowered);
}
std::optional<FilenameNote> LibraryIndex::parseFilename(const std::string &filename){
  const auto stem=std::filesystem::path(wide(filename)).stem().native();size_t start=0,end=0;int pitch=0,matches=0;
  for(size_t i=0;i<stem.size();++i){
    if(i&&alphanumeric(stem[i-1]))continue;auto letter=stem[i];if(letter>=L'a'&&letter<=L'g')letter-=32;if(letter<L'A'||letter>L'G')continue;
    size_t j=i+1;int accidental=0;if(j<stem.size()){const auto c=stem[j];if(c==L'#'||c==L'♯'){accidental=1;++j;}else if(c==L'b'||c==L'♭'){accidental=-1;++j;}}
    int sign=1;if(j<stem.size()&&stem[j]==L'-'){sign=-1;++j;}const auto first=j;int octave=0;
    while(j<stem.size()&&digit(stem[j])&&j-first<2){octave=octave*10+(stem[j]-L'0');++j;}
    if(j==first||(j<stem.size()&&alphanumeric(stem[j])))continue;
    if(++matches>1)return {};octave*=sign;if(octave<-4||octave>10)return {};
    static constexpr int pitches[]={9,11,0,2,4,5,7};pitch=octave*12+pitches[letter-L'A']+accidental;start=i;end=j;i=j-1;
  }
  if(matches!=1)return {};
  auto family=stem.substr(0,start)+L" "+stem.substr(end);std::optional<int> number;size_t first=0;while(first<family.size()&&space(family[first]))++first;
  size_t j=first;int prefix=0;while(j<family.size()&&digit(family[j])&&j-first<3){prefix=prefix*10+(family[j]-L'0');++j;}
  auto separator=[](wchar_t c){return space(c)||c==L'.'||c==L'_'||c==L'-';};
  if(j>first&&j<family.size()&&separator(family[j])){while(j<family.size()&&separator(family[j]))++j;number=prefix;family.erase(0,j);}
  std::wstring normalized;for(auto c:family){if(space(c)||c==L'_'){if(normalized.empty()||normalized.back()!=L' ')normalized+=L' ';}else normalized+=c;}
  constexpr std::wstring_view trim=L" ._-()[]{}";first=normalized.find_first_not_of(trim);family=first==std::wstring::npos?L"":normalized.substr(first,normalized.find_last_not_of(trim)-first+1);
  return FilenameNote{utf8(family),utf8(stem.substr(start,end-start)),pitch,number};
}
LibraryIndex::LibraryIndex(std::vector<LibraryEntry> entries,std::vector<std::string> roots,std::vector<std::string> warnings,std::string indexedAt)
 :entries_(std::move(entries)),roots_(std::move(roots)),warnings_(std::move(warnings)),indexedAt_(indexedAt.empty()?timestamp():std::move(indexedAt)){need(entries_.size()<=maximumFiles,"Sample index exceeds 250,000 files");prepare();}
void LibraryIndex::prepare(){
  struct Member{size_t index;FilenameNote note;};std::map<std::string,std::vector<Member>> families;
  searchable_.reserve(entries_.size());
  for(size_t i=0;i<entries_.size();++i){const auto &entry=entries_[i];SearchRecord record;record.pathKey=pathKey(entry.path);std::map<std::string,std::string> tags;
    for(const auto &folder:entry.folders){tags[fold(folder)]=folder;if(!record.text.empty())record.text+=' ';record.text+=folder;}record.text+=' ';record.text+=entry.name;record.text=fold(record.text);record.tags.assign(tags.begin(),tags.end());searchable_.push_back(std::move(record));
    if(auto note=parseFilename(entry.name)){const auto folder=std::filesystem::path(wide(entry.path)).parent_path();if(note->family.empty())note->family=pathText(folder.filename());const auto extension=pathText(std::filesystem::path(wide(entry.name)).extension());const auto key=pathText(folder)+'\0'+fold(note->family)+'\0'+fold(extension.empty()?extension:extension.substr(1));families[key].push_back({i,std::move(*note)});}
  }
  for(auto &[key,members]:families){
    if(members.size()<2)continue;std::set<int> notes;for(const auto &m:members)notes.insert(m.note.semitone);if(notes.size()<2)continue;
    std::sort(members.begin(),members.end(),[&](const auto &a,const auto &b){return a.note.semitone==b.note.semitone?entries_[a.index].path<entries_[b.index].path:a.note.semitone<b.note.semitone;});
    bool numbered=members.size()>=3;std::optional<int> offset;
    for(const auto &m:members){if(!m.note.number||*m.note.number<0||*m.note.number>=120){numbered=false;break;}const auto n=*m.note.number-m.note.semitone;if(offset&&*offset!=n)numbered=false;offset=n;}
    numbered=numbered&&offset&&*offset%12==0&&*offset>=-48&&*offset<=48;const auto shift=numbered?*offset/12:0;
    Json sources=Json::array();for(const auto &m:members){const auto &e=entries_[m.index];sources.push_back({{"path",e.path},{"filename",e.name},{"sourceNote",m.note.label},{"semitone",m.note.semitone}});}
    const auto explanation=numbered?"Filename numbers agree across all notes; suggested octave offset "+std::string(shift>=0?"+":"")+std::to_string(shift)+". Check the tracker roots below.":"Roots follow filename octaves. Adjust the octave offset if this pack uses a different convention.";
    auto group=std::make_shared<const Json>(Json{{"id",hash(key)},{"name",members[0].note.family},{"folder",pathText(std::filesystem::path(wide(entries_[members[0].index].path)).parent_path())},{"count",members.size()},{"samples",std::move(sources)},{"suggestedOctaveShift",shift},{"explanation",explanation}});
    for(const auto &m:members)multisamples_[pathKey(entries_[m.index].path)]=group;
  }
}
std::shared_ptr<const LibraryIndex> LibraryIndex::scan(const std::vector<std::string> &paths,const std::function<bool()> &cancelled){
  need(paths.size()<=32,"Use at most 32 sample folders");std::vector<std::string> roots,warnings;std::vector<LibraryEntry> entries;std::set<std::string> seen,rootKeys;
  auto cancel=[&]{if(cancelled&&cancelled())throw std::runtime_error("Sample library scan cancelled");};
  auto warn=[&](const std::string &s){if(warnings.size()<20)warnings.push_back(s);};
  for(const auto &path:paths){cancel();const auto root=canonicalPath(path);if(!rootKeys.insert(pathKey(root)).second)continue;roots.push_back(root);const std::filesystem::path base(wide(root));std::error_code error;
    if(!std::filesystem::is_directory(base,error)){warn("Folder is unavailable: "+root);continue;}
    std::filesystem::recursive_directory_iterator iterator(base,std::filesystem::directory_options::skip_permission_denied,error),end;
    if(error){warn("Cannot read "+root);continue;}
    while(iterator!=end){cancel();const auto file=iterator->path();WIN32_FILE_ATTRIBUTE_DATA info{};const bool available=GetFileAttributesExW(file.c_str(),GetFileExInfoStandard,&info)!=0;const auto name=file.filename().native();
      const auto extension=fold(pathText(file.extension()));const bool package=extension==".vst3"||extension==".app"||extension==".component";
      const bool skip=!available||name==L"__MACOSX"||name.starts_with(L'.')||package||(info.dwFileAttributes&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_REPARSE_POINT));
      if(skip)iterator.disable_recursion_pending();
      else if(!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_DEVICE))&&extension.size()>1&&std::binary_search(extensions().begin(),extensions().end(),extension.substr(1))){
        const auto filePath=pathText(file);if(seen.insert(pathKey(filePath)).second){LibraryEntry entry;entry.path=filePath;entry.root=root;entry.name=utf8(name);entry.folders.push_back(rootLabel(base));
          for(const auto &folder:file.lexically_relative(base).parent_path())entry.folders.push_back(pathText(folder));
          entry.bytes=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;ULARGE_INTEGER time{};time.LowPart=info.ftLastWriteTime.dwLowDateTime;time.HighPart=info.ftLastWriteTime.dwHighDateTime;entry.modified=(double(time.QuadPart)-116444736000000000.)/10000000.;entries.push_back(std::move(entry));
          need(entries.size()<maximumFiles,"Library exceeds 250,000 files. Choose smaller sample folders.");
        }
      }
      iterator.increment(error);if(error){warn("Cannot read part of "+root);error.clear();}
    }
  }
  cancel();
  { // Convert each path once, not for every comparison of a large sample pack.
    std::vector<std::wstring> names;names.reserve(entries.size());for(const auto &entry:entries)names.push_back(wide(entry.path));
    std::vector<size_t> order(entries.size());std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[&](size_t a,size_t b){const auto &x=names[a],&y=names[b];const auto n=CompareStringEx(LOCALE_NAME_INVARIANT,NORM_IGNORECASE|SORT_DIGITSASNUMBERS,x.data(),int(x.size()),y.data(),int(y.size()),nullptr,nullptr,0);return n==CSTR_LESS_THAN||(n==CSTR_EQUAL&&entries[a].path<entries[b].path);});
    std::vector<LibraryEntry> sorted;sorted.reserve(entries.size());for(auto i:order)sorted.push_back(std::move(entries[i]));entries=std::move(sorted);
  }
  cancel();return std::make_shared<LibraryIndex>(std::move(entries),std::move(roots),std::move(warnings));
}
Json LibraryIndex::search(const LibraryQuery &query) const {
  need(query.limit>=1&&query.limit<=1000&&query.offset<=maximumFiles&&query.tags.size()<=32,"Invalid sample library query bounds");
  const auto parsed=terms(query.text);const auto tagText=fold(query.tagText);std::set<std::string> wanted;for(const auto &tag:query.tags)wanted.insert(fold(tag));
  std::optional<std::string> prefix;if(query.root){prefix=pathKey(canonicalPath(*query.root));if(!prefix->ends_with('\\'))*prefix+='\\';}
  Json items=Json::array();size_t total=0;std::map<std::string,std::pair<std::string,size_t>> counts;
  for(size_t i=0;i<entries_.size();++i){const auto &record=searchable_[i];if(prefix&&!record.pathKey.starts_with(*prefix))continue;
    if(!std::all_of(parsed.begin(),parsed.end(),[&](const auto &t){return (record.text.find(t.first)!=std::string::npos)!=t.second;}))continue;
    if(!std::all_of(wanted.begin(),wanted.end(),[&](const auto &tag){return std::any_of(record.tags.begin(),record.tags.end(),[&](const auto &t){return t.first==tag;});}))continue;
    if(total>=query.offset&&items.size()<query.limit)items.push_back(entries_[i].dictionary());++total;
    for(const auto &[key,name]:record.tags)if(tagText.empty()||key.find(tagText)!=std::string::npos){auto [it,inserted]=counts.try_emplace(key,name,0);++it->second.second;}
  }
  std::vector<std::pair<std::string,size_t>> facets;facets.reserve(counts.size());for(const auto &[key,value]:counts)facets.push_back(value);
  std::sort(facets.begin(),facets.end(),[](const auto &a,const auto &b){return a.second==b.second?naturalLess(a.first,b.first):a.second>b.second;});Json tags=Json::array();for(size_t i=0;i<std::min<size_t>(1000,facets.size());++i)tags.push_back({{"name",facets[i].first},{"count",facets[i].second}});
  return {{"items",std::move(items)},{"total",total},{"tags",std::move(tags)},{"offset",query.offset}};
}
Json LibraryIndex::multisample(const std::string &path) const {const auto found=multisamples_.find(pathKey(canonicalPath(path)));return found==multisamples_.end()?Json(nullptr):*found->second;}
Json LibraryIndex::snapshot() const {Json entries=Json::array();for(const auto &e:entries_)entries.push_back(e.dictionary());return {{"version",2},{"roots",roots_},{"entries",std::move(entries)},{"warnings",warnings_},{"indexedAt",indexedAt_}};}
std::shared_ptr<const LibraryIndex> LibraryIndex::fromSnapshot(const Json &j){
  need(j.is_object()&&j.contains("version")&&j.at("version").is_number_integer()&&j.at("version")==2,"Unsupported sample index version");auto roots=strings(j.at("roots"),32),warnings=strings(j.at("warnings"),52);const auto at=stringField(j,"indexedAt");
  need(j.at("entries").is_array()&&j.at("entries").size()<=maximumFiles,"Oversized sample index");std::vector<LibraryEntry> entries;std::set<std::string> seen;
  std::set<std::string> rootKeys;
  for(const auto &root:roots){const std::filesystem::path path(wide(root));need(path.is_absolute()&&pathText(path.lexically_normal().make_preferred())==root&&rootKeys.insert(pathKey(root)).second,"Invalid or duplicate sample index root");}
  for(const auto &item:j.at("entries")){LibraryEntry entry;entry.path=stringField(item,"path");entry.root=stringField(item,"root");entry.name=stringField(item,"name");entry.folders=strings(item.at("folders"),4096);
    need(std::find(roots.begin(),roots.end(),entry.root)!=roots.end()&&within(entry.path,entry.root)&&seen.insert(pathKey(entry.path)).second,"Invalid or duplicate cached sample path");
    const std::filesystem::path path(wide(entry.path)),base(wide(entry.root));
    need(path.is_absolute()&&pathText(path.lexically_normal().make_preferred())==entry.path&&pathText(path.filename())==entry.name,"Malformed cached sample path");
    std::vector<std::string> folders={rootLabel(base)};for(const auto &part:path.lexically_relative(base).parent_path())folders.push_back(pathText(part));need(folders==entry.folders,"Malformed cached sample folders");
    need(item.at("bytes").is_number_unsigned()||(item.at("bytes").is_number_integer()&&item.at("bytes")>=0),"Invalid sample size");entry.bytes=item.at("bytes").get<uint64_t>();need(item.at("modified").is_number(),"Invalid sample timestamp");entry.modified=item.at("modified").get<double>();need(std::isfinite(entry.modified),"Invalid sample timestamp");entries.push_back(std::move(entry));
  }
  return std::make_shared<LibraryIndex>(std::move(entries),std::move(roots),std::move(warnings),at);
}
}
