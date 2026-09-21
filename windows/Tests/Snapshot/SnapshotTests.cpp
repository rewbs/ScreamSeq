#include "editor/TrackerDocument.hpp"
#include "editor/SampleArchive.hpp"
#include "editor/SongTiming.hpp"
#include "windows/Project/NativeProject.hpp"
#include <windows.h>
#ifdef small
#undef small
#endif
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace Tracker;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string(__func__)+": "+#x); } while(false)
void titleSnapshot() {
  auto doc=std::make_unique<Document>(MOD_TYPE_MPT,4);
  const std::string title(100,'x');
  doc->transaction([&](CSoundFile &s){s.SetTitle(title);});
  CHECK(doc->song().m_songName==title);
  auto copy=std::make_unique<Document>(doc->snapshotData());
  CHECK(copy->song().m_songName==title);
  doc->undo(); doc->redo(); CHECK(doc->song().m_songName==title);
}
void sparseSnapshot() {
  auto doc=std::make_unique<Document>(MOD_TYPE_IT,4);
  doc->transaction([](CSoundFile &s){CHECK(s.Patterns.Insert(2,64));});
  auto copy=std::make_unique<Document>(doc->snapshotData());
  CHECK(!copy->song().Patterns.IsValidPat(1));
  copy->restoreNative(doc->native());
  doc->addPattern(64,false,0); doc->undo();
  CHECK(!doc->song().Patterns.IsValidPat(1));
  doc->native().validate(doc->song());
}
using Json = nlohmann::json;
Json core(const Document &doc) {
  const auto &s=doc.song();
  Json result={{"title",s.m_songName},{"type",uint32_t(s.GetType())},
    {"channels",s.GetNumChannels()},{"slots",s.Patterns.Size()},{"selected",s.Order.GetCurrentSequenceIndex()}};
  result["patterns"]=Json::array();
  for(PATTERNINDEX index=0;index<s.Patterns.Size();++index) {
    if(!s.Patterns.IsValidPat(index)) { result["patterns"].push_back(nullptr); continue; }
    const auto &p=s.Patterns[index];
    Json cells=Json::array();
    for(const auto &c:p) cells.push_back({c.note,c.instr,uint8_t(c.volcmd),c.vol,uint8_t(c.command),c.param});
    result["patterns"].push_back({{"rows",p.GetNumRows()},{"name",p.GetName()},
      {"beat",p.GetRowsPerBeat()},{"measure",p.GetRowsPerMeasure()},{"color",p.GetColor()},
      {"groove",std::vector<uint32_t>(p.GetTempoSwing().begin(),p.GetTempoSwing().end())},{"cells",cells}});
  }
  result["orders"]=Json::array();
  for(const auto &seq:s.Order) result["orders"].push_back({{"items",std::vector<PATTERNINDEX>(seq.begin(),seq.end())},
    {"name",::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8,seq.GetName())},
    {"restart",seq.GetRestartPos()},{"tempo",seq.GetDefaultTempo().GetRaw()},{"speed",seq.GetDefaultSpeed()}});
  auto timing=songTiming(s);
  result["timing"]={uint32_t(timing.mode),timing.rowsPerBeat,timing.rowsPerMeasure,timing.groove};
  return result;
}
std::unique_ptr<Document> complexSong(MODTYPE type) {
  auto doc=std::make_unique<Document>(type,4);
  doc->transaction([&](CSoundFile &s){
    s.m_modFormat.charset=::OpenMPT::mpt::Charset::UTF8;
    s.SetTitle(std::string(70,'x')+" 日本語 🎵 é");
    s.Patterns.ResizeArray(14);
    CHECK(s.Patterns.Insert(2,64)); CHECK(s.Patterns.Insert(7,64)); CHECK(s.Patterns.Insert(9,64));
    CHECK(s.Patterns.Insert(5,type==MOD_TYPE_MOD || type==MOD_TYPE_S3M ? 64 : 17));
    CHECK(s.Patterns.Insert(11,64)); // Nonempty, unreferenced trailing pattern.
    for(auto index:{0,2,5,7,11}) {
      auto &p=s.Patterns[index];
      p.SetName(std::string(50,'p')+" 音楽"); p.SetColor(0x123456);
      CHECK(p.SetSignature(3,12));
      TempoSwing swing; swing.assign({TempoSwing::Unity/2,TempoSwing::Unity,TempoSwing::Unity*3/2}); p.SetTempoSwing(swing);
      auto &c=*p.GetpModCommand(p.GetNumRows()-1,3); c.note=49; c.instr=1;
      c.volcmd=VOLCMD_VOLUME; c.vol=31; c.command=CMD_VIBRATO; c.param=0x37;
    }
    s.Order().assign(7,0); s.Order()[0]=2; s.Order()[2]=2; s.Order()[3]=PATTERNINDEX_SKIP;
    s.Order()[4]=7; s.Order()[5]=s.Order()[6]=PATTERNINDEX_INVALID;
    s.Order().SetRestartPos(1); s.Order().SetName(U_("Sequence 音楽"));
    s.Order().SetDefaultTempo(TEMPO(137.5));
    if(type==MOD_TYPE_MPT) {
      CHECK(s.Order.AddSequence()==1); s.Order().assign(3,2); s.Order()[2]=PATTERNINDEX_INVALID;
      s.Order().SetDefaultTempo(TEMPO(155.25)); s.Order().SetDefaultSpeed(4);
      s.Order().SetName(U_("Another sequence 🎵"));
    }
  });
  return doc;
}
void structureConservation() {
  for(auto type:{MOD_TYPE_MOD,MOD_TYPE_XM,MOD_TYPE_S3M,MOD_TYPE_IT,MOD_TYPE_MPT}) {
    auto doc=complexSong(type); const auto expected=core(*doc); const auto native=doc->native();
    auto copy=std::make_unique<Document>(doc->snapshotData());
    if(core(*copy)!=expected) std::cerr<<"format "<<uint32_t(type)<<" diff "<<Json::diff(expected,core(*copy)).dump()<<'\n';
    CHECK(core(*copy)==expected); copy->restoreNative(native); CHECK(copy->native()==native);
    auto renderer=std::make_unique<Renderer>(doc->playbackData(),48000,0,false,"",doc->song().Order.GetCurrentSequenceIndex());
    CHECK(renderer->song().m_songName==doc->song().m_songName);
    CHECK(!renderer->song().Patterns.IsValidPat(1)); CHECK(renderer->song().Patterns[7]==doc->song().Patterns[7]);
    doc->addPattern(64,false,0); const auto after=core(*doc); const auto afterNative=doc->native();
    for(int cycle=0;cycle<3;++cycle) {
      doc->undo(); CHECK(core(*doc)==expected); doc->native().validate(doc->song());
      CHECK(doc->native().patterns==native.patterns); CHECK(doc->native().sequences==native.sequences);
      doc->redo(); CHECK(core(*doc)==after); CHECK(doc->native()==afterNative); doc->native().validate(doc->song());
    }
    const auto revision=doc->revision;
    const auto canUndo=doc->canUndo(), canRedo=doc->canRedo();
    try { doc->transaction([](CSoundFile &s){s.SetTitle("broken");s.Patterns.Remove(7);throw std::runtime_error("abort");}); }
    catch(const std::runtime_error &) {}
    CHECK(core(*doc)==after); CHECK(doc->native()==afterNative); CHECK(doc->revision==revision);
    CHECK(doc->canUndo()==canUndo); CHECK(doc->canRedo()==canRedo);
    doc->undo(); CHECK(core(*doc)==expected); doc->redo(); CHECK(core(*doc)==after);
    auto dir=std::filesystem::u8path(std::getenv("TMPDIR"));
    const auto file=dir/("snapshot-conservation-"+std::to_string(uint32_t(type))+".screamseq");
    auto state=ScreamSeq::Project::newProjectState(*doc);
    ScreamSeq::Project::saveNativeProject(*doc,state,file,true);
    auto reopened=ScreamSeq::Project::openNativeProject(file);
    CHECK(core(*reopened.document)==after); CHECK(reopened.document->native()==afterNative);
    CHECK(reopened.state.preserved.at("version")==4); CHECK(reopened.state.preserved.at("native").at("version")==14);
    std::filesystem::remove(file);
  }
}
template<typename F> void rejects(F &&operation) {
  bool threw=false;
  try { operation(); } catch(const std::exception &) { threw=true; }
  CHECK(threw);
}
void moduleExportLoss() {
  auto doc=std::make_unique<Document>(MOD_TYPE_MPT,4);
  doc->transaction([](CSoundFile &s){s.SetTitle(std::string(100,'x'));});
  rejects([&]{doc->validateModuleSampleExport();});
  const auto file=std::filesystem::u8path(std::getenv("TMPDIR"))/"snapshot-module-export.mptm";
  for(bool preserve:{false,true}) rejects([&]{doc->save(file.string(),preserve);});
  CHECK(!std::filesystem::exists(file));
  doc=std::make_unique<Document>(MOD_TYPE_IT,4);
  doc->transaction([](CSoundFile &s){s.m_modFormat.charset=::OpenMPT::mpt::Charset::UTF8;s.SetTitle("🎵");});
  rejects([&]{doc->validateModuleSampleExport();}); // Same bytes in another charset is also title loss.
  doc=std::make_unique<Document>(MOD_TYPE_IT,4);
  doc->transaction([](CSoundFile &s){CHECK(s.Patterns.Insert(2,64));});
  rejects([&]{doc->validateModuleSampleExport();});
}
void exactPatternGroove() {
  auto doc=std::make_unique<Document>(MOD_TYPE_IT,4);
  doc->transaction([](CSoundFile &s){
    auto &p=s.Patterns[0]; CHECK(p.SetSignature(16,64));
    TempoSwing swing; swing.assign(16,TempoSwing::Unity/4); swing[0]=TempoSwing::Unity*4;
    p.SetTempoSwing(swing);
  });
  CHECK(doc->song().Patterns[0].GetTempoSwing()[0]>TempoSwing::Unity*4);
  const auto expected=core(*doc);
  auto copy=std::make_unique<Document>(doc->snapshotData()); CHECK(core(*copy)==expected);
  doc->undo(); doc->redo(); CHECK(core(*doc)==expected);
}
void titlesAndLegacy() {
  for(auto type:{MOD_TYPE_MOD,MOD_TYPE_XM,MOD_TYPE_S3M,MOD_TYPE_IT,MOD_TYPE_MPT}) {
    auto doc=std::make_unique<Document>(type,4);
    std::string unicode; for(int i=0;i<25;++i) unicode+="🎵";
    CHECK(unicode.size()==100);
    for(const auto &title:std::vector<std::string>{std::string(100,'x'),unicode," 日本語 é "}) {
      doc->transaction([&](CSoundFile &s){s.m_modFormat.charset=::OpenMPT::mpt::Charset::UTF8;s.SetTitle(title);});
      auto copy=std::make_unique<Document>(doc->snapshotData());
      CHECK(copy->song().m_songName==title); CHECK(copy->song().GetCharsetInternal()==doc->song().GetCharsetInternal());
      doc->undo(); doc->redo(); CHECK(doc->song().m_songName==title);
      const auto file=std::filesystem::u8path(std::getenv("TMPDIR"))/std::filesystem::u8path("snapshot-exact-title-音楽.screamseq");
      for(unsigned version:{4,5}) {
        auto state=ScreamSeq::Project::newProjectState(*doc); state.preserved["version"]=version;
        ScreamSeq::Project::saveNativeProject(*doc,state,file,true);
        auto reopened=ScreamSeq::Project::openNativeProject(file);
        CHECK(reopened.document->song().m_songName==title);
        CHECK(reopened.document->song().GetCharsetInternal()==doc->song().GetCharsetInternal());
        CHECK(reopened.document->native()==doc->native()); CHECK(reopened.state.preserved.at("version")==version);
      }
      std::filesystem::remove(file);
    }
  }
  auto original=std::make_unique<Document>(MOD_TYPE_MPT,4);
  auto module=original->serialize(); auto base=std::make_unique<Document>(module);
  auto samples=encodeSampleArchive(base->song(),base->song());
  auto legacy=packSongSnapshot(module,samples);
  CHECK(legacy[6]==std::byte{'1'}); CHECK(splitSongSnapshot(legacy).timing.empty());
  auto loaded=std::make_unique<Document>(legacy); CHECK(core(*loaded)==core(*base));
  base->song().Order().SetDefaultTempo(TEMPO(143.75));
  auto timing=encodeTimingArchive(base->song(),loaded->song()); CHECK(!timing.empty());
  legacy=packSongSnapshot(module,samples,timing); CHECK(legacy[6]==std::byte{'2'});
  loaded=std::make_unique<Document>(legacy); CHECK(core(*loaded)==core(*base));
  // A representable song needs no new extension; 1/2 framing remains minimal.
  const auto exact=encodeSampleArchive(loaded->song(),loaded->song());
  CHECK(exact==samples);
}
void malformedArchives() {
  auto doc=std::make_unique<Document>(MOD_TYPE_IT,4);
  doc->transaction([](CSoundFile &s){CHECK(s.Patterns.Insert(2,64));});
  const auto bytes=doc->snapshotData(); const auto parts=splitSongSnapshot(bytes);
  const std::string magic("RSCORE1\0",8);
  std::vector<std::byte> archive(parts.samples.begin(),parts.samples.end());
  auto pos=std::search(archive.begin(),archive.end(),reinterpret_cast<const std::byte *>(magic.data()),
    reinterpret_cast<const std::byte *>(magic.data()+magic.size()));
  CHECK(pos!=archive.end()); const auto start=size_t(pos-archive.begin());
  CHECK(archive[start+8]==std::byte{2});
  auto reject=[&](const std::vector<std::byte> &bad){
    auto packed=packSongSnapshot(parts.module,bad,parts.timing);
    rejects([&]{auto d=std::make_unique<Document>(packed);});
  };
  for(size_t size=start+1;size<archive.size();++size) reject({archive.begin(),archive.begin()+size});
  auto bad=archive; bad.push_back(std::byte{0}); reject(bad);
  bad=archive; bad.insert(bad.end(),archive.begin()+start,archive.end()); reject(bad);
  for(auto flag:{0,8,128,255}) {bad=archive;bad[start+8]=std::byte(flag);reject(bad);}
  bad=archive;bad[start+6]=std::byte{'2'};reject(bad);
  auto u16=[&](size_t offset,uint16_t value){bad=archive;bad[offset]=std::byte(value);bad[offset+1]=std::byte(value>>8);reject(bad);};
  auto u32=[&](size_t offset,uint32_t value){bad=archive;for(int i=0;i<4;++i)bad[offset+i]=std::byte(value>>(8*i));reject(bad);};
  u16(start+9,0);u16(start+9,193);u16(start+11,65535);u16(start+13,65535);u16(start+15,65535);
  u32(start+17,0);u32(start+17,4097);u32(start+33,0xffffffff);u32(start+37,0xffffffff);
  u16(start+15+26+64*4*6,0); // Duplicate, non-increasing pattern index.
  for(size_t size=0;size<20;++size) rejects([&]{splitSongSnapshot(std::span<const std::byte>(bytes).first(size));});
  bad=bytes;bad[6]=std::byte{'9'};rejects([&]{auto d=std::make_unique<Document>(bad);});
  bad=bytes;bad[8]=std::byte{0xff};rejects([&]{splitSongSnapshot(bad);});
  rejects([&]{auto d=std::make_unique<Document>(packSongSnapshot(bytes,parts.samples));});
}
void actualMacFixture() {
  const auto path=std::getenv("SCREAMSEQ_REFERENCE_PROJECT");
  if(!path || !*path) {std::cout<<"SKIP actual Mac fixture: set SCREAMSEQ_REFERENCE_PROJECT\n";return;}
  auto project=ScreamSeq::Project::openNativeProject(std::filesystem::u8path(path));
  CHECK(project.document->song().GetTitle()=="Midnight Circuit");
  CHECK(project.document->song().GetNumSamples()==4); CHECK(project.document->song().GetNumInstruments()==7);
  CHECK(project.document->native().preciseNotes.size()==4); CHECK(project.document->native().envelopeBank.size()==3);
  const auto expected=core(*project.document); const auto native=project.document->native();
  const auto plugins=project.state.preserved.at("plugins");
  auto copy=std::make_unique<Document>(project.document->snapshotData());
  CHECK(core(*copy)==expected); copy->restoreNative(native);
  const auto file=std::filesystem::u8path(std::getenv("TMPDIR"))/"snapshot-mac-fixture-copy.screamseq";
  ScreamSeq::Project::saveNativeProject(*project.document,project.state,file,true);
  auto reopened=ScreamSeq::Project::openNativeProject(file);
  CHECK(core(*reopened.document)==expected); CHECK(reopened.document->native()==native);
  CHECK(reopened.state.preserved.at("plugins")==plugins);
  project.document->transaction([](CSoundFile &s){s.SetTitle(std::string(100,'x'));});
  ScreamSeq::Project::saveNativeProject(*project.document,project.state,file,true);
  reopened=ScreamSeq::Project::openNativeProject(file);
  CHECK(reopened.document->song().m_songName==std::string(100,'x'));
  CHECK(reopened.document->native()==native); CHECK(reopened.state.preserved.at("plugins")==plugins);
  for(SAMPLEINDEX i=1;i<=copy->song().GetNumSamples();++i) {
    const auto &a=copy->song().GetSample(i), &b=reopened.document->song().GetSample(i);
    CHECK(a.GetSampleSizeInBytes()==b.GetSampleSizeInBytes());
    CHECK(!a.GetSampleSizeInBytes() || !std::memcmp(a.sampleb(),b.sampleb(),a.GetSampleSizeInBytes()));
  }
  project.document->undo(); CHECK(core(*project.document)==expected);
  project.document->redo(); CHECK(project.document->song().m_songName==std::string(100,'x'));
  std::filesystem::remove(file);
}
std::unique_ptr<Document> moduleBaseline(MODTYPE type) {
  auto fresh=std::make_unique<Document>(type,4);
  fresh->song().SetTitle("Conservation");
  auto doc=std::make_unique<Document>(fresh->serialize());
  auto converted=std::make_unique<Document>(doc->serialize());
  // No title, topology, setting or cell correction may hide the isolated edit.
  CHECK(core(*converted)==core(*doc));
  doc->validateModuleSampleExport();
  return doc;
}
std::unique_ptr<Document> idleCellDocument(MODTYPE type) {
  auto doc=moduleBaseline(type);
  auto expected=core(*doc);
  const Cell edited{0,0,0,31,0,55};
  CHECK(doc->cell(0,0,0)==Cell{});
  CHECK(doc->edit({Edit{0,0,0,{},edited}}).size()==1);
  CHECK(doc->cell(0,0,0)==edited);
  expected["patterns"][0]["cells"][0]={0,0,0,31,0,55};
  CHECK(core(*doc)==expected);
  auto converted=std::make_unique<Document>(doc->serialize());
  CHECK(converted->cell(0,0,0)==Cell{});
  // Deliberately preserve OpenMPT's semantic equality; only snapshots/exports
  // must distinguish dormant bytes exposed by Document::Cell.
  CHECK(doc->song().Patterns[0]==converted->song().Patterns[0]);
  expected["patterns"][0]["cells"][0]={0,0,0,0,0,0};
  CHECK(core(*converted)==expected);
  return doc;
}
void idleCellSnapshot(MODTYPE type) {
  auto doc=idleCellDocument(type);
  auto copy=std::make_unique<Document>(doc->snapshotData());
  CHECK(copy->cell(0,0,0)==doc->cell(0,0,0));
  CHECK(core(*copy)==core(*doc));
}
void idleCellHistory(MODTYPE type) {
  auto doc=idleCellDocument(type);
  const auto before=core(*doc);
  doc->addPattern(64,false,0);
  const auto after=core(*doc);
  for(int cycle=0;cycle<3;++cycle) {
    doc->undo(); CHECK(core(*doc)==before); doc->native().validate(doc->song());
    doc->redo(); CHECK(core(*doc)==after); doc->native().validate(doc->song());
  }
}
template<typename F> void rejectsPatternLoss(F &&operation) {
  std::string error;
  try { operation(); } catch(const std::runtime_error &e) { error=e.what(); }
  CHECK(error.find("Module export would change pattern")!=std::string::npos);
}
void idleCellExport(MODTYPE type) {
  auto doc=idleCellDocument(type);
  rejectsPatternLoss([&]{doc->validateModuleSampleExport();});
}
struct ExportDestination {
  std::filesystem::path directory, file;
  const std::string original="Existing destination must survive rejected export.\n";
  ExportDestination() {
    static unsigned serial=0;
    directory=std::filesystem::u8path(std::getenv("TMPDIR"))/
      ("snapshot-review-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial));
    CHECK(std::filesystem::create_directory(directory));
    file=directory/"existing.module";
    std::ofstream stream(file,std::ios::binary); stream<<original; stream.close();
    CHECK(stream);
  }
  ~ExportDestination() { std::error_code error; std::filesystem::remove_all(directory,error); }
  void checkRetained() const {
    std::ifstream stream(file,std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
    CHECK(bytes==original);
    CHECK(std::distance(std::filesystem::directory_iterator(directory),std::filesystem::directory_iterator{})==1);
  }
};
void checkRejectedSave(Document &doc, bool preserveSamples) {
  ExportDestination destination;
  std::string error;
  try { doc.save(destination.file.string(),preserveSamples); }
  catch(const std::runtime_error &e) { error=e.what(); }
  destination.checkRetained();
  CHECK(error.find("Module export would change pattern")!=std::string::npos);
}
void idleCellSave(MODTYPE type, bool preserveSamples) {
  auto doc=idleCellDocument(type);
  checkRejectedSave(*doc,preserveSamples);
}
auto decodedPatternName(const Document &doc) {
  return ::OpenMPT::mpt::ToUnicode(doc.song().GetCharsetInternal(),doc.song().Patterns[0].GetName());
}
std::unique_ptr<Document> patternNameDocument(MODTYPE type) {
  auto doc=moduleBaseline(type);
  auto expected=core(*doc);
  const auto name=::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8,U_("日本語"));
  doc->song().m_modFormat.charset=::OpenMPT::mpt::Charset::UTF8;
  doc->song().Patterns[0].SetName(name);
  expected["patterns"][0]["name"]=name;
  CHECK(core(*doc)==expected);
  CHECK(decodedPatternName(*doc)==U_("日本語"));
  auto converted=std::make_unique<Document>(doc->serialize());
  // Raw title/name/cells/settings/slots/orders are unchanged: only decoding
  // differs. This must not pass merely because a different loss was rejected.
  CHECK(core(*converted)==expected);
  CHECK(converted->song().GetCharsetInternal()!=doc->song().GetCharsetInternal());
  CHECK(decodedPatternName(*converted)!=decodedPatternName(*doc));
  CHECK(::OpenMPT::mpt::ToUnicode(converted->song().GetCharsetInternal(),converted->song().m_songName)==
    ::OpenMPT::mpt::ToUnicode(doc->song().GetCharsetInternal(),doc->song().m_songName));
  return doc;
}
void patternNameSnapshot(MODTYPE type) {
  auto doc=patternNameDocument(type);
  auto copy=std::make_unique<Document>(doc->snapshotData());
  CHECK(core(*copy)==core(*doc));
  CHECK(copy->song().GetCharsetInternal()==doc->song().GetCharsetInternal());
  CHECK(decodedPatternName(*copy)==decodedPatternName(*doc));
}
void patternNameHistory(MODTYPE type) {
  auto doc=patternNameDocument(type);
  const auto before=core(*doc);
  const auto decoded=decodedPatternName(*doc);
  const auto charset=doc->song().GetCharsetInternal();
  doc->addPattern(64,false,0);
  const auto after=core(*doc);
  for(int cycle=0;cycle<3;++cycle) {
    doc->undo(); CHECK(core(*doc)==before); CHECK(doc->song().GetCharsetInternal()==charset);
    CHECK(decodedPatternName(*doc)==decoded); doc->native().validate(doc->song());
    doc->redo(); CHECK(core(*doc)==after); CHECK(doc->song().GetCharsetInternal()==charset);
    CHECK(decodedPatternName(*doc)==decoded); doc->native().validate(doc->song());
  }
}
void patternNameExport(MODTYPE type) {
  auto doc=patternNameDocument(type);
  rejectsPatternLoss([&]{doc->validateModuleSampleExport();});
}
void patternNameSave(MODTYPE type, bool preserveSamples) {
  auto doc=patternNameDocument(type);
  checkRejectedSave(*doc,preserveSamples);
}
int main(int argc,char **argv) {
  unsigned failed=0,run=0;
  std::vector<std::pair<std::string,std::function<void()>>> tests{
      {"titleSnapshot",titleSnapshot},{"sparseSnapshot",sparseSnapshot},{"structureConservation",structureConservation},
      {"moduleExportLoss",moduleExportLoss},{"titlesAndLegacy",titlesAndLegacy},{"malformedArchives",malformedArchives},
      {"actualMacFixture",actualMacFixture},{"exactPatternGroove",exactPatternGroove}};
  for(const auto &[type,name]:std::vector<std::pair<MODTYPE,std::string>>{
      {MOD_TYPE_MOD,"MOD"},{MOD_TYPE_XM,"XM"},{MOD_TYPE_S3M,"S3M"},{MOD_TYPE_IT,"IT"},{MOD_TYPE_MPT,"MPTM"}}) {
    tests.emplace_back("idleCellSnapshot/"+name,[type]{idleCellSnapshot(type);});
    tests.emplace_back("idleCellHistory/"+name,[type]{idleCellHistory(type);});
    tests.emplace_back("idleCellExport/"+name,[type]{idleCellExport(type);});
    tests.emplace_back("idleCellSave/"+name+"/false",[type]{idleCellSave(type,false);});
    tests.emplace_back("idleCellSave/"+name+"/true",[type]{idleCellSave(type,true);});
  }
  for(const auto &[type,name]:std::vector<std::pair<MODTYPE,std::string>>{
      {MOD_TYPE_XM,"XM"},{MOD_TYPE_IT,"IT"},{MOD_TYPE_MPT,"MPTM"}}) {
    tests.emplace_back("patternNameSnapshot/"+name,[type]{patternNameSnapshot(type);});
    tests.emplace_back("patternNameHistory/"+name,[type]{patternNameHistory(type);});
    tests.emplace_back("patternNameExport/"+name,[type]{patternNameExport(type);});
    tests.emplace_back("patternNameSave/"+name+"/false",[type]{patternNameSave(type,false);});
    tests.emplace_back("patternNameSave/"+name+"/true",[type]{patternNameSave(type,true);});
  }
  for(const auto &[name,test]:tests) {
    if(argc>1 && name.find(argv[1])==std::string::npos) continue;
    ++run;
    try { test(); std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception &e) { ++failed; std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
  }
  std::cout<<"RESULT "<<run<<" cases, "<<failed<<" failures\n";
  return failed || !run ? 1 : 0;
}
