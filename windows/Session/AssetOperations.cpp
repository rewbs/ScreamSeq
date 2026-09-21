#include "AssetOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/InstrumentEnvelopeTools.hpp"
#include "soundlib/mod_specifications.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include "mpt/binary/base64.hpp"
namespace ScreamSeq {
namespace {
using namespace Tracker;
void require(bool ok,const char *message) { if(!ok) throw Api::ApiError(-32602,message); }
void keys(const Json &p,std::initializer_list<const char *> allowed) {
  require(p.is_object(),"Expected an object");
  for(auto i=p.begin();i!=p.end();++i)
    require(std::any_of(allowed.begin(),allowed.end(),[&](auto k){return i.key()==k;}),"Unknown parameter or field");
}
const Json &field(const Json &p,const char *key) { require(p.contains(key),"Missing required field");return p.at(key); }
double number(const Json &v,double lo,double hi) {
  require(v.is_number()&&!v.is_boolean(),"Expected a number, not a boolean");
  const auto n=v.get<double>();require(std::isfinite(n)&&n>=lo&&n<=hi,"Number outside allowed range");return n;
}
uint64_t integer(const Json &v,uint64_t lo,uint64_t hi) {
  auto n=number(v,double(lo),double(hi));require(std::floor(n)==n,"Expected an integer");return uint64_t(n);
}
bool boolean(const Json &v) { require(v.is_boolean(),"Expected a boolean");return v.get<bool>(); }
bool dryRun(const Json &p) { return p.contains("dryRun")?boolean(p.at("dryRun")):false; }
std::string text(const Json &v,size_t max=8192) {
  require(v.is_string(),"Expected a string");const auto &s=v.get_ref<const std::string &>();
  require(s.size()<=max*4 && s.find('\0')==std::string::npos,"String is oversized or contains NUL");
  if(!s.empty()) { const auto units=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    require(units>0&&size_t(units)<=max,"Invalid UTF-8 or oversized string"); }
  return s;
}
SampleChannels channels(const Json &p) {
  const auto c=text(p.value("channels",Json("both")),10);
  require(c=="both"||c=="left"||c=="right","Unknown sample channel selection");
  return c=="both"?SampleChannels::Both:c=="left"?SampleChannels::Left:SampleChannels::Right;
}
std::string encodeBytes(std::span<const std::byte> data) {
  return ::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8,::mpt::encode_base64(data));
}
std::vector<std::byte> decodeBytes(const Json &v,size_t maximum) {
  require(v.is_string(),"Expected base64 string");const auto &s=v.get_ref<const std::string &>();
  require(s.size()<=4*((maximum+2)/3),"PCM byte limit exceeded");
  try {auto data=::mpt::decode_base64(::OpenMPT::mpt::ToUnicode(::OpenMPT::mpt::Charset::UTF8,s));
    require(data.size()<=maximum&&encodeBytes(data)==s,"Invalid base64 or excessive PCM bytes");return data;
  } catch(const ::mpt::base64_parse_error &) {throw Api::ApiError(-32602,"Invalid base64");}
}
std::string newClipboardId() {
  GUID id{};if(FAILED(CoCreateGuid(&id)))throw Api::ApiError(-32003,"Cannot allocate clipboard identity");
  wchar_t buffer[40]{};StringFromGUID2(id,buffer,40);std::string result;
  for(const auto *p=buffer;*p;++p)result.push_back(char(*p));return result;
}
// Use an exact real Document as the preflight for non-prepared shared APIs.
// Snapshot/native validation failures are fatal, never relaxed or papered over.
// Comparing both snapshots also suppresses history for effective no-ops.
bool preflight(Document &document,const std::function<void(Document &)> &op) {
  auto candidate=std::make_unique<Document>(document.snapshotData());candidate->restoreNative(document.native());
  candidate->song().Order.SetSequence(document.song().Order.GetCurrentSequenceIndex());
  const auto before=candidate->snapshotData();const auto nativeBefore=candidate->native();op(*candidate);
  return before!=candidate->snapshotData()||nativeBefore!=candidate->native();
}
// Stage with the shared importers, keeping decoded allocations alive until commit.
// Never reopen a file after stopping: files (including external SFZ samples) can
// disappear/change between validation and commit. Only asset storage is moved;
// unrelated patterns, orders and settings remain owned by the live Document.
class PreparedAssetImport {
  Document &document_;
  std::vector<std::byte> before_;
  std::unique_ptr<Document> candidate_;
public:
  explicit PreparedAssetImport(Document &document):document_(document),before_(document.snapshotData()),
    candidate_(std::make_unique<Document>(before_)) {
    candidate_->restoreNative(document.native());
    candidate_->song().Order.SetSequence(document.song().Order.GetCurrentSequenceIndex());
    for(SAMPLEINDEX i=1;i<=document.song().GetNumSamples();++i)
      candidate_->song().SetSamplePath(i,document.song().GetSamplePath(i));
  }
  Document &candidate() {return *candidate_;}
  bool changed() {return before_!=candidate_->snapshotData()||document_.native()!=candidate_->native();}
  void commit() {
    document_.transaction([&](CSoundFile &song,NativeSong &native){
      auto &prepared=candidate_->song();
      for(SAMPLEINDEX i=1;i<=std::max(song.GetNumSamples(),prepared.GetNumSamples());++i) {
        auto oldPath=song.GetSamplePath(i);song.SetSamplePath(i,prepared.GetSamplePath(i));prepared.SetSamplePath(i,std::move(oldPath));
        std::swap(song.GetSample(i),prepared.GetSample(i));std::swap(song.m_szNames[i],prepared.m_szNames[i]);
      }
      for(INSTRUMENTINDEX i=1;i<=std::max(song.GetNumInstruments(),prepared.GetNumInstruments());++i)
        std::swap(song.Instruments[i],prepared.Instruments[i]);
      std::swap(song.m_nSamples,prepared.m_nSamples);std::swap(song.m_nInstruments,prepared.m_nInstruments);
      native=candidate_->native();
    });
  }
};
std::string importPath(const Json &v) {
  const auto path=text(v,4096);require(!path.empty()&&std::filesystem::u8path(path).is_absolute(),"Import path must be absolute");return path;
}
Json geometry(const SampleEditGeometry &g) {
  return {{"frames",g.frames},{"loopStart",g.loopStart},{"loopEnd",g.loopEnd},{"loop",bool(g.flags&CHN_LOOP)},
    {"pingpong",bool(g.flags&CHN_PINGPONGLOOP)},{"sustainStart",g.sustainStart},{"sustainEnd",g.sustainEnd},
    {"sustain",bool(g.flags&CHN_SUSTAINLOOP)},{"sustainPingpong",bool(g.flags&CHN_PINGPONGSUSTAIN)},
    {"reverseLoop",bool(g.reverseLoops&1)},{"sustainReverse",bool(g.reverseLoops&2)},{"cues",g.cues}};
}
Json envelopeInfo(const InstrumentEnvelope &e,uint32_t maximum) {
  Json points=Json::array();for(const auto &v:e)points.push_back({v.tick,v.value});
  return {{"points",points},{"enabled",bool(e.dwFlags[ENV_ENABLED])},{"loop",bool(e.dwFlags[ENV_LOOP])},{"sustain",bool(e.dwFlags[ENV_SUSTAIN])},
    {"carry",bool(e.dwFlags[ENV_CARRY])},{"filter",bool(e.dwFlags[ENV_FILTER])},{"loopStart",e.nLoopStart},{"loopEnd",e.nLoopEnd},
    {"sustainPoint",e.nSustainStart},{"sustainEnd",e.nSustainEnd},{"releaseNode",e.nReleaseNode},{"maxPoints",maximum}};
}
InstrumentEnvelope &envelope(ModInstrument &i,const std::string &kind) {
  if(kind=="volume")return i.VolEnv;if(kind=="pan")return i.PanEnv;require(kind=="pitch","Envelope must be volume, pan or pitch");return i.PitchEnv;
}
Json instrumentInfo(const CSoundFile &s,const ModInstrument &i) {
  const auto max=s.GetModSpecifications().envelopePointsMax;
  return {{"name",::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8,s.GetCharsetInternal(),i.GetName())},
    {"volume",i.nGlobalVol},{"pan",i.nPan},{"fadeout",i.nFadeOut},{"nna",uint8_t(i.nNNA)},{"dct",uint8_t(i.nDCT)},{"dna",uint8_t(i.nDNA)},
    {"enabled",bool(i.VolEnv.dwFlags[ENV_ENABLED])},{"sustain",bool(i.VolEnv.dwFlags[ENV_SUSTAIN])},{"sustainPoint",i.VolEnv.nSustainStart},
    {"envelopes",Json::array({envelopeInfo(i.VolEnv,max),envelopeInfo(i.PanEnv,max),envelopeInfo(i.PitchEnv,max)})},{"mapping",i.Keyboard},{"noteMapping",i.NoteMap}};
}
uint64_t nativeID(const Json &v) {
  const auto s=text(v,32);require(s.size()>1&&s.front()=='n',"Invalid native identity");uint64_t id=0;
  for(size_t i=1;i<s.size();++i){require(s[i]>='0'&&s[i]<='9'&&id<NativeSong::maximumID/10,"Invalid native identity");id=id*10+s[i]-'0';}
  require(id>0&&s=="n"+std::to_string(id),"Invalid native identity");return id;
}
AutomationTool envelopeTool(const Json &p,const InstrumentEnvelope &before) {
  AutomationTool t;t.start=uint32_t(integer(p.value("start",Json(0)),0,65535));
  t.end=uint32_t(integer(p.value("end",Json(before.empty()?49:uint32_t(before.back().tick)+1)),1,65536));
  t.operation=text(field(p,"operation"),32);const auto o=p.value("options",Json::object());
  if(t.operation=="shift") {keys(o,{"amount"});const auto n=number(field(o,"amount"),-65535,65535);require(std::floor(n)==n,"Shift uses whole ticks");t.shift=int32_t(n);}
  else if(t.operation=="flip-time"||t.operation=="flip-values")keys(o,{});
  else if(t.operation=="scale") {keys(o,{"amount","offset"});t.amount=number(field(o,"amount"),-16,16);t.offset=number(o.value("offset",Json(0)),-64,64)/64;}
  else if(t.operation=="ramp") {keys(o,{"from","to"});t.from=number(o.value("from",Json(0)),0,64)/64;t.to=number(o.value("to",Json(64)),0,64)/64;}
  else if(t.operation=="sine") {keys(o,{"amplitude","center","cycles","phase","spacing"});t.amount=number(o.value("amplitude",Json(32)),0,64)/64;
    t.offset=number(o.value("center",Json(32)),0,64)/64;t.cycles=number(o.value("cycles",Json(1)),.000001,1024);
    t.phase=number(o.value("phase",Json(0)),-360,360);t.spacing=uint32_t(integer(o.value("spacing",Json(4)),1,65536));}
  else if(t.operation=="humanize") {keys(o,{"amount","jitter","seed"});t.amount=number(o.value("amount",Json(3)),0,64)/64;
    t.jitter=uint32_t(integer(o.value("jitter",Json(0)),0,65536));t.seed=uint32_t(integer(field(o,"seed"),0,UINT32_MAX));}
  else if(t.operation=="paste"||t.operation=="insert") {keys(o,{"clip","repeats"});require(!p.contains("end"),"Paste extent comes from clipboard span; omit end");t.end=65536;
    t.repeats=uint32_t(integer(o.value("repeats",Json(1)),1,4096));const auto &clip=field(o,"clip");keys(clip,{"span","points","units"});
    require(text(field(clip,"units"))=="ticks","Instrument clipboard uses ticks");t.clip.span=uint32_t(integer(field(clip,"span"),1,65536));
    const auto &points=field(clip,"points");require(points.is_array()&&points.size()<=240,"Invalid envelope clipboard point count");
    for(const auto &v:points){require(v.is_array()&&v.size()==2,"Envelope point needs tick and value");
      t.clip.points.push_back({uint32_t(integer(v[0],0,t.clip.span-1)),double(integer(v[1],0,64))/64,AutomationCurve::Linear});}
  } else require(false,"Unknown instrument envelope tool");return t;
}
Json processReport(int sample,bool dry,const SampleProcessResult &r) {
  Json changes=Json::array();for(const auto &v:r.preview) changes.push_back({{"frame",v.frame},{"channel",v.channel},{"before",v.before},{"after",v.after}});
  return {{"sample",sample},{"dryRun",dry},{"start",r.first},{"end",r.last},{"changedFrames",r.changedFrames},
    {"changedSamples",r.changedSamples},{"clippedSamples",r.clippedSamples},{"peakBefore",r.peakBefore},{"peakAfter",r.peakAfter},
    {"patchBytes",r.historyBytes},{"changes",changes},{"previewTruncated",r.changedSamples>r.preview.size()}};
}
}
AssetOperations::AssetOperations(Tracker::Document &d,std::function<void()> stop,std::function<void(const Tracker::Document &)> validate):
  document_(d),stopPlayback_(std::move(stop)),validateImport_(std::move(validate)) {}
