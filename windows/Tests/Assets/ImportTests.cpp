#include "editor/TrackerDocument.hpp"
#include "windows/Session/AssetOperations.hpp"
#include "windows/Project/NativeMetadata.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
using namespace Tracker;
namespace {
namespace fs=std::filesystem;
using Json=nlohmann::json;
bool includeInstrumentImport=true; // Full gate, not the passing advertised subset.
void expect(bool ok,const char *why) {if(!ok)throw std::runtime_error(why);}
std::string utf8(const fs::path &p) {const auto u=p.u8string();return {reinterpret_cast<const char *>(u.data()),u.size()};}
struct Files {
  fs::path dir;
  Files() {
    const auto scratch=std::getenv("TMPDIR");expect(scratch!=nullptr,"Set TMPDIR to disposable scratch");
    // A controlled red crash can leave its fixtures behind; Windows reuses PIDs.
    // Claim a fresh directory without deleting any previous probe's evidence.
    const auto prefix="asset-import-tests-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
    for(unsigned attempt=0;attempt<100;++attempt) {
      dir=fs::u8path(scratch)/(prefix+"-"+std::to_string(attempt));
      if(fs::create_directory(dir))return;
    }
    throw std::runtime_error("Cannot claim fresh import test directory");
  }
  ~Files(){std::error_code ec;fs::remove_all(dir,ec);}
  std::string path(const std::string &name) const {return utf8(dir/fs::u8path(name));}
  // Real minimal PCM WAV, no embedded name. Independent of the importer/exporter.
  std::string wave(const std::string &name,int channels=1,int rate=24000) const {
    const auto p=path(name);std::ofstream f(fs::u8path(p),std::ios::binary);
    auto u16=[&](uint16_t v){f.put(char(v));f.put(char(v>>8));};
    auto u32=[&](uint32_t v){u16(uint16_t(v));u16(uint16_t(v>>16));};
    f.write("RIFF",4);u32(36+32*channels);f.write("WAVEfmt ",8);u32(16);u16(1);u16(uint16_t(channels));
    u32(rate);u32(rate*channels*2);u16(uint16_t(channels*2));u16(16);f.write("data",4);u32(32*channels);
    for(int i=0;i<16;++i)for(int c=0;c<channels;++c)u16(uint16_t(int16_t((i-8)*3000+c*127)));
    expect(bool(f),"Write PCM WAV");return p;
  }
  std::string text(const std::string &name,const std::string &s) const {
    auto p=path(name);std::ofstream f(fs::u8path(p),std::ios::binary);f<<s;expect(bool(f),"Write fixture");return p;
  }
};
void pcm(const Document &d,int slot,int channels=1,int rate=24000) {
  const auto &s=d.song().GetSample(SAMPLEINDEX(slot));
  expect(s.nLength==16&&s.GetNumChannels()==channels&&s.GetElementarySampleSize()==2&&s.nC5Speed==rate,"PCM dimensions/rate");
  for(int i=0;i<16;++i)for(int c=0;c<channels;++c)expect(s.sample16()[i*channels+c]==(i-8)*3000+c*127,"Exact decoded PCM");
}
void roundtrip(Document &d) {
  const auto bytes=d.snapshotData();const auto metadata=ScreamSeq::Project::encodeNativeMetadata(d.native());
  auto loaded=std::make_unique<Document>(bytes);loaded->restoreNative(ScreamSeq::Project::decodeNativeMetadata(metadata));
  expect(loaded->snapshotData()==bytes&&loaded->native()==d.native(),"Exact snapshot/nativeMetadata import roundtrip");
}
void apiSampleImport() {
  Files files;
  for(const auto &name:{"sample.wav","音色-ñ.wav"}) {
    auto path=files.wave(name);auto d=std::make_unique<Document>();const auto before=d->snapshotData();int stopped=0;
    ScreamSeq::AssetOperations api(*d,[&]{++stopped;expect(d->snapshotData()==before,"Stop is before commit");fs::remove(fs::u8path(path));});
    const auto r=api.invoke("sample.import",{{"path",path}});const int sample=r.at("index");
    pcm(*d,sample);expect(stopped==1&&d->revision==1,"Sample import one stop/revision");
    expect(d->song().GetSampleName(SAMPLEINDEX(sample))==utf8(fs::u8path(path).stem()),"API UTF-8 basename");
    roundtrip(*d);auto after=d->snapshotData();d->undo();expect(d->snapshotData()==before&&!d->canUndo(),"API sample one Undo");
    d->redo();expect(d->snapshotData()==after,"API sample exact Redo");
    path=files.wave(name);int noopStops=0;ScreamSeq::AssetOperations repeated(*d,[&]{++noopStops;});const auto revision=d->revision;
    repeated.invoke("sample.import",{{"path",path},{"slot",sample}});
    expect(noopStops==0&&d->revision==revision&&d->snapshotData()==after,"Identical sample replacement noop");
  }
}
template<class F> void invalid(F f,int code=-32602) {
  try{f();}catch(const ScreamSeq::Api::ApiError &e){expect(e.code==code,"Import error code");return;}
  throw std::runtime_error("Invalid import accepted");
}
void apiBatchImport() {
  Files files;
  for(const auto &name:{"batch.wav","一括-ñ.wav"}) {
    const auto first=files.wave(name),second=files.wave(std::string(name)+"-second.wav",2,48000),bad=files.text("破損.wav","invalid");
    auto d=std::make_unique<Document>();int stopped=0;const auto before=d->snapshotData();const auto native=d->native();
    ScreamSeq::AssetOperations api(*d,[&]{++stopped;expect(d->snapshotData()==before,"Batch stop before commit");fs::remove(fs::u8path(first));fs::remove(fs::u8path(second));});
    Json p={{"paths",{first,second}},{"createInstruments",true},{"dryRun",true}};
    const auto preview=api.invoke("sample.importMany",p);
    expect(preview["count"]==2&&preview["samples"][0]["path"]==first&&preview["samples"][1]["path"]==second,"Batch preview preserves order/paths");
    for(bool dry:{true,false}) {
      auto fail=p;fail["dryRun"]=dry;fail["paths"]={first,bad};invalid([&]{api.invoke("sample.importMany",fail);},-32003);
    }
    auto duplicate=p;duplicate["paths"]={first,first};invalid([&]{api.invoke("sample.importMany",duplicate);});
    expect(d->snapshotData()==before&&d->native()==native&&d->revision==0&&!d->canUndo()&&stopped==0,"Batch invalid/preview preserves state/transport");
    p["dryRun"]=false;const auto imported=api.invoke("sample.importMany",p);
    auto expected=preview;expected["dryRun"]=false;expect(imported==expected&&stopped==1&&d->revision==1,"Batch preview/apply parity one commit");
    for(int i=0;i<2;++i){const int slot=imported["samples"][i]["sample"],instrument=imported["samples"][i]["instrument"];
      pcm(*d,slot,i+1,i?48000:24000);expect(d->song().Instruments[instrument]->Keyboard[60]==slot,"Batch instrument mapping");}
    roundtrip(*d);const auto after=d->snapshotData();d->undo();expect(!d->canUndo()&&d->snapshotData()==before,"Batch one Undo");d->redo();expect(d->snapshotData()==after,"Batch exact Redo");
  }
}
void apiInstrumentImport() {
  Files files;
  for(const auto &name:{"instrument.wav","楽器-ñ.wav"}) {
    auto path=files.wave(name);auto d=std::make_unique<Document>();const auto before=d->snapshotData();int stopped=0;
    ScreamSeq::AssetOperations api(*d,[&]{++stopped;expect(d->snapshotData()==before,"Instrument stop before commit");fs::remove(fs::u8path(path));});
    const int instrument=api.invoke("instrument.import",{{"path",path}}).at("index");pcm(*d,d->song().Instruments[instrument]->Keyboard[60]);
    expect(stopped==1&&d->revision==1,"Instrument one stop/revision");roundtrip(*d);
    const auto after=d->snapshotData();d->undo();expect(d->snapshotData()==before&&!d->canUndo(),"Instrument one Undo");d->redo();expect(d->snapshotData()==after,"Instrument exact Redo");
    path=files.wave(name);int noopStops=0;ScreamSeq::AssetOperations repeated(*d,[&]{++noopStops;});const auto rev=d->revision;
    repeated.invoke("instrument.import",{{"path",path},{"slot",instrument}});
    expect(noopStops==0&&d->revision==rev&&d->snapshotData()==after,"Identical instrument replacement noop");
    const auto bad=files.text("破損.instrument","bad input");invalid([&]{repeated.invoke("instrument.import",{{"path",bad},{"slot",instrument}});},-32003);
    expect(noopStops==0&&d->revision==rev&&d->snapshotData()==after,"Unsupported instrument preserves oldDoc");
  }
}
void apiMultisampleImport() {
  Files files;
  for(const auto &name:{"multi.wav","多層-ñ.wav"}) {
    const auto first=files.wave(name),second=files.wave(std::string(name)+"-second.wav",2,48000),bad=files.text("壊れた.wav","bad");
    auto d=std::make_unique<Document>();const auto before=d->snapshotData();const auto native=d->native();int stopped=0;
    ScreamSeq::AssetOperations api(*d,[&]{++stopped;expect(d->snapshotData()==before,"Multisample stop before commit");fs::remove(fs::u8path(first));fs::remove(fs::u8path(second));});
    Json p={{"name","Keys 音色"},{"samples",Json::array({{{"path",second},{"rootNote",61}},{{"path",first},{"rootNote",49}}})},{"dryRun",true}};
    const auto preview=api.invoke("instrument.importMultisample",p);
    expect(preview["zones"][0]["path"]==first&&preview["zones"][0]["lowNote"]==49&&preview["zones"][0]["highNote"]==55&&preview["zones"][1]["lowNote"]==56&&preview["zones"][1]["highNote"]==61,"Multisample exact sorted zones");
    for(bool dry:{true,false}){auto fail=p;fail["dryRun"]=dry;fail["samples"][0]["path"]=bad;invalid([&]{api.invoke("instrument.importMultisample",fail);},-32003);}
    for(int root:{49,0,121}){auto fail=p;fail["samples"][0]["rootNote"]=root;invalid([&]{api.invoke("instrument.importMultisample",fail);});}
    expect(stopped==0&&d->snapshotData()==before&&d->native()==native&&d->revision==0&&!d->canUndo(),"Multisample dry/invalid atomic");
    p["dryRun"]=false;const auto imported=api.invoke("instrument.importMultisample",p);auto expected=preview;expected["dryRun"]=false;
    expect(imported==expected&&d->revision==1&&stopped==1,"Multisample preview/apply parity");
    const int i=imported["instrument"],a=imported["zones"][0]["sample"],b=imported["zones"][1]["sample"];
    pcm(*d,a);pcm(*d,b,2,48000);const auto &ins=*d->song().Instruments[i];
    expect(ins.Keyboard[48]==a&&ins.Keyboard[54]==a&&ins.Keyboard[55]==b&&ins.Keyboard[60]==b&&ins.Keyboard[47]==0&&ins.Keyboard[61]==0,"Exact nearest root keymap");
    expect(ins.NoteMap[48]==NOTE_MIDDLEC&&ins.NoteMap[54]==NOTE_MIDDLEC+6&&ins.NoteMap[55]==NOTE_MIDDLEC-5&&ins.NoteMap[60]==NOTE_MIDDLEC,"Exact multisample note transposition");
    expect(ins.GetName()=="Keys 音色","Native UTF-8 instrument name");roundtrip(*d);const auto after=d->snapshotData();
    d->undo();expect(!d->canUndo()&&d->snapshotData()==before,"Multisample one Undo");d->redo();expect(d->snapshotData()==after,"Multisample exact Redo");
  }
}
void importHostValidation() {
  Files files;const auto a=files.wave("host.wav"),b=files.wave("host2.wav");
  const std::vector<std::pair<std::string,Json>> calls={
    {"sample.import",{{"path",a}}},{"instrument.import",{{"path",a}}},
    {"sample.importMany",{{"paths",{a,b}},{"createInstruments",true}}},
    {"instrument.importMultisample",{{"name","Host"},{"samples",Json::array({{{"path",a},{"rootNote",49}},{{"path",b},{"rootNote",61}}})}}}};
  for(const auto &[method,input]:calls)for(bool dry:{false,true}) {
    if(!includeInstrumentImport&&method=="instrument.import")continue;
    if(dry&&(method=="sample.import"||method=="instrument.import"))continue;
    auto d=std::make_unique<Document>();const auto before=d->snapshotData();int stopped=0,validated=0;auto p=input;if(dry)p["dryRun"]=true;
    ScreamSeq::AssetOperations api(*d,[&]{++stopped;},[&](const Document &candidate){
      ++validated;expect(stopped==0&&candidate.song().GetNumSamples()>d->song().GetNumSamples(),"Host sees fully decoded candidate before stop");
      // Fixture host deliberately cannot resolve its real rack assignments.
      throw ScreamSeq::Api::ApiError(-32601,"Plugin import capacity unavailable");
    });
    invalid([&]{api.invoke(method,p);},-32601);
    expect(stopped==0&&validated==1&&d->snapshotData()==before&&d->revision==0&&!d->canUndo(),"Host rejects before stop, including dry run");
  }
}
void importCharsetAndPreservation() {
  Files files;const auto a=files.wave("音色-ñ.wav"),b=files.wave("別-ñ.wav");
  for(auto type:{MOD_TYPE_MPT,MOD_TYPE_IT,MOD_TYPE_XM})for(int kind=0;kind<4;++kind) {
    if(!includeInstrumentImport&&kind==1)continue;
    auto seed=std::make_unique<Document>(type);seed->song().m_songName="Preserve unrelated";
    if(type!=MOD_TYPE_MPT)seed->song().m_modFormat.charset=::OpenMPT::mpt::Charset::CP437;
    seed->edit({Edit{0,7,2,{},Cell{49,1,0,0,0,0}}});
    auto d=std::make_unique<Document>(seed->snapshotData());d->restoreNative(seed->native());const auto before=d->snapshotData();int stopped=0;
    ScreamSeq::AssetOperations api(*d,[&]{++stopped;});int sample=0;
    if(kind==0)sample=api.invoke("sample.import",{{"path",a}}).at("index");
    if(kind==1){const int i=api.invoke("instrument.import",{{"path",a}}).at("index");sample=d->song().Instruments[i]->Keyboard[60];}
    if(kind==2)sample=api.invoke("sample.importMany",{{"paths",{a,b}},{"createInstruments",true}})["samples"][0]["sample"];
    if(kind==3)sample=api.invoke("instrument.importMultisample",{{"name","Names ñ"},{"samples",Json::array({{{"path",a},{"rootNote",49}},{{"path",b},{"rootNote",61}}})}})["zones"][0]["sample"];
    pcm(*d,sample);expect(stopped==1&&d->cell(0,7,2)==seed->cell(0,7,2)&&d->song().m_songName==seed->song().m_songName,"Unrelated song/title/cell preserved");
    if(kind!=1) {
      const auto expected=::OpenMPT::mpt::ToCharset(d->song().GetCharsetInternal(),::OpenMPT::mpt::Charset::UTF8,"音色-ñ");
      expect(d->song().GetSampleName(SAMPLEINDEX(sample))==expected,"Shared target charset basename contract");
    }
    if(kind!=0)expect(d->song().Instruments[1]->Keyboard[48]==1,"Sample-only notes preserve meaning when entering instrument mode");
    roundtrip(*d);d->undo();expect(!d->canUndo()&&d->snapshotData()==before,"Legacy/native exact import Undo");
  }
}
void importFileFormats() {
  Files files;const auto wave=files.wave("源-ñ.wav",1,8363);auto source=std::make_unique<Document>();const auto instrument=source->importInstrument(wave);
  const auto sample=source->song().Instruments[instrument]->Keyboard[60];
  source->song().GetSample(sample).SetLoop(2,12,true,false,source->song());
  auto &ins=*source->song().Instruments[instrument];ins.name="Instrument fixture";
  ins.VolEnv.push_back(0,64);ins.VolEnv.push_back(8,32);ins.VolEnv.push_back(16,0);ins.VolEnv.dwFlags.set(ENV_ENABLED|ENV_SUSTAIN);ins.VolEnv.nSustainStart=ins.VolEnv.nSustainEnd=1;
  for(const auto &extension:{"iti","xi","flac"})for(const auto &base:{"format","形式-ñ"}) {
    const auto path=files.path(std::string(base)+"."+extension);
    {std::ofstream out(fs::u8path(path),std::ios::binary);bool saved=false;
      if(std::string(extension)=="iti")saved=source->song().SaveITIInstrument(instrument,out,::OpenMPT::mpt::PathString::FromUTF8(path),false,false);
      if(std::string(extension)=="xi")saved=source->song().SaveXIInstrument(instrument,out);
      if(std::string(extension)=="flac")saved=source->song().SaveFLACSample(sample,out);
      expect(saved&&bool(out),"Write actual format fixture");}
    auto d=std::make_unique<Document>();ScreamSeq::AssetOperations api(*d);int slot=0;
    if(std::string(extension)=="flac")slot=api.invoke("sample.import",{{"path",path}}).at("index");
    else {const int index=api.invoke("instrument.import",{{"path",path}}).at("index");const auto &i=*d->song().Instruments[index];slot=i.Keyboard[60];
      expect(i.GetName()=="Instrument fixture"&&i.VolEnv.size()==3&&i.VolEnv[1].tick==8&&i.VolEnv[1].value==32&&i.VolEnv.nSustainStart==1&&i.VolEnv.dwFlags[ENV_ENABLED]&&i.VolEnv.dwFlags[ENV_SUSTAIN],"Actual ITI/XI instrument names and envelopes");}
    pcm(*d,slot,1,8363);const auto &s=d->song().GetSample(SAMPLEINDEX(slot));expect(s.uFlags[CHN_LOOP]&&s.nLoopStart==2&&s.nLoopEnd==12,"Actual ITI/XI/FLAC loops retained");roundtrip(*d);
  }
  const auto second=files.wave("第二-ñ.wav",2,48000);
  const auto sfz=files.text("外部-ñ.sfz","<region> sample=源-ñ.wav key=60\n<region> sample=第二-ñ.wav key=61\n");
  auto d=std::make_unique<Document>();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;fs::remove(fs::u8path(wave));fs::remove(fs::u8path(second));fs::remove(fs::u8path(sfz));});
  const int imported=api.invoke("instrument.import",{{"path",sfz}}).at("index");pcm(*d,d->song().Instruments[imported]->Keyboard[60],1,8363);
  pcm(*d,d->song().Instruments[imported]->Keyboard[61],2,48000);
  expect(stopped==1,"SFZ source files can disappear at commit boundary");roundtrip(*d);

}
void importGuardsAndCapacity() {
  Files files;const auto a=files.wave("guards.wav"),b=files.wave("guards2.wav"),bad=files.text("破損.wav","bad");
  auto d=std::make_unique<Document>();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  for(auto method:{"sample.import","instrument.import"}) {
    if(!includeInstrumentImport&&std::string(method)=="instrument.import")continue;
    for(const Json &p:std::vector<Json>{{{"path","relative.wav"}},{{"path",a},{"slot",true}},{{"path",a},{"dryRun",true}},{{"path",a},{"slot",99999}},{{"path",std::string("x\0y",3)}}})invalid([&]{api.invoke(method,p);});
    invalid([&]{api.invoke(method,{{"path",files.path("missing-音.wav")}});},-32003);
  }
  for(const Json &p:std::vector<Json>{{{"paths",Json::array()}},{{"paths",{a}},{"createInstruments",1}},{{"paths",{a}},{"dryRun",1}},{{"paths",Json::array({true})}},{{"paths",std::vector<std::string>(129,a)}}})invalid([&]{api.invoke("sample.importMany",p);});
  const auto index=api.invoke("sample.import",{{"path",a}}).at("index").get<int>();
  d->edit({Edit{0,0,0,{},Cell{49,1,0,0,0,0}}});d->undo();const auto before=d->snapshotData();const auto native=d->native();const auto rev=d->revision,bytes=d->historyBytes();
  api.invoke("sample.import",{{"path",a},{"slot",index}});
  invalid([&]{api.invoke("sample.importMany",{{"paths",{a,bad}}});},-32003);
  expect(d->snapshotData()==before&&d->native()==native&&d->revision==rev&&d->historyBytes()==bytes&&d->canRedo()&&stopped==1,"Noop/invalid batch preserve existing Undo/Redo byte budget");
  auto exhausted=d->native();exhausted.nextID=NativeSong::maximumID-1;d->restoreNative(exhausted);const auto nativeBefore=d->native();
  for(bool dry:{true,false})invalid([&]{api.invoke("sample.importMany",{{"paths",{a,b}},{"dryRun",dry}});},-32003);
  expect(stopped==1&&d->native()==nativeBefore&&d->snapshotData()==before&&d->revision==rev,"Native ID exhaustion rejected before stop");
  auto full=std::make_unique<Document>(MOD_TYPE_MOD);full->song().m_nSamples=31;
  auto fullMetadata=full->native();fullMetadata.reconcile(full->song());full->restoreNative(std::move(fullMetadata));
  ScreamSeq::AssetOperations bounded(*full,[&]{++stopped;});const auto fullBefore=full->snapshotData();
  invalid([&]{bounded.invoke("sample.import",{{"path",a}});},-32003);
  invalid([&]{bounded.invoke("sample.importMany",{{"paths",{a}},{"dryRun",true}});});
  expect(full->snapshotData()==fullBefore&&!full->canUndo()&&stopped==1,"Format sample capacity rejected before stop");
  const auto huge=files.text("too-large.wav","x");fs::resize_file(fs::u8path(huge),256ull*1024*1024+1);
  auto empty=std::make_unique<Document>();ScreamSeq::AssetOperations limits(*empty,[&]{++stopped;});
  for(auto method:{"sample.import","instrument.import"})if(includeInstrumentImport||std::string(method)!="instrument.import")invalid([&]{limits.invoke(method,{{"path",huge}});},-32003);
  invalid([&]{limits.invoke("sample.importMany",{{"paths",{huge}},{"dryRun",true}});},-32003);
  expect(!empty->canUndo()&&empty->revision==0&&stopped==1,"Encoded file limit before stop");
}
void sharedImports() {
  Files files;
  for(const auto &name:{"control.wav","音色-ñ.wav"}) {
    auto path=files.wave(name);auto second=files.wave(std::string(name)+"-second.wav",2,48000);
    for(int kind=0;kind<4;++kind) {
      auto d=std::make_unique<Document>();const auto before=d->snapshotData();int sample=0;
      if(kind==0)sample=d->importSample(path);
      if(kind==1){const auto instrument=d->importInstrument(path);sample=d->song().Instruments[instrument]->Keyboard[60];}
      if(kind==2){auto items=d->importSamples({path,second},true);sample=items[0].sample;pcm(*d,items[1].sample,2,48000);}
      if(kind==3){auto m=d->importMultisample({{second,61},{path,49}},"Mapped keys");sample=m.zones[0].sample;pcm(*d,m.zones[1].sample,2,48000);}
      pcm(*d,sample);
      if(kind!=1)expect(d->song().GetSampleName(SAMPLEINDEX(sample))==utf8(fs::u8path(path).stem()),"UTF-8 basename in native charset");
      roundtrip(*d);const auto after=d->snapshotData();d->undo();expect(!d->canUndo()&&d->snapshotData()==before,"Shared import one Undo");
      d->redo();expect(d->snapshotData()==after,"Shared import exact Redo");
    }
  }
  const auto good=files.path("control.wav"),bad=files.text("破損-ñ.wav","not audio");
  auto d=std::make_unique<Document>();const auto before=d->snapshotData();const auto native=d->native();
  for(bool dry:{false,true})for(bool multi:{false,true}) {
    bool failed=false;try {
      if(multi)d->importMultisample({{good,49},{bad,61}},"Failure",dry);else d->importSamples({good,bad},false,dry);
    } catch(const std::runtime_error &e) {failed=true;expect(std::string(e.what()).find("破損-ñ.wav")!=std::string::npos,"UTF-8 error basename");}
    expect(failed&&d->snapshotData()==before&&d->native()==native&&!d->canUndo()&&d->revision==0,"Bad second file atomic including dry run");
  }
  // The actual SFZ decoder resolves a relative Unicode sample using FileReader's filename.
  auto sfz=files.text("外部-ñ.sfz","<region> sample=音色-ñ.wav key=60\n");
  const auto instrument=d->importInstrument(sfz);pcm(*d,d->song().Instruments[instrument]->Keyboard[60]);
  roundtrip(*d);d->undo();const auto restored=d->native();
  auto unsupported=files.text("不正-ñ.instrument","not an instrument");
  bool failed=false;try{d->importInstrument(unsupported);}catch(const std::exception &){failed=true;}
  expect(failed&&d->snapshotData()==before&&d->native()==restored&&!d->canUndo()&&d->canRedo(),"Unsupported instrument preserves old document and redo");
}
}
int runExternalImportTests(bool throughAPI) {
  Files files;
  const auto old=files.wave("old.wav");
  files.wave("有効-ñ.wav",2,48000);
  files.text("empty.wav","");files.text("unsupported.wav","unsupported audio");
  {std::ifstream input(fs::u8path(old),std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)),{});expect(bytes.size()>=44,"PCM fixture present");
    bytes.resize(44);bytes[4]=36;for(size_t i:{5,6,7,40,41,42,43})bytes[i]=0;
    files.text("zero-frames.wav",bytes);}
  {std::ifstream input(std::filesystem::path(ASSET_FIXTURE_DIR)/"mono-24000.ogg",std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)),{});expect(bytes.size()>3405,"Vorbis fixture present");
    files.text("truncated.ogg",bytes.substr(0,3405));}
  const std::string valid="<region> sample=有効-ñ.wav key=61\n";
  std::string overCapacity;for(size_t i=0;i<=MAX_SAMPLES;++i)overCapacity+=valid;
  const std::vector<std::pair<std::string,std::string>> invalidRegions={
    {"capacity",overCapacity},
    {"missing","<region> sample=absent.wav key=60\n"},
    {"empty","<region> sample=empty.wav key=60\n"},
    {"zero-frames","<region> sample=zero-frames.wav key=60\n"},
    {"unsupported","<region> sample=unsupported.wav key=60\n"},
    {"truncated-ogg","<region> sample=truncated.ogg key=60\n"},
    {"no-sample","<region> key=60\n"},
    {"unknown-generator","<region> sample=*unknown key=60\n"},
    {"empty-range","<region> sample=有効-ñ.wav lokey=62 hikey=60\n"},
    {"empty-trim","<region> sample=有効-ñ.wav key=60 end=0\n"},
    {"missing-include","#include \"absent.sfz\"\n"}};
  int failures=0,cases=0;
  for(const auto &[name,region]:invalidRegions)for(int order=0;order<3;++order)for(int slot:{0,1}) {
    ++cases;
    const auto sfz=files.text("失敗-ñ.sfz",order==0?region:(order==1?valid+region:region+valid));
    auto d=std::make_unique<Document>();d->importInstrument(old);
    d->edit({Edit{0,7,2,{},Cell{49,1,0,0,0,0}}});d->undo();
    const auto before=d->snapshotData();const auto native=d->native();
    const auto revision=d->revision,history=d->historyBytes();int stopped=0,validated=0;
    ScreamSeq::AssetOperations api(*d,[&]{++stopped;},[&](const Document &){++validated;});
    const auto label=name+"/order="+std::to_string(order)+"/slot="+std::to_string(slot);
    std::cout<<"PROBE "<<(throughAPI?"API ":"shared ")<<label<<std::endl;
    try {
      if(throughAPI)invalid([&]{api.invoke("instrument.import",{{"path",sfz},{"slot",slot}});},-32003);
      else {bool rejected=false;try{d->importInstrument(sfz,slot);}catch(const std::runtime_error &){rejected=true;}
        expect(rejected,"Incomplete external SFZ accepted");}
      expect(stopped==0&&validated==0&&d->snapshotData()==before&&d->native()==native&&d->revision==revision
        &&d->historyBytes()==history&&d->canUndo()&&d->canRedo(),"Rejected SFZ preserves destination/history/native IDs before stop/validation");
      pcm(*d,d->song().Instruments[1]->Keyboard[60]);
      std::cout<<"PASS "<<label<<'\n';
    }catch(const std::exception &e){++failures;std::cerr<<"FAIL "<<label<<": "<<e.what()<<'\n';}
  }
  std::cout<<"External SFZ cases: "<<cases<<", failures: "<<failures<<'\n';
  return failures?1:0;
}
int runExternalImportFailureProbe() {return runExternalImportTests(false);}
int runImportTests(bool full) {
  includeInstrumentImport=full;
  std::cout<<std::unitbuf;int failures=0;
  for(const auto &[name,test]:std::vector<std::pair<const char *,void(*)()>>{{"sharedImports",sharedImports},{"apiSampleImport",apiSampleImport},{"apiBatchImport",apiBatchImport},{"apiInstrumentImport",apiInstrumentImport},{"apiMultisampleImport",apiMultisampleImport},{"importHostValidation",importHostValidation},{"importCharsetAndPreservation",importCharsetAndPreservation},{"importGuardsAndCapacity",importGuardsAndCapacity},{"importFileFormats",importFileFormats}}) {
    if(!full&&(test==apiInstrumentImport||test==importFileFormats))continue;
    try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception &e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
  }
  return failures;
}
