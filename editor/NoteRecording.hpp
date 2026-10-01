#pragma once
#include "NativeSong.hpp"
#include "RecordingClock.hpp"
#include <array>
namespace Tracker {
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
  uint32_t missingTime=0,exhaustedVoices=0,overflow=0;
  bool capturing=true;
  NoteRecording(const NativeSong &,const OpenMPT::CSoundFile &,std::vector<uint16_t>,uint16_t,uint32_t);
  void capture(RecordedPosition,uint8_t status,uint8_t note,uint8_t velocity);
  void stop(std::optional<RecordedPosition> position={});
};
}
