#include "windows/Samples/LibraryIndex.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <set>
using namespace ScreamSeq::Samples;
namespace fs=std::filesystem;
static void check(bool value,const char *what){if(!value)throw std::runtime_error(what);}
template<class F>static void rejects(F f,const char *what){bool rejected=false;try{f();}catch(const std::exception &){rejected=true;}check(rejected,what);}
static std::string text(const fs::path &p){const auto s=p.u8string();return {reinterpret_cast<const char *>(s.data()),s.size()};}
static void file(const fs::path &path){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<"Indexing does not decode files";check(bool(out),"write fixture");}
struct Folder {
  fs::path base,path;
  explicit Folder(const fs::path &p):base(fs::canonical(p)),path(base/("sample-index-"+std::to_string(GetCurrentProcessId()))){check(path.parent_path()==base&&fs::create_directory(path),"create unique owned fixture directory");}
  ~Folder(){if(path.parent_path()==base&&path.filename()=="sample-index-"+std::to_string(GetCurrentProcessId())){std::error_code error;fs::remove_all(path,error);}}
};
int main(int argc,char **argv){try{
  check(argc==2,"one absolute fixture parent required");Folder owned(fs::u8path(argv[1]));const auto root=owned.path/L"Éléctric Pack";
  file(root/L"Drums/Kick 2.wav");file(root/L"Drums/Kick 10.WAV");file(root/L"Drums/Dry Kick.aiff");file(root/L"Drums/Drums/Noisy Snare.ogg");file(root/L"Basses/Deep Bass.flac");
  file(root/L"Keys/048 Piano C3.wav");file(root/L"Keys/052 Piano E3.wav");file(root/L"Keys/055 Piano G3.wav");
  file(root/L"Keys/Soft Pad C2.aif");file(root/L"Keys/Soft Pad F2.aif");file(root/L"Keys/Soft Pad C2.wav");
  file(root/L".hidden/Secret.wav");file(root/L"__MACOSX/Secret.wav");file(root/L"Synth.vst3/Secret.wav");file(root/L"Drums/notes.txt");file(root/L"Hidden.wav");
  check(SetFileAttributesW((root/L"Hidden.wav").c_str(),FILE_ATTRIBUTE_HIDDEN),"mark hidden fixture");
  auto index=LibraryIndex::scan({text(root),text(root/L"Drums"),text(root)});
  check(index->count()==11&&index->roots().size()==2,"extension/hidden/package filtering and overlap deduplication");
  auto all=index->search({});check(all["total"]==11&&all["items"].size()==11,"complete search");
  LibraryQuery query;query.text="electric kick";auto result=index->search(query);check(result["total"]==3,"diacritic and all-term folder search");
  query.text="kick -dry";result=index->search(query);check(result["total"]==2&&result["items"][0]["name"]=="Kick 2.wav"&&result["items"][1]["name"]=="Kick 10.WAV","exclusion and natural order");
  query.text="\"dry kick\"";check(index->search(query)["total"]==1,"quoted phrase");
  query.text="";query.tags={"DRUMS","electric pack"};result=index->search(query);check(result["total"]==4,"AND folded inherited tags");
  check(std::any_of(result["tags"].begin(),result["tags"].end(),[](const auto &tag){return tag["name"]=="Drums"&&tag["count"]==4;}),"repeated ancestor tag counted more than once");
  query.tagText="elec";result=index->search(query);check(result["tags"].size()==1&&result["tags"][0]["count"]==4,"tag query");
  query={};query.offset=3;query.limit=2;result=index->search(query);check(result["total"]==11&&result["items"].size()==2&&result["items"][0]==all["items"][3],"pagination retains total/order");
  query={};query.root=text(root/L"Drums");check(index->search(query)["total"]==4,"overlapping root filters by path not first indexing root");
  query.root=text(root/L"Drum");check(index->search(query)["total"]==0,"root prefix must end at a directory boundary");
  query={};query.limit=0;rejects([&]{index->search(query);},"zero limit");query.limit=1001;rejects([&]{index->search(query);},"oversized limit");query.limit=1;query.offset=250001;rejects([&]{index->search(query);},"oversized offset");
  auto group=index->multisample(text(root/L"Keys/052 Piano E3.wav"));check(group.is_object()&&group["count"]==3&&group["name"]=="Piano"&&group["suggestedOctaveShift"]==1,"numbered octave detection");
  check(group["samples"][0]["semitone"]==36&&group["samples"][1]["semitone"]==40&&group["samples"][2]["semitone"]==43,"pitch sorting");
  check(group==index->multisample(text(root/L"Keys/048 Piano C3.wav")),"group lookup identity");
  auto pad=index->multisample(text(root/L"Keys/Soft Pad C2.aif"));check(pad["count"]==2&&pad["suggestedOctaveShift"]==0,"unnumbered family detection");
  check(index->multisample(text(root/L"Keys/Soft Pad C2.wav")).is_null(),"different extensions grouped together");
  check(index->multisample(text(root/L"Drums/Kick 2.wav")).is_null(),"non-pitched name grouped");
  for(const auto &name:{"Bass C#3.wav","Bass C♯3.wav"}){auto note=LibraryIndex::parseFilename(name);check(note&&note->semitone==37&&note->family=="Bass","sharp parsing");}
  for(const auto &name:{"Bass Db3.wav","Bass D♭3.wav"})check(LibraryIndex::parseFilename(name)->semitone==37,"flat parsing");
  check(LibraryIndex::parseFilename("Piano C-1.wav")->semitone==-12,"negative filename octave");
  check(LibraryIndex::parseFilename("001 - Warm_Piano [C2].wav")->family=="Warm Piano","prefix/separator/brace family cleanup");
  check(!LibraryIndex::parseFilename("PianoC2.wav")&&!LibraryIndex::parseFilename("C2Piano.wav")&&!LibraryIndex::parseFilename("C2-G2 Piano.wav")&&!LibraryIndex::parseFilename("C-5 Piano.wav")&&!LibraryIndex::parseFilename("C11 Piano.wav"),"ambiguous or invalid note tokens accepted");
  check(!LibraryIndex::parseFilename("éC2.wav")&&!LibraryIndex::parseFilename("C2é.wav"),"Unicode note boundaries");
  auto saved=index->snapshot();auto loaded=LibraryIndex::fromSnapshot(saved);check(loaded->search({})==all&&loaded->multisample(text(root/L"Keys/052 Piano E3.wav"))==group,"cache roundtrip changed index or family");
  auto malformed=saved;malformed["entries"][0]["path"]=text(root/L"../outside.wav");rejects([&]{LibraryIndex::fromSnapshot(malformed);},"cache path escape accepted");
  malformed=saved;malformed["entries"].push_back(malformed["entries"][0]);rejects([&]{LibraryIndex::fromSnapshot(malformed);},"cache duplicate accepted");
  malformed=saved;malformed["entries"][0]["folders"]=Json::array({"Forged"});rejects([&]{LibraryIndex::fromSnapshot(malformed);},"cache folder tags inconsistent with path");
  malformed=saved;malformed["entries"][0]["bytes"]=-1;rejects([&]{LibraryIndex::fromSnapshot(malformed);},"negative cached size");
  bool cancelled=false;rejects([&]{LibraryIndex::scan({text(root)},[&]{cancelled=true;return true;});},"cancelled scan published");check(cancelled,"scan did not check cancellation");
  auto unavailable=LibraryIndex::scan({text(root/L"Missing")});check(unavailable->count()==0&&unavailable->warnings().size()==1,"unavailable root warning");
  auto again=LibraryIndex::scan({text(root)});check(again->multisample(text(root/L"Keys/048 Piano C3.wav"))["id"]==group["id"],"family identity changed on rescan");
  std::cout<<"PASS native sample index filtering, overlap deduplication, Unicode search/tags, pagination, note-family detection, octave conventions, cache validation and cancellation\n";
  return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
