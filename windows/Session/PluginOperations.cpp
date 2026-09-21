#include "PluginOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Plugins/WindowsVST3.hpp"
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#include "mpt/binary/base64.hpp"
#include "common/mptString.h"
#include "soundlib/ModInstrument.h"
#include <set>
#include <cmath>
#include <cstring>
namespace ScreamSeq {
namespace {
using namespace Tracker;
void need(bool ok,const char *why){if(!ok)throw Api::ApiError(-32602,why);}
void keys(const Json &p,std::initializer_list<const char *> allowed){need(p.is_object(),"Expected an object");for(auto i=p.begin();i!=p.end();++i)need(std::any_of(allowed.begin(),allowed.end(),[&](auto k){return i.key()==k;}),"Unknown plugin field");}
const Json &field(const Json &p,const char *key){need(p.contains(key),"Missing plugin field");return p.at(key);}
uint64_t integer(const Json &v,uint64_t lo,uint64_t hi){need(v.is_number_integer() && !v.is_boolean() && v>=lo && v<=hi,"Plugin integer outside range");return v.get<uint64_t>();}
double number(const Json &v,double lo,double hi){need(v.is_number()&&!v.is_boolean(),"Expected plugin number");auto n=v.get<double>();need(std::isfinite(n)&&n>=lo&&n<=hi,"Plugin value outside range");return n;}
bool flag(const Json &p,const char *key,bool fallback=false){if(!p.contains(key))return fallback;need(p.at(key).is_boolean(),"Expected plugin boolean");return p.at(key).get<bool>();}
std::string text(const Json &v,size_t max=8192){need(v.is_string(),"Expected plugin text");const auto &s=v.get_ref<const std::string &>();need(s.size()<=max*4&&s.find('\0')==std::string::npos,"Invalid plugin text");if(!s.empty()){auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);need(n>0&&size_t(n)<=max,"Invalid plugin UTF-8");}return s;}
Json blob(std::span<const std::byte> b){std::vector<uint8_t> v(b.size());if(!b.empty())std::memcpy(v.data(),b.data(),b.size());return Json::binary(std::move(v));}
std::string base64(std::span<const std::byte> b){return OpenMPT::mpt::ToCharset(OpenMPT::mpt::Charset::UTF8,::mpt::encode_base64(b));}
std::vector<std::byte> unbase64(const Json &v){need(v.is_string(),"Expected base64 plugin state");const auto &s=v.get_ref<const std::string &>();need(s.size()<=4*((16u*1024u*1024u+2)/3),"Plugin state exceeds 16 MiB");try{auto b=::mpt::decode_base64(OpenMPT::mpt::ToUnicode(OpenMPT::mpt::Charset::UTF8,s));need(b.size()<=16u*1024u*1024u&&base64(b)==s,"Invalid plugin base64");return b;}catch(const ::mpt::base64_parse_error &){throw Api::ApiError(-32602,"Invalid plugin base64");}}
std::string identity(){GUID id{};need(SUCCEEDED(CoCreateGuid(&id)),"Cannot allocate plugin identity");wchar_t b[40]{};StringFromGUID2(id,b,40);std::string s;for(auto c:std::wstring_view(b))s+=char(c);return s;}
Json descriptor(const PluginDescriptor &d){return {{"type",d.type},{"subtype",d.subtype},{"manufacturer",d.manufacturer},{"name",d.name},{"format",d.format},{"path",d.path},{"classID",d.classID},{"isInstrument",d.instrument}};}
PluginDescriptor descriptor(const Json &v){keys(v,{"type","subtype","manufacturer","name","format","path","classID","isInstrument"});PluginDescriptor d;
  d.type=uint32_t(integer(field(v,"type"),0,UINT32_MAX));d.subtype=uint32_t(integer(field(v,"subtype"),0,UINT32_MAX));d.manufacturer=uint32_t(integer(field(v,"manufacturer"),0,UINT32_MAX));
  d.name=text(v.value("name",Json("Plugin")),1024);d.format=text(v.value("format",Json("AU")),16);d.path=text(v.value("path",Json("")),32768);d.classID=text(v.value("classID",Json("")),128);d.instrument=flag(v,"isInstrument")||d.type==audioUnitMusicDeviceType;
  need(d.format=="AU"||d.format=="VST3"||d.format=="Built-in","Unknown plugin format");
  if(d.format=="VST3")need(WindowsVST3::validClassID(d.classID),"Invalid VST3 class identity");
  if(d.format=="Built-in"){auto all=NativePlugin::builtins();auto found=std::find_if(all.begin(),all.end(),[&](const auto &x){return x.classID==d.classID;});need(found!=all.end()&&!d.type&&!d.subtype&&!d.manufacturer&&!d.instrument&&d.path.empty(),"Invalid built-in descriptor");d=*found;}
  return d;
}
Json record(const PluginState &s){auto j=descriptor(s.descriptor);j["instanceID"]=s.instanceID;j["state"]=blob(s.state);j["instrument"]=s.instrument;j["bypass"]=s.bypass;j["instrumentAssignments"]=Json::array();for(auto a:pluginAssignments(s))j["instrumentAssignments"].push_back({{"instrument",a.instrument},{"channel",a.channel}});j["auxiliaryInputs"]=s.auxiliaryInputs;j["auxiliaryOutputs"]=s.auxiliaryOutputs;return j;}
Json parameters(const NativePlugin &plugin){Json j=Json::array();for(const auto &p:plugin.parameters())j.push_back({{"id",p.id},{"name",p.name},{"min",p.min},{"max",p.max},{"value",p.value},{"unit",p.unit},{"unitLabel",p.unitLabel},{"choices",p.choices},{"displayScale",p.logarithmic?"logarithmic":"linear"},{"step",p.step},{"canSlide",p.continuous},{"writable",p.writable}});return j;}
Json buses(const NativePlugin &plugin){Json j=Json::array();for(const auto &b:plugin.buses())j.push_back({{"index",b.index},{"direction",b.input?"input":"output"},{"name",b.name},{"channels",b.channels},{"active",b.active},{"supported",b.supported}});return j;}
size_t stateBytes(const Json &plugins,const Json &automation){size_t n=automation.size()*128;for(const auto &p:plugins)n+=4096+p.at("state").get_binary().size();return n;}
std::string hashText(const std::string &s){std::array<UCHAR,32> digest{};if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(s.data())),ULONG(s.size()),digest.data(),ULONG(digest.size()))<0)throw std::runtime_error("Cannot hash plugin program catalog");std::string out;for(auto b:digest){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;}
}
PluginOperations::PluginOperations(Tracker::Document &d,Project::ProjectState &p,std::function<void()> stop,
  std::function<void(std::span<const ParameterChange>)> liveParameters)
  :document_(d),project_(p),stop_(std::move(stop)),liveParameters_(std::move(liveParameters)){}
