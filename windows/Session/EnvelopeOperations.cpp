#include "EnvelopeOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/EnvelopeBank.hpp"
#include "editor/InstrumentEnvelopeTools.hpp"
#include "soundlib/mod_specifications.h"
#include "windows/Project/BinaryPlist.hpp"
#include "windows/Project/ProjectIO.hpp"
#include "mpt/crypto/hash.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <ostream>
#include <set>
#include <streambuf>
#include <string_view>
namespace ScreamSeq {
namespace {
using namespace Tracker;
void require(bool ok,const char *why) { if(!ok) throw Api::ApiError(-32602,why); }
void keys(const Json &p,std::initializer_list<const char *> allowed) {
  require(p.is_object(),"Expected an object");
  for(auto it=p.begin();it!=p.end();++it)
    require(std::any_of(allowed.begin(),allowed.end(),[&](auto k){return it.key()==k;}),"Unknown parameter or field");
}
const Json &field(const Json &p,const char *k) { require(p.is_object()&&p.contains(k),"Missing required field"); return p.at(k); }
const Json &array(const Json &v,size_t maximum) { require(v.is_array()&&v.size()<=maximum,"Invalid array or capacity"); return v; }
uint64_t integer(const Json &v,uint64_t lo,uint64_t hi) {
  require(v.is_number(),"Expected a number, not a boolean"); const auto n=v.get<double>();
  require(std::isfinite(n)&&n>=double(lo)&&n<=double(hi)&&std::floor(n)==n,"Integer outside allowed range"); return uint64_t(n);
}
double number(const Json &v,double lo,double hi) {
  require(v.is_number(),"Expected a number, not a boolean"); const auto n=v.get<double>();
  require(std::isfinite(n)&&n>=lo&&n<=hi,"Number outside allowed range"); return n;
}
bool boolean(const Json &v) { require(v.is_boolean(),"Expected a boolean"); return v.get<bool>(); }
std::string text(const Json &v,size_t maximum,bool nonempty=false) {
  require(v.is_string(),"Expected a string"); const auto s=v.get<std::string>();
  require(s.size()<=maximum*4&&s.find('\0')==std::string::npos&&(!nonempty||!s.empty()),"Empty, oversized or NUL-containing string");
  if(!s.empty()) {
    const auto units=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    require(units>0&&size_t(units)<=maximum,"Invalid UTF-8 or oversized string");
  }
  return s;
}
std::string id(uint64_t n) { return n?"n"+std::to_string(n):""; }
uint64_t identity(const Json &v) {
  const auto s=text(v,32,true); require(s.size()>1&&s[0]=='n'&&s[1]!='0',"Invalid native identity");
  uint64_t n=0; for(size_t i=1;i<s.size();++i) {
    require(s[i]>='0'&&s[i]<='9'&&n<NativeSong::maximumID/10,"Invalid native identity"); n=n*10+s[i]-'0';
  }
  require(n>0&&n<NativeSong::maximumID,"Invalid native identity"); return n;
}
uint64_t allocate(NativeSong &n) { require(n.nextID>0&&n.nextID<NativeSong::maximumID,"Native identity limit reached"); return n.makeEntity().id; }
constexpr std::array<const char *,9> curveNames{"step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"};
constexpr std::array<const char *,5> targetNames{"parameter","graph","volume","pan","pitch"};
Json encodeShape(const EnvelopeShape &s) {
  Json points=Json::array(); for(const auto &p:s.points) {
    Json v={{"position",p.position},{"value",p.value},{"curve",curveNames.at(size_t(p.curve))}};
    if(!p.formula.source().empty()) v["formula"]=p.formula.source(); points.push_back(std::move(v));
  }
  return {{"span",s.span},{"rowsPerBeat",s.rowsPerBeat},{"points",points},{"instrument",s.instrument},{"flags",s.flags},{"markers",s.markers}};
}
EnvelopeShape decodeShape(const Json &v) {
  keys(v,{"span","rowsPerBeat","points","instrument","flags","markers"}); EnvelopeShape s;
  s.span=uint32_t(integer(field(v,"span"),1,16777216));
  if(v.contains("rowsPerBeat")) s.rowsPerBeat=uint32_t(integer(v.at("rowsPerBeat"),1,65536));
  if(v.contains("instrument")) s.instrument=boolean(v.at("instrument"));
  if(v.contains("flags")) s.flags=uint8_t(integer(v.at("flags"),0,31));
  for(const auto &p:array(field(v,"points"),4096)) {
    keys(p,{"position","value","curve","formula"});
    AutomationPoint point{uint32_t(integer(field(p,"position"),0,s.span-1)),number(field(p,"value"),0,1)};
    if(p.contains("curve")) {
      const auto name=text(p.at("curve"),20); const auto found=std::find(curveNames.begin(),curveNames.end(),name);
      require(found!=curveNames.end(),"Unknown automation curve"); point.curve=AutomationCurve(found-curveNames.begin());
    }
    if(p.contains("formula")) point.formula=CurveFormula(text(p.at("formula"),2048,true));
    s.points.push_back(std::move(point));
  }
  if(v.contains("markers")) {
    const auto &m=array(v.at("markers"),5); require(m.size()==5,"Envelope needs five marker positions");
    for(size_t i=0;i<5;++i) s.markers[i]=uint32_t(integer(m[i],0,UINT32_MAX));
  }
  validateEnvelopeShape(s); return s;
}
Json encodeTarget(const EnvelopeTarget &t) { return {{"kind",targetNames.at(size_t(t.kind))},{"owner",id(t.owner)},{"pattern",id(t.pattern)}}; }
Json encodeBank(const NativeSong &n) {
  Json entries=Json::array(),links=Json::array();
  for(const auto &e:n.envelopeBank) entries.push_back({{"id",id(e.id)},{"name",e.name},{"shape",encodeShape(e.shape)}});
  for(const auto &l:n.envelopeLinks) links.push_back({{"target",encodeTarget(l.target)},{"template",id(l.templateID)},{"span",l.span}});
  return {{"entries",entries},{"links",links}};
}
EnvelopeTemplate &entry(NativeSong &n,const Json &raw) {
  const auto wanted=identity(raw); auto i=std::find_if(n.envelopeBank.begin(),n.envelopeBank.end(),[&](const auto &e){return e.id==wanted;});
  require(i!=n.envelopeBank.end(),"Song envelope template no longer exists"); return *i;
}
std::string sha256(std::span<const std::byte> bytes) {
  ::mpt::crypto::hash::SHA256 hash; hash.process(::mpt::const_byte_span(bytes.data(),bytes.size()));
  const auto digest=hash.result(); std::string result; result.reserve(64);
  constexpr char hex[]="0123456789abcdef";
  for(const auto b:digest) { const auto value=std::to_integer<unsigned>(b); result+=hex[value>>4]; result+=hex[value&15]; }
  return result;
}
std::string uuid() {
  GUID guid{}; require(SUCCEEDED(CoCreateGuid(&guid)),"Cannot allocate catalogue identity");
  wchar_t value[39]{}; require(StringFromGUID2(guid,value,39)==39,"Cannot encode catalogue identity");
  std::string result; for(size_t i=1;i<37;++i) result+=char(value[i]); return result;
}
// A nonblocking process-shared mutex avoids creating directories/lock files on
// reads, dry runs, stale requests and no-ops. Canonical case-folded UTF-16 path
// hashes keep distinct explicit catalogues isolated. The namespace deliberately
// retains the application's legacy org.resonance storage identity.
class CatalogueLock {
  HANDLE mutex_=nullptr;
public:
  explicit CatalogueLock(const std::filesystem::path &path) {
    auto canonical=std::filesystem::weakly_canonical(path).native();
    std::wstring folded(canonical.size(),L'\0');
    require(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,canonical.data(),int(canonical.size()),folded.data(),int(folded.size()),nullptr,nullptr,0)>0,"Cannot canonicalize catalogue lock path");
    canonical=std::move(folded);
    const auto digest=sha256(std::as_bytes(std::span(canonical.data(),canonical.size())));
    const auto name=L"Global\\org.resonance.tracker.envelope-catalogue-v1."+std::wstring(digest.begin(),digest.end());
    mutex_=CreateMutexW(nullptr,FALSE,name.c_str());
    if(!mutex_) Project::FileDetail::fail("Cannot open envelope catalogue lock");
    const auto wait=WaitForSingleObject(mutex_,0);
    if(wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED) return;
    const auto error=GetLastError(); CloseHandle(mutex_); mutex_=nullptr;
    if(wait==WAIT_TIMEOUT) throw Api::ApiError(-32002,"Envelope catalogue is busy; retry shortly");
    throw std::system_error(error,std::system_category(),"Cannot lock envelope catalogue");
  }
  ~CatalogueLock() { if(mutex_) { ReleaseMutex(mutex_); CloseHandle(mutex_); } }
  CatalogueLock(const CatalogueLock &)=delete;
  CatalogueLock &operator=(const CatalogueLock &)=delete;
};
constexpr size_t maximumCatalogueBytes=16u*1024u*1024u;
Project::Limits catalogueLimits() {
  Project::Limits limits; limits.maxInputBytes=limits.maxOutputBytes=limits.maxDataBytes=maximumCatalogueBytes;
  limits.maxStringBytes=maximumCatalogueBytes; limits.maxAllocationBytes=128u*1024u*1024u; return limits;
}
// A non-materializing first pass rejects duplicate decoded keys, malformed
// UTF-8/escapes, non-finite/range-losing numbers and resource excess before the
// DOM parser runs. Input bounds also cap the lexer's individual token buffer.
// This is a conservative allocation-WORK ledger, not an exact RSS guarantee:
// 512 bytes/value or key, 4x decoded string bytes, 2x input bytes for parser work.
// It covers the duplicate-key sets and subsequent DOM; charges never refund.
class CatalogueJsonPreflight : public nlohmann::json_sax<Json> {
  struct Frame {
    bool object=false,entriesKey=false;
    size_t count=0,maximum=65536;
    std::set<std::string> keys;
  };
  std::vector<Frame> frames_;
  size_t values_=0,allocation_=0;
  void charge(size_t bytes) {
    constexpr size_t maximum=128u*1024u*1024u;
    require(bytes<=maximum-allocation_,"Catalogue JSON allocation budget exceeded");allocation_+=bytes;
  }
  void count(size_t stringBytes=0) {
    require(++values_<=250000,"Catalogue JSON value/key limit exceeded");
    require(frames_.size()+1<=128,"Catalogue JSON depth limit exceeded");
    require(stringBytes<=maximumCatalogueBytes,"Catalogue JSON string limit exceeded");
    charge(512);charge(4*stringBytes);
  }
  bool value(size_t stringBytes=0) {
    count(stringBytes);
    if(!frames_.empty()&&!frames_.back().object)
      require(++frames_.back().count<=frames_.back().maximum,"Catalogue JSON array limit exceeded");
    return true;
  }
public:
  explicit CatalogueJsonPreflight(size_t inputBytes) { charge(2*inputBytes); }
  bool null() override { return value(); }
  bool boolean(bool) override { return value(); }
  bool number_integer(number_integer_t) override { return value(); }
  bool number_unsigned(number_unsigned_t) override { return value(); }
  bool number_float(number_float_t n,const string_t &token) override {
    require(std::isfinite(n),"Catalogue JSON number must be finite");
    // nlohmann falls back to double for integers outside int64/uint64. Reject
    // that lossy fallback instead of silently rounding an unknown root value.
    require(token.find_first_of(".eE")!=std::string::npos,"Catalogue JSON integer exceeds 64-bit range");
    const auto mantissa=std::string_view(token).substr(0,token.find_first_of("eE"));
    require(n!=0||mantissa.find_first_of("123456789")==std::string_view::npos,"Catalogue JSON number underflows to zero");
    return value();
  }
  bool string(string_t &s) override { return value(s.size()); }
  bool binary(binary_t &) override { return false; }
  bool start_object(size_t) override { value();frames_.push_back({true});return true; }
  bool key(string_t &s) override {
    count(s.size());auto &frame=frames_.back();
    require(++frame.count<=65536,"Catalogue JSON object member limit exceeded");
    require(frame.keys.insert(s).second,"Duplicate catalogue JSON key");
    frame.entriesKey=frames_.size()==1&&s=="entries";return true;
  }
  bool end_object() override { frames_.pop_back();return true; }
  bool start_array(size_t) override {
    const bool entries=frames_.size()==1&&frames_.back().object&&frames_.back().entriesKey;
    value();Frame frame;frame.maximum=entries?256:65536;frames_.push_back(std::move(frame));return true;
  }
  bool end_array() override { frames_.pop_back();return true; }
  bool parse_error(size_t,const std::string &,const Json::exception &) override { return false; }
};
void preflightCatalogueJson(const std::vector<std::byte> &bytes) {
  require(!bytes.empty()&&bytes.size()<=maximumCatalogueBytes,"Invalid catalogue JSON size");
  const auto *begin=reinterpret_cast<const char *>(bytes.data()),*end=begin+bytes.size();
  CatalogueJsonPreflight preflight(bytes.size());
  require(Json::sax_parse(begin,end,&preflight),"Invalid envelope catalogue JSON");
}
Json decodeCatalogueJson(const std::vector<std::byte> &bytes) {
  preflightCatalogueJson(bytes);
  const auto *begin=reinterpret_cast<const char *>(bytes.data()),*end=begin+bytes.size();
  return Json::parse(begin,end,[](int,Json::parse_event_t event,Json &value) {
    // The JSON lexer uses signed integers ONLY for a leading minus. A signed
    // integer zero therefore means the valid token -0; represent it as -0.0
    // because the DOM's integer subtype cannot retain negative zero. Ordinary
    // 0 stays unsigned, and floating-point -0.0 already retains its IEEE sign.
    if(event==Json::parse_event_t::value&&value.type()==Json::value_t::number_integer&&value.get<int64_t>()==0) value=-0.0;
    return true;
  });
}
// Serialize into a counting sink first: no unbounded dump()/string allocation,
// even when escaped strings or floating-point formatting expand the input.
// The second pass reserves exactly the checked size. Preflighting those bytes
// also prevents publishing a file our own strict reader could not reopen.
class CatalogueJsonSink : public std::streambuf {
  std::vector<std::byte> *bytes_;
  size_t count_=0;
protected:
  std::streamsize xsputn(const char *data,std::streamsize size) override {
    require(size>=0&&uint64_t(size)<=maximumCatalogueBytes-count_,"Envelope catalogue exceeds 16 MiB");
    count_+=size_t(size);
    if(bytes_) { const auto raw=std::as_bytes(std::span(data,size_t(size)));bytes_->insert(bytes_->end(),raw.begin(),raw.end()); }
    return size;
  }
  int_type overflow(int_type c) override {
    if(traits_type::eq_int_type(c,traits_type::eof())) return traits_type::not_eof(c);
    const char value=traits_type::to_char_type(c);xsputn(&value,1);return c;
  }
public:
  explicit CatalogueJsonSink(std::vector<std::byte> *bytes=nullptr):bytes_(bytes) {}
  size_t write(const Json &root) {
    std::ostream stream(this);stream.exceptions(std::ios::badbit|std::ios::failbit);stream<<root;return count_;
  }
};
std::vector<std::byte> encodeCatalogueJson(const Json &root) {
  const auto size=CatalogueJsonSink().write(root);
  std::vector<std::byte> bytes;bytes.reserve(size);CatalogueJsonSink(&bytes).write(root);
  preflightCatalogueJson(bytes);return bytes;
}
struct Catalogue {
  // Encoding follows existing bytes, never the path suffix. New files use the
  // authoritative Mac JSON layout; legacy binary plists remain binary plists.
  bool binaryPlist=false;
  Json root={{"version",1},{"revision","catalogue:0"},{"entries",Json::array()}};
  std::string revision="catalogue:0";
  Json view() const {
    Json result={{"version",1},{"revision",revision},{"entries",root.at("entries")}};
    CatalogueJsonSink().write(result);return result;
  }
};
void validateCatalogue(const Json &root) {
  // Unknown ROOT values remain in the complete typed tree. Entries/shapes are
  // replaced explicitly, so unknown nested fields reject before any data loss.
  integer(field(root,"version"),1,1); text(field(root,"revision"),80,true);
  std::set<std::string> ids;
  for(const auto &e:array(field(root,"entries"),256)) {
    keys(e,{"id","name","shape"}); const auto identity=text(field(e,"id"),80,true);
    require(ids.insert(identity).second,"Duplicate catalogue identity"); text(field(e,"name"),256,true); decodeShape(field(e,"shape"));
  }
}
Catalogue readCatalogue(const std::filesystem::path &path) {
  Catalogue catalogue;
  if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES) {
    const auto error=GetLastError(); if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND) return catalogue;
    throw std::system_error(error,std::system_category(),"Cannot inspect envelope catalogue");
  }
  std::vector<std::byte> bytes;
  try {
    bytes=Project::readProjectBytes(path,maximumCatalogueBytes);
    catalogue.binaryPlist=bytes.size()>=8&&std::memcmp(bytes.data(),"bplist00",8)==0;
    if(catalogue.binaryPlist) catalogue.root=Project::decodePlist(bytes,catalogueLimits());
    else catalogue.root=decodeCatalogueJson(bytes);
  }
  catch(const Json::exception &) { throw Api::ApiError(-32602,"Invalid envelope catalogue JSON"); }
  catch(const std::system_error &) { throw; } // Preserve real filesystem diagnostics.
  catch(const std::runtime_error &e) { throw Api::ApiError(-32602,e.what()); }
  validateCatalogue(catalogue.root); catalogue.revision="catalogue:"+sha256(bytes); return catalogue;
}
void writeCatalogue(Catalogue &catalogue,const std::filesystem::path &path,bool dry) {
  validateCatalogue(catalogue.root); catalogue.root["revision"]="catalogue:"+uuid();
  std::vector<std::byte> bytes;
  try {
    if(catalogue.binaryPlist) bytes=Project::encodePlist(catalogue.root,catalogueLimits());
    else bytes=encodeCatalogueJson(catalogue.root);
  }
  catch(const Json::exception &) { throw Api::ApiError(-32602,"Invalid envelope catalogue JSON"); }
  catch(const std::runtime_error &e) { throw Api::ApiError(-32602,e.what()); }
  if(dry) return;
  std::filesystem::create_directories(path.parent_path());
  Project::writeProjectFile(path,bytes,true);
}
using PendingParameter=std::optional<std::pair<std::string,uint32_t>>;
std::optional<EnvelopeTarget> target(NativeSong &n,const OpenMPT::CSoundFile &song,const Json &v,bool create,PendingParameter &pending) {
  const auto kind=text(field(v,"kind"),16);
  if(kind=="volume"||kind=="pan"||kind=="pitch") {
    keys(v,{"kind","instrument"});
    EnvelopeTarget t{kind=="volume"?EnvelopeTargetKind::Volume:kind=="pan"?EnvelopeTargetKind::Pan:EnvelopeTargetKind::Pitch,identity(field(v,"instrument")),0};
    require(envelopeTargetExists(n,song,t),"Instrument envelope no longer exists"); return t;
  }
  require(kind=="graph"||kind=="parameter","Unknown envelope target kind");
  const auto index=uint16_t(integer(field(v,"pattern"),0,UINT16_MAX));
  require(song.Patterns.IsValidPat(index)&&n.patterns.contains(index),"Pattern no longer exists"); const auto pattern=n.patterns.at(index).id;
  if(kind=="graph") {
    keys(v,{"kind","graph","node","pattern"}); const auto graph=identity(field(v,"graph")),node=identity(field(v,"node"));
    for(auto &g:n.signal.library) if(g.id==graph) for(auto &item:g.nodes) if(item.id==node&&item.kind==SignalNodeKind::Automation) {
      auto e=std::find_if(item.envelopes.begin(),item.envelopes.end(),[&](const auto &e){return e.pattern==pattern;});
      if(e==item.envelopes.end()) {
        if(!create) return {};
        item.envelopes.push_back({pattern,true,{{0,0}}});
        std::sort(item.envelopes.begin(),item.envelopes.end(),[](const auto &a,const auto &b){return a.pattern<b.pattern;});
      }
      return EnvelopeTarget{EnvelopeTargetKind::Graph,node,pattern};
    }
    throw Api::ApiError(-32602,"Automation source no longer exists");
  }
  keys(v,{"kind","pattern","plugin","parameter"}); const auto plugin=text(field(v,"plugin"),128,true);
  const auto parameter=uint32_t(integer(field(v,"parameter"),0,UINT32_MAX));
  for(const auto &lane:n.automation) if(lane.pattern==pattern&&lane.plugin==plugin&&lane.parameter==parameter) return EnvelopeTarget{EnvelopeTargetKind::Parameter,lane.id,0};
  if(!create) return {};
  require(!n.performance.controls(plugin,parameter),"Remove conflicting parameter automation before loading an envelope");
  const auto lane=allocate(n); n.automation.push_back({lane,pattern,plugin,parameter,true,{{0,0}}});
  pending=std::make_pair(plugin,parameter); return EnvelopeTarget{EnvelopeTargetKind::Parameter,lane,0};
}
OpenMPT::InstrumentEnvelope &instrument(NativeSong &n,OpenMPT::CSoundFile &song,const EnvelopeTarget &t) {
  for(const auto &[slot,i]:n.instruments) if(i.id==t.owner&&song.Instruments[slot]) {
    auto &v=*song.Instruments[slot];
    return t.kind==EnvelopeTargetKind::Volume?v.VolEnv:t.kind==EnvelopeTargetKind::Pan?v.PanEnv:v.PitchEnv;
  }
  throw Api::ApiError(-32602,"Instrument envelope no longer exists");
}
}
EnvelopeOperations::EnvelopeOperations(Tracker::Document &d,std::function<void()> stop,EnvelopeHostHooks host,std::optional<std::filesystem::path> path)
 : document_(d),stopPlayback_(std::move(stop)),host_(std::move(host)),cataloguePath_(std::move(path)) {
  require(!cataloguePath_||(cataloguePath_->is_absolute()&&!cataloguePath_->filename().empty()),"Catalogue requires an explicit absolute file path");
}
std::vector<std::string> EnvelopeOperations::reads() { return {"envelope.bank.list","envelope.catalogue.list"}; }
std::vector<std::string> EnvelopeOperations::writes() { return {"envelope.bank.save","envelope.bank.remove","envelope.bank.apply","envelope.bank.unlink","envelope.catalogue.publish","envelope.catalogue.import"}; }
Json EnvelopeOperations::invoke(const std::string &method,const Json &p) {
  using namespace Tracker;
  try {
    auto next=document_.native(); auto &song=document_.song(); PendingParameter pending;
    auto resolve=[&](const Json &v,bool create){return target(next,song,v,create,pending);};
    if(method=="envelope.bank.list") {
      keys(p,{"target"}); auto result=encodeBank(next);
      if(p.contains("target")) {
        const auto t=resolve(p.at("target"),false); result["linkedTemplate"]="";
        if(t) { result["shape"]=encodeShape(captureEnvelope(next,song,*t));
          for(const auto &l:next.envelopeLinks) if(l.target==*t) result["linkedTemplate"]=id(l.templateID); }
      }
      return result;
    }
    if(method=="envelope.catalogue.list") {
      keys(p,{}); require(cataloguePath_.has_value(),"Envelope catalogue path is not configured");
      CatalogueLock lock(*cataloguePath_); return readCatalogue(*cataloguePath_).view();
    }
    if(method=="envelope.catalogue.publish") {
      keys(p,{"template","catalogueID","name","expectedCatalogueRevision","dryRun"});
      const auto e=entry(next,field(p,"template")); const auto expected=text(field(p,"expectedCatalogueRevision"),80,true);
      const auto name=p.contains("name")?text(p.at("name"),256,true):text(Json(e.name),256,true);
      const auto wanted=p.contains("catalogueID")?text(p.at("catalogueID"),80,true):uuid();
      const bool dry=p.contains("dryRun")?boolean(p.at("dryRun")):false;
      require(cataloguePath_.has_value(),"Envelope catalogue path is not configured");
      CatalogueLock lock(*cataloguePath_); auto catalogue=readCatalogue(*cataloguePath_);
      require(expected==catalogue.revision,"Catalogue changed; reload before publishing");
      auto &entries=catalogue.root["entries"];
      auto found=std::find_if(entries.begin(),entries.end(),[&](const auto &v){return v.at("id")==wanted;});
      require(!p.contains("catalogueID")||found!=entries.end(),"Catalogue entry no longer exists");
      const Json item={{"id",wanted},{"name",name},{"shape",encodeShape(e.shape)}};
      const bool changed=found==entries.end()||*found!=item;
      if(found==entries.end()) entries.push_back(item); else *found=item;
      require(entries.size()<=256,"Catalogue holds at most 256 envelopes");
      if(changed) writeCatalogue(catalogue,*cataloguePath_,dry);
      return {{"id",wanted},{"dryRun",dry},{"wouldChange",changed}};
    }
    const auto supported=writes();
    if(std::find(supported.begin(),supported.end(),method)==supported.end()) throw Api::ApiError(-32601,"Unknown envelope bank operation");
    require(document_.editable(),"This document is read-only");
    const bool dry=p.contains("dryRun")?boolean(p.at("dryRun")):false;
    std::vector<std::pair<EnvelopeTarget,OpenMPT::InstrumentEnvelope>> baked;
    auto apply=[&](const EnvelopeTarget &t,const EnvelopeShape &shape,uint32_t span) {
      if(t.kind>=EnvelopeTargetKind::Volume) {
        require(t.kind!=EnvelopeTargetKind::Pitch||song.GetType()!=OpenMPT::MOD_TYPE_XM,"XM cannot store pitch envelopes");
        baked.emplace_back(t,bakeInstrumentEnvelope(shape,span,song.GetModSpecifications().envelopePointsMax));
      } else applyEnvelope(next,song,t,shape,span);
    };
    uint64_t affected=0;
    std::unique_ptr<CatalogueLock> catalogueLock;
    if(method=="envelope.catalogue.import") {
      keys(p,{"catalogueID","expectedCatalogueRevision","name","dryRun"});
      const auto expected=text(field(p,"expectedCatalogueRevision"),80,true),wanted=text(field(p,"catalogueID"),80,true);
      require(cataloguePath_.has_value(),"Envelope catalogue path is not configured");
      catalogueLock=std::make_unique<CatalogueLock>(*cataloguePath_); const auto catalogue=readCatalogue(*cataloguePath_);
      require(expected==catalogue.revision,"Catalogue changed; reload before importing");
      const auto &items=catalogue.root.at("entries");
      const auto found=std::find_if(items.begin(),items.end(),[&](const auto &v){return v.at("id")==wanted;});
      require(found!=items.end(),"Catalogue envelope no longer exists");
      const auto name=text(p.contains("name")?p.at("name"):found->at("name"),256,true);
      affected=allocate(next); next.envelopeBank.push_back({affected,name,decodeShape(found->at("shape"))});
    } else if(method=="envelope.bank.save") {
      keys(p,{"id","name","shape","target","dryRun"});
      require(p.contains("shape")!=p.contains("target"),"Supply a shape or an existing envelope target"); EnvelopeShape shape;
      if(p.contains("shape")) shape=decodeShape(p.at("shape"));
      else { const auto t=resolve(p.at("target"),false); require(t.has_value(),"This target has no envelope yet"); shape=captureEnvelope(next,song,*t); }
      const auto name=text(field(p,"name"),256,true);
      if(p.contains("id")) {
        auto &e=entry(next,p.at("id")); affected=e.id; e.name=name; e.shape=shape;
        for(const auto &l:next.envelopeLinks) if(l.templateID==affected) apply(l.target,shape,l.span);
      } else { affected=allocate(next); next.envelopeBank.push_back({affected,name,shape}); }
    } else if(method=="envelope.bank.remove") {
      keys(p,{"id","dryRun"}); const auto wanted=entry(next,field(p,"id")).id;
      require(std::none_of(next.envelopeLinks.begin(),next.envelopeLinks.end(),[&](const auto &l){return l.templateID==wanted;}),"Make this template's linked uses independent before removing it");
      std::erase_if(next.envelopeBank,[&](const auto &e){return e.id==wanted;});
    } else if(method=="envelope.bank.apply") {
      keys(p,{"template","target","linked","span","dryRun"}); const auto e=entry(next,field(p,"template"));
      const bool linked=boolean(field(p,"linked")); const auto t=*resolve(field(p,"target"),true);
      uint32_t span=envelopeTargetSpan(next,song,t);
      if(t.kind>=EnvelopeTargetKind::Volume) span=p.contains("span")?uint32_t(integer(p.at("span"),1,65536)):e.shape.instrument?(e.shape.span+255)/256:span;
      else require(!p.contains("span"),"Pattern envelopes fit the whole pattern");
      apply(t,e.shape,span);
      auto existing=std::find_if(next.envelopeLinks.begin(),next.envelopeLinks.end(),[&](const auto &l){return l.target==t;});
      if(!linked) { if(existing!=next.envelopeLinks.end()) next.envelopeLinks.erase(existing); }
      else if(existing!=next.envelopeLinks.end()) *existing={t,e.id,span};
      else next.envelopeLinks.push_back({t,e.id,span});
      affected=e.id;
    } else if(method=="envelope.bank.unlink") {
      keys(p,{"target","dryRun"}); const auto t=resolve(field(p,"target"),false);
      if(t) std::erase_if(next.envelopeLinks,[&](const auto &l){return l.target==*t;});
    }
    // Baking never touches the live song during validation or a dry run. The
    // exact shared helper supplies every materialized instrument point/marker.
    // Excluding pending instrument materialization must not hide link capacity.
    require(next.envelopeLinks.size()<=4096,"Envelope bank exceeds its capacity");
    // Pending instrument links are removed only for materialization validation;
    // their storage still belongs in the complete candidate's metadata budget.
    require(next.bytes()<=16*1024*1024,"Native song metadata exceeds 16 MB");
    auto checked=next;
    std::erase_if(checked.envelopeLinks,[&](const auto &l){return std::any_of(baked.begin(),baked.end(),[&](const auto &e){return e.first==l.target;});});
    checked.validate(song);
    if(pending) {
      require(bool(host_.parameterAvailable)&&bool(host_.parameterAutomationConflicts),"Creating a parameter envelope requires host parameter resolution and conflict hooks");
      require(host_.parameterAvailable(pending->first,pending->second),"Plugin parameter is unavailable");
      require(!host_.parameterAutomationConflicts(pending->first,pending->second),"Remove conflicting parameter automation before loading an envelope");
    }
    bool changed=next!=document_.native();
    for(const auto &[t,e]:baked) changed|=!sameInstrumentEnvelope(instrument(next,song,t),e);
    if(changed&&!dry) {
      if(stopPlayback_) stopPlayback_();
      if(baked.empty()) document_.annotate([&](NativeSong &n){n=next;});
      else document_.transaction([&](OpenMPT::CSoundFile &s,NativeSong &n){n=next;for(const auto &[t,e]:baked) instrument(n,s,t)=e;});
    }
    return {{"id",id(affected)},{"dryRun",dry},{"wouldChange",changed}};
  } catch(const std::invalid_argument &e) { throw Api::ApiError(-32602,e.what()); }
    catch(const std::out_of_range &e) { throw Api::ApiError(-32602,e.what()); }
}
}
