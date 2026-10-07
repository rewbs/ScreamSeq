#pragma once
#include "NativeSong.hpp"
#include "RecordingClock.hpp"
#include "TrackerDocument.hpp"
#include <array>
namespace Tracker {
struct NoteRecordingCommit {
  NativeSong native;
  std::vector<Edit> edits;
  bool changed=false;
};
// Prepare and validate the complete merge without changing music or history.
NoteRecordingCommit prepareNoteRecordingCommit(const Document &,std::span<const PreciseNote>,bool replaceRows);
class NoteRecording {
  struct Held {int channel=-1;RecordedPosition start;};
  std::array<Held,2048> held_{};
  std::vector<uint16_t> channels_;
  std::map<uint16_t,uint32_t> lengths_;
  std::map<uint16_t,uint64_t> patterns_,tracks_;
  uint16_t instrument_=0,sequence_=0;
  uint32_t quantum_=0;
  std::optional<RecordedPosition> last_;
  void append(RecordedPosition,uint16_t,uint8_t,uint8_t);
  RecordedPosition quantized(RecordedPosition) const;
public:
  std::vector<PreciseNote> events;
  std::string inputError; // Optional retained input-loss explanation, not musical data.
  uint32_t missingTime=0,exhaustedVoices=0,overflow=0;
  bool capturing=true;
  NoteRecording(const NativeSong &,const OpenMPT::CSoundFile &,std::vector<uint16_t>,uint16_t,uint32_t);
  bool hasHeldNotes() const noexcept {for(const auto &note:held_)if(note.channel>=0)return true;return false;}
  void capture(RecordedPosition,uint8_t status,uint8_t note,uint8_t velocity);
  void stop(std::optional<RecordedPosition> position={});
};
}
