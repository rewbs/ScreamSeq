#pragma once
#include "PatternActivity.hpp"
#include "PatternCommands.hpp"
#include "soundlib/ModInstrument.h"
#include "common/mptString.h"

namespace Tracker {
struct ActivityPlugin {std::string id,name;bool instrument=false,bypass=false;std::vector<uint16_t> assignments;};
struct ActivityLink {
  std::string category,kind,label,plugin,envelope;
  uint64_t graph=0,node=0;
  uint32_t slot=0,pattern=0,row=0,channel=0,column=0,parameter=0;
};
struct PatternActivityCatalog {PatternActivity playback;std::vector<ActivityLink> items;};
inline PatternActivityCatalog patternActivityCatalog(Document &doc,uint16_t pattern,uint32_t order,uint16_t row,uint16_t channel,const std::vector<ActivityPlugin> &plugins,PatternActivityBudget *sharedBudget=nullptr) {
  using namespace OpenMPT;
  PatternActivityBudget localBudget;auto &budget=sharedBudget?*sharedBudget:localBudget;
  PatternActivityCatalog result;
  try {
  budget.check();
  std::set<uint16_t> pluginInstruments;for(const auto &plugin:plugins)if(plugin.instrument)pluginInstruments.insert(plugin.assignments.begin(),plugin.assignments.end());
  for(const auto &[slot,instrument]:doc.native().instruments)for(const auto &route:doc.native().signal.noteRouting.routes)if(route.enabled&&(route.sourceKind==NoteSourceKind::Channel||route.source==instrument.id))pluginInstruments.insert(slot);
  result.playback=patternActivity(doc,pattern,order,row,channel,pluginInstruments,&budget);
  if(!result.playback.reachable||!result.playback.complete)return result;
  const auto &native=doc.native();const auto &song=doc.song();const auto pid=native.patterns.at(pattern).id,track=native.tracks.at(channel).id;
  auto add=[&](std::string category,std::string kind,std::string label)->ActivityLink &{result.items.push_back({std::move(category),std::move(kind),std::move(label)});auto &link=result.items.back();link.pattern=pattern;link.row=row;link.channel=channel;return link;};
  auto name=[&](const auto &value){return OpenMPT::mpt::ToCharset(OpenMPT::mpt::Charset::UTF8,song.GetCharsetInternal(),value);};
  for(auto sample:result.playback.samples)add("Sounds","sample","Sample "+std::to_string(sample)+" · "+name(song.GetSampleName(sample))).slot=sample;
  for(auto instrument:result.playback.instruments){
    add("Sounds","instrument","Instrument "+std::to_string(instrument)+" · "+name(song.GetInstrumentName(instrument))).slot=instrument;
    if(const auto *ins=song.Instruments[instrument])for(const auto &[key,env]:std::initializer_list<std::pair<const char *,const InstrumentEnvelope *>>{{"volume",&ins->VolEnv},{"pan",&ins->PanEnv},{"pitch",&ins->PitchEnv}})if(env->dwFlags[ENV_ENABLED]&&!env->empty()){
      auto &link=add("Envelopes","instrument","Instrument "+std::to_string(instrument)+" · "+key);link.slot=instrument;link.envelope=key;
    }
  }
  std::set<uint64_t> buses{track},graphs;std::set<std::string> routed;
  if(!native.mixer.active())buses.insert(native.masterID);
  const auto &notes=native.signal.noteRouting;
  const auto receivesNotes=[&](const ActivityPlugin &plugin){
    for(auto i:result.playback.instruments){const auto id=native.instruments.at(i).id;
      if(std::find(plugin.assignments.begin(),plugin.assignments.end(),i)!=plugin.assignments.end()&&std::find(notes.suppressedAssignments.begin(),notes.suppressedAssignments.end(),id)==notes.suppressedAssignments.end())return true;
      for(const auto &route:notes.routes)if(route.enabled&&route.plugin==plugin.id&&((route.sourceKind==NoteSourceKind::Channel&&route.source==track)||(route.sourceKind==NoteSourceKind::Instrument&&route.source==id)))return true;
    }return false;
  };
  for(const auto &plugin:plugins)if(plugin.instrument&&receivesNotes(plugin)){
    routed.insert(plugin.id);bool explicitOutput=false;
    for(const auto &output:native.mixer.instruments)if(output.plugin==plugin.id){explicitOutput=true;if(output.target)buses.insert(output.target);}
    if(!explicitOutput)buses.insert(native.masterID);
  }
  bool changed=true;while(changed){budget.check();const auto size=buses.size()+routed.size();
    for(const auto &bus:native.mixer.buses)if(buses.contains(bus.id)){
      routed.insert(bus.inserts.begin(),bus.inserts.end());if(bus.output)buses.insert(bus.output);
      for(const auto &send:bus.sends)if(send.enabled)buses.insert(send.target);
    }
    for(const auto &edge:native.mixer.pluginConnections)if(edge.enabled&&routed.contains(edge.source))routed.insert(edge.target);
    for(const auto &edge:native.mixer.instruments)if(edge.target&&routed.contains(edge.plugin))buses.insert(edge.target);
    for(const auto &edge:native.signal.outputs)if(edge.target&&buses.contains(edge.source))buses.insert(edge.target);
    for(const auto &edge:native.signal.inputs)if(buses.contains(edge.source))buses.insert(edge.target);
    for(const auto &edge:native.signal.stageConnections)if(edge.enabled&&((edge.source.stage&&buses.contains(edge.source.stage))||(!edge.source.plugin.empty()&&routed.contains(edge.source.plugin)))){if(edge.target.stage)buses.insert(edge.target.stage);if(!edge.target.plugin.empty())routed.insert(edge.target.plugin);}
    changed=size!=buses.size()+routed.size();
  }
  if(buses.contains(native.masterID))for(const auto &plugin:plugins)if(!plugin.instrument){
    bool owned=std::find(native.mixer.detached.begin(),native.mixer.detached.end(),plugin.id)!=native.mixer.detached.end();
    for(const auto &bus:native.mixer.buses)owned|=std::find(bus.inserts.begin(),bus.inserts.end(),plugin.id)!=bus.inserts.end();
    for(const auto &chain:native.mixer.detachedChains)owned|=std::find(chain.plugins.begin(),chain.plugins.end(),plugin.id)!=chain.plugins.end();
    if(!owned)routed.insert(plugin.id);
  }
  for(const auto &plugin:plugins)if(routed.contains(plugin.id))add("Plugins","plugin",plugin.name+(plugin.instrument?" · instrument":" · effect")+(plugin.bypass?" · bypassed":"")).plugin=plugin.id;
  for(const auto &assignment:native.signal.assignments)if(buses.contains(assignment.target))graphs.insert(assignment.graph);
  for(const auto &assignment:native.signal.instrumentAssignments)for(auto instrument:result.playback.instruments)if(native.instruments.at(instrument).id==assignment.target)graphs.insert(assignment.graph);
  // Replay graph Start/Stop/Clear in engine order, including prior patterns.
  std::map<uint64_t,std::set<uint64_t>> persistent;
  std::map<std::pair<int,int>,std::pair<unsigned,PatternCommand>> gates;
  std::map<int,std::pair<unsigned,PatternCommand>> continuing;
  using RowKey=std::pair<uint64_t,uint32_t>;
  std::map<RowKey,std::vector<const PatternCommand *>> commandsByRow;
  std::map<RowKey,std::vector<const SignalCommand *>> graphsByRow;
  for(const auto &c:native.performance.commands){budget.check();if(c.track==track||(c.kind==PatternCommandKind::Native&&(c.native==NativePatternOp::GainSet||c.native==NativePatternOp::GainSlide)&&c.arguments[1]==2))commandsByRow[{c.pattern,c.position/65536}].push_back(&c);}
  for(auto &[key,commands]:commandsByRow){budget.check();std::sort(commands.begin(),commands.end(),[](auto a,auto b){return std::tie(a->position,a->track,a->column)<std::tie(b->position,b->track,b->column);});}
  for(const auto &c:native.signal.commands){budget.check();if(buses.contains(c.target))graphsByRow[{c.pattern,c.position/65536}].push_back(&c);}
  GetLengthTarget walk{ORDERINDEX(order),ROWINDEX(row)};walk.onRow=[&](ORDERINDEX o,ROWINDEX r,double seconds){
    budget.visit(seconds);
    if(o>=song.Order().size())return;const auto p=song.Order()[o];if(!native.patterns.contains(p))return;
    const auto id=native.patterns.at(p).id;
    if(const auto found=commandsByRow.find({id,r});found!=commandsByRow.end())for(const auto *command:found->second){const auto &c=*command;
      if(c.kind==PatternCommandKind::Native)continuing[activityNativeFamily(c)]={p,c};
      if(c.native==NativePatternOp::Gate)gates[{int(c.arguments[0]),int(gateBinding(c))}]={p,c};
      else if(c.native==NativePatternOp::GateStop){if(c.arguments[0]==4)gates.clear();else gates.erase({int(c.arguments[0]),int(gateBinding(c))});}
    }
    if(const auto found=graphsByRow.find({id,r});found!=graphsByRow.end())for(const auto *entry:found->second){const auto &command=*entry;
      auto &active=persistent[command.target];
      if(command.kind==SignalCommandKind::Start)active.insert(command.graph);
      else if(command.kind==SignalCommandKind::Stop)active.erase(command.graph);
      else if(command.kind==SignalCommandKind::Clear)active.clear();
      else if(command.kind==SignalCommandKind::Row&&o==order&&r==row)graphs.insert(command.graph);
    }
  };doc.song().GetLength(eNoAdjust,walk);budget.check();
  for(const auto &[target,active]:persistent)graphs.insert(active.begin(),active.end());
  const auto inherited=[&](const auto &source,const std::string &label){if(source.first!=pattern||source.second.position/65536!=row){
    auto &link=add("Pattern FX","effect",label+" · active, inherited from pattern "+std::to_string(source.first)+" row "+std::to_string(source.second.position/65536));link.pattern=source.first;link.row=source.second.position/65536;link.column=source.second.column;
    for(const auto &[index,t]:native.tracks)if(t.id==source.second.track){link.channel=index;break;}
  }};
  for(const auto &[key,source]:gates)inherited(source,"GT · "+std::string(std::array{"volume","pitch","pan","parameter"}.at(size_t(key.first))));
  for(const auto &[family,source]:continuing)if(result.playback.nativeFamilies.contains(family)){
    const auto &info=nativePatternCommand(source.second.native);inherited(source,std::string(info.code)+" · "+std::string(info.name));
  }
  for(const auto &graph:native.signal.library)if(graphs.contains(graph.id)){budget.check();
    auto &g=add("Pattern FX","graph",graph.name+" · active graph");g.graph=graph.id;
    for(const auto &node:graph.nodes){
      if(node.kind==SignalNodeKind::Plugin){auto &link=add("Plugins","graphPlugin",node.plugin.name+" · "+graph.name+(node.plugin.bypass?" · bypassed":""));link.graph=graph.id;link.node=node.id;}
      for(const auto &lane:node.envelopes)if(!node.muted&&lane.enabled&&lane.pattern==pid&&!lane.points.empty()){auto &link=add("Envelopes","graph",graph.name+" · "+node.name);link.graph=graph.id;link.node=node.id;}
    }
  }
  for(const auto &lane:native.automation)if(lane.enabled&&lane.pattern==pid&&!lane.points.empty()){
    auto &link=add("Envelopes","automation","Parameter "+std::to_string(lane.parameter)+" · "+lane.plugin);link.plugin=lane.plugin;link.parameter=lane.parameter;
  }
  const auto effects=patternCommands(song.GetType(),false);
  const auto fx=[&](uint8_t effect,uint8_t param){for(const auto &e:effects)if(e.command==effect&&(param&e.mask)==e.value)return e.label+" · "+e.name;return std::string("Tracker effect ")+std::to_string(effect);};
  {const auto cell=doc.cell(pattern,row,channel);if(cell.effect)add("Pattern FX","effect",fx(cell.effect,cell.parameter)+" · this row");}
  for(const auto &c:native.performance.commands)if(c.pattern==pid&&c.track==track&&c.position/65536==row){
    const auto label=c.kind==PatternCommandKind::Native?std::string(nativePatternCommand(c.native).code)+" · "+std::string(nativePatternCommand(c.native).name):c.kind==PatternCommandKind::TrackerEffect?fx(c.effect,c.parameter):std::string(patternCommandKindName(c.kind));
    auto &link=add("Pattern FX","effect",label+" · row "+std::to_string(c.position/65536));link.row=c.position/65536;link.column=c.column;
  }
  budget.check();
  }catch(const PatternActivityBudget::Expired &){activityLimit(result.playback);result.items.clear();}
  return result;
}
}
