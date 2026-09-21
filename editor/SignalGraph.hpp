#pragma once
#include <cstddef>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "MixerGraph.hpp"
#include "MusicalAutomation.hpp"

namespace Tracker {
// Graph recipes belong to the document, never to a mutable rack slot. Each use
// instantiates its own processors on the control thread before playback begins.
struct GraphPluginRecipe {
  std::string format = "Built-in", name, path, classID;
  uint32_t type = 0, subtype = 0, manufacturer = 0;
  std::vector<std::byte> state;
  std::vector<uint32_t> inputs, outputs;
  bool operator==(const GraphPluginRecipe &) const = default;
};
enum class SignalNodeKind : uint8_t { Input, Output, Plugin, LFO, Follower, Random, NoteEnvelope, MIDI, Amount, Automation };
struct SignalPatternEnvelope {
  uint64_t pattern = 0;
  bool enabled = true;
  std::vector<AutomationPoint> points;
  bool operator==(const SignalPatternEnvelope &) const = default;
};
struct SignalNode {
  uint64_t id = 0;
  SignalNodeKind kind = SignalNodeKind::Plugin;
  std::string name;
  double x = 0, y = 0;
  GraphPluginRecipe plugin;
  // LFO/random rate is cycles per beat. Envelope attack/release use seconds.
  // MIDI controller 0..127, note envelope/follower attack and release.
  double rate = 1, phase = 0, attack = .01, release = .1;
  uint32_t controller = 1;
  std::vector<SignalPatternEnvelope> envelopes;
  bool operator==(const SignalNode &) const = default;
};
struct SignalAudioEdge {
  uint64_t source = 0, target = 0;
  uint32_t output = 0, input = 0;
  double gain = 1;
  bool operator==(const SignalAudioEdge &) const = default;
};
struct SignalModulation {
  uint64_t source = 0, target = 0;
  uint32_t parameter = 0;
  // Parameter values are normalized. Multiple sources add to the base, then
  // clamp once. Minimum/maximum can descend, enabling inverse modulation.
  double minimum = 0, maximum = 1, base = 0;
  bool enabled = true;
  bool operator==(const SignalModulation &) const = default;
};
struct SignalDefinition {
  uint64_t id = 0;
  uint16_t number = 1;
  std::string name;
  std::vector<SignalNode> nodes;
  std::vector<SignalAudioEdge> audio;
  std::vector<SignalModulation> modulation;
  bool operator==(const SignalDefinition &) const = default;
  size_t bytes() const;
};
struct SignalAssignment {
  uint64_t target = 0, graph = 0; // Stable mixer bus identity.
  double amount = 1, wet = 1;
  bool operator==(const SignalAssignment &) const = default;
};
enum class SignalCommandKind : uint8_t { Row, Start, Stop, Clear, Amount, Wet };
struct SignalCommand {
  uint64_t pattern = 0, target = 0, graph = 0;
  uint32_t position = 0; // 65536 units per row, like precise notes.
  uint8_t column = 0; // Dedicated graph lane, including group buses.
  SignalCommandKind kind = SignalCommandKind::Row;
  double amount = 1, wet = 1;
  bool tails = false;
  bool operator==(const SignalCommand &) const = default;
};
struct SignalInputRoute {
  uint64_t source=0,target=0;uint32_t input=1;double gainDB=0;bool preFader=false;
  bool operator==(const SignalInputRoute &) const = default;
};
struct SignalOutputRoute {
  uint64_t source=0,target=0;uint32_t output=1;
  bool operator==(const SignalOutputRoute &) const = default;
};
// Read-only playback observation. Order is one-based across the active stack;
// role is row=0, persistent=1, ordinary=2. Inactive tails have order zero.
struct SignalActivity { uint64_t target=0,graph=0; uint8_t role=0; uint16_t order=0; bool tail=false; uint64_t instrument=0; };
struct SignalGraph {
  std::vector<SignalDefinition> library;
  std::vector<SignalAssignment> assignments;
  std::vector<SignalAssignment> instrumentAssignments; // target is stable instrument identity.
  std::vector<SignalCommand> commands;
  std::map<uint64_t, uint8_t> lanes;
  std::map<std::string,std::array<double,2>> layout;
  std::vector<SignalInputRoute> inputs;
  std::vector<SignalOutputRoute> outputs;
  bool operator==(const SignalGraph &) const = default;
  bool empty() const { return library.empty() && instrumentAssignments.empty() && assignments.empty() && commands.empty() && lanes.empty() && layout.empty() && inputs.empty() && outputs.empty(); }
  size_t bytes() const;
  // Callers supply stable song identities, not slot numbers.
  void validate(const std::vector<uint64_t> &targets,
                const std::map<uint64_t, uint32_t> &patternRows, const std::vector<uint64_t> &instruments = {}) const;
};
// Layout and labels do not invalidate prepared audio processing.
bool sameSignalProcessing(const SignalGraph &, const SignalGraph &);
std::string signalBusIdentity(uint64_t);
MixerGraph signalRoutingGraph(MixerGraph,const SignalGraph &);
struct SignalProcessorInfo {
  uint64_t node = 0;
  uint32_t latency = 0;
  uint64_t inputs = 1, outputs = 1;
};
struct SignalEdgePlan { size_t source = 0, target = 0; uint32_t delay = 0; };
struct SignalPlan {
  std::vector<size_t> order;
  std::vector<SignalEdgePlan> edges;
  std::vector<uint32_t> arrival, latency;
  size_t input = 0, output = 0;
  uint32_t totalLatency = 0;
};
// Includes modulation dependencies. Rejects implicit feedback (including a
// follower fed by a processor it modulates), missing ports and excessive delay.
SignalPlan compileSignal(const SignalDefinition &, const std::vector<SignalProcessorInfo> & = {});
} // namespace Tracker
