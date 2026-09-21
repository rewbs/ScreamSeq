#include "NativeSong.hpp"
#include "soundlib/NativeNoteEffects.h"
#include "soundlib/mod_specifications.h"
#include <set>
#include <stdexcept>

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
void NativeSong::reconcile(const OpenMPT::CSoundFile &s) {
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
  std::erase_if(signal.instrumentAssignments,[&](const auto &a){return std::none_of(instruments.begin(),instruments.end(),[&](const auto &i){return i.second.id==a.target;});});
  std::erase_if(signal.assignments,[&](const auto &a){return !graphTargets.contains(a.target);});
  std::erase_if(signal.inputs,[&](const auto &r){return !graphTargets.contains(r.source)||!graphTargets.contains(r.target);});
  std::erase_if(signal.outputs,[&](const auto &r){return !graphTargets.contains(r.source)||!graphTargets.contains(r.target);});
  std::erase_if(signal.lanes,[&](const auto &a){return !graphTargets.contains(a.first);});
  std::erase_if(signal.commands,[&](const auto &c){
    auto pattern=std::find_if(patterns.begin(),patterns.end(),[&](const auto &p){return p.second.id==c.pattern;});
    return !graphTargets.contains(c.target)||pattern==patterns.end()||uint64_t(c.position)>=uint64_t(s.Patterns[pattern->first].GetNumRows())*65536;
  });
  for(auto &definition:signal.library)for(auto &node:definition.nodes)std::erase_if(node.envelopes,[&](auto &lane){
    const auto pattern=std::find_if(patterns.begin(),patterns.end(),[&](const auto &p){return p.second.id==lane.pattern;});
    if(pattern==patterns.end())return true;
    std::erase_if(lane.points,[&](const auto &point){return point.position>=uint64_t(s.Patterns[pattern->first].GetNumRows())*256;});return lane.points.empty();
  });
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
  for(auto &definition:signal.library)for(auto &node:definition.nodes){
    auto lane=std::find_if(node.envelopes.begin(),node.envelopes.end(),[&](const auto &e){return e.pattern==source;});
    if(lane!=node.envelopes.end()){auto copy=*lane;copy.pattern=destination;node.envelopes.push_back(std::move(copy));}
  }
  for(const auto &link:std::vector<EnvelopeLink>(envelopeLinks))if(link.target.kind==EnvelopeTargetKind::Graph&&link.target.pattern==source){auto l=link;l.target.pattern=destination;envelopeLinks.push_back(l);}
  std::vector<PreciseNote> notes;
  for(const auto &note:preciseNotes)if(note.pattern==source){auto copy=note;copy.pattern=destination;notes.push_back(copy);}
  preciseNotes.insert(preciseNotes.end(),notes.begin(),notes.end());
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
  for (const auto &bus : mixer.buses)
    if (bus.kind != MixerBusKind::Track) check({bus.id, {}, {}, 0});
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
    throw std::invalid_argument("Use between one and eight extra effect subcolumns");
  for(const auto &[id,binding]:performance.bindings)if(!id||id>255||binding.plugin.empty()||binding.plugin.size()>128||
      binding.plugin.find('\0')!=std::string::npos||binding.name.size()>1024||binding.name.find('\0')!=std::string::npos)
    throw std::invalid_argument("Invalid stable plugin parameter binding");
  std::set<std::tuple<uint64_t,uint64_t,uint32_t,uint8_t>> cells;
  std::set<uint64_t> pitchTracks;
  for(const auto &command:performance.commands){
    const auto columns=performance.columns.find(command.track);
    const bool parameter=command.kind==PatternCommandKind::ParameterSet||command.kind==PatternCommandKind::ParameterSlide;
    const bool slide=command.kind==PatternCommandKind::ParameterSlide||command.kind==PatternCommandKind::PitchSlide;
    if(!parameter)pitchTracks.insert(command.track);
    if(uint8_t(command.kind)>uint8_t(PatternCommandKind::PitchSlide)||columns==performance.columns.end()||command.column>=columns->second||
       !cells.emplace(command.pattern,command.track,command.position/performanceUnitsPerRow,command.column).second||
       !std::isfinite(command.value)||(parameter?(command.value<0||command.value>1||!performance.bindings.contains(command.binding)):
       (command.value< -96||command.value>96||command.binding!=0))||(slide?!command.duration:command.duration!=0)||
       command.pitchRange<1||command.pitchRange>96||(parameter&&command.pitchRange!=2))
      throw std::invalid_argument("Invalid or duplicate extra-column pattern command");
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
  std::map<uint64_t,uint32_t> graphPatterns;
  for(const auto &[index,pattern]:patterns)graphPatterns[pattern.id]=s.Patterns[index].GetNumRows();
  std::vector<uint64_t> graphInstruments;for(const auto &[index,instrument]:instruments)graphInstruments.push_back(instrument.id);
  signal.validate(graphTargets,graphPatterns,graphInstruments);
  signalRoutingGraph(mixer,signal).validate(trackIDs);
  for(const auto &definition:signal.library){check({definition.id,{},{},0});for(const auto &node:definition.nodes)check({node.id,{},{},0});}
  for(const auto &e:envelopeBank)check({e.id,e.name,{},0});
  validateEnvelopeBank(*this,s);
  if (bytes() > 16 * 1024 * 1024) throw std::invalid_argument("Native song metadata exceeds 16 MB");
}
bool NativeSong::hasAnnotations() const {
  if (!envelopeBank.empty() || !envelopeLinks.empty() || !signal.empty() || !preciseNotes.empty() || !performance.empty() || !automation.empty() || mixer.active() || !noteTracks.empty() || !columnMutes.empty()) return true;
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