Json AssetOperations::clipboardInfo() const {
  if(!clipboard_)return {{"available",false}};
  const auto &c=*clipboard_;return {{"available",true},{"clipboardId",clipboardId_},{"frames",c.pcm().frames},
    {"format",c.bits==16?"s16le":"s8"},{"channels",c.channels},{"rate",c.rate},{"name",c.name},{"bytes",c.data.size()}};
}
std::vector<std::string> AssetOperations::reads() {return {"sample.snap.get","sample.pcm.get","sample.clipboard.get",
  "instrument.get","instrument.envelope.get","instrument.envelope.copy"};}
std::vector<std::string> AssetOperations::writes() {return {"sample.import","sample.importMany","sample.process","sample.loops.set","sample.draw","sample.crossfade","sample.copyToNew",
  "sample.patch","sample.pcm.set","sample.clipboard.copy","sample.clipboard.set","sample.cut","sample.delete","sample.paste",
  "instrument.import","instrument.importMultisample","instrument.create","instrument.patch","instrument.envelope.transform"};}
Json AssetOperations::invoke(const std::string &method,const Json &p) {
  if(owner_!=std::this_thread::get_id()) throw Api::ApiError(-32003,"Asset operations require their document control worker");
  const auto r=reads(),w=writes();
  const bool write=std::find(w.begin(),w.end(),method)!=w.end();
  if(!write&&std::find(r.begin(),r.end(),method)==r.end()) throw Api::ApiError(-32601,"Unknown asset operation");
  require(p.is_object(),"Expected an object");
  require(!write||document_.editable(),"This document is read-only");
  try {return dispatch(method,p);}
  catch(const std::invalid_argument &e) {throw Api::ApiError(-32602,e.what());}
  catch(const std::out_of_range &e) {throw Api::ApiError(-32602,e.what());}
}
Json AssetOperations::dispatch(const std::string &method,const Json &p) {
  using namespace Tracker;
  auto &song=document_.song();
  const auto sampleIndex=[&]{return int(integer(field(p,"sample"),1,song.GetNumSamples()));};
  const auto stop=[&]{if(stopPlayback_)stopPlayback_();};
  if(method=="instrument.importMultisample") {
    keys(p,{"name","samples","dryRun"});const auto name=text(field(p,"name"),128);const bool dry=dryRun(p);
    const auto &input=field(p,"samples");require(input.is_array()&&input.size()>=2&&input.size()<=128,"Select between 2 and 128 note samples");
    std::vector<Document::MultisampleSource> sources;
    for(const auto &v:input){keys(v,{"path","rootNote"});sources.push_back({importPath(field(v,"path")),int(integer(field(v,"rootNote"),1,120))});}
    PreparedAssetImport prepared(document_);Document::ImportedMultisample imported;
    try {imported=prepared.candidate().importMultisample(std::move(sources),name);}
    catch(const std::runtime_error &e){throw Api::ApiError(-32003,e.what());}
    Json zones=Json::array();for(const auto &z:imported.zones)zones.push_back({{"path",z.path},{"sample",z.sample},{"rootNote",z.rootNote},{"lowNote",z.lowNote},{"highNote",z.highNote}});
    Json result={{"instrument",imported.instrument},{"zones",zones},{"count",zones.size()},{"dryRun",dry}};
    if(validateImport_)validateImport_(prepared.candidate());
    if(!dry&&prepared.changed()){stop();prepared.commit();}return result;
  }
  if(method=="sample.importMany") {
    keys(p,{"paths","createInstruments","dryRun"});const auto &input=field(p,"paths");
    require(input.is_array()&&!input.empty()&&input.size()<=128,"Select between 1 and 128 samples per import");
    std::vector<std::string> paths;for(const auto &v:input)paths.push_back(importPath(v));
    const bool instruments=p.contains("createInstruments")?boolean(p.at("createInstruments")):false,dry=dryRun(p);
    PreparedAssetImport prepared(document_);std::vector<Document::ImportedSample> imported;
    try {imported=prepared.candidate().importSamples(paths,instruments);}
    catch(const std::runtime_error &e){throw Api::ApiError(-32003,e.what());}
    Json items=Json::array();for(const auto &item:imported)items.push_back({{"path",item.path},{"sample",item.sample},{"instrument",item.instrument}});
    Json result={{"samples",items},{"count",items.size()},{"dryRun",dry}};
    if(validateImport_)validateImport_(prepared.candidate());
    if(!dry&&prepared.changed()){stop();prepared.commit();}return result;
  }
  if(method=="sample.import"||method=="instrument.import") {
    keys(p,{"path","slot"});const auto path=importPath(field(p,"path"));const bool instrument=method=="instrument.import";
    const auto slot=int(integer(p.value("slot",Json(0)),0,instrument?song.GetNumInstruments():song.GetNumSamples()));
    PreparedAssetImport prepared(document_);int index;
    try {index=instrument?prepared.candidate().importInstrument(path,slot):prepared.candidate().importSample(path,slot);}
    catch(const std::runtime_error &e){throw Api::ApiError(-32003,e.what());}
    if(validateImport_)validateImport_(prepared.candidate());
    const auto changed=prepared.changed();if(changed){stop();prepared.commit();}return {{"index",index}};
  }
  if(method=="sample.process") {
    keys(p,{"sample","operation","start","end","channels","curve","exponent","gainDB","targetDB","window","dryRun"});
    const auto index=sampleIndex();const auto length=song.GetSample(index).nLength;
    SampleProcessOptions o;o.operation=text(field(p,"operation"),24);
    const bool fade=o.operation=="fade-in"||o.operation=="fade-out";
    require(!p.contains("curve")||fade,"curve applies only to fades");
    require(!p.contains("exponent")||fade,"exponent applies only to fades");
    require(!p.contains("gainDB")||o.operation=="gain","gainDB applies only to gain");
    require(!p.contains("targetDB")||o.operation=="normalize","targetDB applies only to normalize");
    require(!p.contains("window")||o.operation=="smooth","window applies only to smooth");
    o.first=uint32_t(integer(p.value("start",Json(0)),0,length));o.last=uint32_t(integer(p.value("end",Json(length)),0,length));
    require(o.first<o.last,"Sample range must be nonempty with exclusive end");o.channels=channels(p);
    const auto curve=text(p.value("curve",Json("linear")),20);
    require(curve=="linear"||curve=="smooth"||curve=="exponential"||curve=="logarithmic","Unknown fade curve");
    require(!p.contains("exponent")||curve=="exponential"||curve=="logarithmic","Exponent requires curved fade");
    o.curve=curve=="linear"?SampleFadeCurve::Linear:curve=="smooth"?SampleFadeCurve::Smooth:curve=="exponential"?SampleFadeCurve::Exponential:SampleFadeCurve::Logarithmic;
    o.exponent=number(p.value("exponent",Json(3)),.1,8);o.gainDB=number(p.value("gainDB",Json(0)),-96,24);
    o.targetDB=number(p.value("targetDB",Json(0)),-96,0);o.window=uint32_t(integer(p.value("window",Json(5)),3,255));
    require(o.window%2==1,"Smoothing window must be odd");const bool dry=dryRun(p);
    if(o.operation=="trim") {
      require(o.channels==SampleChannels::Both,"Trim changes the whole sample; select Both");
      require(song.GetSample(index).HasSampleData(),"Sample data is missing");const auto removed=length-(o.last-o.first);
      if(removed){const auto op=[&](Document &d){d.processSample(index,"trim",o.first,o.last);};
        preflight(document_,op);if(!dry){stop();op(document_);}}
      return {{"sample",index},{"operation",o.operation},{"start",o.first},{"end",o.last},{"dryRun",dry},{"removedFrames",removed},{"resultFrames",o.last-o.first}};
    }
    auto prepared=document_.prepareSampleProcess(index,o);auto result=processReport(index,dry,prepared.result());
    result["operation"]=o.operation;result["channels"]=p.value("channels",Json("both"));
    if(!dry&&prepared.hasChanges()){stop();document_.applySampleProcess(std::move(prepared));}return result;
  }
  if(method=="sample.loops.set") {
    keys(p,{"sample","normal","sustain","dryRun"});const auto index=sampleIndex();const auto length=song.GetSample(index).nLength;
    const auto parse=[&](const char *key)->std::optional<SampleLoopSettings> {
      if(!p.contains(key))return {};const auto &v=p.at(key);keys(v,{"start","end","enabled","pingpong","reverse"});
      return SampleLoopSettings{uint32_t(integer(field(v,"start"),0,length)),uint32_t(integer(field(v,"end"),0,length)),
        boolean(field(v,"enabled")),v.contains("pingpong")?boolean(v.at("pingpong")):false,v.contains("reverse")?boolean(v.at("reverse")):false};
    };
    auto prepared=document_.prepareSampleLoops(index,parse("normal"),parse("sustain"));
    const auto encode=[](const SampleEditGeometry &g)->Json{return {
      {"normal",{{"start",g.loopStart},{"end",g.loopEnd},{"enabled",bool(g.flags&CHN_LOOP)},{"pingpong",bool(g.flags&CHN_PINGPONGLOOP)},{"reverse",bool(g.reverseLoops&1)}}},
      {"sustain",{{"start",g.sustainStart},{"end",g.sustainEnd},{"enabled",bool(g.flags&CHN_SUSTAINLOOP)},{"pingpong",bool(g.flags&CHN_PINGPONGSUSTAIN)},{"reverse",bool(g.reverseLoops&2)}}}};};
    const bool dry=dryRun(p);const auto &[before,after]=*prepared.geometry();
    Json r={{"sample",index},{"dryRun",dry},{"before",encode(before)},{"after",encode(after)},
      {"loopsChanged",prepared.hasChanges()},{"patchBytes",prepared.result().historyBytes}};
    if(!dry&&prepared.hasChanges()){stop();document_.applySampleProcess(std::move(prepared));}return r;
  }
  if(method=="sample.draw") {
    keys(p,{"sample","points","channels","interpolation","dryRun"});const auto index=sampleIndex();const auto frames=song.GetSample(index).nLength;
    SampleDrawOptions o;o.channels=channels(p);const auto interpolation=text(p.value("interpolation",Json("linear")),10);
    require(interpolation=="linear"||interpolation=="step","Unknown drawing interpolation");
    o.interpolation=interpolation=="linear"?SampleDrawInterpolation::Linear:SampleDrawInterpolation::Step;
    const auto &points=field(p,"points");require(points.is_array()&&points.size()<=maximumSampleDrawPoints,"Too many drawing points");
    for(const auto &point:points){keys(point,{"frame","value"});o.points.push_back({uint32_t(integer(field(point,"frame"),0,frames?frames-1:0)),number(field(point,"value"),-1,1)});}
    const bool dry=dryRun(p);auto prepared=document_.prepareSampleDraw(index,o);auto r=processReport(index,dry,prepared.result());
    r["channels"]=p.value("channels",Json("both"));r["interpolation"]=interpolation;
    if(!dry&&prepared.hasChanges()){stop();document_.applySampleProcess(std::move(prepared));}return r;
  }
  if(method=="sample.crossfade") {
    keys(p,{"sample","loop","mode","frames","curve","dryRun"});const auto index=sampleIndex();
    const auto loop=text(p.value("loop",Json("normal")),12),mode=text(p.value("mode",Json("preserve")),12),curve=text(p.value("curve",Json("linear")),16);
    require(loop=="normal"||loop=="sustain","Unknown crossfade loop target");require(mode=="preserve"||mode=="overlap","Unknown crossfade mode");
    require(curve=="linear"||curve=="equal-power","Unknown crossfade curve");SampleCrossfadeOptions o;
    o.frames=uint32_t(integer(field(p,"frames"),2,maximumSampleCrossfadeFrames));o.sustain=loop=="sustain";
    o.mode=mode=="overlap"?SampleCrossfadeMode::Overlap:SampleCrossfadeMode::Preserve;o.curve=curve=="linear"?SampleCrossfadeCurve::Linear:SampleCrossfadeCurve::EqualPower;
    const bool dry=dryRun(p);auto prepared=document_.prepareSampleCrossfade(index,o);auto r=processReport(index,dry,prepared.result());
    const auto &[before,after]=*prepared.geometry();const auto startBefore=o.sustain?before.sustainStart:before.loopStart;
    const auto startAfter=o.sustain?after.sustainStart:after.loopStart,end=o.sustain?before.sustainEnd:before.loopEnd;
    r.update({{"loop",loop},{"mode",mode},{"frames",o.frames},{"curve",curve},{"loopBefore",{{"start",startBefore},{"end",end},{"frames",end-startBefore}}},
      {"loopAfter",{{"start",startAfter},{"end",end},{"frames",end-startAfter}}},{"loopChanged",startBefore!=startAfter}});
    if(!dry&&prepared.hasChanges()){stop();document_.applySampleProcess(std::move(prepared));}return r;
  }
  if(method=="sample.snap.get") {
    keys(p,{"sample","positions","mode","direction","channels","radius","step","origin"});const auto index=sampleIndex();const auto frames=song.GetSample(index).nLength;
    const auto mode=text(p.value("mode",Json("zero")),12),direction=text(p.value("direction",Json("nearest")),12);const bool grid=mode=="grid";
    require(mode=="zero"||grid,"Unknown sample snapping mode");require(direction=="nearest"||direction=="before"||direction=="after","Unknown snapping direction");
    require(grid||(!p.contains("step")&&!p.contains("origin")),"step and origin apply only to grid");
    require(!grid||(!p.contains("radius")&&!p.contains("channels")),"radius and channels apply only to zero crossing");
    SampleSnapOptions o;o.mode=grid?SampleSnapMode::Grid:SampleSnapMode::ZeroCrossing;o.channels=channels(p);
    o.direction=direction=="nearest"?SampleSnapDirection::Nearest:direction=="before"?SampleSnapDirection::Before:SampleSnapDirection::After;
    o.radius=uint32_t(integer(p.value("radius",Json(2048)),0,maximumSampleSnapRadius));o.step=grid?uint32_t(integer(field(p,"step"),1,MAX_SAMPLE_LENGTH)):1;
    o.origin=uint32_t(integer(p.value("origin",Json(0)),0,frames));const auto &list=field(p,"positions");
    require(list.is_array()&&!list.empty()&&list.size()<=maximumSampleSnapPositions,"Invalid snap position count");std::vector<uint32_t> positions;
    for(const auto &v:list)positions.push_back(uint32_t(integer(v,0,frames)));Json results=Json::array();
    for(const auto &v:document_.snapSample(index,positions,o))results.push_back({{"before",v.before},{"after",v.after},{"matched",v.matched}});
    return {{"sample",index},{"mode",mode},{"direction",direction},{"positions",results}};
  }
  if(method=="sample.copyToNew") {
    keys(p,{"sample","start","end","channels","name","dryRun"});const auto index=sampleIndex();const auto length=song.GetSample(index).nLength;
    const auto first=uint32_t(integer(p.value("start",Json(0)),0,length)),last=uint32_t(integer(p.value("end",Json(length)),0,length));
    std::optional<std::string> name;if(p.contains("name"))name=text(p.at("name"),200);const bool dry=dryRun(p);
    auto prepared=document_.prepareSampleCopy(index,first,last,channels(p),name);const auto &s=prepared.settings();
    Json r={{"source",index},{"start",first},{"end",last},{"channels",p.value("channels",Json("both"))},{"sample",prepared.sample()},
      {"id","n"+std::to_string(prepared.identity())},{"dryRun",dry},{"reusesEmptySlot",prepared.reusesEmptySlot()},{"historyBytes",prepared.historyBytes()},
      {"name",::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8,song.GetCharsetInternal(),prepared.name())},{"frames",s.nLength},
      {"bits",s.GetElementarySampleSize()*8},{"sampleChannels",s.GetNumChannels()},{"rate",s.GetSampleRate(song.GetType())},
      {"volume",s.nVolume/4},{"globalVolume",s.nGlobalVol},{"pan",s.nPan},{"loop",bool(s.uFlags[CHN_LOOP])},{"loopStart",s.nLoopStart},{"loopEnd",s.nLoopEnd},
      {"pingpong",bool(s.uFlags[CHN_PINGPONGLOOP])},{"sustain",bool(s.uFlags[CHN_SUSTAINLOOP])},{"reverseLoop",bool(s.nativeReverseLoops&1)},
      {"sustainReverse",bool(s.nativeReverseLoops&2)},{"sustainStart",s.nSustainStart},{"sustainEnd",s.nSustainEnd},{"sustainPingpong",bool(s.uFlags[CHN_PINGPONGSUSTAIN])},{"cues",s.cues}};
    if(!dry){stop();document_.applySampleCopy(std::move(prepared));}return r;
  }
  if(method=="sample.pcm.get") {
    keys(p,{"sample","start","frames"});const auto &s=song.GetSample(sampleIndex());
    const auto start=integer(p.value("start",Json(0)),0,s.nLength),maximum=std::min<uint64_t>(65536,s.nLength-start);
    const auto frames=integer(p.value("frames",Json(maximum)),0,maximum);require(!frames||s.HasSampleData(),"Sample data is missing");
    std::span<const std::byte> bytes;
    if(frames)bytes={reinterpret_cast<const std::byte *>(s.sampleb())+start*s.GetBytesPerSample(),size_t(frames*s.GetBytesPerSample())};
    return {{"format",s.uFlags[CHN_16BIT]?"s16le":"s8"},{"channels",s.GetNumChannels()},{"rate",s.nC5Speed},{"totalFrames",s.nLength},
      {"start",start},{"frames",frames},{"data",encodeBytes(bytes)}};
  }
  if(method=="sample.pcm.set") {
    keys(p,{"sample","format","channels","rate","data","name"});int index=int(integer(p.value("sample",Json(0)),0,song.GetNumSamples()));
    if(!index)index=song.GetNumSamples()+1;
    require(index<MAX_SAMPLES&&index<=song.GetModSpecifications().samplesMax,"Sample slots are full");
    const auto format=text(field(p,"format"),10);require(format=="s8"||format=="s16le","PCM format must be s8 or s16le");
    const auto ch=int(integer(field(p,"channels"),1,2)),bits=format=="s8"?8:16;
    require(!(song.GetType()&MOD_TYPE_MOD)||(ch==1&&bits==8),"MOD samples must be mono s8");
    const auto rate=int(integer(field(p,"rate"),100,192000));const auto data=decodeBytes(field(p,"data"),4*1048576);
    const size_t stride=ch*bits/8;require(!data.empty()&&data.size()%stride==0&&data.size()/stride<=1048576,"PCM data has invalid frame count");
    const auto name=text(p.value("name",Json("API sample")),200);
    const auto operation=[&](Document &d){d.transaction([&](CSoundFile &s){
      auto &x=s.GetSample(index);x.FreeSample();x.Initialize(s.GetType());x.uFlags.set(CHN_16BIT,bits==16);x.uFlags.set(CHN_STEREO,ch==2);
      x.nLength=SmpLength(data.size()/stride);x.nC5Speed=rate;if(s.GetType()&(MOD_TYPE_MOD|MOD_TYPE_XM))x.FrequencyToTranspose();
      if(!x.AllocateSample())throw std::runtime_error("Cannot allocate sample data");std::memcpy(x.samplev(),data.data(),data.size());
      s.m_nSamples=std::max(s.m_nSamples,SAMPLEINDEX(index));s.m_szNames[index]=::OpenMPT::mpt::ToCharset(s.GetCharsetInternal(),::OpenMPT::mpt::Charset::UTF8,name);x.PrecomputeLoops(s,false);
    });};
    if(preflight(document_,operation)){stop();operation(document_);}return {{"sample",index},{"frames",data.size()/stride}};
  }
  if(method=="sample.patch") {
    keys(p,{"sample","values"});const auto index=sampleIndex();const auto &v=field(p,"values");
    keys(v,{"name","rate","volume","pan","loopStart","loopEnd","loop","pingpong"});const auto &s=song.GetSample(index);
    const auto rate=int(integer(v.value("rate",Json(s.nC5Speed)),100,192000)),volume=int(integer(v.value("volume",Json(s.nVolume/4)),0,64)),pan=int(integer(v.value("pan",Json(s.nPan)),0,256));
    const auto start=uint32_t(integer(v.value("loopStart",Json(s.nLoopStart)),0,s.nLength)),end=uint32_t(integer(v.value("loopEnd",Json(s.nLoopEnd)),0,s.nLength));
    const bool loop=boolean(v.value("loop",Json(bool(s.uFlags[CHN_LOOP])))),pingpong=boolean(v.value("pingpong",Json(bool(s.uFlags[CHN_PINGPONGLOOP]))));
    require(!loop||start<end,"Loop end must follow its start inside the sample");
    std::optional<std::string> name;if(v.contains("name"))name=text(v.at("name"),200);
    if(v.empty())return Json::object();
    const auto op=[&](Document &d){d.sampleSettings(index,rate,volume,pan,start,end,loop,pingpong,name);};
    if(preflight(document_,op)){stop();op(document_);}return Json::object();
  }
  if(method=="sample.clipboard.get") {
    keys(p,{"start","frames"});require(!p.contains("start")||p.contains("frames"),"Clipboard start requires a frame count");auto info=clipboardInfo();
    if(p.contains("frames")) {
      require(clipboard_.has_value(),"Sample clipboard is empty");const auto pcm=clipboard_->pcm();
      const auto start=integer(p.value("start",Json(0)),0,pcm.frames),count=integer(p.at("frames"),1,65536),frames=std::min<uint64_t>(count,pcm.frames-start);
      info["data"]=encodeBytes(pcm.data.subspan(size_t(start*pcm.stride()),size_t(frames*pcm.stride())));info["start"]=start;info["readFrames"]=frames;
    }return info;
  }
  if(method=="sample.clipboard.set"||method=="sample.clipboard.copy") {
    SampleClipboard clip;
    if(method=="sample.clipboard.set") {
      keys(p,{"format","channels","rate","name","data"});const auto format=text(field(p,"format"),10);
      require(format=="s8"||format=="s16le","Clipboard format must be s8 or s16le");clip.bits=format=="s8"?8:16;
      clip.channels=uint8_t(integer(field(p,"channels"),1,2));clip.rate=uint32_t(integer(field(p,"rate"),100,768000));clip.name=text(p.value("name",Json("Clipboard")),200);
      clip.data=decodeBytes(field(p,"data"),16*1024*1024);clip.validate();
    } else {
      keys(p,{"sample","start","end","channels"});const auto index=sampleIndex();const auto length=song.GetSample(index).nLength;
      clip=document_.copySample(index,uint32_t(integer(p.value("start",Json(0)),0,length)),uint32_t(integer(p.value("end",Json(length)),0,length)),channels(p));
    }
    auto identity=newClipboardId();clipboard_=std::move(clip);clipboardId_=std::move(identity);return clipboardInfo();
  }
  if(method=="sample.cut"||method=="sample.delete"||method=="sample.paste") {
    const bool paste=method=="sample.paste",cut=method=="sample.cut";
    if(paste)keys(p,{"sample","at","end","mode","channels","rateMode","sourceGainDB","destinationGainDB","clipboardId","dryRun"});
    else keys(p,{"sample","start","end","dryRun"});const auto index=sampleIndex();const auto length=song.GetSample(index).nLength;const bool dry=dryRun(p);
    Document::PreparedSampleEdit prepared;std::optional<SampleClipboard> cutClipboard;std::string cutIdentity;
    if(paste) {
      require(clipboard_.has_value(),"Sample clipboard is empty");if(text(field(p,"clipboardId"),128)!=clipboardId_)throw Api::ApiError(-32001,"Clipboard changed; read it and prepare paste again");
      SamplePasteOptions o;o.at=uint32_t(integer(field(p,"at"),0,length));const auto mode=text(p.value("mode",Json("insert")),16),rate=text(p.value("rateMode",Json("resample")),16);
      require(mode=="insert"||mode=="overwrite"||mode=="mix"||mode=="replace","Unknown paste mode");require(rate=="resample"||rate=="keep-frames","Unknown rate mode");
      require(!p.contains("destinationGainDB")||mode=="mix","Destination gain is only available for Mix");require(p.contains("end")==bool(mode=="replace"),"Only Replace requires an end");
      o.mode=mode=="insert"?SamplePasteMode::Insert:mode=="overwrite"?SamplePasteMode::Overwrite:mode=="mix"?SamplePasteMode::Mix:SamplePasteMode::Replace;
      if(p.contains("end"))o.end=uint32_t(integer(p.at("end"),o.at,length));o.rateMode=rate=="resample"?SampleRateMode::Resample:SampleRateMode::KeepFrames;
      o.channels=channels(p);o.sourceGainDB=number(p.value("sourceGainDB",Json(0)),-96,24);o.destinationGainDB=number(p.value("destinationGainDB",Json(0)),-96,24);
      prepared=document_.prepareSamplePaste(index,*clipboard_,o);
    } else {
      const auto first=uint32_t(integer(field(p,"start"),0,length)),last=uint32_t(integer(field(p,"end"),0,length));prepared=document_.prepareSampleErase(index,first,last);
      if(cut&&!dry){cutClipboard=document_.copySample(index,first,last,SampleChannels::Both);cutIdentity=newClipboardId();}
    }
    const auto &r=prepared.result();Json changes=Json::array();for(const auto &v:r.preview)changes.push_back({{"frame",v.frame},{"channel",v.channel},{"before",v.before?Json(*v.before):Json(nullptr)},{"after",v.after?Json(*v.after):Json(nullptr)}});
    Json report={{"sample",index},{"dryRun",dry},{"start",r.first},{"removedFrames",r.removedFrames},{"insertedFrames",r.insertedFrames},{"resultFrames",r.resultFrames},
      {"changedSamples",r.changedSamples},{"clippedSamples",r.clippedSamples},{"peakAfter",r.peakAfter},{"historyBytes",r.historyBytes},{"changes",changes},
      {"previewTruncated",r.changedSamples>r.preview.size()},{"before",geometry(*r.before)},{"after",geometry(*r.after)}};
    if(paste)report["clipboardId"]=clipboardId_;
    if(!dry&&prepared.hasChanges()){stop();document_.applySampleEdit(std::move(prepared));
      if(cut){clipboard_=std::move(*cutClipboard);clipboardId_=std::move(cutIdentity);report["clipboardId"]=clipboardId_;}}
    return report;
  }
  if(method=="instrument.create") {
    keys(p,{"sample","empty","name","dryRun"});const auto sample=int(integer(p.value("sample",Json(0)),0,song.GetNumSamples()));
    const bool empty=p.contains("empty")?boolean(p.at("empty")):false,dry=dryRun(p);
    const auto name=text(p.value("name",Json("Plugin instrument")),200);
    require(!empty||sample==0,"An empty plugin trigger instrument cannot map a sample");
    require(empty||(!p.contains("name")&&!dry),"Name and dryRun require empty:true");
    require(song.GetType()==MOD_TYPE_IT||song.GetType()==MOD_TYPE_XM||song.GetType()==MOD_TYPE_MPT,"This format does not support instruments");
    if(empty){
      const auto previous=std::max(1,int(song.GetNumSamples()));
      const auto index=(song.GetNumInstruments()?int(song.GetNumInstruments()):previous)+1;
      require(index<MAX_INSTRUMENTS&&index<=song.GetModSpecifications().instrumentsMax&&index<=255,"Instrument slots are full");
      PreparedAssetImport prepared(document_);
      prepared.candidate().transaction([&](CSoundFile &s){
        if(!s.GetNumInstruments())for(int i=1;i<=previous;++i){s.Instruments[i]=new ModInstrument(SAMPLEINDEX(i));s.Instruments[i]->name=s.GetSampleName(SAMPLEINDEX(std::min(i,int(s.GetNumSamples()))));}
        s.Instruments[index]=new ModInstrument(SAMPLEINDEX(0));
        s.Instruments[index]->name=::OpenMPT::mpt::ToCharset(s.GetCharsetInternal(),::OpenMPT::mpt::Charset::UTF8,name);s.m_nInstruments=INSTRUMENTINDEX(index);
      });
      if(validateImport_)validateImport_(prepared.candidate());
      if(!dry){stop();prepared.commit();}
      return {{"instrument",index},{"empty",true},{"dryRun",dry}};
    }
    const int count=song.GetNumInstruments()?song.GetNumInstruments()+1:std::max(1,int(song.GetNumSamples()));
    require(count<MAX_INSTRUMENTS&&count<=song.GetModSpecifications().instrumentsMax,"Instrument slots are full");int index=0;
    const auto op=[&](Document &d){d.transaction([&](CSoundFile &s){
      if(!s.GetNumInstruments()) {
        for(int i=1;i<=std::max(1,int(s.GetNumSamples()));++i){s.Instruments[i]=new ModInstrument(SAMPLEINDEX(i));s.Instruments[i]->name=s.GetSampleName(SAMPLEINDEX(std::min(i,int(s.GetNumSamples()))));}
        s.m_nInstruments=std::max(SAMPLEINDEX(1),s.GetNumSamples());index=std::clamp(sample,1,int(s.m_nInstruments));
      } else {index=s.GetNumInstruments()+1;s.Instruments[index]=new ModInstrument(SAMPLEINDEX(sample));s.Instruments[index]->name="New instrument";s.m_nInstruments=INSTRUMENTINDEX(index);}
    });};
    preflight(document_,[&](Document &d){op(d);if(validateImport_)validateImport_(d);});stop();op(document_);return {{"instrument",index}};
  }
  if(method=="instrument.get"||method=="instrument.patch") {
    if(method=="instrument.get")keys(p,{"instrument"});else keys(p,{"instrument","values"});
    const auto index=INSTRUMENTINDEX(integer(field(p,"instrument"),1,song.GetNumInstruments()));require(song.Instruments[index]!=nullptr,"Select an instrument");
    const auto &before=*song.Instruments[index];if(method=="instrument.get")return instrumentInfo(song,before);
    const auto &v=field(p,"values");keys(v,{"name","volume","pan","fadeout","nna","dct","dna","mapping","envelope","points","enabled","sustain","sustainPoint","sustainEnd","loop","loopStart","loopEnd","filter"});
    auto next=before;const int kind=int(integer(v.value("envelope",Json(0)),0,2));require(kind!=2||song.GetType()!=MOD_TYPE_XM,"XM does not store pitch envelopes");
    auto &e=kind==0?next.VolEnv:kind==1?next.PanEnv:next.PitchEnv;
    if(v.contains("name"))next.name=::OpenMPT::mpt::ToCharset(song.GetCharsetInternal(),::OpenMPT::mpt::Charset::UTF8,text(v.at("name"),200));
    if(v.contains("volume"))next.nGlobalVol=uint16_t(integer(v.at("volume"),0,64));
    if(v.contains("pan")){next.nPan=uint16_t(integer(v.at("pan"),0,256));next.dwFlags.set(INS_SETPANNING);}
    if(v.contains("fadeout"))next.nFadeOut=uint16_t(integer(v.at("fadeout"),0,32768));
    if(v.contains("nna"))next.nNNA=NewNoteAction(integer(v.at("nna"),0,3));
    if(v.contains("dct"))next.nDCT=DuplicateCheckType(integer(v.at("dct"),0,4));
    if(v.contains("dna"))next.nDNA=DuplicateNoteAction(integer(v.at("dna"),0,2));
    if(v.contains("mapping")){const auto &map=v.at("mapping");require(map.is_array()&&map.size()==128,"Keymap needs 128 sample indices");for(size_t i=0;i<128;++i)next.Keyboard[i]=SAMPLEINDEX(integer(map[i],0,song.GetNumSamples()));}
    if(v.contains("points")) {
      const auto &points=v.at("points");require(points.is_array()&&points.size()<=song.GetModSpecifications().envelopePointsMax,"Envelope exceeds format point limit");
      e.clear();int previous=-1;for(const auto &point:points){require(point.is_array()&&point.size()==2,"Envelope point needs tick and value");
        const auto tick=int(integer(point[0],0,65535)),value=int(integer(point[1],0,64));require(tick>previous,"Envelope ticks must strictly increase");previous=tick;e.push_back(uint16_t(tick),uint8_t(value));}
      e.Sanitize();
    }
    const auto last=e.empty()?0:e.size()-1;
    for(auto key:{"sustainPoint","sustainEnd","loopStart","loopEnd"})if(v.contains(key))integer(v.at(key),0,last);
    for(auto key:{"enabled","sustain","loop","filter"})if(v.contains(key))boolean(v.at(key));
    if(v.contains("enabled"))e.dwFlags.set(ENV_ENABLED,boolean(v.at("enabled")));
    if(v.contains("sustain"))e.dwFlags.set(ENV_SUSTAIN,boolean(v.at("sustain")));
    if(v.contains("sustainPoint"))e.nSustainStart=uint8_t(integer(v.at("sustainPoint"),0,last));
    if(v.contains("sustainEnd"))e.nSustainEnd=uint8_t(std::max<uint64_t>(e.nSustainStart,integer(v.at("sustainEnd"),0,last)));
    else if(v.contains("sustainPoint"))e.nSustainEnd=e.nSustainStart;
    if(v.contains("loop"))e.dwFlags.set(ENV_LOOP,boolean(v.at("loop")));
    if(v.contains("loopStart"))e.nLoopStart=uint8_t(integer(v.at("loopStart"),0,last));
    if(v.contains("loopEnd"))e.nLoopEnd=uint8_t(std::max<uint64_t>(e.nLoopStart,integer(v.at("loopEnd"),0,last)));
    if(v.contains("filter")&&kind==2)e.dwFlags.set(ENV_FILTER,boolean(v.at("filter")));e.Sanitize();
    const auto op=[&](Document &d){d.transaction([&](CSoundFile &s){*s.Instruments[index]=next;});};
    if(preflight(document_,op)){stop();op(document_);}return Json::object();
  }
  if(method=="instrument.envelope.get"||method=="instrument.envelope.copy"||method=="instrument.envelope.transform") {
    const bool read=method=="instrument.envelope.get",copy=method=="instrument.envelope.copy";
    if(read)keys(p,{"instrument","envelope"});else if(copy)keys(p,{"instrument","envelope","start","end"});
    else keys(p,{"instrument","envelope","operation","start","end","options","dryRun"});
    const auto identity=nativeID(field(p,"instrument"));const auto &instruments=document_.native().instruments;
    const auto found=std::find_if(instruments.begin(),instruments.end(),[&](const auto &i){return i.second.id==identity;});
    require(found!=instruments.end()&&song.Instruments[found->first],"Instrument no longer exists");const auto index=found->first;
    const auto kind=text(field(p,"envelope"),16);const auto before=envelope(*song.Instruments[index],kind);const auto maximum=song.GetModSpecifications().envelopePointsMax;
    const bool supported=maximum>0&&!(kind=="pitch"&&song.GetType()==MOD_TYPE_XM);
    const auto info=[&](const InstrumentEnvelope &e){auto value=envelopeInfo(e,maximum);value.update({{"instrument","n"+std::to_string(identity)},
      {"index",index},{"envelope",kind},{"name",::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8,song.GetCharsetInternal(),song.GetInstrumentName(index))},
      {"editable",supported&&document_.editable()},{"units","ticks"},{"maxTick",65535},{"maxValue",64}});return value;};
    if(read)return info(before);
    if(copy) {
      const auto start=uint32_t(integer(p.value("start",Json(0)),0,65535)),end=uint32_t(integer(p.value("end",Json(before.empty()?49:uint32_t(before.back().tick)+1)),1,65536));
      const auto clip=copyInstrumentEnvelope(before,start,end);Json points=Json::array();for(const auto &v:clip.points)points.push_back({v.position,std::lround(v.value*64)});
      return {{"span",clip.span},{"points",points},{"units","ticks"}};
    }
    require(supported,"This format does not store this instrument envelope");const bool dry=dryRun(p);
    const auto transformed=transformInstrumentEnvelope(before,maximum,envelopeTool(p,before));const bool changed=!sameInstrumentEnvelope(before,transformed.envelope);
    Json result={{"before",info(before)},{"after",info(transformed.envelope)},{"wouldChange",changed},{"dryRun",dry},
      {"clippedValues",transformed.clipped},{"roundedValues",transformed.rounded},{"reanchoredMarkers",transformed.reanchored}};
    if(changed){const auto op=[&](Document &d){d.transaction([&](CSoundFile &s){envelope(*s.Instruments[index],kind)=transformed.envelope;});};
      preflight(document_,op);if(!dry){stop();op(document_);}}
    return result;
  }
  throw Api::ApiError(-32601,"Unknown asset operation");
}
}
