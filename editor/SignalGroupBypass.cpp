#include "SignalGroupBypass.hpp"
#include "SignalGraph.hpp"
#include <algorithm>
#include <stdexcept>
namespace Tracker {
namespace {
void need(bool value,const char *message){if(!value)throw std::invalid_argument(message);}
template<class T> void appendUnique(std::vector<T> &values,T value){if(std::find(values.begin(),values.end(),value)==values.end())values.push_back(std::move(value));}
template<class Route,class Input,class Output>
std::vector<Route> resolve(const std::vector<Route> &saved,const std::vector<Input> &inputs,const std::vector<Output> &outputs,bool required){
  if(saved.empty()){
    if(outputs.empty())return {};
    if(inputs.empty()){std::vector<Route> result;for(const auto &out:outputs)result.push_back({{},out});return result;}
    if(inputs.size()==1&&outputs.size()==1)return {{inputs.front(),outputs.front()}};
    need(!required,"Choose a dry input for every outgoing group boundary before bypassing");return {};
  }
  need(saved.size()<=256&&saved.size()==outputs.size(),"A group dry map must cover every outgoing audio boundary exactly once");
  std::vector<Output> mapped;
  for(const auto &route:saved){
    need(std::find(outputs.begin(),outputs.end(),route.output)!=outputs.end(),"A group dry-map output no longer crosses this boundary");
    need(std::find(mapped.begin(),mapped.end(),route.output)==mapped.end(),"A group boundary output has more than one dry mapping");mapped.push_back(route.output);
    need(inputs.empty() ? route.input==Input{} : std::find(inputs.begin(),inputs.end(),route.input)!=inputs.end(),"A group dry-map input no longer enters this boundary");
  }
  return saved;
}
std::set<std::string> songMembers(const SignalGraph &graph,uint64_t id){
  need(std::any_of(graph.groups.begin(),graph.groups.end(),[&](const auto &g){return g.id==id;}),"Song processing group no longer exists");
  std::set<uint64_t> groups{id};std::set<std::string> members;
  for(size_t pass=0;pass<graph.groups.size();++pass)for(const auto &g:graph.groups)if(groups.contains(g.parent))groups.insert(g.id);
  for(const auto &g:graph.groups)if(groups.contains(g.id))members.insert(g.nodes.begin(),g.nodes.end());return members;
}
}
std::set<uint64_t> signalGroupMembers(const SignalDefinition &definition,uint64_t id){
  need(std::any_of(definition.groups.begin(),definition.groups.end(),[&](const auto &g){return g.id==id;}),"Processing group no longer exists");
  std::set<uint64_t> groups{id},members;
  for(size_t pass=0;pass<definition.groups.size();++pass)for(const auto &g:definition.groups)if(groups.contains(g.parent))groups.insert(g.id);
  for(const auto &g:definition.groups)if(groups.contains(g.id))members.insert(g.nodes.begin(),g.nodes.end());return members;
}
SignalGroupBoundary signalGroupBoundary(const SignalDefinition &definition,uint64_t id){
  const auto members=signalGroupMembers(definition,id);SignalGroupBoundary result;
  for(const auto &edge:definition.audio){
    if(!members.contains(edge.source)&&members.contains(edge.target))appendUnique(result.inputs,SignalGroupInput{edge.source,edge.target,edge.output,edge.input});
    if(members.contains(edge.source)&&!members.contains(edge.target))appendUnique(result.outputs,SignalGroupOutput{edge.source,edge.output});
  }
  return result;
}
std::vector<SignalGroupDryRoute> resolvedSignalGroupDryRoutes(const SignalDefinition &definition,uint64_t id,bool required){
  const auto boundary=signalGroupBoundary(definition,id);
  const auto &group=*std::find_if(definition.groups.begin(),definition.groups.end(),[&](const auto &g){return g.id==id;});
  return resolve(group.dryRoutes,boundary.inputs,boundary.outputs,required);
}
void remapSignalGroupDryRoutes(SignalDefinition &definition,const std::map<uint64_t,uint64_t> &mapping){
  std::map<std::string,std::string> keys;
  for(const auto &e:definition.audio)if(mapping.contains(e.source)&&mapping.contains(e.target)){
    keys[audioTrimKey(SignalGroupInput{e.source,e.target,e.output,e.input})]=audioTrimKey(SignalGroupInput{mapping.at(e.source),mapping.at(e.target),e.output,e.input});
    keys[audioTrimKey(SignalGroupOutput{e.source,e.output})]=audioTrimKey(SignalGroupOutput{mapping.at(e.source),e.output});
  }
  auto sources=[&](AudioPortTrims &trims){for(auto &[key,edges]:trims.modulation){std::erase_if(edges,[&](auto &edge){if(!mapping.contains(edge.source))return true;edge.source=mapping.at(edge.source);return false;});}std::erase_if(trims.modulation,[](const auto &p){return p.second.empty();});};
  for(auto &n:definition.nodes)sources(n.trims);
  for(auto &g:definition.groups){sources(g.trims);AudioPortTrims next;
    for(const auto &[key,value]:g.trims.gains)if(keys.contains(key))next.gains[keys.at(key)]=value;
    for(const auto &[a,b]:g.trims.links)if(keys.contains(a)&&keys.contains(b))next.links[keys.at(a)]=keys.at(b);
    for(const auto &[key,value]:g.trims.modulation)if(keys.contains(key))next.modulation[keys.at(key)]=value;
    g.trims=std::move(next);
  }
  for(auto &g:definition.groups)for(auto &r:g.dryRoutes){if(r.input.source){r.input.source=mapping.at(r.input.source);r.input.target=mapping.at(r.input.target);}r.output.node=mapping.at(r.output.node);}
}
void pruneSignalGroupDryRoutes(SignalDefinition &definition){
  auto sources=[&](AudioPortTrims &trims){for(auto &[key,edges]:trims.modulation)std::erase_if(edges,[&](const auto &edge){return std::none_of(definition.nodes.begin(),definition.nodes.end(),[&](const auto &n){return n.id==edge.source;});});std::erase_if(trims.modulation,[](const auto &p){return p.second.empty();});};
  for(auto &n:definition.nodes)sources(n.trims);for(auto &g:definition.groups)sources(g.trims);

  for(auto &g:definition.groups){const auto boundary=signalGroupBoundary(definition,g.id);
    std::erase_if(g.dryRoutes,[&](const auto &r){return std::find(boundary.outputs.begin(),boundary.outputs.end(),r.output)==boundary.outputs.end();});
    if(boundary.inputs.empty())for(auto &r:g.dryRoutes)r.input={};
  }
}
SignalSongGroupBoundary signalSongGroupBoundary(const SignalGraph &graph,const MixerGraph &mixer,const std::vector<std::string> &rack,uint64_t id){
  if(!mixer.detachedChains.empty())return signalSongGroupBoundary(graph,projectMixerDetachedChains(mixer),rack,id);
  const auto members=songMembers(graph,id);SignalSongGroupBoundary result;
  std::set<std::string> owned(mixer.detached.begin(),mixer.detached.end());
  for(const auto &bus:mixer.buses)owned.insert(bus.inserts.begin(),bus.inserts.end());
  std::map<uint64_t,std::vector<std::string>> chains;
  std::map<uint64_t,std::string> ends;
  auto busKey=[](uint64_t bus){return "n"+std::to_string(bus);};
  for(const auto &bus:mixer.buses){auto &chain=chains[bus.id];chain=bus.inserts;if(bus.kind==MixerBusKind::Master)for(const auto &plugin:rack)if(!owned.contains(plugin))chain.push_back(plugin);ends[bus.id]=chain.empty()?busKey(bus.id):"plugin:"+chain.back();}
  auto add=[&](std::string from,std::string to,SignalRouteIdentity identity){if(!members.contains(from)&&members.contains(to))appendUnique(result.inputs,identity);if(members.contains(from)&&!members.contains(to))appendUnique(result.outputs,std::move(identity));};
  for(const auto &bus:mixer.buses){
    auto previous=busKey(bus.id);
    for(const auto &plugin:chains[bus.id]){const auto next="plugin:"+plugin;if(std::find(mixer.disconnectedMainInputs.begin(),mixer.disconnectedMainInputs.end(),plugin)==mixer.disconnectedMainInputs.end())add(previous,next,{"insert",busKey(bus.id),{},plugin,"main-path"});previous=next;}
    if(bus.output)add(previous,busKey(bus.output),{"output",busKey(bus.id),busKey(bus.output)});
    if(bus.kind==MixerBusKind::Master&&!mixer.masterOutputDisconnected)add(previous,"main-output",{"master-output",busKey(bus.id),{},{},"pre-master-fader"});
    for(const auto &send:bus.sends)add(previous,busKey(send.target),{"send",busKey(bus.id),busKey(send.target)});
  }
  for(const auto &route:graph.stageConnections)add(signalStageEndpointKey(route.source),signalStageEndpointKey(route.target),signalStageRouteIdentity(route));
  for(const auto &route:mixer.pluginConnections)add("plugin:"+route.source,"plugin:"+route.target,{"plugin-connection","plugin:"+route.source,"plugin:"+route.target,{},"post-gain",route.input,route.output});
  for(const auto &route:mixer.sidechains)if(ends.contains(route.source)&&std::none_of(graph.inputs.begin(),graph.inputs.end(),[&](const auto &r){return r.source==route.source&&signalBusIdentity(r.target)==route.plugin&&r.input==route.input;}))add(ends.at(route.source),"plugin:"+route.plugin,{"plugin-input",busKey(route.source),{},route.plugin,"post-gain",route.input,0});
  for(const auto &route:mixer.instruments)if(route.target&&std::none_of(graph.outputs.begin(),graph.outputs.end(),[&](const auto &r){return signalBusIdentity(r.source)==route.plugin&&r.target==route.target&&r.output==route.output;}))add("plugin:"+route.plugin,busKey(route.target),{"plugin-output",{},busKey(route.target),route.plugin,"post-gain",0,route.output});
  for(const auto &route:graph.inputs)if(ends.contains(route.source))add(ends.at(route.source),"graph:"+busKey(route.target),{"graph-input",busKey(route.source),busKey(route.target),{},"post-gain",route.input,0});
  for(const auto &route:graph.outputs)add("graph:"+busKey(route.source),busKey(route.target),{"graph-output",busKey(route.source),busKey(route.target),{},"post-gain",0,route.output});
  for(const auto &source:graph.songSources)if(source.node.kind==SignalNodeKind::Follower){
    const auto from=source.audioStage?"stage:"+busKey(source.audioStage):!source.audioPlugin.empty()?"plugin:"+source.audioPlugin:source.audioBus&&ends.contains(source.audioBus)?ends.at(source.audioBus):std::string{};
    if(!from.empty())add(from,"source:"+busKey(source.node.id),{"follower-input",source.audioStage?"stage:"+busKey(source.audioStage):!source.audioPlugin.empty()?"plugin:"+source.audioPlugin:busKey(source.audioBus),"source:"+busKey(source.node.id),{},"post-gain",0,source.output});
  }
  return result;
}
std::vector<SignalSongGroupDryRoute> resolvedSongGroupDryRoutes(const SignalGraph &graph,const MixerGraph &mixer,const std::vector<std::string> &rack,uint64_t id,bool required){
  const auto boundary=signalSongGroupBoundary(graph,mixer,rack,id);
  const auto &group=*std::find_if(graph.groups.begin(),graph.groups.end(),[&](const auto &g){return g.id==id;});
  return resolve(group.dryRoutes,boundary.inputs,boundary.outputs,required);
}
void preserveSongGroupInsertion(SignalGraph &next,const SignalGraph &previous,
  const MixerGraph &previousMixer,const MixerGraph &nextMixer,
  const std::vector<std::string> &previousRack,const std::vector<std::string> &nextRack,
  const std::string &insertedPlugin){
  if(previous.groups.empty())return;
  need(!insertedPlugin.empty()&&std::find(previousRack.begin(),previousRack.end(),insertedPlugin)==previousRack.end()&&std::find(nextRack.begin(),nextRack.end(),insertedPlugin)!=nextRack.end(),"Choose one newly inserted effect");
  const auto mixer=projectMixerDetachedChains(nextMixer);
  const MixerBus *owner=nullptr;std::set<std::string> owned(mixer.detached.begin(),mixer.detached.end());
  for(const auto &bus:mixer.buses){owned.insert(bus.inserts.begin(),bus.inserts.end());if(std::find(bus.inserts.begin(),bus.inserts.end(),insertedPlugin)!=bus.inserts.end())owner=&bus;}
  if(!owner&&!owned.contains(insertedPlugin))for(const auto &bus:mixer.buses)if(bus.kind==MixerBusKind::Master)owner=&bus;
  // A genuinely detached effect does not split an existing group's cable.
  if(!owner)return;
  const SignalRouteIdentity inserted{"insert","n"+std::to_string(owner->id),{},insertedPlugin,"main-path"};
  auto prepared=next;
  for(auto &group:prepared.groups){
    const auto old=std::find_if(previous.groups.begin(),previous.groups.end(),[&](const auto &g){return g.id==group.id;});
    if(old==previous.groups.end()||old->dryRoutes.empty())continue;
    // Never reinterpret a stale map as belonging to the newly inserted cable.
    const auto saved=resolvedSongGroupDryRoutes(previous,previousMixer,previousRack,group.id,old->bypass);
    const auto boundary=signalSongGroupBoundary(prepared,mixer,nextRack,group.id);
    std::vector<SignalSongGroupDryRoute> maps;
    for(auto map:saved){
      if(map.input!=SignalRouteIdentity{}&&std::find(boundary.inputs.begin(),boundary.inputs.end(),map.input)==boundary.inputs.end()){
        need(std::find(boundary.inputs.begin(),boundary.inputs.end(),inserted)!=boundary.inputs.end(),"Adding this effect changes a group dry input; choose its boundary explicitly");map.input=inserted;
      }
      if(std::find(boundary.outputs.begin(),boundary.outputs.end(),map.output)==boundary.outputs.end()){
        need(std::find(boundary.outputs.begin(),boundary.outputs.end(),inserted)!=boundary.outputs.end(),"Adding this effect changes a group dry output; choose its boundary explicitly");map.output=inserted;
      }
      const auto same=std::find_if(maps.begin(),maps.end(),[&](const auto &r){return r.output==map.output;});
      if(same==maps.end())maps.push_back(map);
      else need(same->input==map.input,"Adding after this group merges different dry paths; choose one shared dry input first");
    }
    group.dryRoutes=std::move(maps);
    (void)resolvedSongGroupDryRoutes(prepared,mixer,nextRack,group.id,group.bypass);
  }
  next.groups=std::move(prepared.groups);
}
void preserveSongGroupDetachment(SignalGraph &next,const SignalGraph &previous,
  const MixerGraph &previousMixer,const MixerGraph &nextMixer,const std::vector<std::string> &rack){
  if(previous.groups.empty())return;
  struct SerialWire {std::string source;SignalRouteIdentity identity;};
  auto serialWires=[&](const MixerGraph &input){
    const auto mixer=projectMixerDetachedChains(input);std::map<std::string,SerialWire> wires;
    std::set<std::string> owned(mixer.detached.begin(),mixer.detached.end());
    for(const auto &bus:mixer.buses)owned.insert(bus.inserts.begin(),bus.inserts.end());
    for(const auto &bus:mixer.buses){auto chain=bus.inserts;
      if(bus.kind==MixerBusKind::Master)for(const auto &id:rack)if(!owned.contains(id))chain.push_back(id);
      const auto owner="n"+std::to_string(bus.id);auto source=owner;
      for(const auto &plugin:chain){if(std::find(mixer.disconnectedMainInputs.begin(),mixer.disconnectedMainInputs.end(),plugin)==mixer.disconnectedMainInputs.end())
          wires.emplace(plugin,SerialWire{source,{"insert",owner,{},plugin,"main-path"}});
        source="plugin:"+plugin;}
    }
    return wires;
  };
  const auto before=serialWires(previousMixer),after=serialWires(nextMixer);
  auto retain=[&](SignalRouteIdentity &route,const std::vector<SignalRouteIdentity> &boundary){
    if(std::find(boundary.begin(),boundary.end(),route)!=boundary.end())return true;
    if(route.kind!="insert")return false;
    const auto old=before.find(route.plugin),now=after.find(route.plugin);
    if(old==before.end()||now==after.end()||old->second.identity!=route||old->second.source!=now->second.source||
       std::find(boundary.begin(),boundary.end(),now->second.identity)==boundary.end())return false;
    route=now->second.identity;return true;
  };
  auto prepared=next;
  for(auto &group:prepared.groups){
    const auto old=std::find_if(previous.groups.begin(),previous.groups.end(),[&](const auto &g){return g.id==group.id;});
    if(old==previous.groups.end())continue;
    const auto saved=resolvedSongGroupDryRoutes(previous,previousMixer,rack,group.id,old->bypass);
    if(!old->dryRoutes.empty()){
      const auto boundary=signalSongGroupBoundary(prepared,nextMixer,rack,group.id);std::vector<SignalSongGroupDryRoute> maps;
      for(auto map:saved){
        if(!retain(map.output,boundary.outputs))continue;
        if(boundary.inputs.empty())map.input={};
        else need(retain(map.input,boundary.inputs),"Detaching removes a selected dry input from a surviving group output; choose its dry path explicitly");
        maps.push_back(std::move(map));
      }
      // Do not let an emptied explicit map infer a different surviving input.
      need(maps.size()==boundary.outputs.size(),"Detaching creates a new group boundary; choose its dry path explicitly");
      group.dryRoutes=std::move(maps);
    }
    (void)resolvedSongGroupDryRoutes(prepared,nextMixer,rack,group.id,group.bypass);
  }
  next.groups=std::move(prepared.groups);
}

}
