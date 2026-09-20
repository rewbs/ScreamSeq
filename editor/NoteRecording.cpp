#include "NoteRecording.hpp"
#include <set>
namespace Tracker {
NoteRecording::NoteRecording(const NativeSong &native,const OpenMPT::CSoundFile &song,std::vector<uint16_t> channels,uint16_t instrument,uint32_t quantum)
 :channels_(std::move(channels)),instrument_(instrument),sequence_(song.Order.GetCurrentSequenceIndex()),quantum_(quantum) {
  std::set<uint16_t> unique(channels_.begin(),channels_.end());
  if(channels_.empty()||channels_.size()>127||unique.size()!=channels_.size()||instrument>255||quantum>performanceUnitsPerRow)
    throw std::invalid_argument("Recording needs distinct note columns, an instrument and a quantization of at most one row");
  for(auto channel:channels_)if(!native.tracks.contains(channel))throw std::invalid_argument("Recording column does not exist");
  for(const auto &[index,pattern]:native.patterns){patterns_[index]=pattern.id;lengths_[index]=song.Patterns[index].GetNumRows()*performanceUnitsPerRow;}
  for(const auto &[index,track]:native.tracks)tracks_[index]=track.id;
  events.reserve(maximumPreciseNotes);
}
RecordedPosition NoteRecording::quantized(RecordedPosition p) const {
  if(quantum_)p.position=uint32_t(std::min<uint64_t>(UINT32_MAX,((uint64_t(p.position)+quantum_/2)/quantum_)*quantum_));
  p.position=std::min(p.position,lengths_.at(uint16_t(p.pattern))-1);return p;
}
void NoteRecording::append(RecordedPosition p,uint16_t channel,uint8_t note,uint8_t velocity) {
  if(events.size()>=maximumPreciseNotes){++overflow;return;}
  events.push_back({patterns_.at(uint16_t(p.pattern)),tracks_.at(channel),p.position,uint16_t(note<128?instrument_:0),note,uint8_t(note<128?velocity:127)});
}
void NoteRecording::capture(RecordedPosition position,uint8_t status,uint8_t note,uint8_t velocity) {
  if(!capturing)return;
  if(position.sequence!=sequence_||!patterns_.contains(uint16_t(position.pattern))||position.pattern>UINT16_MAX){++missingTime;return;}
  auto p=quantized(position);last_=p;
  const uint8_t type=status&0xf0;const auto key=size_t(status&15)*128+(note&127);
  if(type==0xb0){if(note==120||note==123)for(size_t i=size_t(status&15)*128;i<size_t((status&15)+1)*128;++i)
    if(held_[i].channel>=0){append(p,uint16_t(held_[i].channel),note==120?254:255,127);held_[i].channel=-1;}return;}
  if(type!=0x80&&type!=0x90)return;
  auto &held=held_[key];const bool on=type==0x90&&velocity;
  if(held.channel>=0) {
    if(p.order==held.start.order&&p.pattern==held.start.pattern&&p.position<=held.start.position)
      p.position=std::min(held.start.position+1,lengths_.at(uint16_t(p.pattern))-1);
    append(p,uint16_t(held.channel),255,127);held.channel=-1;
  }
  if(!on)return;
  if(events.size()+129>=maximumPreciseNotes){++overflow;return;}
  // Leave room for a final release, even when quantization lands on the end.
  p.position=std::min(p.position,lengths_.at(uint16_t(p.pattern))-2);
  // Core note range is C-0..B-9. Never collapse higher MIDI notes onto B-9.
  if(note>=120){++exhaustedVoices;return;}
  auto free=std::find_if(channels_.begin(),channels_.end(),[&](auto channel){return std::none_of(held_.begin(),held_.end(),[&](const auto &h){return h.channel==channel;});});
  if(free==channels_.end()&&channels_.size()==1) {
    for(auto &previous:held_)if(previous.channel==channels_[0]) {
      if(p.order==previous.start.order&&p.pattern==previous.start.pattern&&p.position<=previous.start.position)
        p.position=std::min(previous.start.position+1,lengths_.at(uint16_t(p.pattern))-2);
      append(p,uint16_t(previous.channel),255,127);previous.channel=-1;
    }
    free=channels_.begin();
  }
  if(free==channels_.end()){++exhaustedVoices;return;}
  append(p,*free,uint8_t(note+1),velocity);held={int(*free),p};
}
void NoteRecording::stop(std::optional<RecordedPosition> position) {
  if(!capturing)return;
  capturing=false;
  if(position&&position->sequence==sequence_&&position->pattern<=UINT16_MAX&&patterns_.contains(uint16_t(position->pattern)))last_=quantized(*position);
  if(!last_)return;
  for(auto &held:held_)if(held.channel>=0){auto p=*last_;
    if(p.order==held.start.order&&p.pattern==held.start.pattern&&p.position<=held.start.position)
      p.position=std::min(held.start.position+1,lengths_.at(uint16_t(p.pattern))-1);
    append(p,uint16_t(held.channel),255,127);held.channel=-1;}
}
}
