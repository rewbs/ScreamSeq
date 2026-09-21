#include "editor/TrackerDocument.hpp"
#include "windows/Project/NativeMetadata.hpp"
#include "windows/Api/SessionAdapter.hpp"
#ifdef HAVE_ASSET_OPERATIONS
#include "windows/Session/AssetOperations.hpp"
#endif
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include "editor/InstrumentEnvelopeTools.hpp"
using namespace Tracker;
using Json=nlohmann::json;
void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
std::unique_ptr<Document> fixture() {
  auto d=std::make_unique<Document>();
  d->transaction([](CSoundFile &s){
    s.m_nSamples=1; auto &x=s.GetSample(1); x.Initialize(s.GetType());
    x.nLength=16; x.nC5Speed=44100; x.uFlags.set(CHN_16BIT);
    if(!x.AllocateSample()) throw std::bad_alloc();
    for(int i=0;i<16;++i) x.sample16()[i]=int16_t((i-8)*3000);
    x.PrecomputeLoops(s,false);
  });
  auto clean=std::make_unique<Document>(d->snapshotData()); clean->restoreNative(d->native()); return clean;
}
#ifdef HAVE_ASSET_OPERATIONS
void processTest() {
  auto d=fixture(); int stopped=0; ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  auto before=d->copySample(1,0,16,SampleChannels::Both).data;
  auto waveform=d->waveform(1,0,16,16,SampleChannels::Both);
  auto p=Json{{"sample",1},{"operation","invert"},{"dryRun",true}};
  auto preview=api.invoke("sample.process",p);
  check(preview.at("changedSamples")==15,"real PCM preview");
  check(stopped==0&&!d->canUndo(),"dry run preserves transport/history");
  p["dryRun"]=false; api.invoke("sample.process",p);
  check(stopped==1&&d->canUndo(),"process applies one stop/history");
  check(d->song().GetSample(1).sample16()[0]==24000,"real PCM inverted");
  check(d->waveform(1,0,16,16,SampleChannels::Both)[0]==-waveform[0],"real waveform invalidates after PCM edit");
  d->undo(); check(before==d->copySample(1,0,16,SampleChannels::Both).data&&!d->canUndo(),"one Undo restores PCM");
  check(d->waveform(1,0,16,16,SampleChannels::Both)==waveform,"waveform Undo invalidation");
  d->redo(); check(d->song().GetSample(1).sample16()[0]==24000,"Redo PCM");
}
template<class F> void rejects(F f,int code=-32602) { try {f();} catch(const ScreamSeq::Api::ApiError &e) {check(e.code==code,"expected API error code");return;} throw std::runtime_error("invalid operation accepted"); }
void loopsTest() {
  auto d=fixture();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  Json p={{"sample",1},{"normal",{{"start",4},{"end",14},{"enabled",true},{"reverse",true}}},
    {"sustain",{{"start",2},{"end",10},{"enabled",true},{"pingpong",true}}}};
  auto preview=p;preview["dryRun"]=true;check(api.invoke("sample.loops.set",preview)["loopsChanged"]==true,"loop preview");
  check(!d->canUndo()&&stopped==0,"loop dry run");
  auto bad=p;bad["sustain"]["end"]=1;rejects([&]{api.invoke("sample.loops.set",bad);});
  check(!d->canUndo()&&stopped==0,"invalid full loop batch atomic");
  api.invoke("sample.loops.set",p);check(d->song().GetSample(1).nativeReverseLoops==1,"reverse loop saved");
  check(api.invoke("sample.loops.set",p)["loopsChanged"]==false&&stopped==1,"loop noop");
  d->undo();check(!d->song().GetSample(1).uFlags[CHN_LOOP]&&!d->canUndo(),"one loop Undo");
}
void drawTest() {
  auto d=fixture();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  Json p={{"sample",1},{"points",Json::array({{{"frame",0},{"value",1}},{{"frame",15},{"value",-1}}})}};
  auto bad=p;bad["points"][1]["frame"]=0;rejects([&]{api.invoke("sample.draw",bad);});
  check(!d->canUndo()&&stopped==0,"draw validates complete batch");
  auto r=api.invoke("sample.draw",p);check(r["end"]==16&&d->song().GetSample(1).sample16()[0]==32767&&d->song().GetSample(1).sample16()[15]==-32768,"draw saturated signed 16 PCM");
  api.invoke("sample.draw",p);check(stopped==1,"draw noop");d->undo();check(!d->canUndo(),"one draw Undo");
}
void snapTest() {
  auto d=fixture();ScreamSeq::AssetOperations api(*d);
  auto r=api.invoke("sample.snap.get",{{"sample",1},{"positions",{1,7,16}},{"mode","grid"},{"step",4}});
  check(r["positions"][1]["after"]==8&&!d->canUndo(),"shared grid snap read");
  rejects([&]{api.invoke("sample.snap.get",{{"sample",1},{"positions",{1,7}},{"mode","grid"},{"step",4},{"radius",3}});});
}
void crossfadeTest() {
  auto d=fixture();d->song().GetSample(1).SetLoop(4,14,true,false,d->song());
  int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  Json p={{"sample",1},{"frames",2},{"mode","overlap"},{"dryRun",true}};
  auto r=api.invoke("sample.crossfade",p);check(r["loopAfter"]["start"]==6&&!d->canUndo()&&stopped==0,"crossfade dry preview");
  p["dryRun"]=false;api.invoke("sample.crossfade",p);check(d->song().GetSample(1).nLoopStart==6&&stopped==1,"overlap uses shared geometry");
  d->undo();check(d->song().GetSample(1).nLoopStart==4&&!d->canUndo(),"crossfade one Undo");
}
void copyTest() {
  auto d=fixture();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  Json p={{"sample",1},{"start",2},{"end",12},{"name","copy"},{"dryRun",true}};
  auto r=api.invoke("sample.copyToNew",p);check(r["frames"]==10&&stopped==0&&!d->canUndo(),"copy dry");
  p["dryRun"]=false;auto applied=api.invoke("sample.copyToNew",p);const int slot=applied["sample"];
  check(applied["id"]==r["id"]&&d->song().GetSample(slot).sample16()[0]==-18000,"copy real PCM stable identity");
  d->undo();check(!d->canUndo()&&d->song().GetNumSamples()==1,"copy one Undo");d->redo();
  check(d->song().GetSample(slot).nLength==10,"copy Redo");
}
void clipboardTest() {
  auto d=fixture();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  check(api.invoke("sample.clipboard.get",Json::object())["available"]==false,"empty clipboard");
  auto clip=api.invoke("sample.clipboard.copy",{{"sample",1},{"start",2},{"end",6}});auto id=clip["clipboardId"];
  auto read=api.invoke("sample.clipboard.get",{{"frames",65536}});check(read["readFrames"]==4&&read["bytes"]==8,"bounded clipboard PCM");
  auto input=Json{{"format","s16le"},{"channels",1},{"rate",44100},{"data",read["data"]}};
  auto replacement=api.invoke("sample.clipboard.set",input);check(replacement["clipboardId"]!=id,"clipboard replacement identity");
  rejects([&]{api.invoke("sample.paste",{{"sample",1},{"at",2},{"clipboardId",id}});},-32001);
  check(!d->canUndo()&&stopped==0,"stale clip rejected before stop");
  auto paste=Json{{"sample",1},{"at",2},{"mode","overwrite"},{"clipboardId",replacement["clipboardId"]}};
  check(api.invoke("sample.paste",paste)["changedSamples"]==0&&stopped==0&&!d->canUndo(),"paste noop");
  paste["mode"]="insert";paste["dryRun"]=true;api.invoke("sample.paste",paste);check(stopped==0&&!d->canUndo(),"paste dry");
  paste["dryRun"]=false;api.invoke("sample.paste",paste);check(d->song().GetSample(1).nLength==20&&stopped==1,"paste insert real PCM");
  d->undo();check(d->song().GetSample(1).nLength==16&&!d->canUndo(),"paste one Undo");
  auto cut=Json{{"sample",1},{"start",2},{"end",6},{"dryRun",true}};api.invoke("sample.cut",cut);
  check(api.invoke("sample.clipboard.get",Json::object())["clipboardId"]==replacement["clipboardId"],"dry cut preserves clipboard");
  cut["dryRun"]=false;auto removed=api.invoke("sample.cut",cut);check(d->song().GetSample(1).nLength==12&&removed.contains("clipboardId"),"cut PCM and clip");
  d->undo();check(!d->canUndo(),"cut one Undo");api.invoke("sample.delete",{{"sample",1},{"start",0},{"end",16}});
  check(d->song().GetSample(1).nLength==0,"delete entire sample");d->undo();check(d->song().GetSample(1).nLength==16,"delete undo");
  rejects([&]{auto invalid=input;invalid["data"]="!!!!";api.invoke("sample.clipboard.set",invalid);});
}
void pcmTest() {
  auto d=fixture();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  auto r=api.invoke("sample.pcm.get",{{"sample",1},{"frames",16}});check(r["frames"]==16&&r["format"]=="s16le","PCM read");
  Json p={{"sample",0},{"format",r["format"]},{"channels",r["channels"]},{"rate",r["rate"]},{"data",r["data"]},{"name","音色"}};
  auto bad=p;bad["channels"]=true;rejects([&]{api.invoke("sample.pcm.set",bad);});check(stopped==0&&!d->canUndo(),"PCM validation before stop");
  auto applied=api.invoke("sample.pcm.set",p);int index=applied["sample"];
  check(d->copySample(index,0,16,SampleChannels::Both).data==d->copySample(1,0,16,SampleChannels::Both).data,"PCM append byte exact");
  p["sample"]=index;api.invoke("sample.pcm.set",p);check(stopped==1,"PCM identical replacement noop");
  d->undo();check(!d->canUndo()&&d->song().GetNumSamples()==1,"PCM one Undo");
  rejects([&]{api.invoke("sample.pcm.get",{{"sample",1},{"frames",65537}});});
}
void patchTest() {
  auto d=fixture();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  Json p={{"sample",1},{"values",{{"name","波形"},{"volume",40},{"pan",64},{"loopStart",3},{"loopEnd",14},{"loop",true}}}};
  auto bad=p;bad["values"]["loopEnd"]=2;rejects([&]{api.invoke("sample.patch",bad);});check(stopped==0&&!d->canUndo(),"sample patch atomic");
  api.invoke("sample.patch",p);check(d->song().GetSample(1).nVolume==160&&stopped==1,"sample patch shared settings");
  api.invoke("sample.patch",p);check(stopped==1,"sample patch noop");d->undo();check(!d->canUndo()&&d->song().GetSample(1).nVolume==256,"patch one Undo");
}
std::unique_ptr<Document> instrumentFixture() {
  auto d=fixture();d->transaction([](CSoundFile &s){s.m_nInstruments=1;s.Instruments[1]=new ModInstrument(1);
    auto &e=s.Instruments[1]->VolEnv;e.push_back(0,64);e.push_back(8,48);e.push_back(16,32);e.push_back(24,0);e.dwFlags.set(ENV_ENABLED);e.dwFlags.set(ENV_LOOP);e.dwFlags.set(ENV_SUSTAIN);
    e.nLoopStart=1;e.nLoopEnd=3;e.nSustainStart=1;e.nSustainEnd=2;e.nReleaseNode=2;});
  auto clean=std::make_unique<Document>(d->snapshotData());clean->restoreNative(d->native());return clean;
}
void instrumentTest() {
  auto d=fixture();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  auto r=api.invoke("instrument.create",{{"sample",1}});check(r["instrument"]==1&&stopped==1,"instrument create");
  auto id=d->native().instruments.at(1).id;auto info=api.invoke("instrument.get",{{"instrument",1}});
  check(info["mapping"].size()==128&&info["mapping"][48]==1&&info["noteMapping"][48]==49,"initial sample/note mapping");
  api.invoke("instrument.create",{{"sample",0}});check(d->native().instruments.at(1).id==id,"stable instrument identity after append");
  d->undo();d->undo();check(!d->canUndo()&&d->song().GetNumInstruments()==0,"creation one Undo each");
}
void instrumentPatchTest() {
  auto d=instrumentFixture();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  auto before=api.invoke("instrument.get",{{"instrument",1}});
  Json p={{"instrument",1},{"values",{{"volume",42},{"name","弦"},{"envelope",0},{"points",{{0,64},{12,40},{30,0}}},{"sustainPoint",1},{"loopEnd",2}}}};
  auto bad=p;bad["values"]["points"][2][0]=12;rejects([&]{api.invoke("instrument.patch",bad);});
  bad=p;bad["values"]["loopEnd"]=3;rejects([&]{api.invoke("instrument.patch",bad);});
  check(stopped==0&&!d->canUndo()&&api.invoke("instrument.get",{{"instrument",1}})==before,"invalid envelope batch atomic");
  api.invoke("instrument.patch",p);check(d->song().Instruments[1]->nGlobalVol==42&&stopped==1,"instrument patch");
  check(api.invoke("instrument.get",{{"instrument",1}})["name"]=="弦","UTF-8 instrument name readback");
  api.invoke("instrument.patch",p);check(stopped==1,"instrument patch noop");d->undo();check(!d->canUndo()&&api.invoke("instrument.get",{{"instrument",1}})==before,"instrument patch one Undo");
}
void envelopeTest() {
  auto d=instrumentFixture();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  const auto identity="n"+std::to_string(d->native().instruments.at(1).id);Json base={{"instrument",identity},{"envelope","volume"}};
  const auto before=api.invoke("instrument.envelope.get",base);check(before["releaseNode"]==2&&before["maxTick"]==65535,"envelope markers read");
  auto copy=base;copy["start"]=8;copy["end"]=25;const auto clip=api.invoke("instrument.envelope.copy",copy);
  check(clip["span"]==17&&clip["points"][0][0]==0&&clip["points"].size()==3,"envelope copy actual points");
  auto p=base;p.update({{"operation","flip-time"},{"dryRun",true}});auto preview=api.invoke("instrument.envelope.transform",p);
  check(preview["after"]["releaseNode"]==1&&!d->canUndo()&&stopped==0,"release anchor follows shared flip");
  p["dryRun"]=false;api.invoke("instrument.envelope.transform",p);check(stopped==1&&d->song().Instruments[1]->VolEnv.nReleaseNode==1,"envelope apply");
  d->undo();check(!d->canUndo()&&api.invoke("instrument.envelope.get",base)==before,"envelope one Undo");
  p["operation"]="scale";p["options"]={{"amount",1}};api.invoke("instrument.envelope.transform",p);
  check(stopped==1&&!d->canUndo()&&d->canRedo(),"envelope noop preserves redo");
  p["operation"]="shift";p["options"]={{"amount",1}};rejects([&]{api.invoke("instrument.envelope.transform",p);});
  p["operation"]="sine";p["end"]=65536;p["options"]={{"spacing",1}};rejects([&]{api.invoke("instrument.envelope.transform",p);});
  auto bad=base;bad["instrument"]="n01";rejects([&]{api.invoke("instrument.envelope.get",bad);});
  check(stopped==1&&!d->canUndo(),"rejected envelope bounds preserve transport/history");
}
void modelRoundtripTest() {
  auto d=instrumentFixture();ScreamSeq::AssetOperations api(*d);
  api.invoke("sample.loops.set",{{"sample",1},{"normal",{{"start",4},{"end",14},{"enabled",true},{"reverse",true}}}});
  api.invoke("sample.draw",{{"sample",1},{"points",Json::array({{{"frame",0},{"value",1}}})}});
  api.invoke("instrument.patch",{{"instrument",1},{"values",{{"name","Persistent"},{"pan",64}}}});
  const auto metadata=ScreamSeq::Project::encodeNativeMetadata(d->native());
  auto loaded=std::make_unique<Document>(d->snapshotData());loaded->restoreNative(ScreamSeq::Project::decodeNativeMetadata(metadata));
  ScreamSeq::AssetOperations reopened(*loaded);
  check(loaded->native()==d->native(),"all native metadata roundtrip");
  check(reopened.invoke("instrument.get",{{"instrument",1}})==api.invoke("instrument.get",{{"instrument",1}}),"snapshot instrument envelopes roundtrip");
  check(loaded->copySample(1,0,16,SampleChannels::Both).data==d->copySample(1,0,16,SampleChannels::Both).data,"snapshot actual PCM roundtrip");
  check(loaded->song().GetSample(1).nativeReverseLoops==1,"snapshot reverse loops roundtrip");
}
void processOptionsTest() {
  for(const auto &operation:{"reverse","invert","normalize","gain","fade-in","fade-out","silence","remove-dc","smooth"}) {
    auto d=fixture();ScreamSeq::AssetOperations api(*d);SampleProcessOptions o;o.operation=operation;o.first=1;o.last=15;
    Json p={{"sample",1},{"operation",operation},{"start",1},{"end",15}};
    if(o.operation=="gain"){o.gainDB=6;p["gainDB"]=6;}
    // This is an independent invocation of the existing shared planner, not a fake PCM response.
    auto expected=d->prepareSampleProcess(1,o).result();auto r=api.invoke("sample.process",p);
    check(r["changedSamples"]==expected.changedSamples&&r["clippedSamples"]==expected.clippedSamples,"shared process result parity");
  }
  auto d=fixture();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  api.invoke("sample.process",{{"sample",1},{"operation","trim"}});check(stopped==0&&!d->canUndo(),"whole trim noop");
  auto before=d->copySample(1,0,16,SampleChannels::Both).data;
  api.invoke("sample.process",{{"sample",1},{"operation","trim"},{"start",2},{"end",12}});
  check(d->song().GetSample(1).nLength==10&&stopped==1,"trim shared length");d->undo();check(!d->canUndo()&&d->copySample(1,0,16,SampleChannels::Both).data==before,"trim one Undo");
}
void envelopeOptionsTest() {
  const Json choices=Json::array({
    {{"operation","flip-values"}},{{"operation","shift"},{"start",8},{"options",{{"amount",1}}}},
    {{"operation","scale"},{"options",{{"amount",.5},{"offset",3}}}},{{"operation","ramp"},{"options",{{"from",5},{"to",60}}}},
    {{"operation","sine"},{"options",{{"spacing",4}}}},{{"operation","humanize"},{"options",{{"seed",42}}}},
    {{"operation","paste"},{"options",{{"clip",{{"span",16},{"units","ticks"},{"points",{{0,20},{8,40}}}}}}}},
    {{"operation","insert"},{"options",{{"clip",{{"span",16},{"units","ticks"},{"points",{{0,20},{8,40}}}}}}}}
  });
  for(const auto &choice:choices) {
    auto d=instrumentFixture();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});auto p=choice;
    p["instrument"]="n"+std::to_string(d->native().instruments.at(1).id);p["envelope"]="volume";p["dryRun"]=true;
    auto preview=api.invoke("instrument.envelope.transform",p);check(stopped==0&&!d->canUndo(),"all envelope tool previews preserve state");
    p["dryRun"]=false;auto applied=api.invoke("instrument.envelope.transform",p);check(applied["after"]==preview["after"],"envelope preview and apply match");
    if(applied["wouldChange"]==true){check(stopped==1,"envelope tool stops once");d->undo();check(!d->canUndo(),"envelope tool one Undo");}
  }
}
void mixSaturationTest() {
  auto d=fixture();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});
  auto id=api.invoke("sample.clipboard.copy",{{"sample",1}})["clipboardId"];
  const auto before=d->copySample(1,0,16,SampleChannels::Both).data;
  auto result=api.invoke("sample.paste",{{"sample",1},{"at",0},{"mode","mix"},{"sourceGainDB",24},{"clipboardId",id}});
  check(result["clippedSamples"]>0&&d->song().GetSample(1).sample16()[0]==-32768&&d->song().GetSample(1).sample16()[15]==32767,"mix saturates 16-bit PCM");
  check(stopped==1,"mix stop once");d->undo();check(!d->canUndo()&&d->copySample(1,0,16,SampleChannels::Both).data==before,"mix one Undo byte exact");
}
void guardsTest() {
  auto d=fixture();int stopped=0;ScreamSeq::AssetOperations api(*d,[&]{++stopped;});const auto rev=d->revision;
  rejects([&]{api.invoke("sample.process",{{"sample",1},{"operation","gain"},{"gainDB",true}});});
  rejects([&]{api.invoke("sample.draw",{{"sample",1},{"points",{{{"frame",0},{"value",2}}}}});});
  rejects([&]{api.invoke("sample.patch",{{"sample",1},{"values",{{"name","valid"},{"volume",65}}}});});
  rejects([&]{api.invoke("sample.pcm.set",{{"sample",0},{"format","s16le"},{"channels",1},{"rate",44100},{"data",std::string(4*((4*1048576+2)/3)+4,'A')}});});
  rejects([&]{api.invoke("sample.clipboard.set",{{"format","s8"},{"channels",1},{"rate",44100},{"data",std::string(4*((16*1048576+2)/3)+4,'A')}});});
  bool workerRejected=false;std::thread t([&]{try{api.invoke("sample.pcm.get",{{"sample",1}});}catch(const ScreamSeq::Api::ApiError &e){workerRejected=e.code==-32003;}});t.join();
  check(workerRejected&&stopped==0&&d->revision==rev&&!d->canUndo(),"worker/type/byte guards preserve document");
  rejects([&]{api.invoke("instrument.import",Json::object());},-32602);
  for(auto unsupported:{"sample.get","sample.waveform.get","plugin.state.set"})
    rejects([&]{api.invoke(unsupported,Json::object());},-32601);
  auto methods=ScreamSeq::AssetOperations::reads();const auto writes=ScreamSeq::AssetOperations::writes();methods.insert(methods.end(),writes.begin(),writes.end());
  std::sort(methods.begin(),methods.end());check(std::adjacent_find(methods.begin(),methods.end())==methods.end(),"catalog disjoint");
}
int unicodeImportProbe(const char *scratch) {
  namespace fs=std::filesystem;const auto dir=fs::u8path(scratch)/("asset-import-probe-"+std::to_string(GetCurrentProcessId()));
  check(fs::create_directory(dir),"probe directory must be new");struct Cleanup{fs::path dir;~Cleanup(){std::error_code ec;fs::remove_all(dir,ec);}}cleanup{dir};
  auto source=fixture();std::vector<std::string> paths;
  for(const auto &name:{L"control.wav",L"音色-ñ.wav"}){
    auto path=dir/name;{std::ofstream file(path,std::ios::binary);check(source->song().SaveWAVSample(1,file),"write real PCM WAV");}
    const auto u=path.u8string();paths.emplace_back(reinterpret_cast<const char *>(u.data()),u.size());
  }
  int failures=0;
  for(const auto &op:{"sample","instrument","batch","multisample"})for(int unicode=0;unicode<2;++unicode){
    auto d=std::make_unique<Document>();try{
      if(std::string(op)=="sample")d->importSample(paths[unicode]);
      else if(std::string(op)=="instrument")d->importInstrument(paths[unicode]);
      else if(std::string(op)=="batch")d->importSamples({paths[unicode]},false);
      else {const auto other=dir/(unicode?L"別-ñ.wav":L"second.wav");fs::copy_file(fs::u8path(paths[unicode]),other);
        const auto u=other.u8string();d->importMultisample({{paths[unicode],49},{std::string(reinterpret_cast<const char *>(u.data()),u.size()),61}},"Probe");}
      std::cout<<"PASS "<<op<<(unicode?" UTF8":" ASCII")<<'\n';
    }catch(const std::exception &e){++failures;std::cout<<"BLOCKED "<<op<<(unicode?" UTF8":" ASCII")<<": "<<e.what()<<'\n';}
  }
  return failures?1:0;
}
#endif
int runImportTests(bool full);
int runExternalImportFailureProbe();
int runVorbisProbeTests();
int runMO3VorbisProbeTests();
int runExternalImportTests(bool throughAPI);
int main(int argc,char **argv) { try {
  if(argc==2&&std::string(argv[1])=="--probe-mo3-vorbis")return runMO3VorbisProbeTests();
  if(argc==2&&std::string(argv[1])=="--external-imports")return runExternalImportTests(true);
  if(argc==2&&std::string(argv[1])=="--probe-vorbis")return runVorbisProbeTests();
  if(argc==2&&std::string(argv[1])=="--imports")return runImportTests(true)?1:0;
  if(argc==2&&std::string(argv[1])=="--imports-safe")return runImportTests(false)?1:0;
  if(argc==2&&std::string(argv[1])=="--probe-external-import")return runExternalImportFailureProbe();
#ifdef HAVE_ASSET_OPERATIONS
  if(argc==3&&std::string(argv[1])=="--probe-import")return unicodeImportProbe(argv[2]);
#endif
#ifdef HAVE_ASSET_OPERATIONS
  int failures=0;
  for(const auto &[name,test]:std::vector<std::pair<const char *,void(*)()>>{{"process",processTest},{"loops",loopsTest},{"draw",drawTest},{"snap",snapTest},{"crossfade",crossfadeTest},{"copyToNew",copyTest},{"clipboard",clipboardTest},{"pcm",pcmTest},{"patch",patchTest},{"instrument",instrumentTest},{"instrumentPatch",instrumentPatchTest},{"envelope",envelopeTest},{"modelRoundtrip",modelRoundtripTest},{"processOptions",processOptionsTest},{"envelopeOptions",envelopeOptionsTest},{"guards",guardsTest},{"mixSaturation",mixSaturationTest}}) {
    try {test();std::cout<<"PASS "<<name<<'\n';} catch(const std::exception &e) {++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
  }
  if(failures)return 1;
#else
  throw std::runtime_error("AssetOperations sample.process is not implemented");
#endif
  return 0;
} catch(const std::exception &e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; } }
