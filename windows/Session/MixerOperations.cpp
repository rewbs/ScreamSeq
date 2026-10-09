#include "MixerOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/NativeMetadata.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/SignalGroupBypass.hpp"
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
std::string text(const Json &v,size_t maximum){return Project::validatedNativeText(v,maximum);}
std::string id(uint64_t value){return value?"n"+std::to_string(value):"";}
uint64_t identity(const Json &v){const auto s=text(v,32);need(s.size()>1&&s[0]=='n'&&s[1]!='0',"Invalid native identity");uint64_t n=0;for(size_t i=1;i<s.size();++i){need(s[i]>='0'&&s[i]<='9'&&n<NativeSong::maximumID/10,"Invalid native identity");n=n*10+s[i]-'0';}need(n>0&&n<NativeSong::maximumID,"Invalid native identity");return n;}
uint64_t allocate(NativeSong &n){need(n.nextID>0&&n.nextID<NativeSong::maximumID,"Native identity limit reached");return n.makeEntity().id;}
Json meterObjects(const MixerGraph &graph,const PlaybackFeedback &feedback){auto meters=Json::array();for(size_t i=0;i<std::min(graph.buses.size(),feedback.meters.size());++i)meters.push_back({{"bus",id(graph.buses[i].id)},{"left",feedback.meters[i].left},{"right",feedback.meters[i].right}});return meters;}
}
MixerOperations::MixerOperations(Tracker::Document &d,std::function<void()> stop,MixerHostHooks hooks):document_(d),stop_(std::move(stop)),host_(std::move(hooks)){}
std::vector<std::string> MixerOperations::reads(){return {"mixer.get","mixer.meters"};}
std::vector<std::string> MixerOperations::writes(){return {"mixer.enable","mixer.bus.add","mixer.bus.set","mixer.bus.remove","mixer.inserts.move","mixer.inserts.detach","mixer.sends.set","mixer.sidechains.set","mixer.instrument.route","mixer.plugin.route","mixer.plugin.connection.set"};}
Json MixerOperations::invoke(const std::string &method,const Json &p) {
  using namespace Tracker;
  try {
    const auto feedback=host_.feedback?host_.feedback():PlaybackFeedback{};
    const auto &native=document_.native();const auto &original=native.mixer;
    std::map<size_t,std::vector<PluginAudioBus>> ports;
    const auto buses=[&](size_t slot,bool required=false)->const std::vector<PluginAudioBus> &{if(!ports.contains(slot)){need(bool(host_.buses),"Mixer needs a real plugin bus catalog");ports[slot]=host_.buses(slot,required);}return ports.at(slot);};
    if(method=="mixer.meters"){keys(p,{});return {{"meters",meterObjects(original,feedback)},{"playing",feedback.playing}};}
    if(method=="mixer.get") {
      keys(p,{"includeImplicit"});const bool implicit=flag(p,"includeImplicit")&&!original.active();
      NativeSong projection;if(implicit){projection.tracks=native.tracks;projection.nextID=native.nextID;projection.masterID=native.masterID;projection.mixer=native.mixer;projection.ensureMixer();}
      auto result=Project::encodeMixerMetadata(implicit?projection.mixer:original);result["active"]=original.active();result["implicit"]=implicit;result["meters"]=meterObjects(original,feedback);result["sampleRate"]=feedback.sampleRate;result["playing"]=feedback.playing;result["latencySeconds"]=feedback.playing?feedback.latency:0;
      auto &available=result["plugins"]=Json::array();
      for(size_t i=0;i<host_.plugins.size();++i){const auto &plugin=host_.plugins[i];Json audio=Json::array();unsigned count=0;for(const auto &b:buses(i)){if(!b.input)++count;audio.push_back({{"index",b.index},{"direction",b.input?"input":"output"},{"name",b.name},{"channels",b.channels},{"active",b.active},{"supported",b.supported},{"physicalBus",b.physicalChannels?b.physicalBus:b.index},{"firstChannel",b.firstChannel},{"physicalChannels",b.physicalChannels?b.physicalChannels:b.channels}});}available.push_back({{"id",plugin.instanceID},{"slot",i},{"name",plugin.descriptor.name},{"instrument",plugin.descriptor.instrument||plugin.descriptor.type==audioUnitMusicDeviceType},{"bypass",plugin.bypass},{"outputBuses",count},{"audioBuses",audio}});}
      return result;
    }
    const auto supported=writes();need(std::find(supported.begin(),supported.end(),method)!=supported.end(),"Unknown mixer operation");need(document_.editable(),"This document is read-only");
    const bool preview=flag(p,"preview"),dry=flag(p,"dryRun");NativeSong next;if(preview){next.mixer=original;next.signal=native.signal;}else next=native;auto &graph=next.mixer;uint64_t affected=0;
    if(method!="mixer.enable"&&method!="mixer.inserts.detach"&&!preview)next.ensureMixer();
    auto findBus=[&](const Json &raw)->MixerBus &{const auto wanted=identity(raw);auto found=std::find_if(graph.buses.begin(),graph.buses.end(),[&](const auto &b){return b.id==wanted;});need(found!=graph.buses.end(),"Mixer bus does not exist");return *found;};
    auto master=[&]{auto found=std::find_if(graph.buses.begin(),graph.buses.end(),[](const auto &b){return b.kind==MixerBusKind::Master;});need(found!=graph.buses.end(),"Enable the mixer first");return found->id;};
    auto pluginSlot=[&](const Json &raw){const auto wanted=text(raw,128);auto found=std::find_if(host_.plugins.begin(),host_.plugins.end(),[&](const auto &v){return v.instanceID==wanted;});need(found!=host_.plugins.end(),"Plugin instance does not exist");return size_t(found-host_.plugins.begin());};
    auto isInstrument=[&](size_t slot){const auto &d=host_.plugins.at(slot).descriptor;return d.instrument||d.type==audioUnitMusicDeviceType;};
    auto plugin=[&](const Json &raw,bool instrument){auto slot=pluginSlot(raw);need(isInstrument(slot)==instrument,instrument?"Select an instrument plugin":"Insert chains require effect plugins");return host_.plugins[slot].instanceID;};
    if(method=="mixer.enable") {
      keys(p,{"enabled","dryRun"});if(flag(p,"enabled",true))next.ensureMixer();
      else if(!flag(p,"enabled",true)){need(graph.detachedChains.empty()&&graph.disconnectedMainInputs.empty()&&graph.pluginConnections.empty()&&next.signal.stageConnections.empty()&&std::none_of(next.signal.songSources.begin(),next.signal.songSources.end(),[](const auto &s){return s.audioStage!=0;})&&!graph.masterOutputDisconnected,"Reconnect cut cables and place detached chains before disabling routing");auto detached=std::move(graph.detached);graph={};graph.detached=std::move(detached);next.noteTracks.clear();next.signal.assignments.clear();next.signal.commands.clear();next.signal.lanes.clear();next.signal.inputs.clear();next.signal.outputs.clear();}
    } else if(method=="mixer.bus.add") {
      keys(p,{"kind","name","output","sendFrom","position","dryRun"});const auto kind=text(field(p,"kind"),16);need(kind=="group"||kind=="return","Add a group or a return; track buses follow song tracks");const auto output=p.contains("output")?identity(p.at("output")):master();affected=allocate(next);graph.buses.push_back({affected,output,kind=="group"?MixerBusKind::Group:MixerBusKind::Return,text(p.value("name",Json(kind=="group"?"Group":"Return")),256)});
      if(p.contains("sendFrom")){need(kind=="return","An automatic send requires a return bus");auto &source=findBus(p.at("sendFrom"));need(source.kind!=MixerBusKind::Master,"Master cannot send upstream");source.sends.push_back({affected,-96,false,false});}
      if(p.contains("position")){const auto &position=p.at("position");keys(position,{"x","y"});next.signal.layout[id(affected)]={number(field(position,"x"),0,100000),number(field(position,"y"),0,100000)};}
    } else if(method=="mixer.bus.set") {
      keys(p,{"bus","name","color","output","preGainDB","prePan","gainDB","pan","width","timingMS","mute","solo","inserts","mainOutputConnected","dryRun","preview"});auto &bus=findBus(field(p,"bus"));affected=bus.id;
      if(p.contains("mainOutputConnected")){need(bus.kind==MixerBusKind::Master,"Only Master has the final output cable");graph.masterOutputDisconnected=!flag(p,"mainOutputConnected");}
      if(p.contains("name"))bus.name=text(p.at("name"),256);if(p.contains("color"))bus.color=uint32_t(integer(p.at("color"),0,0xffffff));if(p.contains("output"))bus.output=p.at("output").is_null()?0:identity(p.at("output"));
      if(p.contains("preGainDB"))bus.preGainDB=number(p.at("preGainDB"),-96,24);if(p.contains("prePan"))bus.prePan=number(p.at("prePan"),-1,1);if(p.contains("gainDB"))bus.gainDB=number(p.at("gainDB"),-96,24);if(p.contains("pan"))bus.pan=number(p.at("pan"),-1,1);if(p.contains("width"))bus.width=number(p.at("width"),0,2);if(p.contains("timingMS"))bus.timingMS=number(p.at("timingMS"),-500,500);if(p.contains("mute"))bus.mute=flag(p,"mute");if(p.contains("solo"))bus.solo=flag(p,"solo");
      if(p.contains("inserts")){bus.inserts.clear();for(const auto &v:array(p.at("inserts"),32)){auto id=plugin(v,false);bus.inserts.push_back(id);std::erase(graph.detached,id);for(auto &chain:graph.detachedChains)std::erase(chain.plugins,id);std::erase(graph.disconnectedMainInputs,id);}std::erase_if(graph.detachedChains,[](const auto &c){return c.plugins.empty();});for(const auto &chain:graph.detachedChains)std::erase(graph.disconnectedMainInputs,chain.plugins.front());}
    } else if(method=="mixer.inserts.move"||method=="mixer.inserts.detach") {
      const bool detach=method=="mixer.inserts.detach";
      if(detach)keys(p,{"plugins","positions","dryRun"});else keys(p,{"plugins","target","before","positions","dryRun"});std::vector<std::string> rack,moving;
      for(size_t i=0;i<host_.plugins.size();++i)if(!isInstrument(i))rack.push_back(host_.plugins[i].instanceID);
      for(const auto &v:array(field(p,"plugins"),32))moving.push_back(plugin(v,false));
      need(!moving.empty(),"Select an effect chain");
      if(detach){if(std::find(graph.detached.begin(),graph.detached.end(),moving.front())==graph.detached.end())next.ensureMixer();
        const auto previousMixer=graph;const auto previousSignal=next.signal;
        detachMixerInserts(graph,rack,moving,[&]{return allocate(next);});
        preserveSongGroupDetachment(next.signal,previousSignal,previousMixer,graph,rack);}
      else {affected=identity(field(p,"target"));moveMixerInserts(graph,rack,moving,affected,p.contains("before")&&!p.at("before").is_null()?text(p.at("before"),128):"");}
      const auto positions=p.value("positions",Json::array());
      for(const auto &v:array(positions,64)){keys(v,{"node","x","y"});auto key=text(field(v,"node"),256);auto group=std::find_if(next.signal.groups.begin(),next.signal.groups.end(),[&](const auto &g){return key==id(g.id);});if(group!=next.signal.groups.end()){const auto members=songSignalGroupNodes(next.signal,group->id);need(std::any_of(members.begin(),members.end(),[](const auto &m){return m.starts_with("plugin:");})&&std::all_of(members.begin(),members.end(),[&](const auto &m){return !m.starts_with("plugin:")||std::find(moving.begin(),moving.end(),m.substr(7))!=moving.end();}),"Move every group processor together");moveSongSignalGroup(next.signal,group->id,number(field(v,"x"),0,100000),number(field(v,"y"),0,100000));}
        else{need(std::any_of(moving.begin(),moving.end(),[&](const auto &id){return key=="plugin:"+id;}),"Position must belong to a moved insert");next.signal.layout[key]={number(field(v,"x"),0,100000),number(field(v,"y"),0,100000)};}}
    } else if(method=="mixer.bus.remove") {
      keys(p,{"bus","dryRun"});const auto bus=findBus(field(p,"bus"));affected=bus.id;need(bus.kind==MixerBusKind::Group||bus.kind==MixerBusKind::Return,"Only groups and returns can be removed");
      std::erase_if(next.noteTracks,[&](const auto &v){return v.bus==affected;});std::erase_if(next.signal.assignments,[&](const auto &v){return v.target==affected;});std::erase_if(next.signal.commands,[&](const auto &v){return v.target==affected;});next.signal.lanes.erase(affected);
      std::erase_if(next.signal.stageConnections,[&](const auto &v){return v.source.stage==affected||v.target.stage==affected;});
      for(auto &source:next.signal.songSources)if(source.audioStage==affected){source.audioStage=0;source.output=0;source.preFader=false;}
      std::erase_if(next.signal.inputs,[&](const auto &v){return v.source==affected||v.target==affected;});std::erase_if(next.signal.outputs,[&](const auto &v){return v.source==affected||v.target==affected;});std::erase_if(graph.buses,[&](const auto &v){return v.id==affected;});
      for(auto &v:graph.buses){if(v.output==affected)v.output=bus.output;std::erase_if(v.sends,[&](const auto &s){return s.target==affected;});}for(auto &v:graph.instruments)if(v.target==affected)v.target=bus.output?bus.output:master();std::erase_if(graph.sidechains,[&](const auto &v){return v.source==affected;});
      auto &destination=findBus(id(bus.output?bus.output:master()));destination.inserts.insert(destination.inserts.begin(),bus.inserts.begin(),bus.inserts.end());
    } else if(method=="mixer.sends.set") {
      keys(p,{"bus","sends","dryRun"});auto &bus=findBus(field(p,"bus"));affected=bus.id;bus.sends.clear();for(const auto &v:array(field(p,"sends"),16)){keys(v,{"target","gainDB","preFader","enabled"});bus.sends.push_back({identity(field(v,"target")),number(v.value("gainDB",Json(-12)),-96,12),flag(v,"preFader"),flag(v,"enabled",true)});}
    } else if(method=="mixer.plugin.connection.set") {
      keys(p,{"source","output","target","input","gainDB","enabled","replace","dryRun"});
      auto endpoint=[&](const Json &v){return MixerPluginConnection{text(field(v,"source"),128),uint32_t(integer(field(v,"output"),0,63)),text(field(v,"target"),128),uint32_t(integer(field(v,"input"),0,63))};};
      auto value=endpoint(p);std::optional<MixerPluginConnection> old;if(p.contains("replace")){keys(p.at("replace"),{"source","output","target","input"});old=endpoint(p.at("replace"));}
      auto previous=std::find_if(graph.pluginConnections.begin(),graph.pluginConnections.end(),[&](const auto &r){const auto &key=old?*old:value;return r.source==key.source&&r.output==key.output&&r.target==key.target&&r.input==key.input;});
      if(previous!=graph.pluginConnections.end()){value.gainDB=previous->gainDB;value.enabled=previous->enabled;}
      if(p.contains("gainDB"))value.gainDB=number(p.at("gainDB"),-96,12);if(p.contains("enabled"))value.enabled=flag(p,"enabled");
      for(bool input:{false,true}){const auto &id=input?value.target:value.source;const auto slot=pluginSlot(id);const auto &available=buses(slot,true);const auto port=input?value.input:value.output;need(std::any_of(available.begin(),available.end(),[&](const auto &b){return b.input==input&&b.index==port&&b.supported;}),"Choose an available physical plugin audio port");if(!isInstrument(slot))rootDetachedMixerPlugin(graph,id,[&]{return allocate(next);});}
      setMixerPluginConnection(graph,value,old?&*old:nullptr);
        if(old){const SignalRouteIdentity before{"plugin-connection","plugin:"+old->source,"plugin:"+old->target,{},"post-gain",old->input,old->output},after{"plugin-connection","plugin:"+value.source,"plugin:"+value.target,{},"post-gain",value.input,value.output};for(auto &g:next.signal.groups)for(auto &r:g.dryRoutes){if(r.input==before)r.input=after;if(r.output==before)r.output=after;}}
      if(old)for(auto &path:next.signal.presentation.cables)if(!path.modulation&&path.source=="plugin:"+old->source&&path.target=="plugin:"+old->target&&path.output==old->output&&path.input==old->input){path.source="plugin:"+value.source;path.target="plugin:"+value.target;path.output=value.output;path.input=value.input;}
    } else if(method=="mixer.sidechains.set") {
      keys(p,{"plugin","input","sources","dryRun"});auto target=text(field(p,"plugin"),128);const auto input=uint32_t(integer(field(p,"input"),0,63));const auto &sources=array(field(p,"sources"),128);
      if(!sources.empty()){const auto slot=pluginSlot(p.at("plugin"));target=host_.plugins[slot].instanceID;if(!isInstrument(slot))rootDetachedMixerPlugin(graph,target,[&]{return allocate(next);});const auto &available=buses(pluginSlot(target),true);need(std::any_of(available.begin(),available.end(),[&](const auto &b){return b.input&&b.index==input&&b.supported;}),"Select an available plugin input; connected ports enable on playback");}
      std::erase_if(graph.sidechains,[&](const auto &s){return s.plugin==target&&s.input==input;});for(const auto &v:sources){keys(v,{"source","gainDB","preFader","enabled"});graph.sidechains.push_back({findBus(field(v,"source")).id,target,input,number(v.value("gainDB",Json(0)),-96,12),flag(v,"preFader"),flag(v,"enabled",true)});}
    } else if(method=="mixer.instrument.route"||method=="mixer.plugin.route") {
      keys(p,{"plugin","output","target","targets","disconnected","dryRun"});auto targetPlugin=text(field(p,"plugin"),128);const auto output=uint32_t(integer(p.value("output",Json(0)),0,63));need(p.contains("target")!=p.contains("targets"),"Supply target or targets, not both");need(!p.contains("targets")||!p.contains("disconnected"),"Use empty targets to disconnect");const auto raw=p.value("target",Json());const bool disconnected=flag(p,"disconnected"),remove=raw.is_null()&&!disconnected&&!p.contains("targets");need(!disconnected||raw.is_null(),"A disconnected output requires a null target");const auto target=remove||disconnected||p.contains("targets")?0:findBus(raw).id;affected=target;
      if(!remove){const auto slot=pluginSlot(targetPlugin);const bool instrument=isInstrument(slot);if(!instrument){rootDetachedMixerPlugin(graph,targetPlugin,[&]{return allocate(next);});const auto projected=projectMixerDetachedChains(graph);need(std::any_of(projected.buses.begin(),projected.buses.end(),[&](const auto &b){return std::find(b.inserts.begin(),b.inserts.end(),targetPlugin)!=b.inserts.end();}),"Effect has no processing owner");}const auto &available=buses(slot,true);need(std::any_of(available.begin(),available.end(),[&](const auto &b){return !b.input&&b.index==output&&b.supported;}),"Select an available plugin output; connected ports enable on playback");}
      std::vector<uint64_t> targets;
      if(p.contains("targets")&&!array(p.at("targets"),128).empty()){std::set<uint64_t> seen;for(const auto &v:array(p.at("targets"),128)){auto id=findBus(v).id;need(seen.insert(id).second,"Duplicate output destination");targets.push_back(id);}}
      else if(!remove)targets.push_back(target);
      // Target order has no musical meaning. Keep retained cables in place,
      // including unrelated outputs; append only genuinely new destinations.
      std::erase_if(graph.instruments,[&](const auto &v){return v.plugin==targetPlugin&&v.output==output&&std::find(targets.begin(),targets.end(),v.target)==targets.end();});
      for(auto destination:targets)if(std::none_of(graph.instruments.begin(),graph.instruments.end(),[&](const auto &v){return v.plugin==targetPlugin&&v.output==output&&v.target==destination;}))graph.instruments.push_back({targetPlugin,destination,output});
    }
    std::vector<uint64_t> tracks;for(const auto &[channel,t]:native.tracks)tracks.push_back(t.id);std::vector<MixerProcessorInfo> processors;
    for(size_t i=0;i<host_.plugins.size();++i){const auto &s=host_.plugins[i];uint32_t count=1;uint64_t outputs=0,inputs=0;
      for(const auto &b:buses(i))if(b.supported){need(b.index<64,"Invalid host audio port index");if(!b.input){count=std::max(count,b.index+1);outputs|=uint64_t(1)<<b.index;}else inputs|=uint64_t(1)<<b.index;}
      processors.push_back({s.instanceID,0,0,isInstrument(i),false,count,outputs,inputs,0,isInstrument(i)});
    }
    for(const auto &b:graph.buses){const auto in=signalStagePorts(next.signal,b.id,true),out=signalStagePorts(next.signal,b.id,false);
      if(in.empty()&&out.empty()&&std::none_of(next.signal.assignments.begin(),next.signal.assignments.end(),[&](const auto &a){return a.target==b.id;})&&std::none_of(next.signal.commands.begin(),next.signal.commands.end(),[&](const auto &c){return c.target==b.id&&(c.kind==SignalCommandKind::Row||c.kind==SignalCommandKind::Start);}))continue;
      uint64_t inputs=0,outputs=1;uint32_t count=1;for(auto p:in)inputs|=uint64_t(1)<<p;for(auto p:out){outputs|=uint64_t(1)<<p;count=std::max(count,p+1);}processors.push_back({signalBusIdentity(b.id),0,0,false,false,count,outputs,inputs});
    }
    validatePluginCapacity(host_.plugins,graph.buses.size());if(!preview)next.validate(document_.song());if(host_.validateCandidate)host_.validateCandidate(next);const auto plan=compileMixer(signalRoutingGraph(graph,next.signal),tracks,processors,feedback.sampleRate);
    auto structural=graph;if(structural.buses.size()==original.buses.size())for(size_t i=0;i<structural.buses.size();++i){auto &b=structural.buses[i];const auto &old=original.buses[i];b.preGainDB=old.preGainDB;b.prePan=old.prePan;b.gainDB=old.gainDB;b.pan=old.pan;b.width=old.width;b.mute=old.mute;b.solo=old.solo;b.name=old.name;b.color=old.color;}
    const bool controlsOnly=structural==original,different=graph!=original||(!preview&&next.signal.layout!=native.signal.layout);need(!preview||(controlsOnly&&graph.active()),"Only mixer gain, balance, width, mute and solo can be previewed live");
    Json result={{"mixer",Project::encodeMixerMetadata(graph)},{"wouldChange",different},{"preview",preview},{"bus",affected?Json(id(affected)):Json()},{"controlsOnly",controlsOnly}};
    if(!dry&&(different||preview||(method=="mixer.bus.set"&&graph.active()))) {
      std::vector<MixerControls> controls;const auto projected=projectMixerDetachedChains(graph);if(controlsOnly&&graph.active())for(size_t i=0;i<projected.buses.size();++i){const auto &b=projected.buses[i];controls.push_back({b.preGainDB,b.gainDB,b.pan,b.width,plan.nodes[i].audible,b.prePan});}
      const bool active=graph.active();
      const auto prepared=!controlsOnly&&different&&(host_.prepareNativeUpdate||host_.preparePublication)?(host_.prepareNativeUpdate?host_.prepareNativeUpdate(native,next):host_.preparePublication(next)):std::function<void()>{};
      auto publish=[&]{if(controlsOnly&&active){if(host_.controls&&!host_.controls(controls))throw Api::ApiError(-32002,"Mixer control queue is busy; retry the same revision");need(bool(host_.controls)||!feedback.playing,"Active mixer needs a real live control hook");}else if(prepared)prepared();else if(!controlsOnly&&!host_.prepareNativeUpdate&&!host_.preparePublication&&stop_)stop_();};
      if(preview||!different)publish();else document_.annotate([&](NativeSong &n){n=std::move(next);},publish);
    }
    return result;
  }catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}catch(const std::out_of_range &e){throw Api::ApiError(-32602,e.what());}catch(const Json::exception &e){throw Api::ApiError(-32602,e.what());}
}
}