PluginOperations::~PluginOperations()=default;
std::vector<GraphRackRecord> PluginOperations::graphRack() const {
  std::vector<GraphRackRecord> result;const auto states=projectPluginStates(project_);
  for(size_t i=0;i<states.size();++i){const auto &s=states[i];GraphRackRecord item{descriptor(s.descriptor),s.instanceID,uint32_t(i),s.bypass};for(auto a:pluginAssignments(s))item.instruments.push_back(uint16_t(a.instrument));result.push_back(std::move(item));}
  return result;
}
GraphRackClone PluginOperations::cloneRackSlot(uint32_t index) {
  const auto states=projectPluginStates(project_);need(index<states.size(),"Plugin rack slot no longer exists");
  (void)editor(index); // Availability is real; never fabricate a missing recipe.
  const auto &s=states[index];const auto &d=s.descriptor;GraphRackClone result;
  result.recipe={d.format,d.name,d.path,d.classID,d.type,d.subtype,d.manufacturer,s.state,s.auxiliaryInputs,s.auxiliaryOutputs};
  result.instrument=d.instrument||d.type==audioUnitMusicDeviceType;for(auto a:pluginAssignments(s))result.instruments.push_back(uint16_t(a.instrument));return result;
}
std::vector<PluginAudioBus> PluginOperations::audioBuses(size_t index,bool required) {
  try{return editor(index).buses();}catch(const std::exception &){if(required)throw;return {};}
}
std::vector<std::string> PluginOperations::reads(){return {"plugin.discover","plugin.parameters.get","plugin.state.get","plugin.buses.get","plugin.instruments.get","plugin.programs.get","automation.target.get","graph.plugin.get"};}
std::vector<std::string> PluginOperations::writes(){return {"plugin.add","plugin.remove","plugin.move","plugin.bypass","plugin.assign","plugin.parameters.set","plugin.state.set","plugin.buses.set","plugin.instruments.set","instrument.plugin.set","plugin.programs.load","plugin.editor.open","plugin.editor.close","graph.plugin.set","graph.plugin.editor.open","graph.plugin.editor.commit","graph.plugin.editor.close"};}
#include "GraphPluginOperations.inc"
size_t PluginOperations::slot(const Json &p) const {
  const auto &rack=project_.preserved.at("plugins");
  if(p.contains("plugin")){auto id=text(p.at("plugin"),128);for(size_t i=0;i<rack.size();++i)if(rack[i].at("instanceID")==id)return i;throw Api::ApiError(-32602,"Plugin instance no longer exists");}
  need(!rack.empty(),"Plugin rack is empty");return size_t(integer(field(p,"slot"),0,rack.size()-1));
}
PluginOperations::History PluginOperations::snapshot() const {auto &p=project_.preserved;return {p.at("plugins"),p.at("automation"),stateBytes(p.at("plugins"),p.at("automation"))};}
void PluginOperations::commit(Json plugins,Json automation,bool keepEditors,bool parameterOnly,std::span<const ParameterChange> changes) {
  if(plugins==project_.preserved.at("plugins")&&automation==project_.preserved.at("automation"))return;
  auto candidate=project_;candidate.preserved["plugins"]=plugins;candidate.preserved["automation"]=automation;
  validatePluginCapacity(projectPluginStates(candidate),document_.native().mixer.buses.size());(void)projectAbsoluteAutomation(candidate);
  auto before=snapshot();need(before.bytes<=128u*1024u*1024u,"Plugin Undo state exceeds 128 MiB");
  undo_.push_back(std::move(before)); // Allocate history before stopping or publishing.
  try {if(parameterOnly && liveParameters_) {if(!changes.empty())liveParameters_(changes);}else if(stop_)stop_();}catch(...){undo_.pop_back();throw;}
  if(!keepEditors){editors_.clear();openEditors_.clear();pendingParameters_.clear();}
  project_.preserved["plugins"].swap(plugins);project_.preserved["automation"].swap(automation);
  ++project_.pluginRevision;Project::invalidateRecoveryTake(project_);redo_.clear();
  size_t bytes=0;for(const auto &h:undo_)bytes+=h.bytes;
  while(undo_.size()>128||bytes>128u*1024u*1024u){bytes-=undo_.front().bytes;undo_.pop_front();}
}
Tracker::NativePlugin &PluginOperations::editor(size_t index) {
  auto state=projectPluginStates(project_).at(index);auto &p=editors_[state.instanceID];
  if(!p)p=std::make_unique<NativePlugin>(state,48000);return *p;
}
bool PluginOperations::flushEditors(bool force) {
  const bool graphClosed=graphEditorWindowOpen_&&graphEditor_&&!graphEditor_->editorOpen();
  if(graphClosed)graphEditorWindowOpen_=false;
  if(openEditors_.empty())return graphClosed;
  const auto &rack=project_.preserved.at("plugins");
  const auto now=std::chrono::steady_clock::now();
  std::vector<ParameterChange> liveChanges;
  for(size_t slot=0;slot<rack.size();++slot){const auto &p=rack[slot];const auto &key=p.at("instanceID").get_ref<const std::string &>();auto found=editors_.find(key);if(found==editors_.end()||!openEditors_.contains(key))continue;
    std::map<uint32_t,float> edits;
    uint32_t id=0;float value=0;while(found->second->popEdit(id,value)){edits[id]=value;pendingParameters_[key][id]=value;lastTouched_={{"plugin",p.at("instanceID")},{"parameter",id},{"source","editor"}};++touchSequence_;}
    for(auto [parameter,v]:edits)liveChanges.push_back({uint32_t(slot),parameter,v,0});
    if(!edits.empty())lastEditorChange_=now;
    if(!found->second->editorOpen())force=true;
  }
  if(!liveChanges.empty() && liveParameters_)liveParameters_(liveChanges);
  // Fast gesture delivery does not serialize vendor state or copy the rack.
  // Capture after the gesture settles, or immediately for save/close/read.
  // Periodic idle captures also retain opaque preset/IR changes without edits.
  if(!force && (now-lastEditorChange_<std::chrono::milliseconds(400) || now-lastStateCapture_<std::chrono::milliseconds(400)))return graphClosed;
  lastStateCapture_=now;Json next;bool changed=false;std::vector<std::string> closed;
  for(size_t slot=0;slot<rack.size();++slot){const auto &p=rack[slot];const auto &key=p.at("instanceID").get_ref<const std::string &>();auto found=editors_.find(key);if(found==editors_.end()||!openEditors_.contains(key))continue;
    const auto state=blob(found->second->state().state);
    if(state!=p.at("state")){if(!changed)next=rack;next[slot]["state"]=state;changed=true;}
    if(!found->second->editorOpen())closed.push_back(key);
  }
  if(changed){
    // Presets and IR loads can emit parameter edits AND change opaque state.
    // Only keep playback running when replaying the gesture on the saved
    // baseline reproduces the complete editor state. This work is never DSP.
    bool parameterOnly=bool(liveParameters_);const auto baseline=projectPluginStates(project_);
    for(size_t i=0;parameterOnly && i<next.size();++i)if(next[i].at("state")!=project_.preserved.at("plugins")[i].at("state")){
      auto edits=pendingParameters_.find(baseline[i].instanceID);
      if(edits==pendingParameters_.end()){parameterOnly=false;break;}
      try {NativePlugin probe(baseline[i],48000);for(auto [id,value]:edits->second)if(!probe.parameter(id,value)){parameterOnly=false;break;}
        if(parameterOnly)parameterOnly=blob(probe.state().state)==next[i].at("state");
      }catch(const std::exception &){parameterOnly=false;}
    }
    commit(std::move(next),project_.preserved.at("automation"),true,parameterOnly);
  }
  for(const auto &key:closed)openEditors_.erase(key);
  pendingParameters_.clear();return changed||graphClosed;
}
Json PluginOperations::invoke(const std::string &method,const Json &p) {
  auto rack=project_.preserved.at("plugins"),automation=project_.preserved.at("automation");
  Json touch=nullptr;std::vector<ParameterChange> liveChanges;
  const bool dry=flag(p,"dryRun");
  if(method=="plugin.discover") {
    keys(p,{"format","rescan"});const auto format=text(p.value("format",Json("")),16);need(format.empty()||format=="AU"||format=="VST3"||format=="Built-in","Unknown plugin format");
    if(flag(p,"rescan")&&format!="Built-in"&&format!="AU") {
      std::string failures;size_t count=0;
      for(const auto &path:WindowsVST3::candidates(WindowsVST3::defaultSearchRoots()))try{WindowsVST3::rescan(path);}catch(const std::exception &e){if(++count<=5)failures+="\n"+path+": "+e.what();}
      if(count)throw Api::ApiError(-32003,"Scan completed with "+std::to_string(count)+" failed modules; successfully scanned plugins remain in the library."+failures);
    }
    auto all=NativePlugin::builtins();if(format!="Built-in"&&format!="AU"){auto native=NativePlugin::discover();all.insert(all.end(),native.begin(),native.end());}
    Json result=Json::array();for(const auto &d:all)if(format.empty()||format==d.format)result.push_back(descriptor(d));return result;
  }
  if(method=="automation.target.get") {keys(p,{});auto target=lastTouched_;if(!target.is_null()){auto found=std::find_if(rack.begin(),rack.end(),[&](const auto &x){return x.at("instanceID")==target.at("plugin");});target["available"]=found!=rack.end();target["slot"]=found==rack.end()?Json(nullptr):Json(found-rack.begin());if(found==rack.end())target["reason"]="Plugin was removed";else target["pluginName"]=found->at("name");}return {{"token","touch:"+std::to_string(touchSequence_)},{"target",target}};}
  if(method=="history.undo"||method=="history.redo") {
    keys(p,{"domain"});need(field(p,"domain")=="plugins","Wrong plugin history domain");auto &from=method=="history.undo"?undo_:redo_;auto &to=method=="history.undo"?redo_:undo_;
    if(from.empty())return Json::object();auto candidate=project_;candidate.preserved["plugins"]=from.back().plugins;candidate.preserved["automation"]=from.back().automation;
    validatePluginCapacity(projectPluginStates(candidate),document_.native().mixer.buses.size());auto before=snapshot();to.push_back(std::move(before));
    try{if(stop_)stop_();}catch(...){to.pop_back();throw;}editors_.clear();openEditors_.clear();pendingParameters_.clear();project_.preserved["plugins"].swap(from.back().plugins);project_.preserved["automation"].swap(from.back().automation);from.pop_back();++project_.pluginRevision;Project::invalidateRecoveryTake(project_);return Json::object();
  }
  if(method=="plugin.add") {
    keys(p,{"descriptor","dryRun"});need(rack.size()<maximumNativePlugins,"Plugin rack is full");PluginState state{descriptor(field(p,"descriptor"))};state.instanceID=identity();
    // Construct a disposable candidate before any song/history/transport change.
    NativePlugin probe(state,48000);state.state=probe.state().state;
    rack.push_back(record(state));if(!dry)commit(std::move(rack),std::move(automation));return {{"slot",project_.preserved.at("plugins").size()-(dry?0:1)},{"dryRun",dry}};
  }
  if(method=="instrument.plugin.set") {
    keys(p,{"instrument","plugin","channel","dryRun"});auto instrument=uint32_t(integer(field(p,"instrument"),1,document_.song().GetNumInstruments()));need(document_.song().Instruments[instrument],"Instrument does not exist");auto id=text(field(p,"plugin"),128);auto channel=uint32_t(integer(p.value("channel",Json(1)),1,16));
    auto states=projectPluginStates(project_);bool found=id.empty();
    bool changed=false;
    try{for(size_t i=0;i<states.size();++i){auto &s=states[i];const auto before=pluginAssignments(s);
      if(s.instanceID==id){need(s.descriptor.instrument||s.descriptor.type==audioUnitMusicDeviceType,"Choose an instrument plugin");auto a=pluginAssignments(s);auto existing=std::find_if(a.begin(),a.end(),[&](const auto &v){return v.instrument==instrument;});if(existing==a.end())a.push_back({instrument,channel});else existing->channel=channel;setPluginAssignments(s,a);found=true;}
      else removePluginAssignment(s,instrument);
      if(before!=pluginAssignments(s)){changed=true;rack[i]["instrument"]=s.instrument;rack[i]["instrumentAssignments"]=record(s).at("instrumentAssignments");}
    }
    need(found,"Plugin instance no longer exists");validatePluginCapacity(states,document_.native().mixer.buses.size());
    }catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
    if(changed&&!dry)commit(std::move(rack),std::move(automation));return {{"instrument",instrument},{"plugin",id},{"channel",channel},{"wouldChange",changed},{"dryRun",dry}};
  }
  const auto index=slot(p);auto states=projectPluginStates(project_);const auto &state=states.at(index);
  if(method=="plugin.parameters.get"){keys(p,{"slot"});return parameters(editor(index));}
  if(method=="plugin.state.get"){keys(p,{"slot"});return {{"descriptor",descriptor(state.descriptor)},{"data",base64(state.state)},{"kind","saved-baseline"}};}
  if(method=="plugin.buses.get"){keys(p,{"slot"});return {{"plugin",state.instanceID},{"buses",buses(editor(index))}};}
  if(method=="plugin.editor.open"||method=="plugin.editor.close") {
    keys(p,{"slot"});auto &plugin=editor(index);if(method=="plugin.editor.open"){plugin.showEditor();openEditors_.insert(state.instanceID);}else {flushEditors(true);plugin.closeEditor();openEditors_.erase(state.instanceID);}return {{"open",plugin.editorOpen()},{"plugin",state.instanceID}};
  }
  if(method=="plugin.instruments.get"||method=="plugin.instruments.set"||method=="plugin.assign") {
    const bool get=method=="plugin.instruments.get",assign=method=="plugin.assign";
    if(get)keys(p,{"plugin"});else if(assign)keys(p,{"slot","instrument","dryRun"});else keys(p,{"plugin","assignments","dryRun"});
    auto &next=states[index];bool changed=false;
    // Capture only the tiny routing lists, not another copy of every vendor's
    // opaque saved state, to compare the proposed assignments with the baseline.
    std::vector<std::vector<PluginInstrumentAlias>> previous;
    if(!get)for(const auto &s:states)previous.push_back(pluginAssignments(s));
    if(!get){try{
      need(next.descriptor.instrument||next.descriptor.type==audioUnitMusicDeviceType,"Select an instrument plugin");
      std::vector<PluginInstrumentAlias> a;
      if(assign){
        const auto instrument=uint32_t(integer(field(p,"instrument"),0,document_.song().GetNumInstruments()));
        if(instrument){
          const auto previous=pluginAssignments(next);const auto found=std::find_if(previous.begin(),previous.end(),[&](const auto &v){return v.instrument==instrument;});
          a.push_back({instrument,found==previous.end()?1:found->channel});
          for(auto alias:next.aliases)if(alias.instrument!=instrument)a.push_back(alias);
          for(size_t i=0;i<states.size();++i)if(i!=index)removePluginAssignment(states[i],instrument);
        }
      }else{
        const auto &v=field(p,"assignments");need(v.is_array()&&v.size()<=255,"Invalid instrument assignments");
        for(const auto &entry:v){keys(entry,{"instrument","channel"});a.push_back({uint32_t(integer(field(entry,"instrument"),1,document_.song().GetNumInstruments())),uint32_t(integer(field(entry,"channel"),1,16))});}
      }
      for(auto v:a)need(document_.song().Instruments[v.instrument],"Assigned instrument does not exist");
      setPluginAssignments(next,a);validatePluginCapacity(states,document_.native().mixer.buses.size());
      for(size_t i=0;i<states.size();++i)if(previous[i]!=pluginAssignments(states[i])){changed=true;rack[i]["instrument"]=states[i].instrument;rack[i]["instrumentAssignments"]=record(states[i]).at("instrumentAssignments");}
      }catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
      if(changed&&!dry)commit(rack,automation);
    }
    Json assignments=Json::array(),instruments=Json::array();for(auto a:pluginAssignments(next))assignments.push_back({{"instrument",a.instrument},{"channel",a.channel},{"available",a.instrument<=document_.song().GetNumInstruments()&&bool(document_.song().Instruments[a.instrument])}});
    for(unsigned i=1;i<=document_.song().GetNumInstruments();++i)if(document_.song().Instruments[i]){std::string owner;for(const auto &s:states)for(auto a:pluginAssignments(s))if(a.instrument==i)owner=s.instanceID;instruments.push_back({{"instrument",i},{"name",OpenMPT::mpt::ToCharset(OpenMPT::mpt::Charset::UTF8,document_.song().GetCharsetInternal(),document_.song().GetInstrumentName(i))},{"owner",owner}});}
    Json routing={{"plugin",state.instanceID},{"name",state.descriptor.name},{"isInstrument",state.descriptor.instrument||state.descriptor.type==audioUnitMusicDeviceType},{"assignments",assignments},{"instruments",instruments}};
    if(get)return routing;if(assign)return Json::object();return {{"wouldChange",changed},{"dryRun",dry},{"routing",routing}};
  }
  if(method=="plugin.remove") {keys(p,{"slot","dryRun"});rack.erase(rack.begin()+index);Json remaining=Json::array();for(auto point:automation){auto s=point.at(0).get<size_t>();if(s==index)continue;if(s>index)point[0]=s-1;remaining.push_back(std::move(point));}automation=std::move(remaining);}
  else if(method=="plugin.move") {keys(p,{"slot","direction","dryRun"});const auto direction=number(field(p,"direction"),-1,1);need(direction==-1||direction==1,"Direction must be -1 or 1");const auto target=int(index)+int(direction);if(target<0||target>=rack.size())return Json::object();std::swap(rack[index],rack[target]);for(auto &point:automation){if(point[0]==index)point[0]=target;else if(point[0]==target)point[0]=index;}}
  else if(method=="plugin.bypass") {keys(p,{"slot","bypass","dryRun"});need(p.contains("bypass"),"bypass is required");rack[index]["bypass"]=flag(p,"bypass");}
  else if(method=="plugin.state.set"||method=="plugin.parameters.set") {
    auto next=state;
    if(method=="plugin.state.set"){keys(p,{"slot","data","dryRun"});next.state=unbase64(field(p,"data"));}
    else keys(p,{"slot","values","dryRun"});
    NativePlugin probe(next,48000);
    if(method=="plugin.parameters.set") {auto available=probe.parameters();const auto &values=field(p,"values");need(values.is_array()&&!values.empty()&&values.size()<=4096,"Invalid parameter batch");std::set<uint32_t> seen;std::vector<std::pair<uint32_t,float>> prepared;
      for(const auto &v:values){keys(v,{"id","value"});const auto id=uint32_t(integer(field(v,"id"),0,UINT32_MAX));need(seen.insert(id).second,"Duplicate plugin parameter");auto found=std::find_if(available.begin(),available.end(),[&](const auto &x){return x.id==id;});need(found!=available.end()&&found->writable,"Plugin parameter is not writable");prepared.emplace_back(id,float(number(field(v,"value"),found->min,found->max)));}
      for(auto [id,value]:prepared){need(probe.parameter(id,value),"Plugin rejected parameter");liveChanges.push_back({uint32_t(index),id,value,0});}if(!dry)touch={{"plugin",state.instanceID},{"parameter",prepared.back().first},{"source","api"}};
    }
    rack[index]["state"]=blob(probe.state().state);
  } else if(method=="plugin.buses.set") {
    keys(p,{"slot","inputs","outputs","dryRun"});need(p.contains("inputs")||p.contains("outputs"),"Specify auxiliary ports");auto available=editor(index).buses();
    for(const auto *direction:{"inputs","outputs"})if(p.contains(direction)){const bool input=std::string(direction)=="inputs";const auto &values=p.at(direction);need(values.is_array()&&values.size()<=63,"Invalid auxiliary ports");std::set<uint32_t> ports;for(const auto &v:values){auto i=uint32_t(integer(v,1,63));need(ports.insert(i).second,"Duplicate auxiliary port");need(std::any_of(available.begin(),available.end(),[&](const auto &b){return b.input==input&&b.index==i&&b.supported;}),"Unsupported auxiliary port");}rack[index][input?"auxiliaryInputs":"auxiliaryOutputs"]=ports;}
    if(!dry && rack!=project_.preserved.at("plugins")){auto candidate=project_;candidate.preserved["plugins"]=rack;NativePlugin probe(projectPluginStates(candidate).at(index),48000);}
  } else if(method=="plugin.programs.get"||method=="plugin.programs.load") {
    const bool load=method=="plugin.programs.load";if(load)keys(p,{"plugin","program","expectedCatalogRevision","dryRun"});else keys(p,{"plugin"});
    const auto programs=[](const NativePlugin &plugin){Json entries=Json::array();for(const auto &program:plugin.programs())entries.push_back({{"id",program.id},{"name",program.name},{"group",program.group},{"loadable",program.loadable}});return entries;};
    const auto entries=programs(editor(index));
    const auto token="programs:"+hashText(entries.dump());
    if(!load)return {{"plugin",state.instanceID},{"name",state.descriptor.name},{"catalogRevision",token},{"programs",entries}};
    if(text(field(p,"expectedCatalogRevision"),80)!=token)throw Api::ApiError(-32001,"Program catalog changed");const auto id=text(field(p,"program"),128);
    const auto selected=std::find_if(entries.begin(),entries.end(),[&](const auto &x){return x.at("id")==id&&x.at("loadable")==true;});need(selected!=entries.end(),"Program is not loadable");
    const Json result={{"plugin",state.instanceID},{"program",*selected},{"catalogRevision",token},{"validated",true},{"loaded",!dry},{"dryRun",dry}};
    if(dry)return result; // Catalog validation never loads a vendor program.
    NativePlugin probe(state,48000);if("programs:"+hashText(programs(probe).dump())!=token)throw Api::ApiError(-32001,"Saved plugin state exposes a different program catalog");
    probe.loadProgram(id);rack[index]["state"]=blob(probe.state().state);commit(std::move(rack),std::move(automation));return result;
  } else throw Api::ApiError(-32601,"Unknown plugin operation");
  const bool changed=rack!=project_.preserved.at("plugins")||automation!=project_.preserved.at("automation");
  if(!dry){commit(std::move(rack),std::move(automation),false,method=="plugin.parameters.set",liveChanges);if(!touch.is_null()){lastTouched_=std::move(touch);++touchSequence_;}}return {{"wouldChange",changed},{"dryRun",dry}};
}
}
