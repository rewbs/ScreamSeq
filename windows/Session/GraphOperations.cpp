#include "GraphOperations.hpp"
#include "NoteActivityJson.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/NativeMetadata.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/GraphEditing.hpp"
#include "editor/GraphClipboard.hpp"
#include <cmath>
#include <set>
namespace ScreamSeq {
namespace {
using namespace Tracker;
void require(bool yes,const char *why) { if(!yes) throw Api::ApiError(-32602,why); }
void keys(const Json &p,std::initializer_list<const char *> allowed) {
  require(p.is_object(),"Expected an object");
  for(auto it=p.begin();it!=p.end();++it)
    require(std::any_of(allowed.begin(),allowed.end(),[&](auto k){return it.key()==k;}),"Unknown parameter or field");
}
const Json &field(const Json &p,const char *k) { require(p.is_object()&&p.contains(k),"Missing required field"); return p.at(k); }
const Json &array(const Json &v,size_t max) { require(v.is_array()&&v.size()<=max,"Invalid array or capacity"); return v; }
bool boolean(const Json &v) { require(v.is_boolean(),"Expected a boolean"); return v.get<bool>(); }
double number(const Json &v,double lo,double hi) {
  require(v.is_number(),"Expected a number, not a boolean"); const auto n=v.get<double>();
  require(std::isfinite(n)&&n>=lo&&n<=hi,"Number outside allowed range"); return n;
}
uint64_t integer(const Json &v,uint64_t lo,uint64_t hi) {
  const auto n=number(v,double(lo),double(hi)); require(std::floor(n)==n,"Expected an integer"); return uint64_t(n);
}
std::string text(const Json &v,size_t max) {
  return Project::validatedNativeText(v,max);
}
std::string id(uint64_t n) { return n ? "n"+std::to_string(n) : ""; }
uint64_t identity(const Json &v) {
  const auto s=text(v,32); require(s.size()>1&&s[0]=='n'&&s[1]!='0',"Invalid native identity");
  uint64_t n=0;
  for(size_t i=1;i<s.size();++i) { require(s[i]>='0'&&s[i]<='9'&&n<NativeSong::maximumID/10,"Invalid native identity"); n=n*10+s[i]-'0'; }
  require(n>0&&n<NativeSong::maximumID,"Invalid native identity"); return n;
}
SignalStageEndpoint stageEndpoint(const Json &v) {
  keys(v,{"plugin","stage"});require(v.contains("plugin")!=v.contains("stage"),"Choose one plugin or graph stage endpoint");
  return {v.contains("plugin")?text(v.at("plugin"),128):std::string{},v.contains("stage")?identity(v.at("stage")):0};
}
SignalStageConnection stageCable(const Json &v) {
  return {stageEndpoint(field(v,"source")),stageEndpoint(field(v,"target")),uint32_t(integer(field(v,"output"),0,63)),uint32_t(integer(field(v,"input"),0,63)),number(v.value("gainDB",Json(0)),-96,12),v.contains("enabled")?boolean(v.at("enabled")):true};
}
uint64_t allocate(Tracker::NativeSong &next) {
  require(next.nextID>0&&next.nextID<Tracker::NativeSong::maximumID,"Native song identity limit reached");
  return next.makeEntity().id;
}
uint32_t rowsPerBeat(const OpenMPT::CSoundFile &song,uint16_t pattern) {
  return std::max<uint32_t>(1,song.Patterns[pattern].GetOverrideSignature() ? song.Patterns[pattern].GetRowsPerBeat() : song.m_nDefaultRowsPerBeat ? song.m_nDefaultRowsPerBeat : 4);
}
void integral(Json &v,const char *key,uint64_t lo=0,uint64_t hi=UINT32_MAX) {
  if(v.contains(key)) v[key]=integer(v.at(key),lo,hi);
}
void strictPoints(Json &points) {
  array(points,4096);
  for(auto &p:points) { keys(p,{"position","value","curve","formula"}); integral(p,"position"); }
}
void strictSongSource(Json &source) {
  keys(source,{"id","kind","name","x","y","rate","phase","attack","release","controller","envelopes","muted",
    "audioBus","audioPlugin","audioStage","output","preFader","noteTarget","noteInstrument","amount"});
  integral(source,"controller",0,127);integral(source,"output",0,63);
  if(source.contains("envelopes")) {array(source["envelopes"],1024);for(auto &e:source["envelopes"]) {
    keys(e,{"pattern","enabled","points"});field(e,"points");strictPoints(e["points"]);
  }}
}
void strictRecipe(Json &v) {
  keys(v,{"format","name","path","classID","type","subtype","manufacturer","state","inputs","outputs","parameters","bypass","audioLayout"});
  if(v.contains("parameters")){array(v["parameters"],4096);for(auto &p:v["parameters"]){keys(p,{"id","value"});integral(p,"id");}}
  for(auto k:{"type","subtype","manufacturer"}) integral(v,k);
  for(auto k:{"inputs","outputs"}) if(v.contains(k)) { array(v[k],63); for(auto &port:v[k]) port=integer(port,1,63); }
}
void presentationKeys(const Json &p) {
  keys(p,{"regions","cables","collapsedNodes"});
  if(p.contains("regions"))for(const auto &r:array(p.at("regions"),128))keys(r,{"id","scope","kind","title","text","x","y","width","height","color","collapsed","nodes"});
  if(p.contains("cables"))for(const auto &c:array(p.at("cables"),2048))keys(c,{"source","target","output","input","modulation","points","connection"});
}
void strictDefinition(Json &d) {
  keys(d,{"id","number","name","nodes","audio","modulation","groups","presentation"}); if(d.contains("presentation"))presentationKeys(d.at("presentation")); integral(d,"number",1,999);
  array(field(d,"nodes"),64);
  for(auto &n:d["nodes"]) {
    keys(n,{"id","kind","name","x","y","plugin","rate","phase","attack","release","controller","envelopes","muted"});
    integral(n,"controller",0,127);
    if(n.contains("plugin")) strictRecipe(n["plugin"]);
    if(n.contains("envelopes")) { array(n["envelopes"],1024); for(auto &e:n["envelopes"]) { keys(e,{"pattern","enabled","points"}); field(e,"points"); strictPoints(e["points"]); } }
  }
  if(d.contains("groups")) for(auto &g:array(d["groups"],64)) {
    keys(g,{"id","parent","name","x","y","nodes","bypass","dryRoutes"}); array(field(g,"nodes"),64);
    if(g.contains("bypass"))boolean(g.at("bypass"));
    if(g.contains("dryRoutes"))for(auto &r:array(g["dryRoutes"],256)){keys(r,{"input","output"});const auto &in=field(r,"input");if(!in.is_null()&&in!=""){keys(in,{"source","target","input","output"});}keys(field(r,"output"),{"node","port"});}
  }
  array(field(d,"audio"),256);
  for(auto &e:d["audio"]) { keys(e,{"source","target","input","output","gain"}); integral(e,"input",0,63); integral(e,"output",0,63); }
  array(field(d,"modulation"),256);
  for(auto &e:d["modulation"]) { keys(e,{"source","target","parameter","minimum","maximum","base","enabled","quantized"}); integral(e,"parameter"); }
}
SignalDefinition &definition(NativeSong &n,const Json &raw) {
  const auto wanted=identity(raw); auto &lib=n.signal.library;
  const auto it=std::find_if(lib.begin(),lib.end(),[&](const auto &d){return d.id==wanted;});
  require(it!=lib.end(),"Subgraph does not exist"); return *it;
}
SignalNode &node(SignalDefinition &d,const Json &raw) {
  const auto wanted=identity(raw); auto it=std::find_if(d.nodes.begin(),d.nodes.end(),[&](const auto &n){return n.id==wanted;});
  require(it!=d.nodes.end(),"Graph node does not exist"); return *it;
}
SignalSongSource &songSource(NativeSong &n,const Json &raw) {
  const auto wanted=identity(raw);auto &sources=n.signal.songSources;
  auto found=std::find_if(sources.begin(),sources.end(),[&](const auto &s){return s.node.id==wanted;});
  require(found!=sources.end(),"Song modulation source does not exist");return *found;
}
Json &encodedDefinition(Json &metadata,uint64_t wanted) {
  for(auto &d:metadata["signalGraph"]["library"]) if(d["id"]==id(wanted)) return d;
  throw Api::ApiError(-32602,"Subgraph does not exist");
}
Json &encodedNode(Json &metadata,uint64_t graph,uint64_t wanted) {
  auto &nodes=graph?encodedDefinition(metadata,graph)["nodes"]:metadata["signalGraph"]["songSources"];
  for(auto &node:nodes)if(node["id"]==id(wanted))return node;
  throw Api::ApiError(-32602,"Graph source node does not exist");
}
// Reuse the complete known-field codec, never a second graph serialization.
// Links are restored after parsing so the shared reconciler, not this adapter,
// removes deleted targets; validation still rejects edits through linked uses.
void patchModel(NativeSong &next,const std::function<void(Json &)> &change) {
  auto metadata=Project::encodeNativeMetadata(next); change(metadata);
  metadata["envelopeBank"]["links"]=Json::array();
  auto decoded=Project::decodeNativeMetadata(metadata);
  decoded.envelopeLinks=next.envelopeLinks; next=std::move(decoded);
}
bool pluginInstrument(const std::vector<GraphRackRecord> &rack,uint16_t index) {
  return std::any_of(rack.begin(),rack.end(),[&](const auto &r){return std::find(r.instruments.begin(),r.instruments.end(),index)!=r.instruments.end();});
}
}
GraphOperations::GraphOperations(Tracker::Document &d,std::function<void()> stop,GraphHostHooks host)
  : document_(d),stopPlayback_(std::move(stop)),host_(std::move(host)) {}
std::vector<std::string> GraphOperations::reads() { return {"graph.note.activity","graph.get","graph.selection.copy","graph.group.boundary","graph.automation.get","graph.provenance.get"}; }
std::vector<std::string> GraphOperations::writes() { return {"graph.audio.connection.set","graph.note.connect","graph.note.update","graph.note.disconnect","graph.note.restoreAssignment","graph.create","graph.clone","graph.makeIndependent","graph.selection.paste","graph.selection.cut","graph.selection.duplicate","graph.source.mute","graph.group.bypass","graph.song.source.add","graph.song.source.update","graph.song.source.remove","graph.song.modulation.set","graph.song.modulation.remove","graph.song.group.create","graph.song.group.update","graph.song.group.remove","graph.song.group.export","graph.group.create","graph.group.update","graph.group.remove","graph.group.export","graph.update","graph.remove","graph.node.add","graph.node.remove","graph.nodes.insert","graph.nodes.detach","graph.automation.set","graph.assign","graph.instrument.assign","graph.routes.set","graph.connections.remove","graph.layout.set","graph.presentation.set","graph.commands.set"}; }
Json GraphOperations::invoke(const std::string &method,const Json &p) {
  using namespace Tracker;
  try {
    const auto &song=document_.song();
    if(method=="graph.note.activity") {keys(p,{});return noteActivityJson(host_.noteActivity?host_.noteActivity():NoteActivitySnapshot{},host_.noteActive);}
    if(method=="graph.get") {
      keys(p,{"includeState","includeImplicitMixer"}); const bool state=p.contains("includeState")?boolean(p.at("includeState")):true;
      const auto encoded=Project::encodeNativeMetadata(document_.native()); auto result=encoded.at("signalGraph");
      if(!state) for(auto &d:result["library"]) for(auto &n:d["nodes"]) if(n.contains("plugin")) n["plugin"].erase("state");
      const bool implicit=p.contains("includeImplicitMixer")&&boolean(p.at("includeImplicitMixer"))&&!document_.native().mixer.active();
      if(implicit){NativeSong projection;projection.tracks=document_.native().tracks;projection.masterID=document_.native().masterID;projection.nextID=document_.native().nextID;projection.mixer=document_.native().mixer;projection.ensureMixer();result["mixer"]=Project::encodeMixerMetadata(projection.mixer);}
      else result["mixer"]=encoded.at("mixer");
      result["implicitMixer"]=implicit;result["unitsPerRow"]=65536;
      const auto rack=host_.rack?host_.rack():host_.cachedRack;
      result["plugins"]=Json::array();
      for(const auto &r:rack) { require(r.descriptor.is_object(),"Host rack descriptor must be an object"); auto item=r.descriptor;
        item["audioLayout"]=r.audioLayout; item["id"]=r.id; item["slot"]=r.slot; item["bypass"]=r.bypass; item["instruments"]=r.instruments;item["assignments"]=Json::array();
        for(const auto &assignment:r.assignments){const auto native=document_.native().instruments.find(uint16_t(assignment.instrument));item["assignments"].push_back({{"instrument",assignment.instrument},{"instrumentID",native==document_.native().instruments.end()?std::string{}:id(native->second.id)},{"channel",assignment.channel}});}
        result["plugins"].push_back(std::move(item)); }
      result["patterns"]=Json::array();
      for(const auto &[index,e]:document_.native().patterns) if(song.Patterns.IsValidPat(index))
        result["patterns"].push_back({{"index",index},{"id",id(e.id)},{"rows",song.Patterns[index].GetNumRows()},{"rowsPerBeat",rowsPerBeat(song,index)},{"name",e.name}});
      result["instruments"]=Json::array();
      for(const auto &[index,e]:document_.native().instruments) result["instruments"].push_back({{"index",index},{"id",id(e.id)},
        {"name",::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8,song.GetCharsetInternal(),song.GetInstrumentName(index))},{"plugin",pluginInstrument(rack,index)}});
      result["activity"]=Json::array();
      const auto activity=host_.activity?host_.activity():host_.cachedActivity;
      static const char *roles[]={"row","persistent","ordinary","instrument"};
      for(const auto &a:activity) { require(a.role<4,"Invalid host graph activity role"); result["activity"].push_back({{"target",id(a.target)},{"graph",id(a.graph)},
        {"role",roles[a.role]},{"order",a.order},{"tail",a.tail},{"instrument",id(a.instrument)}}); }
      return result;
    }
#include "GraphProvenanceOperations.inc"
#include "GroupBypassOperations.inc"
    if(method=="graph.selection.copy") {
      keys(p,{"graph","nodes"});const auto graphID=identity(field(p,"graph"));const auto &library=document_.native().signal.library;
      const auto d=std::find_if(library.begin(),library.end(),[&](const auto &v){return v.id==graphID;});require(d!=library.end(),"Subgraph no longer exists");
      std::vector<uint64_t> ids;for(const auto &value:array(field(p,"nodes"),128))ids.push_back(identity(value));
      return {{"fragment",Project::encodeSignalDefinitionMetadata(copySignalSelection(*d,ids))},{"version",1}};
    }
    if(method=="graph.automation.get") {
      keys(p,{"graph","node","pattern"});
      auto metadata=Project::encodeNativeMetadata(document_.native());
      const auto graphID=field(p,"graph").is_null()?0:identity(p.at("graph")),nodeID=identity(field(p,"node"));
      const auto index=uint16_t(integer(field(p,"pattern"),0,UINT16_MAX)); require(song.Patterns.IsValidPat(index),"Pattern does not exist");
      const auto pattern=document_.native().patterns.at(index).id;
      const auto &encoded=encodedNode(metadata,graphID,nodeID);require(encoded.at("kind")=="automation","Choose an automation source node");
      Json points=Json::array(); bool enabled=true;
      for(const auto &e:encoded.at("envelopes")) if(e["pattern"]==id(pattern)) { points=e.at("points"); enabled=e.at("enabled").get<bool>(); }
      return {{"graph",p.at("graph")},{"node",id(nodeID)},{"pattern",index},{"patternID",id(pattern)},
        {"rows",song.Patterns[index].GetNumRows()},{"rowsPerBeat",rowsPerBeat(song,index)},{"unitsPerRow",256},{"enabled",enabled},{"points",points}};
    }
    const auto supported=writes();
    if(std::find(supported.begin(),supported.end(),method)==supported.end()) throw Api::ApiError(-32601,"Unknown or unavailable graph method");
    require(p.is_object(),"Expected parameters object"); require(document_.editable(),"This document is read-only");
    const bool dry=p.contains("dryRun")?boolean(p.at("dryRun")):false;
    NativeSong next=document_.native(); auto &graph=next.signal;
    uint64_t affected=0,nodeID=0,groupID=0;Json clipboardFragment;
    auto ensureMixer=[&]() {next.ensureMixer();};
    auto bus=[&](const Json &raw) {
      const auto target=identity(raw); ensureMixer();
      require(std::any_of(next.mixer.buses.begin(),next.mixer.buses.end(),[&](const auto &b){return b.id==target;}),"Graph target bus does not exist"); return target;
    };
    auto newNumber=[&]() { for(uint16_t n=1;n<=999;++n) if(std::none_of(graph.library.begin(),graph.library.end(),[&](const auto &d){return d.number==n;})) return n; throw Api::ApiError(-32602,"No free subgraph number"); };
#include "SongModulationOperations.inc"
#include "GraphPresentationOperations.inc"
#include "NoteRoutingOperations.inc"
#include "GroupBypassWriteOperations.inc"
#include "StageConnectionOperations.inc"
    if(method=="graph.selection.cut") {
      keys(p,{"graph","nodes","dryRun"});affected=identity(field(p,"graph"));std::vector<uint64_t> ids;for(const auto &v:array(field(p,"nodes"),128))ids.push_back(identity(v));clipboardFragment=Project::encodeSignalDefinitionMetadata(cutSignalSelection(next,affected,ids));
    } else if(method=="graph.selection.paste"||method=="graph.selection.duplicate") {
      const bool duplicate=method=="graph.selection.duplicate";
      if(duplicate)keys(p,{"graph","nodes","parent","x","y","dryRun"});else keys(p,{"graph","fragment","patternMap","parent","x","y","dryRun"});
      affected=identity(field(p,"graph"));SignalDefinition fragment;std::map<uint64_t,uint64_t> patterns;
      if(duplicate){std::vector<uint64_t> ids;for(const auto &v:array(field(p,"nodes"),128))ids.push_back(identity(v));fragment=copySignalSelection(definition(next,field(p,"graph")),ids);for(const auto &n:fragment.nodes)for(const auto &e:n.envelopes)patterns[e.pattern]=e.pattern;}
      else{auto raw=field(p,"fragment");strictDefinition(raw);fragment=Project::decodeSignalDefinitionMetadata(raw);const auto maps=p.value("patternMap",Json::array());for(const auto &m:array(maps,1024)){keys(m,{"source","target"});require(patterns.emplace(identity(field(m,"source")),identity(field(m,"target"))).second,"Duplicate pattern mapping");}}
      const auto pasted=pasteSignalSelection(next,affected,fragment,patterns,p.contains("parent")?identity(p.at("parent")):0,number(p.value("x",Json(0)),0,100000),number(p.value("y",Json(0)),0,100000));
      if(!pasted.identities.empty())nodeID=pasted.identities.begin()->second;
    } else if(method=="graph.makeIndependent") {
      keys(p,{"graph","target","scope","name","number","dryRun"});const auto scope=text(field(p,"scope"),16);require(scope=="channel"||scope=="instrument","Choose channel or instrument scope");
      const auto source=identity(field(p,"graph")),target=identity(field(p,"target"));
      affected=makeSignalUseIndependent(next,source,target,scope=="instrument",p.contains("name")?std::optional<std::string>{text(p.at("name"),256)}:std::nullopt,p.contains("number")?uint16_t(integer(p.at("number"),1,999)):0).graph;
    } else if(method=="graph.source.mute") {
      keys(p,{"graph","node","muted","dryRun"});nodeID=identity(field(p,"node"));affected=field(p,"graph").is_null()?0:identity(p.at("graph"));muteSignalSource(next,affected,nodeID,boolean(field(p,"muted")));
    } else if(method=="graph.connections.remove") {
      keys(p,{"connections","dryRun"});std::vector<SongConnectionRef> cables;
      for(const auto &c:array(field(p,"connections"),512)) {
        const auto kind=text(field(c,"kind"),32);SongConnectionRef ref{};
        if(kind=="output"||kind=="send") {keys(c,{"kind","source","target"});ref.kind=kind=="output"?SongConnectionKind::Output:SongConnectionKind::Send;ref.source=identity(field(c,"source"));ref.target=identity(field(c,"target"));}
        else if(kind=="insert") {keys(c,{"kind","source","plugin"});ref.kind=SongConnectionKind::Insert;ref.source=identity(field(c,"source"));ref.plugin=text(field(c,"plugin"),128);}
        else if(kind=="master-output") {keys(c,{"kind","source"});ref.kind=SongConnectionKind::MasterOutput;ref.source=identity(field(c,"source"));}
        else if(kind=="graph-input"||kind=="graph-output") {const bool input=kind=="graph-input";if(input)keys(c,{"kind","source","target","input"});else keys(c,{"kind","source","target","output"});ref.kind=input?SongConnectionKind::GraphInput:SongConnectionKind::GraphOutput;ref.source=identity(field(c,"source"));ref.target=identity(field(c,"target"));ref.port=uint32_t(integer(field(c,input?"input":"output"),1,63));}
        else if(kind=="stage-connection") {keys(c,{"kind","source","target","output","input"});const auto r=stageCable(c);ref.kind=SongConnectionKind::StageConnection;ref.source=r.source.stage;ref.sourcePlugin=r.source.plugin;ref.target=r.target.stage;ref.plugin=r.target.plugin;ref.output=r.output;ref.port=r.input;}
        else if(kind=="plugin-connection") {keys(c,{"kind","source","output","target","input"});ref.kind=SongConnectionKind::PluginConnection;ref.sourcePlugin=text(field(c,"source"),128);ref.plugin=text(field(c,"target"),128);ref.output=uint32_t(integer(field(c,"output"),0,63));ref.port=uint32_t(integer(field(c,"input"),0,63));}
        else if(kind=="plugin-input") {keys(c,{"kind","source","plugin","input"});ref.kind=SongConnectionKind::PluginInput;ref.source=identity(field(c,"source"));ref.plugin=text(field(c,"plugin"),256);ref.port=uint32_t(integer(field(c,"input"),0,63));}
        else if(kind=="plugin-output") {keys(c,{"kind","target","plugin","output"});ref.kind=SongConnectionKind::PluginOutput;ref.target=identity(field(c,"target"));ref.plugin=text(field(c,"plugin"),256);ref.port=uint32_t(integer(field(c,"output"),0,63));}
        else if(kind=="note") {keys(c,{"kind","route","instrument"});require(c.contains("route")!=c.contains("instrument"),"Choose an explicit note route or implicit instrument assignment");ref.kind=SongConnectionKind::Note;if(c.contains("route"))ref.source=identity(c.at("route"));else ref.target=identity(c.at("instrument"));}
        else if(kind=="modulation") {keys(c,{"kind","source","plugin","parameter"});ref.kind=SongConnectionKind::Modulation;ref.source=identity(field(c,"source"));ref.plugin=text(field(c,"plugin"),256);ref.port=uint32_t(integer(field(c,"parameter"),0,UINT32_MAX));}
        else if(kind=="follower-input") {keys(c,{"kind","node","source","plugin","stage","output","preFader"});ref.kind=SongConnectionKind::FollowerInput;ref.target=identity(field(c,"node"));if(c.contains("stage"))ref.stage=identity(c.at("stage"));if(c.contains("source"))ref.source=identity(c.at("source"));if(c.contains("plugin"))ref.plugin=text(c.at("plugin"),256);ref.port=c.contains("output")?uint32_t(integer(c.at("output"),0,63)):0;ref.preFader=c.contains("preFader")?boolean(c.at("preFader")):false;}
        else throw Api::ApiError(-32602,"Unknown song cable kind");
        cables.push_back(std::move(ref));
      }
      std::vector<std::string> instruments,effects;for(const auto &r:host_.rack?host_.rack():host_.cachedRack)(r.descriptor.value("isInstrument",false)?instruments:effects).push_back(r.id);
      removeSongConnections(next,cables,instruments,effects);
    } else if(method.starts_with("graph.song.group.")) {
      if(method=="graph.song.group.create") {
        keys(p,{"nodes","groups","parent","name","positions","dryRun"});std::vector<std::string> members;std::vector<uint64_t> children;
        const auto rack=host_.rack?host_.rack():host_.cachedRack;
        if(p.contains("nodes")) for(const auto &raw:array(p.at("nodes"),240)){const auto key=text(raw,256);require(std::any_of(graph.songSources.begin(),graph.songSources.end(),[&](const auto &source){return key=="source:n"+std::to_string(source.node.id);})||std::any_of(rack.begin(),rack.end(),[&](const auto &r){return key=="plugin:"+r.id&&!r.descriptor.value("isInstrument",false);}),"Select existing rack effects or modulation sources; instruments and mixer buses are separate boundaries");members.push_back(key);}
        if(p.contains("groups")) for(const auto &raw:array(p.at("groups"),128))children.push_back(identity(raw));
        if(p.contains("positions")) for(const auto &v:array(p.at("positions"),240)){keys(v,{"node","x","y"});const auto key=text(field(v,"node"),256);require(std::find(members.begin(),members.end(),key)!=members.end(),"Position must belong to a selected processor");next.signal.layout[key]={number(field(v,"x"),0,100000),number(field(v,"y"),0,100000)};}
        groupID=allocate(next);groupSongSignalNodes(next.signal,members,children,groupID,!p.contains("parent")||p.at("parent").is_null()?0:identity(p.at("parent")),text(p.value("name","Group"),256));
      } else if(method=="graph.song.group.update") {
        keys(p,{"group","name","x","y","dryRun"});groupID=identity(field(p,"group"));auto g=std::find_if(next.signal.groups.begin(),next.signal.groups.end(),[&](const auto &g){return g.id==groupID;});require(g!=next.signal.groups.end(),"Song processing group does not exist");
        if(p.contains("name"))g->name=text(p.at("name"),256);const auto x=p.contains("x")?number(p.at("x"),0,100000):g->x,y=p.contains("y")?number(p.at("y"),0,100000):g->y;
        if(p.contains("x")||p.contains("y"))moveSongSignalGroup(next.signal,groupID,x,y);
      } else if(method=="graph.song.group.remove") {keys(p,{"group","dryRun"});groupID=identity(field(p,"group"));ungroupSongSignalNodes(next.signal,groupID);}
      else if(method=="graph.song.group.export") {
        keys(p,{"group","name","number","dryRun"});groupID=identity(field(p,"group"));
        const auto rack=host_.rack?host_.rack():host_.cachedRack;require(bool(host_.cloneRackSlot),"Saving a rack chain needs the host baseline-state hook");
        std::set<uint64_t> descendants{groupID};for(size_t i=0;i<graph.groups.size();++i)for(const auto &g:graph.groups)if(descendants.contains(g.parent))descendants.insert(g.id);
        std::set<std::string> selected;for(const auto &g:graph.groups)if(descendants.contains(g.id))selected.insert(g.nodes.begin(),g.nodes.end());
        std::vector<std::pair<std::string,GraphPluginRecipe>> effects;
        for(const auto &r:rack)if(!r.descriptor.value("isInstrument",false)){
          GraphPluginRecipe recipe;
          if(selected.contains("plugin:"+r.id)){auto clone=host_.cloneRackSlot(r.slot);require(!clone.instrument,"Choose effect processors");recipe=std::move(clone.recipe);recipe.bypass=r.bypass;}
          effects.emplace_back(r.id,std::move(recipe));
        }
        auto implicit=next;if(!implicit.mixer.active())implicit.ensureMixer();
        auto copy=extractSongSignalGroup(graph,implicit.mixer,groupID,effects,[&]{return allocate(next);});
        copy.number=p.contains("number")?uint16_t(integer(p.at("number"),1,999)):newNumber();if(p.contains("name"))copy.name=text(p.at("name"),256);affected=copy.id;graph.library.push_back(std::move(copy));
      }
      else throw Api::ApiError(-32601,"Unknown song processing group method");
    } else if(method=="graph.create") {
      keys(p,{"name","number","dryRun"}); SignalDefinition d; d.id=allocate(next); affected=d.id;
      d.number=p.contains("number")?uint16_t(integer(p.at("number"),1,999)):newNumber();
      d.name=text(p.value("name",Json("New subgraph")),256);
      const auto input=allocate(next),output=allocate(next);
      d.nodes={{input,SignalNodeKind::Input,"Input",40,100},{output,SignalNodeKind::Output,"Output",620,100}};
      d.audio={{input,output}}; graph.library.push_back(std::move(d));
    } else if(method=="graph.clone"||method=="graph.group.export") {
      const bool exporting=method=="graph.group.export";
      if(exporting) keys(p,{"graph","group","name","number","dryRun"}); else keys(p,{"graph","name","number","dryRun"});
      auto copy=definition(next,field(p,"graph"));
      if(exporting) {const auto input=allocate(next),output=allocate(next);copy=extractSignalGroup(copy,identity(field(p,"group")),input,output);}
      copy.id=allocate(next); affected=copy.id;
      copy.number=p.contains("number")?uint16_t(integer(p.at("number"),1,999)):newNumber();
      if(p.contains("name")) copy.name=text(p.at("name"),256);
      std::map<uint64_t,uint64_t> mapping;
      for(auto &n:copy.nodes) { const auto fresh=allocate(next); mapping[n.id]=fresh; n.id=fresh; }
      for(auto &g:copy.groups) {const auto fresh=allocate(next);mapping[g.id]=fresh;g.id=fresh;}
      for(auto &g:copy.groups) {if(g.parent)g.parent=mapping.at(g.parent);for(auto &member:g.nodes)member=mapping.at(member);}
        remapSignalGroupDryRoutes(copy,mapping);
      for(auto link:std::vector<EnvelopeLink>(next.envelopeLinks)) if(link.target.kind==EnvelopeTargetKind::Graph&&mapping.contains(link.target.owner)) { link.target.owner=mapping.at(link.target.owner); next.envelopeLinks.push_back(link); }
      std::map<std::string,std::string> visualIDs;for(auto [from,to]:mapping)visualIDs[id(from)]=id(to);remapSignalPresentation(copy.presentation,visualIDs);
      for(auto &e:copy.audio) { e.source=mapping.at(e.source); e.target=mapping.at(e.target); }
      for(auto &e:copy.modulation) { e.source=mapping.at(e.source); e.target=mapping.at(e.target); }
      graph.library.push_back(std::move(copy));
    } else if(method=="graph.update") {
      keys(p,{"definition","dryRun"}); auto replacement=field(p,"definition"); strictDefinition(replacement);
      const auto previous=definition(next,field(replacement,"id")); affected=previous.id;
      patchModel(next,[&](Json &m) {
        const auto previousJSON=encodedDefinition(m,affected);
        if(!replacement.contains("groups")) replacement["groups"]=previousJSON.at("groups");
        if(!replacement.contains("presentation"))replacement["presentation"]=previousJSON.at("presentation");
        for(const auto &g:replacement["groups"]) {
          const auto gid=identity(field(g,"id"));
          require(std::any_of(previous.groups.begin(),previous.groups.end(),[&](const auto &old){return old.id==gid;}),"Allocate group identities using graph.group.create");
        }
        for(auto &n:replacement["nodes"]) {
          const auto nid=identity(field(n,"id")); const auto it=std::find_if(previousJSON["nodes"].begin(),previousJSON["nodes"].end(),[&](const auto &v){return v["id"]==id(nid)&&v["kind"]==field(n,"kind");});
          require(it!=previousJSON["nodes"].end(),"Add nodes using graph.node.add; identities and kinds cannot be replaced");
          if(n.contains("plugin")&&!n["plugin"].contains("state")) n["plugin"]["state"]=it->at("plugin").at("state");
          if(n.contains("plugin")&&(!n["plugin"].contains("audioLayout")||n["plugin"]["audioLayout"]==""))n["plugin"]["audioLayout"]=it->at("plugin").value("audioLayout",Json(""));
        }
        encodedDefinition(m,affected)=replacement;
      });
      auto &updated=definition(next,field(replacement,"id"));
      if(host_.prepareRecipe)for(auto &n:updated.nodes)if(n.kind==SignalNodeKind::Plugin){const auto old=std::find_if(previous.nodes.begin(),previous.nodes.end(),[&](const auto &v){return v.id==n.id;});if(old==previous.nodes.end()||old->plugin!=n.plugin)host_.prepareRecipe(n.plugin);}
      reconcileSignalPresentation(updated,previous);
    } else if(method=="graph.group.create") {
      keys(p,{"graph","nodes","parent","name","dryRun"});auto &d=definition(next,field(p,"graph"));affected=d.id;groupID=allocate(next);
      std::vector<uint64_t> members;for(const auto &raw:array(field(p,"nodes"),64))members.push_back(identity(raw));
      const auto parent=!p.contains("parent")||p.at("parent").is_null()?0:identity(p.at("parent"));
      groupSignalNodes(d,members,groupID,parent,text(p.value("name",Json("Group")),256));
    } else if(method=="graph.group.update") {
      keys(p,{"graph","group","name","x","y","dryRun"});auto &d=definition(next,field(p,"graph"));affected=d.id;groupID=identity(field(p,"group"));
      auto g=std::find_if(d.groups.begin(),d.groups.end(),[&](const auto &g){return g.id==groupID;});require(g!=d.groups.end(),"Processing group does not exist");
      if(p.contains("name"))g->name=text(p.at("name"),256);
      if(p.contains("x")||p.contains("y"))moveSignalGroup(d,groupID,p.contains("x")?number(p.at("x"),-100000,100000):g->x,p.contains("y")?number(p.at("y"),-100000,100000):g->y);
    } else if(method=="graph.group.remove") {
      keys(p,{"graph","group","deleteContents","dryRun"});auto &d=definition(next,field(p,"graph"));affected=d.id;groupID=identity(field(p,"group"));
      require(std::any_of(d.groups.begin(),d.groups.end(),[&](const auto &g){return g.id==groupID;}),"Processing group does not exist");
      if(p.contains("deleteContents")&&boolean(p.at("deleteContents"))) {
        std::set<uint64_t> groups{groupID},members;bool changed=true;
        while(changed){changed=false;for(const auto &g:d.groups)if(groups.contains(g.parent))changed|=groups.insert(g.id).second;}
        for(const auto &g:d.groups)if(groups.contains(g.id))members.insert(g.nodes.begin(),g.nodes.end());
        std::erase_if(d.nodes,[&](const auto &n){return members.contains(n.id);});
        std::erase_if(d.audio,[&](const auto &e){return members.contains(e.source)||members.contains(e.target);});
        std::erase_if(d.modulation,[&](const auto &e){return members.contains(e.source)||members.contains(e.target);});
        pruneSignalGroups(d);
      } else ungroupSignalNodes(d,groupID);
    } else if(method=="graph.remove") {
      keys(p,{"graph","dryRun"}); affected=definition(next,field(p,"graph")).id;
      require(std::none_of(graph.assignments.begin(),graph.assignments.end(),[&](const auto &a){return a.graph==affected;})&&std::none_of(graph.instrumentAssignments.begin(),graph.instrumentAssignments.end(),[&](const auto &a){return a.graph==affected;})&&std::none_of(graph.commands.begin(),graph.commands.end(),[&](const auto &c){return c.graph==affected;}),"Remove assignments and pattern commands before deleting this graph");
      std::erase_if(graph.library,[&](const auto &d){return d.id==affected;});
    } else if(method=="graph.node.add") {
      keys(p,{"graph","kind","name","x","y","slot","plugin","insertAfter","insertEdge","connect","audioInput","parent","dryRun"});
      auto &d=definition(next,field(p,"graph")); affected=d.id; const auto kind=text(field(p,"kind"),32);
      static const std::vector<std::string> names={"input","output","plugin","lfo","follower","random","note-envelope","midi","amount","automation"};
      const auto which=std::find(names.begin()+2,names.end(),kind); require(which!=names.end(),"Choose an effect or modulation node kind");
      SignalNode n; n.id=allocate(next); nodeID=n.id; n.kind=SignalNodeKind(which-names.begin());
      n.name=text(p.value("name",Json(kind)),256); n.x=number(p.value("x",Json(300)),-100000,100000); n.y=number(p.value("y",Json(100)),-100000,100000);
      if(n.kind==SignalNodeKind::Plugin) {
        require(p.contains("slot")!=p.contains("plugin"),"Supply exactly one rack slot or plugin recipe");
        if(p.contains("slot")) {
          const auto slot=uint32_t(integer(p.at("slot"),0,63));
          require(bool(host_.cloneRackSlot),"Rack cloning needs a real host baseline-state hook; use a plugin recipe instead");
          auto source=host_.cloneRackSlot(slot);
          require(!source.instrument&&source.instruments.empty()&&source.recipe.type!=0x61756d75,"Subgraph inserts require an effect plugin");
          n.plugin=std::move(source.recipe); if(!p.contains("name")) n.name=n.plugin.name;
        }
      } else require(!p.contains("slot")&&!p.contains("plugin"),"Only plugin nodes accept recipes");
      require(!p.contains("insertAfter")||!p.contains("insertEdge"),"Choose insertAfter or insertEdge, not both");
      if(p.contains("insertEdge")) {
        require(n.kind==SignalNodeKind::Plugin,"Only effects can be inserted into an audio chain");
        const auto index=size_t(integer(p.at("insertEdge"),0,255));require(index<d.audio.size(),"Insertion cable no longer exists");
        auto &edge=d.audio[index];const auto source=edge.source;const auto output=edge.output;const auto gain=edge.gain;
        edge.source=nodeID;edge.output=0;edge.gain=1;d.audio.push_back({source,nodeID,output,0,gain});
      }
      if(p.contains("insertAfter")) {
        require(n.kind==SignalNodeKind::Plugin,"Only effects can be inserted into audio chains"); const auto after=identity(p.at("insertAfter"));
        auto edge=std::find_if(d.audio.begin(),d.audio.end(),[&](const auto &e){return e.source==after&&e.output==0&&e.input==0;});
        require(edge!=d.audio.end()&&std::count_if(d.audio.begin(),d.audio.end(),[&](const auto &e){return e.source==after&&e.output==0;})==1,"Insert after a node with one main audio destination");
        edge->source=nodeID; d.audio.push_back({after,nodeID});
      }
      if(p.contains("connect")) {
        require(!p.contains("insertAfter")&&!p.contains("insertEdge"),"Choose cable insertion or connection, not both");
        const auto &c=p.at("connect");keys(c,{"node","port","output","modulation","base","quantized"});
        const auto endpoint=identity(field(c,"node"));auto existing=std::find_if(d.nodes.begin(),d.nodes.end(),[&](const auto &v){return v.id==endpoint;});
        require(existing!=d.nodes.end(),"Connection endpoint no longer exists");
        const bool output=boolean(field(c,"output")),mod=c.contains("modulation")?boolean(c.at("modulation")):false;
        const auto port=uint32_t(integer(c.value("port",Json(0)),0,mod?UINT32_MAX:63));
        if(mod) {
          require(!output,"Choose a parameter input before creating its modulation source");
          double base=number(c.value("base",Json(0)),0,1);
          const auto peer=std::find_if(d.modulation.begin(),d.modulation.end(),[&](const auto &e){return e.target==endpoint&&e.parameter==port;});if(peer!=d.modulation.end())base=peer->base;
          d.modulation.push_back({nodeID,endpoint,port,0,0,base,true,c.contains("quantized")?boolean(c.at("quantized")):false});
        } else {
          require(!c.contains("quantized"),"Quantized mode applies only to a parameter modulation connection");
          d.audio.push_back({output?endpoint:nodeID,output?nodeID:endpoint,output?port:0,output?0:port});
          if(port>0&&existing->kind==SignalNodeKind::Plugin){auto &ports=output?existing->plugin.outputs:existing->plugin.inputs;if(std::find(ports.begin(),ports.end(),port)==ports.end())ports.push_back(port);}
        }
      }
      if(p.contains("audioInput")) {
        require(n.kind==SignalNodeKind::Follower&&p.contains("connect"),"An audioInput creates a follower and its parameter connection together");
        const auto &connect=p.at("connect");require(connect.contains("modulation")&&boolean(connect.at("modulation")),"audioInput requires a parameter modulation connection");
        const auto &input=p.at("audioInput");keys(input,{"node","port"});
        const auto source=identity(field(input,"node"));const auto port=uint32_t(integer(input.value("port",Json(0)),0,63));
        auto existing=std::find_if(d.nodes.begin(),d.nodes.end(),[&](const auto &v){return v.id==source;});require(existing!=d.nodes.end(),"Follower audio source no longer exists");
        d.audio.push_back({source,nodeID,port,0});
        if(port>0&&existing->kind==SignalNodeKind::Plugin){auto &ports=existing->plugin.outputs;if(std::find(ports.begin(),ports.end(),port)==ports.end())ports.push_back(port);}
      }
      if(p.contains("parent")&&!p.at("parent").is_null()) {
        const auto parent=identity(p.at("parent"));auto g=std::find_if(d.groups.begin(),d.groups.end(),[&](const auto &g){return g.id==parent;});
        require(g!=d.groups.end(),"Processing group parent does not exist");g->nodes.push_back(nodeID);
      }
      d.nodes.push_back(std::move(n));
      if(p.contains("plugin")) {
        auto recipe=p.at("plugin"); strictRecipe(recipe);
        patchModel(next,[&](Json &m){ encodedDefinition(m,affected)["nodes"].back()["plugin"]=recipe; });
        if(host_.prepareRecipe)host_.prepareRecipe(definition(next,id(affected)).nodes.back().plugin);
      }
    } else if(method=="graph.assign"||method=="graph.instrument.assign") {
      const bool instrument=method=="graph.instrument.assign";
      if(instrument) keys(p,{"instrument","graph","amount","wet","dryRun"}); else keys(p,{"target","graph","amount","wet","dryRun"});
      const auto &raw=field(p,"graph");
      const double amount=number(p.value("amount",Json(1)),0,1),wet=number(p.value("wet",Json(1)),0,1);
      uint64_t target=0;
      if(instrument) {
        const auto index=uint16_t(integer(field(p,"instrument"),1,255));
        require(index<=song.GetNumInstruments()&&song.Instruments[index]&&next.instruments.contains(index),"Instrument does not exist");
        target=next.instruments.at(index).id;
        if(!raw.is_null()) require(!pluginInstrument(host_.rack?host_.rack():host_.cachedRack,index),"Plugin instruments use their output bus graph; this assignment processes sample voices");
        ensureMixer();
      } else target=bus(field(p,"target"));
      auto &assignments=instrument?graph.instrumentAssignments:graph.assignments;
      auto it=std::find_if(assignments.begin(),assignments.end(),[&](const auto &a){return a.target==target;});
      if(raw.is_null()) { if(it!=assignments.end()) assignments.erase(it); }
      else { affected=definition(next,raw).id; const SignalAssignment value{target,affected,amount,wet};
        if(it==assignments.end()) assignments.push_back(value); else *it=value; }
    } else if(method=="graph.routes.set") {
      keys(p,{"inputs","outputs","dryRun"});
      if(p.contains("inputs")) {
        array(p.at("inputs"),128); graph.inputs.clear();
        for(const auto &r:p.at("inputs")) { keys(r,{"source","target","input","gainDB","preFader"});
          graph.inputs.push_back({bus(field(r,"source")),bus(field(r,"target")),uint32_t(integer(field(r,"input"),1,63)),number(r.value("gainDB",Json(0)),-96,12),r.contains("preFader")?boolean(r.at("preFader")):false}); }
      }
      if(p.contains("outputs")) {
        array(p.at("outputs"),128); graph.outputs.clear();
        for(const auto &r:p.at("outputs")) { keys(r,{"source","target","output"});
          graph.outputs.push_back({bus(field(r,"source")),bus(field(r,"target")),uint32_t(integer(field(r,"output"),1,63))}); }
      }
    } else if(method=="graph.layout.set") {
      keys(p,{"positions","groups","reset","dryRun"});
      if(p.contains("groups")) for(const auto &v:array(p.at("groups"),128)){keys(v,{"group","x","y"});moveSongSignalGroup(graph,identity(field(v,"group")),number(field(v,"x"),0,100000),number(field(v,"y"),0,100000));} if(p.contains("reset")&&boolean(p.at("reset"))) graph.layout.clear();
      if(p.contains("positions")) for(const auto &v:array(p.at("positions"),8192)) { keys(v,{"node","x","y"});
        graph.layout[text(field(v,"node"),256)]={number(field(v,"x"),0,100000),number(field(v,"y"),0,100000)}; }
    } else if(method=="graph.commands.set") {
      keys(p,{"pattern","lanes","commands","dryRun"});
      const auto index=uint16_t(integer(field(p,"pattern"),0,UINT16_MAX)); require(song.Patterns.IsValidPat(index),"Pattern does not exist");
      const auto pattern=next.patterns.at(index).id;
      // Resolve all buses before encoding; lane changes and command replacement
      // must be decoded together so a valid lane removal can clear its commands.
      if(p.contains("lanes")) for(const auto &l:array(p.at("lanes"),240)) { keys(l,{"target","count"}); bus(field(l,"target")); integer(field(l,"count"),0,8); }
      Json commands=Json::array();
      if(p.contains("commands")) {
        commands=array(p.at("commands"),65536);
        for(auto &c:commands) { keys(c,{"target","graph","position","column","kind","amount","wet","tails"});
          integral(c,"position"); integral(c,"column",0,7); c["pattern"]=id(pattern);
          if(c.value("kind",Json())=="clear"&&!c.contains("graph")) c["graph"]="";
        }
      }
      const auto previous=graph.commands;
      patchModel(next,[&](Json &m) {
        auto &g=m["signalGraph"];
        if(p.contains("lanes")) for(const auto &l:p.at("lanes")) {
          auto &lanes=g["lanes"]; const auto target=identity(l.at("target")); const auto count=integer(l.at("count"),0,8);
          lanes.erase(std::remove_if(lanes.begin(),lanes.end(),[&](const auto &v){return v["target"]==id(target);}),lanes.end());
          if(count) lanes.push_back({{"target",id(target)},{"count",count}});
        }
        if(p.contains("commands")) {
          auto &all=g["commands"]; all.erase(std::remove_if(all.begin(),all.end(),[&](const auto &c){return c["pattern"]==id(pattern);}),all.end());
          for(const auto &c:commands) all.push_back(c);
        }
      });
      // A replacement identical to this pattern must not merely reorder its
      // commands around other patterns and thereby create history or stop audio.
      std::vector<SignalCommand> oldPattern,newPattern;
      for(const auto &c:previous) if(c.pattern==pattern) oldPattern.push_back(c);
      for(const auto &c:graph.commands) if(c.pattern==pattern) newPattern.push_back(c);
      if(oldPattern==newPattern) graph.commands=previous;
    } else if(method=="graph.automation.set") {
      keys(p,{"graph","node","pattern","enabled","points","dryRun"});
      const bool songSourceTarget=field(p,"graph").is_null();
      affected=songSourceTarget?0:identity(p.at("graph"));
      auto &n=songSourceTarget?songSource(next,field(p,"node")).node:node(definition(next,p.at("graph")),field(p,"node"));
      nodeID=n.id; require(n.kind==SignalNodeKind::Automation,"Choose an automation source node");
      const auto index=uint16_t(integer(field(p,"pattern"),0,UINT16_MAX)); require(song.Patterns.IsValidPat(index),"Pattern does not exist");
      const auto pattern=next.patterns.at(index).id; auto points=field(p,"points"); strictPoints(points);
      const bool enabled=p.contains("enabled")?boolean(p.at("enabled")):true;
      auto previous=n.envelopes;
      patchModel(next,[&](Json &m){
        auto &envelopes=encodedNode(m,affected,nodeID).at("envelopes");
        envelopes.erase(std::remove_if(envelopes.begin(),envelopes.end(),[&](const auto &e){return e["pattern"]==id(pattern);}),envelopes.end());
        if(!points.empty()) envelopes.push_back({{"pattern",id(pattern)},{"enabled",enabled},{"points",points}});
      });
      auto &envelopes=(songSourceTarget?songSource(next,id(nodeID)).node:node(definition(next,id(affected)),id(nodeID))).envelopes;
      const auto lane=std::find_if(envelopes.begin(),envelopes.end(),[&](const auto &e){return e.pattern==pattern;});
      replaceSignalEnvelope(previous,lane==envelopes.end()?SignalPatternEnvelope{pattern}:*lane);
      envelopes=std::move(previous);
    } else if(method=="graph.nodes.detach") {
      keys(p,{"graph","nodes","remove","positions","heal","dryRun"});auto &d=definition(next,field(p,"graph"));affected=d.id;
      std::vector<uint64_t> moving;for(const auto &v:array(field(p,"nodes"),64))moving.push_back(identity(v));
      const bool remove=p.contains("remove")?boolean(p.at("remove")):false;
      require(!remove||!p.contains("positions"),"Removed nodes cannot have positions");
      std::optional<SignalHealPath> heal;if(p.contains("heal")){const auto &h=p.at("heal");keys(h,{"incoming","outgoing"});heal.emplace();if(h.contains("incoming")&&!h.at("incoming").is_null())heal->incoming=size_t(integer(h.at("incoming"),0,255));if(h.contains("outgoing")&&!h.at("outgoing").is_null())heal->outgoing=size_t(integer(h.at("outgoing"),0,255));}
      detachSignalNodes(d,moving,remove,heal);
      const auto positions=p.value("positions",Json::array());
      for(const auto &v:array(positions,64)){keys(v,{"node","x","y"});auto id=identity(field(v,"node"));const auto x=number(field(v,"x"),0,100000),y=number(field(v,"y"),0,100000);if(std::any_of(d.groups.begin(),d.groups.end(),[&](const auto &g){return g.id==id;})){const auto members=signalGroupMembers(d,id);require(std::all_of(members.begin(),members.end(),[&](auto node){return std::find(moving.begin(),moving.end(),node)!=moving.end();}),"Positioned group must be entirely selected");moveSignalGroup(d,id,x,y);}else{require(std::find(moving.begin(),moving.end(),id)!=moving.end(),"Position must belong to a detached node");auto &n=*std::find_if(d.nodes.begin(),d.nodes.end(),[&](const auto &n){return n.id==id;});n.x=x;n.y=y;}}
    } else if(method=="graph.nodes.insert") {
      keys(p,{"graph","nodes","edge","positions","dryRun"});auto &d=definition(next,field(p,"graph"));affected=d.id;
      std::vector<uint64_t> moving;for(const auto &v:array(field(p,"nodes"),32))moving.push_back(identity(v));
      insertSignalNodes(d,moving,size_t(integer(field(p,"edge"),0,255)));
      const auto positions=p.value("positions",Json::array());
      for(const auto &v:array(positions,32)){keys(v,{"node","x","y"});auto id=identity(field(v,"node"));require(std::find(moving.begin(),moving.end(),id)!=moving.end(),"Position must belong to an inserted node");auto &n=*std::find_if(d.nodes.begin(),d.nodes.end(),[&](const auto &n){return n.id==id;});n.x=number(field(v,"x"),0,100000);n.y=number(field(v,"y"),0,100000);}
    } else if(method=="graph.node.remove") {
      keys(p,{"graph","node","dryRun"}); auto &d=definition(next,field(p,"graph")); affected=d.id;
      auto &n=node(d,field(p,"node")); nodeID=n.id;
      require(n.kind!=SignalNodeKind::Input&&n.kind!=SignalNodeKind::Output,"Input and Output nodes cannot be removed");
      std::erase_if(d.nodes,[&](const auto &v){return v.id==nodeID;});
      std::erase_if(d.audio,[&](const auto &e){return e.source==nodeID||e.target==nodeID;});
      std::erase_if(d.modulation,[&](const auto &e){return e.source==nodeID||e.target==nodeID;});
      pruneSignalGroups(d);
    }
    reconcileEnvelopeLinks(next,song); next.validate(song);
    if(host_.validateCandidate)host_.validateCandidate(next);
    const bool changed=next!=document_.native();
    if(changed&&!dry) {
      std::function<void()> publish;
      if(next.mixer!=document_.native().mixer||!sameSignalProcessing(next.signal,document_.native().signal)) {
        if(host_.preparePublication)publish=host_.preparePublication(next);else publish=stopPlayback_;
      }
      document_.annotate([&](NativeSong &n){n=next;},publish);
    }
    Json response={{"dryRun",dry},{"wouldChange",changed},{"graph",id(affected)},{"node",id(nodeID)},{"route",method.starts_with("graph.note.")?id(nodeID):std::string{}},{"group",id(groupID)}};if(!clipboardFragment.is_null()){response["fragment"]=std::move(clipboardFragment);response["version"]=1;}return response;
  } catch(const std::invalid_argument &e) { throw Api::ApiError(-32602,e.what()); }
    catch(const std::out_of_range &e) { throw Api::ApiError(-32602,e.what()); }
    catch(const Json::exception &e) { throw Api::ApiError(-32602,e.what()); }
}
}
