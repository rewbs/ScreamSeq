#include "GraphOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/NativeMetadata.hpp"
#include "editor/TrackerDocument.hpp"
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
  require(v.is_string(),"Expected a string"); const auto s=v.get<std::string>();
  require(s.size()<=max*4&&s.find('\0')==std::string::npos,"Invalid or oversized string");
  if(!s.empty()) { const auto units=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    require(units>0&&size_t(units)<=max,"Invalid UTF-8 or oversized string"); }
  return s;
}
std::string id(uint64_t n) { return n ? "n"+std::to_string(n) : ""; }
uint64_t identity(const Json &v) {
  const auto s=text(v,32); require(s.size()>1&&s[0]=='n'&&s[1]!='0',"Invalid native identity");
  uint64_t n=0;
  for(size_t i=1;i<s.size();++i) { require(s[i]>='0'&&s[i]<='9'&&n<NativeSong::maximumID/10,"Invalid native identity"); n=n*10+s[i]-'0'; }
  require(n>0&&n<NativeSong::maximumID,"Invalid native identity"); return n;
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
void strictRecipe(Json &v) {
  keys(v,{"format","name","path","classID","type","subtype","manufacturer","state","inputs","outputs"});
  for(auto k:{"type","subtype","manufacturer"}) integral(v,k);
  for(auto k:{"inputs","outputs"}) if(v.contains(k)) { array(v[k],63); for(auto &port:v[k]) port=integer(port,1,63); }
}
void strictDefinition(Json &d) {
  keys(d,{"id","number","name","nodes","audio","modulation"}); integral(d,"number",1,999);
  array(field(d,"nodes"),64);
  for(auto &n:d["nodes"]) {
    keys(n,{"id","kind","name","x","y","plugin","rate","phase","attack","release","controller","envelopes"});
    integral(n,"controller",0,127);
    if(n.contains("plugin")) strictRecipe(n["plugin"]);
    if(n.contains("envelopes")) { array(n["envelopes"],1024); for(auto &e:n["envelopes"]) { keys(e,{"pattern","enabled","points"}); field(e,"points"); strictPoints(e["points"]); } }
  }
  array(field(d,"audio"),256);
  for(auto &e:d["audio"]) { keys(e,{"source","target","input","output","gain"}); integral(e,"input",0,63); integral(e,"output",0,63); }
  array(field(d,"modulation"),256);
  for(auto &e:d["modulation"]) { keys(e,{"source","target","parameter","minimum","maximum","base","enabled"}); integral(e,"parameter"); }
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
Json &encodedDefinition(Json &metadata,uint64_t wanted) {
  for(auto &d:metadata["signalGraph"]["library"]) if(d["id"]==id(wanted)) return d;
  throw Api::ApiError(-32602,"Subgraph does not exist");
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
std::vector<std::string> GraphOperations::reads() { return {"graph.get","graph.automation.get"}; }
std::vector<std::string> GraphOperations::writes() { return {"graph.create","graph.clone","graph.update","graph.remove","graph.node.add","graph.node.remove","graph.automation.set","graph.assign","graph.instrument.assign","graph.routes.set","graph.layout.set","graph.commands.set"}; }
Json GraphOperations::invoke(const std::string &method,const Json &p) {
  using namespace Tracker;
  try {
    const auto &song=document_.song();
    if(method=="graph.get") {
      keys(p,{"includeState"}); const bool state=p.contains("includeState")?boolean(p.at("includeState")):true;
      const auto encoded=Project::encodeNativeMetadata(document_.native()); auto result=encoded.at("signalGraph");
      if(!state) for(auto &d:result["library"]) for(auto &n:d["nodes"]) if(n.contains("plugin")) n["plugin"].erase("state");
      result["mixer"]=encoded.at("mixer"); result["unitsPerRow"]=65536;
      const auto rack=host_.rack?host_.rack():host_.cachedRack;
      result["plugins"]=Json::array();
      for(const auto &r:rack) { require(r.descriptor.is_object(),"Host rack descriptor must be an object"); auto item=r.descriptor;
        item["id"]=r.id; item["slot"]=r.slot; item["bypass"]=r.bypass; item["instruments"]=r.instruments; result["plugins"].push_back(std::move(item)); }
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
    if(method=="graph.automation.get") {
      keys(p,{"graph","node","pattern"});
      auto metadata=Project::encodeNativeMetadata(document_.native());
      const auto graphID=identity(field(p,"graph")),nodeID=identity(field(p,"node"));
      const auto index=uint16_t(integer(field(p,"pattern"),0,UINT16_MAX)); require(song.Patterns.IsValidPat(index),"Pattern does not exist");
      const auto pattern=document_.native().patterns.at(index).id;
      const auto &d=encodedDefinition(metadata,graphID); const auto it=std::find_if(d["nodes"].begin(),d["nodes"].end(),[&](const auto &n){return n["id"]==id(nodeID)&&n["kind"]=="automation";});
      require(it!=d["nodes"].end(),"Choose an automation source node");
      Json points=Json::array(); bool enabled=true;
      for(const auto &e:it->at("envelopes")) if(e["pattern"]==id(pattern)) { points=e.at("points"); enabled=e.at("enabled").get<bool>(); }
      return {{"graph",id(graphID)},{"node",id(nodeID)},{"pattern",index},{"patternID",id(pattern)},
        {"rows",song.Patterns[index].GetNumRows()},{"rowsPerBeat",rowsPerBeat(song,index)},{"unitsPerRow",256},{"enabled",enabled},{"points",points}};
    }
    const auto supported=writes();
    if(std::find(supported.begin(),supported.end(),method)==supported.end()) throw Api::ApiError(-32601,"Unknown or unavailable graph method");
    require(p.is_object(),"Expected parameters object"); require(document_.editable(),"This document is read-only");
    const bool dry=p.contains("dryRun")?boolean(p.at("dryRun")):false;
    NativeSong next=document_.native(); auto &graph=next.signal;
    uint64_t affected=0,nodeID=0;
    auto ensureMixer=[&]() {
      if(next.mixer.active()) return;
      const auto master=allocate(next);
      for(const auto &[channel,track]:next.tracks) next.mixer.buses.push_back({track.id,master,MixerBusKind::Track,track.name.empty()?"Track "+std::to_string(channel+1):track.name,track.color});
      next.mixer.buses.push_back({master,0,MixerBusKind::Master,"Master"});
    };
    auto bus=[&](const Json &raw) {
      const auto target=identity(raw); ensureMixer();
      require(std::any_of(next.mixer.buses.begin(),next.mixer.buses.end(),[&](const auto &b){return b.id==target;}),"Graph target bus does not exist"); return target;
    };
    auto newNumber=[&]() { for(uint16_t n=1;n<=999;++n) if(std::none_of(graph.library.begin(),graph.library.end(),[&](const auto &d){return d.number==n;})) return n; throw Api::ApiError(-32602,"No free subgraph number"); };
    if(method=="graph.create") {
      keys(p,{"name","number","dryRun"}); SignalDefinition d; d.id=allocate(next); affected=d.id;
      d.number=p.contains("number")?uint16_t(integer(p.at("number"),1,999)):newNumber();
      d.name=text(p.value("name",Json("New subgraph")),256);
      const auto input=allocate(next),output=allocate(next);
      d.nodes={{input,SignalNodeKind::Input,"Input",40,100},{output,SignalNodeKind::Output,"Output",620,100}};
      d.audio={{input,output}}; graph.library.push_back(std::move(d));
    } else if(method=="graph.clone") {
      keys(p,{"graph","name","number","dryRun"}); auto copy=definition(next,field(p,"graph"));
      copy.id=allocate(next); affected=copy.id;
      copy.number=p.contains("number")?uint16_t(integer(p.at("number"),1,999)):newNumber();
      if(p.contains("name")) copy.name=text(p.at("name"),256);
      std::map<uint64_t,uint64_t> mapping;
      for(auto &n:copy.nodes) { const auto fresh=allocate(next); mapping[n.id]=fresh; n.id=fresh; }
      for(auto link:std::vector<EnvelopeLink>(next.envelopeLinks)) if(link.target.kind==EnvelopeTargetKind::Graph&&mapping.contains(link.target.owner)) { link.target.owner=mapping.at(link.target.owner); next.envelopeLinks.push_back(link); }
      for(auto &e:copy.audio) { e.source=mapping.at(e.source); e.target=mapping.at(e.target); }
      for(auto &e:copy.modulation) { e.source=mapping.at(e.source); e.target=mapping.at(e.target); }
      graph.library.push_back(std::move(copy));
    } else if(method=="graph.update") {
      keys(p,{"definition","dryRun"}); auto replacement=field(p,"definition"); strictDefinition(replacement);
      const auto &previous=definition(next,field(replacement,"id")); affected=previous.id;
      patchModel(next,[&](Json &m) {
        const auto previousJSON=encodedDefinition(m,affected);
        for(auto &n:replacement["nodes"]) {
          const auto nid=identity(field(n,"id")); const auto it=std::find_if(previousJSON["nodes"].begin(),previousJSON["nodes"].end(),[&](const auto &v){return v["id"]==id(nid)&&v["kind"]==field(n,"kind");});
          require(it!=previousJSON["nodes"].end(),"Add nodes using graph.node.add; identities and kinds cannot be replaced");
          if(n.contains("plugin")&&!n["plugin"].contains("state")) n["plugin"]["state"]=it->at("plugin").at("state");
        }
        encodedDefinition(m,affected)=replacement;
      });
    } else if(method=="graph.remove") {
      keys(p,{"graph","dryRun"}); affected=definition(next,field(p,"graph")).id;
      require(std::none_of(graph.assignments.begin(),graph.assignments.end(),[&](const auto &a){return a.graph==affected;})&&std::none_of(graph.instrumentAssignments.begin(),graph.instrumentAssignments.end(),[&](const auto &a){return a.graph==affected;})&&std::none_of(graph.commands.begin(),graph.commands.end(),[&](const auto &c){return c.graph==affected;}),"Remove assignments and pattern commands before deleting this graph");
      std::erase_if(graph.library,[&](const auto &d){return d.id==affected;});
    } else if(method=="graph.node.add") {
      keys(p,{"graph","kind","name","x","y","slot","plugin","insertAfter","dryRun"});
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
      if(p.contains("insertAfter")) {
        require(n.kind==SignalNodeKind::Plugin,"Only effects can be inserted into audio chains"); const auto after=identity(p.at("insertAfter"));
        auto edge=std::find_if(d.audio.begin(),d.audio.end(),[&](const auto &e){return e.source==after&&e.output==0&&e.input==0;});
        require(edge!=d.audio.end()&&std::count_if(d.audio.begin(),d.audio.end(),[&](const auto &e){return e.source==after&&e.output==0;})==1,"Insert after a node with one main audio destination");
        edge->source=nodeID; d.audio.push_back({after,nodeID});
      }
      d.nodes.push_back(std::move(n));
      if(p.contains("plugin")) {
        auto recipe=p.at("plugin"); strictRecipe(recipe);
        patchModel(next,[&](Json &m){ encodedDefinition(m,affected)["nodes"].back()["plugin"]=recipe; });
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
      keys(p,{"positions","reset","dryRun"}); if(p.contains("reset")&&boolean(p.at("reset"))) graph.layout.clear();
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
      auto &d=definition(next,field(p,"graph")); affected=d.id;
      auto &n=node(d,field(p,"node")); nodeID=n.id; require(n.kind==SignalNodeKind::Automation,"Choose an automation source node");
      const auto index=uint16_t(integer(field(p,"pattern"),0,UINT16_MAX)); require(song.Patterns.IsValidPat(index),"Pattern does not exist");
      const auto pattern=next.patterns.at(index).id; auto points=field(p,"points"); strictPoints(points);
      const bool enabled=p.contains("enabled")?boolean(p.at("enabled")):true;
      auto previous=n.envelopes;
      patchModel(next,[&](Json &m){
        auto &nodes=encodedDefinition(m,affected)["nodes"];
        auto it=std::find_if(nodes.begin(),nodes.end(),[&](const auto &v){return v["id"]==id(nodeID);});
        auto &envelopes=it->at("envelopes");
        envelopes.erase(std::remove_if(envelopes.begin(),envelopes.end(),[&](const auto &e){return e["pattern"]==id(pattern);}),envelopes.end());
        if(!points.empty()) envelopes.push_back({{"pattern",id(pattern)},{"enabled",enabled},{"points",points}});
      });
      auto &envelopes=node(definition(next,id(affected)),id(nodeID)).envelopes;
      const auto lane=std::find_if(envelopes.begin(),envelopes.end(),[&](const auto &e){return e.pattern==pattern;});
      replaceSignalEnvelope(previous,lane==envelopes.end()?SignalPatternEnvelope{pattern}:*lane);
      envelopes=std::move(previous);
    } else if(method=="graph.node.remove") {
      keys(p,{"graph","node","dryRun"}); auto &d=definition(next,field(p,"graph")); affected=d.id;
      auto &n=node(d,field(p,"node")); nodeID=n.id;
      require(n.kind!=SignalNodeKind::Input&&n.kind!=SignalNodeKind::Output,"Input and Output nodes cannot be removed");
      std::erase_if(d.nodes,[&](const auto &v){return v.id==nodeID;});
      std::erase_if(d.audio,[&](const auto &e){return e.source==nodeID||e.target==nodeID;});
      std::erase_if(d.modulation,[&](const auto &e){return e.source==nodeID||e.target==nodeID;});
    }
    reconcileEnvelopeLinks(next,song); next.validate(song);
    if(host_.validateCandidate)host_.validateCandidate(next);
    const bool changed=next!=document_.native();
    if(changed&&!dry) {
      if((next.mixer!=document_.native().mixer||!sameSignalProcessing(next.signal,document_.native().signal))&&stopPlayback_) stopPlayback_();
      document_.annotate([&](NativeSong &n){n=next;});
    }
    return {{"dryRun",dry},{"wouldChange",changed},{"graph",id(affected)},{"node",id(nodeID)}};
  } catch(const std::invalid_argument &e) { throw Api::ApiError(-32602,e.what()); }
    catch(const std::out_of_range &e) { throw Api::ApiError(-32602,e.what()); }
    catch(const Json::exception &e) { throw Api::ApiError(-32602,e.what()); }
}
}
