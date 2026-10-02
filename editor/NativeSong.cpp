#include "NativeSong.hpp"
#include "soundlib/NativeNoteEffects.h"
#include "soundlib/mod_specifications.h"
#include <set>
#include <stdexcept>
#include <tuple>

namespace Tracker {
size_t PatternPerformance::bytes() const {
  size_t result=sizeof(*this)+columns.size()*(sizeof(uint64_t)+sizeof(uint8_t))+commands.size()*sizeof(PatternCommand);
  for(const auto &[id,binding]:bindings)result+=sizeof(binding)+sizeof(id)+binding.plugin.size()+binding.name.size();
  return result;
}

NativeEntity NativeSong::makeEntity() {
  if (!nextID || nextID >= maximumID) throw std::runtime_error("Native song identity limit reached");
  return {nextID++, {}, {}, 0};
}
void NativeSong::reserveMasterIdentity() {
  if(masterID)return;
  const auto master=std::find_if(mixer.buses.begin(),mixer.buses.end(),[](const auto &bus){return bus.kind==MixerBusKind::Master;});
  masterID=master==mixer.buses.end()?makeEntity().id:master->id;
}
void NativeSong::ensureMixer() {
  reserveMasterIdentity();
  if (mixer.active()) return;
  const auto master = masterID;
  for (const auto &[channel, track] : tracks)
    mixer.buses.push_back({track.id, master, MixerBusKind::Track,
      track.name.empty() ? "Track " + std::to_string(channel + 1) : track.name, track.color});
  mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
}
void NativeSong::removePluginRoutes(const std::string &instance) {
  std::erase_if(signal.noteRouting.routes,[&](const auto &route){return route.plugin==instance;});
  std::erase(mixer.detached,instance);std::erase(mixer.disconnectedMainInputs,instance);
  for(auto &chain:mixer.detachedChains)std::erase(chain.plugins,instance);
  std::erase_if(mixer.detachedChains,[](const auto &chain){return chain.plugins.empty();});for(const auto &chain:mixer.detachedChains)std::erase(mixer.disconnectedMainInputs,chain.plugins.front());
  for(auto &bus:mixer.buses)std::erase(bus.inserts,instance);
  std::erase_if(mixer.instruments,[&](const auto &route){return route.plugin==instance;});
  std::erase_if(mixer.pluginConnections,[&](const auto &route){return route.source==instance||route.target==instance;});
  std::erase_if(mixer.sidechains,[&](const auto &route){return route.plugin==instance;});
  const auto node="plugin:"+instance;
  signal.layout.erase(node);
  removeSignalPresentationNode(signal.presentation,node);
  std::vector<std::string> remaining;
  for(const auto &group:signal.groups)for(const auto &member:group.nodes)
    if(member!=node)remaining.push_back(member);
  pruneSongSignalGroups(signal,remaining);
}
void NativeSong::removeSongSources(const std::vector<uint64_t> &sources) {
  std::set<uint64_t> removed;
  for(auto id:sources)if(!removed.insert(id).second||std::none_of(signal.songSources.begin(),signal.songSources.end(),[&](const auto &s){return s.node.id==id;}))throw std::invalid_argument("Select distinct existing modulation sources");
  std::erase_if(signal.songSources,[&](const auto &s){return removed.contains(s.node.id);});
  std::erase_if(signal.songModulation,[&](const auto &e){return removed.contains(e.source);});
  std::erase_if(envelopeLinks,[&](const auto &link){return link.target.kind==EnvelopeTargetKind::Graph&&removed.contains(link.target.owner);});
  std::set<std::string> keys;for(auto id:removed){auto key="source:n"+std::to_string(id);keys.insert(key);signal.layout.erase(key);removeSignalPresentationNode(signal.presentation,key);}
  std::vector<std::string> remaining;for(const auto &g:signal.groups)for(const auto &key:g.nodes)if(!keys.contains(key))remaining.push_back(key);
  pruneSongSignalGroups(signal,remaining);
}
void removeSongConnections(NativeSong &song,const std::vector<SongConnectionRef> &connections,
                           const std::vector<std::string> &instrumentPlugins,const std::vector<std::string> &effectRack) {
  if(connections.empty())return;
  if(connections.size()>512)throw std::invalid_argument("At most 512 song cables can be removed at once");
  auto next=song;if(std::any_of(connections.begin(),connections.end(),[](const auto &c){return c.kind!=SongConnectionKind::FollowerInput&&c.kind!=SongConnectionKind::Modulation&&c.kind!=SongConnectionKind::Note;}))next.ensureMixer();
  auto require=[](bool valid,const char *message){if(!valid)throw std::invalid_argument(message);};
  std::set<std::tuple<SongConnectionKind,uint64_t,uint64_t,std::string,uint32_t,bool,std::string,uint32_t>> seen;
  auto remove=[&](auto &routes,auto predicate){const auto count=std::erase_if(routes,predicate);require(count==1,"Song cable no longer exists; refresh the graph");};
  for(const auto &c:connections) {
    require(seen.emplace(c.kind,c.source,c.target,c.plugin,c.port,c.preFader,c.sourcePlugin,c.output).second,"The cable removal batch contains a duplicate");
    require(c.kind==SongConnectionKind::Modulation||c.port<=63,"Cable port is outside 0…63");
    require(c.kind==SongConnectionKind::FollowerInput||!c.preFader,"Only follower tap identities accept preFader");
    require(c.kind==SongConnectionKind::PluginConnection||(c.sourcePlugin.empty()&&!c.output),"Only direct plugin cables accept a plugin source/output");
    switch(c.kind) {
    case SongConnectionKind::PluginConnection:
      require(!c.source&&!c.target&&!c.plugin.empty()&&!c.sourcePlugin.empty()&&c.output<64,"Invalid direct plugin cable identity");
      remove(next.mixer.pluginConnections,[&](const auto &r){return r.source==c.sourcePlugin&&r.target==c.plugin&&r.output==c.output&&r.input==c.port;});
      std::erase_if(next.signal.presentation.cables,[&](const auto &p){return p.source=="plugin:"+c.sourcePlugin&&p.target=="plugin:"+c.plugin&&p.output==c.output&&p.input==c.port&&!p.modulation;});break;
    case SongConnectionKind::Insert:
      require(c.source&&!c.target&&!c.plugin.empty()&&!c.port,"Invalid insert cable identity");
      disconnectMixerInsert(next.mixer,effectRack,c.source,c.plugin);break;
    case SongConnectionKind::MasterOutput:
      require(c.source==next.masterID&&!c.target&&c.plugin.empty()&&!c.port&&!next.mixer.masterOutputDisconnected,"Master output cable no longer exists");
      next.mixer.masterOutputDisconnected=true;break;
    case SongConnectionKind::Note: {
      require(bool(c.source)!=bool(c.target)&&c.plugin.empty()&&!c.port,"Choose an explicit note route or implicit instrument assignment");
      const auto connection=c.source?"note:n"+std::to_string(c.source):"note-assignment:n"+std::to_string(c.target);
      if(c.source)remove(next.signal.noteRouting.routes,[&](const auto &route){return route.id==c.source;});
      else {
        require(std::any_of(next.instruments.begin(),next.instruments.end(),[&](const auto &i){return i.second.id==c.target;}),"Note instrument no longer exists");
        auto &suppressed=next.signal.noteRouting.suppressedAssignments;
        require(std::find(suppressed.begin(),suppressed.end(),c.target)==suppressed.end(),"Implicit note cable is already disconnected");suppressed.push_back(c.target);
      }
      std::erase_if(next.signal.presentation.cables,[&](const auto &path){return path.connection==connection;});break;
    }
    case SongConnectionKind::Output: {
      require(c.source&&c.target&&c.plugin.empty()&&!c.port,"Invalid main-output cable identity");
      auto b=std::find_if(next.mixer.buses.begin(),next.mixer.buses.end(),[&](const auto &v){return v.id==c.source;});
      require(b!=next.mixer.buses.end()&&b->output==c.target,"Song cable no longer exists; refresh the graph");b->output=0;break;
    }
    case SongConnectionKind::Send: {
      require(c.source&&c.target&&c.plugin.empty()&&!c.port,"Invalid send cable identity");
      auto b=std::find_if(next.mixer.buses.begin(),next.mixer.buses.end(),[&](const auto &v){return v.id==c.source;});
      require(b!=next.mixer.buses.end(),"Song cable source no longer exists");remove(b->sends,[&](const auto &v){return v.target==c.target;});break;
    }
    case SongConnectionKind::GraphInput:
      require(c.source&&c.target&&c.plugin.empty()&&c.port,"Invalid graph-input cable identity");
      remove(next.signal.inputs,[&](const auto &v){return v.source==c.source&&v.target==c.target&&v.input==c.port;});break;
    case SongConnectionKind::GraphOutput:
      require(c.source&&c.target&&c.plugin.empty()&&c.port,"Invalid graph-output cable identity");
      remove(next.signal.outputs,[&](const auto &v){return v.source==c.source&&v.target==c.target&&v.output==c.port;});break;
    case SongConnectionKind::PluginInput:
      require(c.source&&!c.target&&!c.plugin.empty(),"Invalid plugin-input cable identity");
      remove(next.mixer.sidechains,[&](const auto &v){return v.source==c.source&&v.plugin==c.plugin&&v.input==c.port;});break;
    case SongConnectionKind::PluginOutput: {
      require(!c.source&&c.target&&!c.plugin.empty(),"Invalid plugin-output cable identity");
      const auto samePort=[&](const auto &v){return v.plugin==c.plugin&&v.output==c.port;};
      const bool hasPort=std::any_of(next.mixer.instruments.begin(),next.mixer.instruments.end(),samePort);
      if(hasPort)remove(next.mixer.instruments,[&](const auto &v){return samePort(v)&&v.target==c.target;});
      else require(c.port==0&&c.target==next.masterID&&std::find(instrumentPlugins.begin(),instrumentPlugins.end(),c.plugin)!=instrumentPlugins.end(),"Song cable no longer exists; refresh the graph");
      // Absence means an instrument's default Master route. Keep an explicit
      // disconnected marker when cutting its last destination, even if the
      // plugin is currently unavailable and its role cannot be queried.
      if(std::none_of(next.mixer.instruments.begin(),next.mixer.instruments.end(),samePort))next.mixer.instruments.push_back({c.plugin,0,c.port});
      break;
    }
    case SongConnectionKind::FollowerInput: {
      require(c.target&&((c.source&&!c.port&&c.plugin.empty())||(!c.source&&!c.plugin.empty()&&!c.preFader)),"Invalid follower input identity");
      auto s=std::find_if(next.signal.songSources.begin(),next.signal.songSources.end(),[&](const auto &v){return v.node.id==c.target&&v.node.kind==SignalNodeKind::Follower;});
      require(s!=next.signal.songSources.end()&&s->audioBus==c.source&&s->audioPlugin==c.plugin&&s->output==c.port&&s->preFader==c.preFader,"Follower input no longer exists; refresh the graph");
      std::erase_if(next.signal.presentation.cables,[&](const auto &path){return path.target=="source:n"+std::to_string(c.target)&&!path.modulation;});
      s->audioBus=0;s->audioPlugin.clear();s->output=0;s->preFader=false;break;
    }
    case SongConnectionKind::Modulation:
      require(c.source&&!c.target&&!c.plugin.empty(),"Invalid modulation cable identity");
      remove(next.signal.songModulation,[&](const auto &v){return v.source==c.source&&v.plugin==c.plugin&&v.parameter==c.port;});
      std::erase_if(next.signal.presentation.cables,[&](const auto &path){return path.source=="source:n"+std::to_string(c.source)&&path.target=="plugin:"+c.plugin&&path.input==c.port&&path.modulation;});break;
    default:throw std::invalid_argument("Unsupported song cable kind");
    }
  }
  std::vector<uint64_t> tracks;for(const auto &[index,track]:next.tracks)tracks.push_back(track.id);
  next.mixer.validate(tracks);signalRoutingGraph(next.mixer,next.signal).validate(tracks);
  song=std::move(next);
}
void NativeSong::reconcile(const OpenMPT::CSoundFile &s) {
  for(const auto &[index,instrument]:instruments)if(!index||index>s.GetNumInstruments()||!s.Instruments[index]) {
    const auto key="note-instrument:n"+std::to_string(instrument.id);signal.layout.erase(key);removeSignalPresentationNode(signal.presentation,key);
  }
  auto sync = [&](auto &items, int begin, int end, auto valid) {
    for (auto i = items.begin(); i != items.end();) {
      if (i->first < begin || i->first >= end || !valid(i->first)) i = items.erase(i);
      else ++i;
    }
    for (int i = begin; i < end; ++i)
      if (valid(i) && !items.count(uint16_t(i))) items.emplace(uint16_t(i), makeEntity());
  };
  sync(patterns, 0, s.Patterns.Size(), [&](int i) { return s.Patterns.IsValidPat(i); });
  sync(tracks, 0, s.GetNumChannels(), [](int) { return true; });
  sync(samples, 1, s.GetNumSamples() + 1, [](int) { return true; });
  sync(instruments, 1, s.GetNumInstruments() + 1, [](int) { return true; });
  sequences.resize(s.Order.GetNumSequences());
  for (size_t i = 0; i < sequences.size(); ++i) {
    auto &sequence = sequences[i];
    if (!sequence.info.id) sequence.info = makeEntity();
    sequence.orders.resize(s.Order(OpenMPT::SEQUENCEINDEX(i)).size());
    for (auto &slot : sequence.orders) if (!slot.id) slot = makeEntity();
  }
  reserveMasterIdentity();
  reconcileEnvelopeLinks(*this,s);
  // Structural edits own their loss semantics (e.g. shrinking a pattern).
  // Preserve surviving points and never let a removed identity retarget a lane.
  for (auto lane = automation.begin(); lane != automation.end();) {
    auto pattern = std::find_if(patterns.begin(), patterns.end(), [&](const auto &p) { return p.second.id == lane->pattern; });
    if (pattern == patterns.end()) { lane = automation.erase(lane); continue; }
    const uint32_t end = uint32_t(s.Patterns[pattern->first].GetNumRows()) * 256;
    std::erase_if(lane->points, [&](const auto &p) { return p.position >= end; });
    if (lane->points.empty()) lane = automation.erase(lane); else ++lane;
  }
  std::set<uint64_t> performanceTracks;
  for(const auto &[index,track]:tracks)performanceTracks.insert(track.id);
  std::erase_if(performance.columns,[&](const auto &entry){return !performanceTracks.contains(entry.first);});
  std::erase_if(performance.commands,[&](auto &command){
    const auto pattern=std::find_if(patterns.begin(),patterns.end(),[&](const auto &p){return p.second.id==command.pattern;});
    if(pattern==patterns.end()||!performanceTracks.contains(command.track))return true;
    const uint64_t end=uint64_t(s.Patterns[pattern->first].GetNumRows())*performanceUnitsPerRow;
    if(command.position>=end)return true;
    command.duration=uint32_t(std::min<uint64_t>(command.duration,end-command.position));return false;
  });
  std::erase_if(preciseNotes,[&](const auto &note){
    const auto pattern=std::find_if(patterns.begin(),patterns.end(),[&](const auto &p){return p.second.id==note.pattern;});
    return pattern==patterns.end()||!performanceTracks.contains(note.track)||
      note.position>=uint64_t(s.Patterns[pattern->first].GetNumRows())*performanceUnitsPerRow;
  });
  if (mixer.active()) {
    std::set<uint64_t> trackIDs;
    for (const auto &[index, track] : tracks) trackIDs.insert(track.id);
    std::set<uint64_t> removed;
    std::erase_if(mixer.buses, [&](const auto &bus) {
      if (bus.kind != MixerBusKind::Track || trackIDs.count(bus.id)) return false;
      // A removed channel must not move a disconnected insert into Master
      // and silence the entire song. Keep its ordered chain on a silent root.
      if(!bus.inserts.empty()&&std::any_of(bus.inserts.begin(),bus.inserts.end(),[&](const auto &id){return std::find(mixer.disconnectedMainInputs.begin(),mixer.disconnectedMainInputs.end(),id)!=mixer.disconnectedMainInputs.end();})) {
        mixer.detachedChains.push_back({makeEntity().id,bus.inserts});std::erase(mixer.disconnectedMainInputs,bus.inserts.front());
      }
      removed.insert(bus.id); return true;
    });
    std::erase_if(mixer.sidechains, [&](const auto &side) { return removed.count(side.source); });
    auto master = std::find_if(mixer.buses.begin(), mixer.buses.end(), [](const auto &bus) { return bus.kind == MixerBusKind::Master; });
    if (master != mixer.buses.end()) {
      const auto masterID = master->id;
      for (auto &source : mixer.instruments) if (removed.count(source.target)) source.target = masterID;
      for (const auto &[index, track] : tracks)
        if (std::none_of(mixer.buses.begin(), mixer.buses.end(), [&](const auto &bus) { return bus.id == track.id; }))
          mixer.buses.push_back({track.id, masterID, MixerBusKind::Track, "Track " + std::to_string(index + 1)});
    }
  }
  std::set<uint64_t> graphTargets;
  for(const auto &bus:mixer.buses)graphTargets.insert(bus.id);
  if(!mixer.active()) {
    for(const auto &[index,track]:tracks)graphTargets.insert(track.id);
    if(masterID)graphTargets.insert(masterID);
  }
  const auto instrumentExists=[&](uint64_t id){return std::any_of(instruments.begin(),instruments.end(),[&](const auto &i){return i.second.id==id;});};
  std::erase_if(signal.noteRouting.suppressedAssignments,[&](uint64_t id){return !instrumentExists(id);});
  std::erase_if(signal.noteRouting.triggerSources,[&](const auto &source){return !instrumentExists(source.instrument);});
  std::erase_if(signal.noteRouting.routes,[&](const auto &route){return route.sourceKind==NoteSourceKind::Instrument?!instrumentExists(route.source):std::none_of(tracks.begin(),tracks.end(),[&](const auto &t){return t.second.id==route.source;});});
  reconcileNoteCableGeometry(signal.presentation,signal.noteRouting);
  std::erase_if(signal.instrumentAssignments,[&](const auto &a){return std::none_of(instruments.begin(),instruments.end(),[&](const auto &i){return i.second.id==a.target;});});
  std::erase_if(signal.assignments,[&](const auto &a){return !graphTargets.contains(a.target);});
  std::erase_if(signal.inputs,[&](const auto &r){return !graphTargets.contains(r.source)||!graphTargets.contains(r.target);});
  std::erase_if(signal.outputs,[&](const auto &r){return !graphTargets.contains(r.source)||!graphTargets.contains(r.target);});
  std::erase_if(signal.lanes,[&](const auto &a){return !graphTargets.contains(a.first);});
  std::erase_if(signal.commands,[&](const auto &c){
    auto pattern=std::find_if(patterns.begin(),patterns.end(),[&](const auto &p){return p.second.id==c.pattern;});
    return !graphTargets.contains(c.target)||pattern==patterns.end()||uint64_t(c.position)>=uint64_t(s.Patterns[pattern->first].GetNumRows())*65536;
  });
  std::set<uint64_t> removedSongSources;
  std::erase_if(signal.songSources,[&](auto &source){
    if((source.noteTarget&&!graphTargets.contains(source.noteTarget)) ||
      (source.noteInstrument&&std::none_of(instruments.begin(),instruments.end(),[&](const auto &i){return i.second.id==source.noteInstrument;}))){removedSongSources.insert(source.node.id);return true;}
    if(source.audioBus&&!graphTargets.contains(source.audioBus)){source.audioBus=0;source.preFader=false;}
    return false;
  });
  std::erase_if(signal.songModulation,[&](const auto &edge){return removedSongSources.contains(edge.source);});
  for(auto id:removedSongSources){const auto key="source:n"+std::to_string(id);signal.layout.erase(key);removeSignalPresentationNode(signal.presentation,key);}
  if(!removedSongSources.empty()) {
    std::vector<std::string> remaining;for(const auto &g:signal.groups)for(const auto &key:g.nodes)if(!key.starts_with("source:")||std::any_of(signal.songSources.begin(),signal.songSources.end(),[&](const auto &source){return key=="source:n"+std::to_string(source.node.id);}))remaining.push_back(key);
    pruneSongSignalGroups(signal,remaining);
  }
  auto trimGraphEnvelope=[&](SignalNode &node){std::erase_if(node.envelopes,[&](auto &lane){
    const auto pattern=std::find_if(patterns.begin(),patterns.end(),[&](const auto &p){return p.second.id==lane.pattern;});
    if(pattern==patterns.end())return true;
    std::erase_if(lane.points,[&](const auto &point){return point.position>=uint64_t(s.Patterns[pattern->first].GetNumRows())*256;});return lane.points.empty();
  });};
  for(auto &definition:signal.library)for(auto &node:definition.nodes)trimGraphEnvelope(node);
  for(auto &source:signal.songSources)trimGraphEnvelope(source.node);
  reconcileEnvelopeLinks(*this,s);
  std::set<uint64_t> surviving;
  for (const auto &[index, track] : tracks) surviving.insert(track.id);
  std::erase_if(columnMutes, [&](const auto &entry) { return !surviving.contains(entry.first); });
  for (auto &track : noteTracks)
    std::erase_if(track.columns, [&](auto id) { return !surviving.contains(id); });
  std::erase_if(noteTracks, [&](const auto &track) {
    return track.columns.empty() || std::none_of(mixer.buses.begin(), mixer.buses.end(),
      [&](const auto &bus) { return bus.id == track.bus && bus.kind == MixerBusKind::Group; });
  });
}
bool PatternPerformance::controls(const std::string &plugin, uint32_t parameter) const {
  for (const auto &command : commands) {
    const auto binding = bindings.find(command.binding);
    if (binding != bindings.end() && binding->second.plugin == plugin && binding->second.parameter == parameter) return true;
  }
  return false;
}
void NativeSong::clonePatternAutomation(uint64_t source, uint64_t destination) {
  std::vector<MusicalAutomationLane> copies;
  for (const auto &lane : automation) if (lane.pattern == source) {
    auto copy = lane; copy.id = makeEntity().id; copy.pattern = destination;
    for(const auto &link:std::vector<EnvelopeLink>(envelopeLinks))if(link.target.kind==EnvelopeTargetKind::Parameter&&link.target.owner==lane.id){auto l=link;l.target.owner=copy.id;envelopeLinks.push_back(l);}
    copies.push_back(std::move(copy));
  }
  automation.insert(automation.end(), copies.begin(), copies.end());
  std::vector<PatternCommand> commands;
  for(const auto &command:performance.commands)if(command.pattern==source){auto copy=command;copy.pattern=destination;commands.push_back(copy);}
  performance.commands.insert(performance.commands.end(),commands.begin(),commands.end());
  std::vector<SignalCommand> graphCommands;
  for(const auto &command:signal.commands)if(command.pattern==source){auto copy=command;copy.pattern=destination;graphCommands.push_back(copy);}
  signal.commands.insert(signal.commands.end(),graphCommands.begin(),graphCommands.end());
  auto cloneGraphEnvelope=[&](SignalNode &node){
    auto lane=std::find_if(node.envelopes.begin(),node.envelopes.end(),[&](const auto &e){return e.pattern==source;});
    if(lane!=node.envelopes.end()){auto copy=*lane;copy.pattern=destination;node.envelopes.push_back(std::move(copy));}
  };
  for(auto &definition:signal.library)for(auto &node:definition.nodes)cloneGraphEnvelope(node);
  for(auto &control:signal.songSources)cloneGraphEnvelope(control.node);
  for(const auto &link:std::vector<EnvelopeLink>(envelopeLinks))if(link.target.kind==EnvelopeTargetKind::Graph&&link.target.pattern==source){auto l=link;l.target.pattern=destination;envelopeLinks.push_back(l);}
  std::vector<PreciseNote> notes;
  for(const auto &note:preciseNotes)if(note.pattern==source){auto copy=note;copy.pattern=destination;notes.push_back(copy);}
  preciseNotes.insert(preciseNotes.end(),notes.begin(),notes.end());
}
void NativeSong::prepareEffects(OpenMPT::CSoundFile &song) const {
  song.nativePatternEffects.clear();
  std::map<uint64_t,uint16_t> patternIndices, trackIndices;
  for(const auto &[index,entity]:patterns) patternIndices[entity.id]=index;
  for(const auto &[index,entity]:tracks) trackIndices[entity.id]=index;
  for(const auto &c:performance.commands) if(c.kind==PatternCommandKind::TrackerEffect && c.column>0) {
    auto &e=song.nativePatternEffects[{patternIndices.at(c.pattern),OpenMPT::ROWINDEX(c.position/performanceUnitsPerRow),trackIndices.at(c.track)}][c.column-1];
    e.command=OpenMPT::EffectCommand(c.effect); e.param=c.parameter;
  }
}

void NativeSong::validate(const OpenMPT::CSoundFile &s) const {
  NativeSong shape = *this;
  shape.reconcile(s);
  if (shape != *this) throw std::invalid_argument("Native metadata does not match the song structure");
  std::set<uint64_t> ids;
  auto check = [&](const NativeEntity &e) {
    if (!e.id || e.id >= nextID || nextID > maximumID || !ids.insert(e.id).second)
      throw std::invalid_argument("Invalid or duplicate native song identity");
    if (e.name.size() > 1024 || e.annotation.size() > 16384 || e.color > 0xffffff ||
        e.name.find('\0') != std::string::npos || e.annotation.find('\0') != std::string::npos)
      throw std::invalid_argument("Native song annotation exceeds its limits");
  };
  for (const auto *items : {&patterns, &tracks, &samples, &instruments})
    for (const auto &[index, item] : *items) check(item);
  for (const auto &sequence : sequences) {
    check(sequence.info);
    for (const auto &slot : sequence.orders) check(slot);
  }
  if (automation.size() > 256) throw std::invalid_argument("Use at most 256 musical automation lanes");
  std::set<std::tuple<uint64_t, std::string, uint32_t>> targets;
  size_t pointCount = 0;
  for (const auto &lane : automation) {
    check({lane.id, {}, {}, 0});
    if (lane.plugin.empty() || lane.plugin.size() > 128 || lane.plugin.find('\0') != std::string::npos ||
        !targets.emplace(lane.pattern, lane.plugin, lane.parameter).second)
      throw std::invalid_argument("Invalid or duplicate musical automation target");
    if (lane.points.empty() || lane.points.size() > 4096)
      throw std::invalid_argument("An automation lane requires between 1 and 4096 points");
    uint32_t last = 0;
    for (size_t i = 0; i < lane.points.size(); ++i) {
      const auto &point = lane.points[i];
      if ((i && point.position <= last) || !std::isfinite(point.value) || point.value < 0 || point.value > 1 ||
          uint8_t(point.curve) > uint8_t(AutomationCurve::Scripted))
        throw std::invalid_argument("Automation points must be ordered, distinct and normalized");
      if(point.curve == AutomationCurve::Scripted && point.formula.source().empty()) throw std::invalid_argument("Scripted points require a formula");
      last = point.position;
    }
    pointCount += lane.points.size();
  }
  if (pointCount > 65536) throw std::invalid_argument("Musical automation exceeds 65536 points");
  std::vector<uint64_t> trackIDs;
  for (const auto &[index, track] : tracks) trackIDs.push_back(track.id);
  mixer.validate(trackIDs);
  check({masterID, {}, {}, 0});
  for (const auto &bus : mixer.buses)
    if(bus.kind==MixerBusKind::Master) {
      if(bus.id!=masterID)throw std::invalid_argument("Mixer Master differs from its reserved song identity");
    } else if (bus.kind != MixerBusKind::Track) check({bus.id, {}, {}, 0});
  std::set<uint64_t> columnIDs, groupIDs;
  for (const auto &track : noteTracks) {
    if (track.columns.empty() || !groupIDs.insert(track.bus).second)
      throw std::invalid_argument("Note tracks need distinct processing groups and at least one column");
    int previous = -1;
    for (auto id : track.columns) {
      auto column = std::find_if(tracks.begin(), tracks.end(), [&](const auto &t) { return t.second.id == id; });
      auto bus = std::find_if(mixer.buses.begin(), mixer.buses.end(), [&](const auto &b) { return b.id == id; });
      if (column == tracks.end() || !columnIDs.insert(id).second || (previous >= 0 && column->first != previous + 1) ||
          bus == mixer.buses.end() || bus->output != track.bus)
        throw std::invalid_argument("Note columns must be adjacent, belong to one track and route to its shared group");
      previous = column->first;
    }
  }
  if(performance.bindings.size()>255||performance.commands.size()>maximumPatternCommands)
    throw std::invalid_argument("Pattern performance exceeds binding or command limits");
  for(const auto &[track,count]:performance.columns)if(!count||count>maximumEffectColumns)
    throw std::invalid_argument("Use between one and eight FX columns");
  for(const auto &[id,binding]:performance.bindings)if(!id||id>255||binding.plugin.empty()||binding.plugin.size()>128||
      binding.plugin.find('\0')!=std::string::npos||binding.name.size()>1024||binding.name.find('\0')!=std::string::npos)
    throw std::invalid_argument("Invalid stable plugin parameter binding");
  std::set<std::tuple<uint64_t,uint64_t,uint32_t,uint8_t>> cells;
  std::set<uint64_t> pitchTracks;
  for(const auto &command:performance.commands){
    const auto columns=performance.columns.find(command.track);
    const bool parameter=command.kind==PatternCommandKind::ParameterSet||command.kind==PatternCommandKind::ParameterSlide;
    const bool nudge=isNudge(command.kind);
    const bool slide=command.kind==PatternCommandKind::ParameterSlide||command.kind==PatternCommandKind::PitchSlide||nudge;
    const bool cut=command.kind==PatternCommandKind::NoteCut;
    const bool tracker=command.kind==PatternCommandKind::TrackerEffect;
    if(!parameter&&!cut&&!tracker)pitchTracks.insert(command.track);
    if(uint8_t(command.kind)>uint8_t(PatternCommandKind::NudgeReverse)||command.column>=(columns==performance.columns.end()?1:columns->second)||
       !cells.emplace(command.pattern,command.track,command.position/performanceUnitsPerRow,command.column).second||
       !std::isfinite(command.value)||(parameter?(command.value<0||command.value>1||!performance.bindings.contains(command.binding)):
       (command.value< -96||command.value>96||command.binding!=0))||(slide?!command.duration:command.duration!=0)||
       command.pitchRange<1||command.pitchRange>96||((parameter||cut||nudge)&&command.pitchRange!=2)||(nudge&&(command.value<0||command.value>1))||
       ((cut||tracker)&&(command.value!=0||command.binding!=0)) ||
       (tracker && (!command.column || command.position%performanceUnitsPerRow || command.effect>=OpenMPT::MAX_EFFECTS || !s.GetModSpecifications().HasCommand(OpenMPT::EffectCommand(command.effect)))) ||
       (!tracker && (command.effect||command.parameter)))
      throw std::invalid_argument("Invalid or duplicate FX-column command");
  }
  if(pitchTracks.size()>16)throw std::invalid_argument("Use at most 16 tracks with native pitch commands");
  if(preciseNotes.size()>maximumPreciseNotes)throw std::invalid_argument("Use at most 65536 precise note events");
  std::set<std::tuple<uint64_t,uint64_t,uint32_t,bool>> notePositions;
  for(const auto &note:preciseNotes) {
    const bool on=note.note>=1&&note.note<=120;
    if((!on&&note.note!=254&&note.note!=255)||!note.velocity||note.velocity>127||
       (!on&&(note.instrument||note.velocity!=127||note.effect||note.parameter))||note.instrument>255||
       !OpenMPT::NativeNoteEffectSupported(note.effect,note.parameter)||
       !s.GetModSpecifications().HasCommand(OpenMPT::EffectCommand(note.effect))||
       !notePositions.emplace(note.pattern,note.track,note.position,on).second)
      throw std::invalid_argument("Invalid or duplicate precise note event");
  }
  std::vector<uint64_t> graphTargets;
  for(const auto &bus:mixer.buses)graphTargets.push_back(bus.id);
  if(!mixer.active()) {
    for(const auto &[index,track]:tracks)graphTargets.push_back(track.id);
    if(masterID)graphTargets.push_back(masterID);
  }
  std::map<uint64_t,uint32_t> graphPatterns;
  for(const auto &[index,pattern]:patterns)graphPatterns[pattern.id]=s.Patterns[index].GetNumRows();
  std::vector<uint64_t> graphInstruments;for(const auto &[index,instrument]:instruments)graphInstruments.push_back(instrument.id);
  signal.validate(graphTargets,graphPatterns,graphInstruments);
  signal.noteRouting.validate(trackIDs,graphInstruments);
  for(const auto &route:signal.noteRouting.routes)check({route.id,{},{},0});
  for(const auto &chain:mixer.detachedChains)check({chain.id,{},{},0});
  for(const auto &group:signal.groups)check({group.id,{},{},0});
  for(const auto &source:signal.songSources)check({source.node.id,{},{},0});
  signalRoutingGraph(mixer,signal).validate(trackIDs);
  for(const auto &definition:signal.library){check({definition.id,{},{},0});for(const auto &node:definition.nodes)check({node.id,{},{},0});for(const auto &group:definition.groups)check({group.id,{},{},0});}
  for(const auto &e:envelopeBank)check({e.id,e.name,{},0});
  validateEnvelopeBank(*this,s);
  if (bytes() > 16 * 1024 * 1024) throw std::invalid_argument("Native song metadata exceeds 16 MB");
}
bool NativeSong::hasAnnotations() const {
  if (!envelopeBank.empty() || !envelopeLinks.empty() || !signal.empty() || !preciseNotes.empty() || !performance.empty() || !automation.empty() || mixer.active() || !mixer.detached.empty() || !mixer.detachedChains.empty() || !mixer.disconnectedMainInputs.empty() || mixer.masterOutputDisconnected || !noteTracks.empty() || !columnMutes.empty()) return true;
  auto has = [](const NativeEntity &e) { return !e.name.empty() || !e.annotation.empty() || e.color; };
  for (const auto *items : {&patterns, &tracks, &samples, &instruments})
    for (const auto &[index, item] : *items) if (has(item)) return true;
  for (const auto &sequence : sequences) {
    if (has(sequence.info)) return true;
    for (const auto &slot : sequence.orders) if (has(slot)) return true;
  }
  return false;
}
size_t NativeSong::bytes() const {
  size_t n = sizeof(NativeSong) + signal.bytes() + preciseNotes.size()*sizeof(PreciseNote) + performance.bytes() + mixer.bytes() + columnMutes.size() * (sizeof(uint64_t) + sizeof(bool));
  for (const auto &track : noteTracks) n += sizeof(track) + track.columns.size() * sizeof(uint64_t);
  auto add = [&](const NativeEntity &e) { n += sizeof(e) + e.name.size() + e.annotation.size(); };
  for (const auto *items : {&patterns, &tracks, &samples, &instruments})
    for (const auto &[index, item] : *items) add(item);
  for (const auto &sequence : sequences) {
    add(sequence.info);
    for (const auto &slot : sequence.orders) add(slot);
  }
  for (const auto &lane : automation) { n += sizeof(lane) + lane.plugin.size() + lane.points.size() * sizeof(AutomationPoint); for(const auto &p:lane.points) n+=p.formula.bytes(); }
  n+=envelopeLinks.size()*sizeof(EnvelopeLink);
  for(const auto &e:envelopeBank){n+=sizeof(e)+e.name.size()+e.shape.points.size()*sizeof(AutomationPoint);for(const auto &p:e.shape.points)n+=p.formula.bytes();}
  return n;
}
} // namespace Tracker
