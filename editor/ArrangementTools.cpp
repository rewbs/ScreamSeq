#include "ArrangementTools.hpp"
#include "soundlib/mod_specifications.h"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
namespace Tracker {
namespace {
using FxKey = std::tuple<uint64_t,uint32_t,uint8_t>; // track, row, column
using NoteKey = std::tuple<uint64_t,uint32_t,bool>; // track, position, onset
FxKey fxKey(const PatternCommand &c) {return {c.track,c.position/performanceUnitsPerRow,c.column};}
NoteKey noteKey(const PreciseNote &n) {return {n.track,n.position,n.note>=1&&n.note<=120};}
void replaceCommands(NativeSong &native,uint64_t pattern,std::vector<PatternCommand> replacement) {
  const auto key=[](const PatternCommand &c) {return std::tuple(c.pattern,c.track,c.position,c.column,c.kind,c.duration,c.binding,c.value,c.pitchRange,c.effect,c.parameter);};
  const auto less=[&](const auto &a,const auto &b){return key(a)<key(b);};
  std::vector<PatternCommand> before;
  for(const auto &c:native.performance.commands)if(c.pattern==pattern)before.push_back(c);
  std::sort(before.begin(),before.end(),less);std::sort(replacement.begin(),replacement.end(),less);
  if(before==replacement)return; // Preserve all original ordering on a musical no-op.
  std::erase_if(native.performance.commands,[&](const auto &c){return c.pattern==pattern;});
  native.performance.commands.insert(native.performance.commands.end(),replacement.begin(),replacement.end());
}
void applyPrepared(Document &doc,const ArrangementCopyPlan &plan) {
  doc.transaction([&](OpenMPT::CSoundFile &s,NativeSong &native) {
    if(plan.clone) {
      const auto rows=s.Patterns[plan.originalPattern].GetNumRows();
      if(!s.Patterns.Insert(plan.targetPattern,rows))throw std::runtime_error("Could not allocate independent pattern");
      // Keep exact imported signature/swing/name/color; do not normalize again.
      s.Patterns[plan.targetPattern]=s.Patterns[plan.originalPattern];
      s.Order()[plan.targetOrder]=plan.targetPattern;
    }
    native=*plan.native;
    for(const auto &edit:plan.edits)Document::put(s,edit);
  });
}
}
ArrangementMatrixSummary summarizeArrangement(const Document &doc,const ArrangementMatrixRange &range) {
  const auto &song=doc.song();const auto &native=doc.native();
  const uint32_t totalOrders=song.Order().size(),totalChannels=song.GetNumChannels();
  if(range.startOrder>totalOrders||!range.orderCount||range.orderCount>128)
    throw std::invalid_argument("Choose up to 128 orders inside the current sequence");
  if(range.startChannel>=totalChannels)throw std::invalid_argument("Matrix tracks are outside the song");
  const auto channels=range.channelCount.value_or(std::min<uint32_t>(16,totalChannels-range.startChannel));
  if(!channels||channels>32||channels>totalChannels-range.startChannel)
    throw std::invalid_argument("Choose between one and 32 tracks inside the song");
  ArrangementMatrixSummary result;
  result.sequence=song.Order.GetCurrentSequenceIndex();result.totalOrders=totalOrders;result.totalChannels=totalChannels;
  result.startOrder=range.startOrder;result.startChannel=range.startChannel;result.channelCount=channels;
  const auto count=std::min(range.orderCount,totalOrders-range.startOrder);
  result.orders.reserve(count);result.patterns.reserve(count);
  if(!count)return result;
  std::map<uint64_t,uint32_t> patternIndices,trackIndices;
  for(uint32_t i=0;i<channels;++i)trackIndices.emplace(native.tracks.at(range.startChannel+i).id,i);
  for(uint32_t order=range.startOrder;order<range.startOrder+count;++order) {
    const auto pattern=song.Order()[OpenMPT::ORDERINDEX(order)];
    ArrangementOrderDensity item{order,pattern};
    if(song.Patterns.IsValidPat(pattern)) {
      const auto id=native.patterns.at(pattern).id;
      const auto [at,inserted]=patternIndices.try_emplace(id,uint32_t(result.patterns.size()));
      item.summaryIndex=at->second;
      if(inserted) {
        auto &summary=result.patterns.emplace_back();summary.pattern=pattern;summary.patternID=id;summary.rows=song.Patterns[pattern].GetNumRows();
        summary.blocks.reserve(channels);
        for(uint32_t i=0;i<channels;++i) {
          auto &block=summary.blocks.emplace_back();block.channel=range.startChannel+i;block.trackID=native.tracks.at(block.channel).id;
          for(uint32_t row=0;row<summary.rows;++row) {
            const auto cell=doc.cell(pattern,uint16_t(row),uint16_t(block.channel));
            if(cell!=Cell{}){++block.events;++block.trackerEvents;++block.bins[uint64_t(row)*16/summary.rows];}
            if(cell.note>=OpenMPT::NOTE_MIN&&cell.note<=OpenMPT::NOTE_MAX)++block.notes;
          }
        }
      }
    }
    result.orders.push_back(item);
  }
  if(result.patterns.empty())return result;
  const auto addNative=[&](uint64_t pattern,uint64_t track,uint32_t position,bool precise,bool onset) {
    const auto p=patternIndices.find(pattern),t=trackIndices.find(track);
    if(p==patternIndices.end()||t==trackIndices.end())return;
    auto &summary=result.patterns[p->second];const auto end=uint64_t(summary.rows)*performanceUnitsPerRow;
    if(position>=end)throw std::invalid_argument("Native matrix event is outside its pattern");
    auto &block=summary.blocks[t->second];++block.events;
    if(precise)++block.preciseEvents;else ++block.nativeFxEvents;
    if(onset)++block.notes;
    ++block.bins[uint64_t(position)*16/end];
  };
  for(const auto &note:native.preciseNotes)addNative(note.pattern,note.track,note.position,true,note.note>=1&&note.note<=120);
  for(const auto &command:native.performance.commands)addNative(command.pattern,command.track,command.position,false,false);
  return result;
}
ArrangementCopyPlan prepareArrangementCopy(Document &doc,const ArrangementCopy &copy,
    const std::function<void(Document &)> &validateCandidate) {
  const auto &s=doc.song();const auto &orders=s.Order();const auto &original=doc.native();
  if(copy.sourceOrder>=orders.size()||copy.targetOrder>=orders.size())throw std::invalid_argument("Choose source and destination orders in the current sequence");
  const auto source=orders[copy.sourceOrder],destination=orders[copy.targetOrder];
  if(!s.Patterns.IsValidPat(source)||!s.Patterns.IsValidPat(destination))throw std::invalid_argument("End and skip orders do not contain pattern blocks");
  if(!copy.channels||size_t(copy.sourceChannel)+copy.channels>s.GetNumChannels()||size_t(copy.targetChannel)+copy.channels>s.GetNumChannels())throw std::invalid_argument("Block channels are outside the song");
  const auto rows=s.Patterns[source].GetNumRows(),targetRows=s.Patterns[destination].GetNumRows();
  if(rows!=targetRows&&!copy.clip)throw std::invalid_argument("Pattern lengths differ; enable clipping to copy their overlapping rows");
  const auto count=std::min(rows,targetRows);const auto end=uint32_t(count)*performanceUnitsPerRow;
  if(size_t(count)*copy.channels>maximumPatternToolCells)throw std::invalid_argument("Block copy exceeds the pattern tool cell limit");
  std::vector<Cell> cells;cells.reserve(size_t(count)*copy.channels);
  for(uint16_t r=0;r<count;++r)for(uint16_t c=0;c<copy.channels;++c)cells.push_back(doc.cell(source,r,copy.sourceChannel+c));
  ArrangementCopyPlan plan{doc.revision,s.Order.GetCurrentSequenceIndex(),copy.targetOrder,destination,destination};
  plan.owner=&doc;plan.orderID=original.sequences.at(plan.sequence).orders.at(copy.targetOrder).id;
  plan.originalPatternID=original.patterns.at(destination).id;
  auto pasted=preparePatternPaste(doc,destination,0,copy.targetChannel,count,copy.channels,cells,PatternAll,copy.mode,false);
  // An exact physical span already contains every source layer. In particular,
  // do not normalize legal imported legacy/native FX 1 coexistence or clone an
  // alias merely because the caller selected a different order occurrence.
  // Paste preparation above still validates the requested mode and cells.
  if(source==destination&&copy.sourceChannel==copy.targetChannel)return plan;
  const auto sourceID=original.patterns.at(source).id,destinationID=plan.originalPatternID;
  std::map<uint64_t,uint64_t> channels;std::set<uint64_t> targetTracks;
  for(uint16_t c=0;c<copy.channels;++c) {
    channels[original.tracks.at(copy.sourceChannel+c).id]=original.tracks.at(copy.targetChannel+c).id;
    targetTracks.insert(original.tracks.at(copy.targetChannel+c).id);
  }
  // Capture every source before touching a destination: ranges may overlap in
  // the same pattern and precise/FX vector order is not musical identity.
  std::map<FxKey,PatternCommand> sourceFx,destinationFx;
  for(const auto &command:original.performance.commands) {
    if(command.pattern==destinationID)destinationFx.emplace(fxKey(command),command);
    if(command.pattern==sourceID&&command.position<end&&channels.contains(command.track)) {
      auto c=command;c.pattern=destinationID;c.track=channels.at(command.track);
      c.duration=std::min(c.duration,end-c.position);sourceFx.emplace(fxKey(c),c);
    }
  }
  std::map<NoteKey,PreciseNote> sourceNotes,destinationNotes;
  for(const auto &note:original.preciseNotes) {
    if(note.pattern==destinationID)destinationNotes.emplace(noteKey(note),note);
    if(note.pattern==sourceID&&note.position<end&&channels.contains(note.track)) {
      auto n=note;n.pattern=destinationID;n.track=channels.at(note.track);sourceNotes.emplace(noteKey(n),n);
    }
  }
  std::map<std::pair<uint16_t,uint16_t>,Edit> edits;
  for(const auto &edit:pasted)edits[{edit.row,edit.channel}]=edit;
  auto next=original;PrimaryEffectCells replaced;
  std::vector<PatternCommand> primary;
  for(uint16_t row=0;row<count;++row)for(uint16_t channel=0;channel<copy.channels;++channel) {
    const auto target=uint16_t(copy.targetChannel+channel);const auto track=original.tracks.at(target).id;
    const auto before=doc.cell(destination,row,target),src=cells[size_t(row)*copy.channels+channel];
    const FxKey key{track,row,0};const auto sourceCommand=sourceFx.find(key);
    const bool sourceOccupied=sourceCommand!=sourceFx.end()||src.effect||src.parameter;
    const bool targetOccupied=destinationFx.contains(key)||before.effect||before.parameter;
    const bool accept=copy.mode=="overwrite"||(sourceOccupied&&(copy.mode=="merge"||!targetOccupied));
    auto at=edits.try_emplace({row,target},Edit{destination,row,target,before,before}).first;auto &after=at->second.after;
    if(accept) {
      replaced.emplace(destinationID,track,row);
      if(sourceCommand!=sourceFx.end()) {
        if(OpenMPT::ModCommand::IsPcNote(after.note))throw std::invalid_argument("Replace the imported parameter-control note before pasting precise FX 1");
        after.effect=after.parameter=0;primary.push_back(sourceCommand->second);
      } else {after.effect=src.effect;after.parameter=src.parameter;}
    } else {after.effect=before.effect;after.parameter=before.parameter;}
  }
  next.clearPrimaryEffects(replaced);
  std::map<FxKey,PatternCommand> effects;
  for(const auto &c:next.performance.commands)if(c.pattern==destinationID)effects.emplace(fxKey(c),c);
  if(copy.mode=="overwrite")std::erase_if(effects,[&](const auto &entry){const auto &c=entry.second;return c.column&&targetTracks.contains(c.track)&&c.position<end;});
  for(const auto &c:primary)effects[fxKey(c)]=c;
  for(const auto &[key,c]:sourceFx)if(c.column) {
    if(copy.mode=="mix"&&destinationFx.contains(key))continue;
    effects[key]=c;
    const auto previous=next.performance.columns.contains(c.track)?next.performance.columns.at(c.track):uint8_t(1);
    if(c.column+1>previous)next.performance.columns[c.track]=uint8_t(c.column+1);
  }
  std::vector<PatternCommand> commands;commands.reserve(effects.size());for(const auto &[key,c]:effects)commands.push_back(c);
  next.performance.commands=original.performance.commands;
  replaceCommands(next,destinationID,std::move(commands));
  if(copy.mode=="overwrite")std::erase_if(destinationNotes,[&](const auto &entry){return targetTracks.contains(entry.second.track)&&entry.second.position<end;});
  for(const auto &[key,n]:sourceNotes) {
    if(copy.mode=="mix")destinationNotes.try_emplace(key,n);else destinationNotes[key]=n;
  }
  std::vector<PreciseNote> notes;notes.reserve(destinationNotes.size());for(const auto &[key,n]:destinationNotes)notes.push_back(n);
  replacePreciseNotesForPattern(next.preciseNotes,destinationID,std::move(notes));
  for(const auto &[key,edit]:edits)if(edit.before!=edit.after)plan.edits.push_back(edit);
  doc.validateEdits(plan.edits);
  if(plan.edits.empty()&&next==original)return plan;
  size_t uses=0;for(OpenMPT::SEQUENCEINDEX seq=0;seq<s.Order.GetNumSequences();++seq)for(auto p:s.Order(seq))if(p==destination)++uses;
  if(copy.makeUnique&&uses>1) {
    size_t unused=0;while(s.Patterns.IsValidPat(OpenMPT::PATTERNINDEX(unused)))++unused;
    if(unused>=s.GetModSpecifications().patternsMax)throw std::invalid_argument("No free pattern slot for an independent copy");
    plan.clone=true;plan.targetPattern=uint16_t(unused);
    auto cloned=original;auto entity=original.patterns.at(destination);entity.id=cloned.makeEntity().id;
    cloned.clonePatternAutomation(destinationID,entity.id);cloned.patterns[plan.targetPattern]=entity;
    cloned.performance.columns=next.performance.columns;
    std::vector<PatternCommand> fx;for(auto c:next.performance.commands)if(c.pattern==destinationID){c.pattern=entity.id;fx.push_back(c);}
    replaceCommands(cloned,entity.id,std::move(fx));
    std::vector<PreciseNote> precise;for(auto n:next.preciseNotes)if(n.pattern==destinationID){n.pattern=entity.id;precise.push_back(n);}
    replacePreciseNotesForPattern(cloned.preciseNotes,entity.id,std::move(precise));
    next=std::move(cloned);for(auto &edit:plan.edits)edit.pattern=plan.targetPattern;
  }
  plan.native=std::move(next);
  // Validate complete prospective metadata (including clone capacities and
  // links) against the exact prospective engine state before a host stops.
  Document candidate(doc.snapshotData());candidate.restoreNative(original);
  candidate.revision=doc.revision;
  candidate.song().Order.SetSequence(plan.sequence);applyPrepared(candidate,plan);
  plan.native=candidate.native();
  if(validateCandidate)validateCandidate(candidate);
  return plan;
}
void applyArrangementCopy(Document &doc,const ArrangementCopyPlan &plan) {
  if(plan.owner!=&doc||doc.revision!=plan.revision||doc.song().Order.GetCurrentSequenceIndex()!=plan.sequence||
     plan.targetOrder>=doc.song().Order().size()||doc.song().Order()[plan.targetOrder]!=plan.originalPattern||
     doc.native().sequences.at(plan.sequence).orders.at(plan.targetOrder).id!=plan.orderID||
     doc.native().patterns.at(plan.originalPattern).id!=plan.originalPatternID)
    throw std::invalid_argument("Song changed since the block copy was prepared");
  if(!plan.changed())return;
  applyPrepared(doc,plan);
}
} // namespace Tracker
