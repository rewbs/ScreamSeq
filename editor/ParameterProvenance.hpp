#pragma once
#include "NativeSong.hpp"
#include <optional>

namespace Tracker {
// Read-only control-thread projection. These are references to existing edits,
// never an additional automation lane or an additive modulation connection.
enum class ParameterProvenanceKind : uint8_t { Envelope, PatternCommands, Recorded };
struct ParameterProvenanceCommand {
  PatternCommandKind kind=PatternCommandKind::ParameterSet;
  uint32_t position=0,duration=0;
  double value=0;
};
struct ParameterProvenanceSource {
  ParameterProvenanceKind kind=ParameterProvenanceKind::Envelope;
  std::string key,plugin;
  uint32_t parameter=0,patternIndex=UINT32_MAX,channel=UINT32_MAX;
  uint64_t lane=0,pattern=0,track=0;
  uint16_t binding=0;
  uint8_t column=0;
  uint32_t position=0,endPosition=0,count=0,omittedCommands=0,omittedOrders=0;
  uint64_t firstFrame=0,lastFrame=0;
  bool enabled=false;
  std::vector<std::pair<uint16_t,uint32_t>> orders;
  std::vector<ParameterProvenanceCommand> commands;
};
struct ParameterProvenanceRecording {
  uint32_t count=0;
  uint64_t firstFrame=0,lastFrame=0; // Canonical 48-kHz song time, not device time.
};
struct ParameterProvenancePage {
  std::vector<ParameterProvenanceSource> sources;
  uint32_t total=0,offset=0;
};
ParameterProvenancePage parameterProvenance(const NativeSong &,const OpenMPT::CSoundFile &,
  const std::string &plugin,uint32_t parameter,std::optional<uint16_t> pattern={},
  ParameterProvenanceRecording recorded={},uint32_t offset=0,uint32_t limit=128);
}
