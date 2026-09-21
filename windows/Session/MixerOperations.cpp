#include "MixerOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/NativeMetadata.hpp"
#include "editor/TrackerDocument.hpp"
#include <cmath>
#include <set>
namespace ScreamSeq {
namespace {
using namespace Tracker;
void need(bool ok,const char *why){if(!ok)throw Api::ApiError(-32602,why);}
void keys(const Json &p,std::initializer_list<const char *> allowed){need(p.is_object(),"Expected mixer object");for(auto i=p.begin();i!=p.end();++i)need(std::any_of(allowed.begin(),allowed.end(),[&](auto k){return i.key()==k;}),"Unknown mixer field");}
const Json &field(const Json &p,const char *k){need(p.contains(k),"Missing mixer field");return p.at(k);}
double number(const Json &v,double lo,double hi){need(v.is_number()&&!v.is_boolean(),"Expected mixer number");auto n=v.get<double>();need(std::isfinite(n)&&n>=lo&&n<=hi,"Mixer number outside range");return n;}
uint64_t integer(const Json &v,uint64_t lo,uint64_t hi){auto n=number(v,double(lo),double(hi));need(std::floor(n)==n,"Expected mixer integer");return uint64_t(n);}
bool flag(const Json &p,const char *k,bool fallback=false){if(!p.contains(k))return fallback;need(p.at(k).is_boolean(),"Expected mixer boolean");return p.at(k).get<bool>();}
const Json &array(const Json &v,size_t maximum){need(v.is_array()&&v.size()<=maximum,"Invalid mixer array or capacity");return v;}
std::string text(const Json &v,size_t maximum){need(v.is_string(),"Expected mixer text");const auto s=v.get<std::string>();need(s.size()<=maximum*4&&s.find('\0')==std::string::npos,"Invalid mixer text");if(!s.empty()){auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);need(n>0&&size_t(n)<=maximum,"Invalid UTF-8 or oversized mixer text");}return s;}
std::string id(uint64_t value){return value?"n"+std::to_string(value):"";}
uint64_t identity(const Json &v){const auto s=text(v,32);need(s.size()>1&&s[0]=='n'&&s[1]!='0',"Invalid native identity");uint64_t n=0;for(size_t i=1;i<s.size();++i){need(s[i]>='0'&&s[i]<='9'&&n<NativeSong::maximumID/10,"Invalid native identity");n=n*10+s[i]-'0';}need(n>0&&n<NativeSong::maximumID,"Invalid native identity");return n;}
uint64_t allocate(NativeSong &n){need(n.nextID>0&&n.nextID<NativeSong::maximumID,"Native identity limit reached");return n.makeEntity().id;}
Json meterObjects(const MixerGraph &graph,const PlaybackFeedback &feedback){auto meters=Json::array();for(size_t i=0;i<std::min(graph.buses.size(),feedback.meters.size());++i)meters.push_back({{"bus",id(graph.buses[i].id)},{"left",feedback.meters[i].left},{"right",feedback.meters[i].right}});return meters;}
}
MixerOperations::MixerOperations(Tracker::Document &d,std::function<void()> stop,MixerHostHooks hooks):document_(d),stop_(std::move(stop)),host_(std::move(hooks)){}
std::vector<std::string> MixerOperations::reads(){return {"mixer.get","mixer.meters"};}
std::vector<std::string> MixerOperations::writes(){return {"mixer.enable","mixer.bus.add","mixer.bus.set","mixer.bus.remove","mixer.sends.set","mixer.sidechains.set","mixer.instrument.route","mixer.plugin.route"};}
Json MixerOperations::invoke(const std::string &method,const Json &p) {
  using namespace Tracker;
  try {
    const auto feedback=host_.feedback?host_.feedback():PlaybackFeedback{};
    const auto &native=document_.native();const auto &original=native.mixer;
    std::map<size_t,std::vector<PluginAudioBus>> ports;
    const auto buses=[&](size_t slot,bool required=false)->const std::vector<PluginAudioBus> &{if(!ports.contains(slot)){need(bool(host_.buses),"Mixer needs a real plugin bus catalog");ports[slot]=host_.buses(slot,required);}return ports.at(slot);};
    if(method=="mixer.meters"){keys(p,{});return {{"meters",meterObjects(original,feedback)},{"playing",feedback.playing}};}
    if(method=="mixer.get") {
      keys(p,{});auto result=Project::encodeMixerMetadata(original);result["active"]=original.active();result["meters"]=meterObjects(original,feedback);result["sampleRate"]=feedback.sampleRate;result["playing"]=feedback.playing;result["latencySeconds"]=feedback.playing?feedback.latency:0;
      auto &available=result["plugins"]=Json::array();
      for(size_t i=0;i<host_.plugins.size();++i){const auto &plugin=host_.plugins[i];Json audio=Json::array();unsigned count=0;for(const auto &b:buses(i)){if(!b.input)++count;audio.push_back({{"index",b.index},{"direction",b.input?"input":"output"},{"name",b.name},{"channels",b.channels},{"active",b.active},{"supported",b.supported}});}available.push_back({{"id",plugin.instanceID},{"slot",i},{"name",plugin.descriptor.name},{"instrument",plugin.descriptor.instrument||plugin.descriptor.type==audioUnitMusicDeviceType},{"bypass",plugin.bypass},{"outputBuses",count},{"audioBuses",audio}});}
      return result;
    }
    const auto supported=writes();need(std::find(supported.begin(),supported.end(),method)!=supported.end(),"Unknown mixer operation");need(document_.editable(),"This document is read-only");
    const bool preview=flag(p,"preview"),dry=flag(p,"dryRun");NativeSong next;if(preview)next.mixer=original;else next=native;auto &graph=next.mixer;uint64_t affected=0;
    auto findBus=[&](const Json &raw)->MixerBus &{const auto wanted=identity(raw);auto found=std::find_if(graph.buses.begin(),graph.buses.end(),[&](const auto &b){return b.id==wanted;});need(found!=graph.buses.end(),"Mixer bus does not exist");return *found;};
    auto master=[&]{auto found=std::find_if(graph.buses.begin(),graph.buses.end(),[](const auto &b){return b.kind==MixerBusKind::Master;});need(found!=graph.buses.end(),"Enable the mixer first");return found->id;};
    auto pluginSlot=[&](const Json &raw){const auto wanted=text(raw,128);auto found=std::find_if(host_.plugins.begin(),host_.plugins.end(),[&](const auto &v){return v.instanceID==wanted;});need(found!=host_.plugins.end(),"Plugin instance does not exist");return size_t(found-host_.plugins.begin());};
    auto isInstrument=[&](size_t slot){const auto &d=host_.plugins.at(slot).descriptor;return d.instrument||d.type==audioUnitMusicDeviceType;};
    auto plugin=[&](const Json &raw,bool instrument){auto slot=pluginSlot(raw);need(isInstrument(slot)==instrument,instrument?"Select an instrument plugin":"Insert chains require effect plugins");return host_.plugins[slot].instanceID;};
    if(method=="mixer.enable") {
      keys(p,{"enabled","dryRun"});if(flag(p,"enabled",true)&&!graph.active()){const auto output=allocate(next);for(const auto &[channel,t]:next.tracks)graph.buses.push_back({t.id,output,MixerBusKind::Track,t.name.empty()?"Track "+std::to_string(channel+1):t.name,t.color});graph.buses.push_back({output,0,MixerBusKind::Master,"Master"});}
      else if(!flag(p,"enabled",true)){graph={};next.noteTracks.clear();next.signal.assignments.clear();next.signal.commands.clear();next.signal.lanes.clear();next.signal.inputs.clear();next.signal.outputs.clear();}
    } else if(method=="mixer.bus.add") {
      keys(p,{"kind","name","output","dryRun"});const auto kind=text(field(p,"kind"),16);need(kind=="group"||kind=="return","Add a group or a return; track buses follow song tracks");const auto output=p.contains("output")?identity(p.at("output")):master();affected=allocate(next);graph.buses.push_back({affected,output,kind=="group"?MixerBusKind::Group:MixerBusKind::Return,text(p.value("name",Json(kind=="group"?"Group":"Return")),256)});
    } else if(method=="mixer.bus.set") {
      keys(p,{"bus","name","color","output","preGainDB","prePan","gainDB","pan","width","timingMS","mute","solo","inserts","dryRun","preview"});auto &bus=findBus(field(p,"bus"));affected=bus.id;
      if(p.contains("name"))bus.name=text(p.at("name"),256);if(p.contains("color"))bus.color=uint32_t(integer(p.at("color"),0,0xffffff));if(p.contains("output"))bus.output=p.at("output").is_null()?0:identity(p.at("output"));
      if(p.contains("preGainDB"))bus.preGainDB=number(p.at("preGainDB"),-96,24);if(p.contains("prePan"))bus.prePan=number(p.at("prePan"),-1,1);if(p.contains("gainDB"))bus.gainDB=number(p.at("gainDB"),-96,24);if(p.contains("pan"))bus.pan=number(p.at("pan"),-1,1);if(p.contains("width"))bus.width=number(p.at("width"),0,2);if(p.contains("timingMS"))bus.timingMS=number(p.at("timingMS"),-500,500);if(p.contains("mute"))bus.mute=flag(p,"mute");if(p.contains("solo"))bus.solo=flag(p,"solo");
      if(p.contains("inserts")){bus.inserts.clear();for(const auto &v:array(p.at("inserts"),32))bus.inserts.push_back(plugin(v,false));}
    } else if(method=="mixer.bus.remove") {
      keys(p,{"bus","dryRun"});const auto bus=findBus(field(p,"bus"));affected=bus.id;need(bus.kind==MixerBusKind::Group||bus.kind==MixerBusKind::Return,"Only groups and returns can be removed");
      std::erase_if(next.noteTracks,[&](const auto &v){return v.bus==affected;});std::erase_if(next.signal.assignments,[&](const auto &v){return v.target==affected;});std::erase_if(next.signal.commands,[&](const auto &v){return v.target==affected;});next.signal.lanes.erase(affected);
      std::erase_if(next.signal.inputs,[&](const auto &v){return v.source==affected||v.target==affected;});std::erase_if(next.signal.outputs,[&](const auto &v){return v.source==affected||v.target==affected;});std::erase_if(graph.buses,[&](const auto &v){return v.id==affected;});
      for(auto &v:graph.buses){if(v.output==affected)v.output=bus.output;std::erase_if(v.sends,[&](const auto &s){return s.target==affected;});}for(auto &v:graph.instruments)if(v.target==affected)v.target=bus.output?bus.output:master();std::erase_if(graph.sidechains,[&](const auto &v){return v.source==affected;});
      auto &destination=findBus(id(bus.output?bus.output:master()));destination.inserts.insert(destination.inserts.begin(),bus.inserts.begin(),bus.inserts.end());
    } else if(method=="mixer.sends.set") {
      keys(p,{"bus","sends","dryRun"});auto &bus=findBus(field(p,"bus"));affected=bus.id;bus.sends.clear();for(const auto &v:array(field(p,"sends"),16)){keys(v,{"target","gainDB","preFader","enabled"});bus.sends.push_back({identity(field(v,"target")),number(v.value("gainDB",Json(-12)),-96,12),flag(v,"preFader"),flag(v,"enabled",true)});}
    } else if(method=="mixer.sidechains.set") {
      keys(p,{"plugin","input","sources","dryRun"});auto target=text(field(p,"plugin"),128);const auto input=uint32_t(integer(field(p,"input"),1,63));const auto &sources=array(field(p,"sources"),128);
      if(!sources.empty()){target=plugin(p.at("plugin"),false);const auto &available=buses(pluginSlot(target),true);need(std::any_of(available.begin(),available.end(),[&](const auto &b){return b.input&&b.index==input&&b.active&&b.supported;}),"Enable an available auxiliary effect input before routing a sidechain");}
      std::erase_if(graph.sidechains,[&](const auto &s){return s.plugin==target&&s.input==input;});for(const auto &v:sources){keys(v,{"source","gainDB","preFader","enabled"});graph.sidechains.push_back({findBus(field(v,"source")).id,target,input,number(v.value("gainDB",Json(0)),-96,12),flag(v,"preFader"),flag(v,"enabled",true)});}
    } else if(method=="mixer.instrument.route"||method=="mixer.plugin.route") {
      keys(p,{"plugin","output","target","disconnected","dryRun"});auto targetPlugin=text(field(p,"plugin"),128);const auto output=uint32_t(integer(p.value("output",Json(0)),0,63));const auto &raw=field(p,"target");const bool disconnected=flag(p,"disconnected"),remove=raw.is_null()&&!disconnected;need(!disconnected||raw.is_null(),"A disconnected output requires a null target");const auto target=remove||disconnected?0:findBus(raw).id;affected=target;
      if(!remove){const auto slot=pluginSlot(targetPlugin);const bool instrument=isInstrument(slot);need(instrument||output>0,"Effect main output follows its insert chain; select an auxiliary output");if(!instrument)need(std::any_of(graph.buses.begin(),graph.buses.end(),[&](const auto &b){return std::find(b.inserts.begin(),b.inserts.end(),targetPlugin)!=b.inserts.end();}),"Assign the effect to a bus before routing its auxiliary output");const auto &available=buses(slot,true);need(std::any_of(available.begin(),available.end(),[&](const auto &b){return !b.input&&b.index==output&&b.active&&b.supported;}),"Enable an available plugin output before routing it");}
      std::erase_if(graph.instruments,[&](const auto &v){return v.plugin==targetPlugin&&v.output==output;});if(!remove)graph.instruments.push_back({targetPlugin,target,output});
    }
    std::vector<uint64_t> tracks;for(const auto &[channel,t]:native.tracks)tracks.push_back(t.id);std::vector<MixerProcessorInfo> processors;
    for(size_t i=0;i<host_.plugins.size();++i){const auto &s=host_.plugins[i];uint32_t count=1;uint64_t outputs=1,inputs=0;for(const auto &b:buses(i)){if(!b.input){count=std::max(count,b.index+1);if(b.active)outputs|=uint64_t(1)<<b.index;}else if(b.index&&b.active)inputs|=uint64_t(1)<<b.index;}processors.push_back({s.instanceID,0,0,isInstrument(i),s.bypass,count,outputs,inputs});}
    validatePluginCapacity(host_.plugins,graph.buses.size());if(!preview)next.validate(document_.song());const auto plan=compileMixer(graph,tracks,processors,feedback.sampleRate);
    auto structural=graph;if(structural.buses.size()==original.buses.size())for(size_t i=0;i<structural.buses.size();++i){auto &b=structural.buses[i];const auto &old=original.buses[i];b.preGainDB=old.preGainDB;b.prePan=old.prePan;b.gainDB=old.gainDB;b.pan=old.pan;b.width=old.width;b.mute=old.mute;b.solo=old.solo;b.name=old.name;b.color=old.color;}
    const bool controlsOnly=structural==original,different=graph!=original;need(!preview||(controlsOnly&&graph.active()),"Only mixer gain, balance, width, mute and solo can be previewed live");
    Json result={{"mixer",Project::encodeMixerMetadata(graph)},{"wouldChange",different},{"preview",preview},{"bus",affected?Json(id(affected)):Json()},{"controlsOnly",controlsOnly}};
    if(!dry&&(different||preview||(method=="mixer.bus.set"&&graph.active()))) {
      std::vector<MixerControls> controls;if(controlsOnly&&graph.active())for(size_t i=0;i<graph.buses.size();++i){const auto &b=graph.buses[i];controls.push_back({b.preGainDB,b.gainDB,b.pan,b.width,plan.nodes[i].audible,b.prePan});}
      const bool active=graph.active();
      auto publish=[&]{if(controlsOnly&&active){if(host_.controls&&!host_.controls(controls))throw Api::ApiError(-32002,"Mixer control queue is busy; retry the same revision");need(bool(host_.controls)||!feedback.playing,"Active mixer needs a real live control hook");}else if(stop_)stop_();};
      if(preview||!different)publish();else document_.annotate([&](NativeSong &n){n=std::move(next);},publish);
    }
    return result;
  }catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}catch(const std::out_of_range &e){throw Api::ApiError(-32602,e.what());}catch(const Json::exception &e){throw Api::ApiError(-32602,e.what());}
}
}
