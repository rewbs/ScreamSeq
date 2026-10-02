#pragma once
#include <cstddef>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include <functional>
#include <optional>
#include "MixerGraph.hpp"
#include "NoteRouting.hpp"
#include "GraphPresentation.hpp"
#include "SignalGroupBypass.hpp"
#include "MusicalAutomation.hpp"

namespace Tracker {
// Graph recipes belong to the document, never to a mutable rack slot. Each use
// instantiates its own processors on the control thread before playback begins.
struct GraphPluginRecipe {
  std::string format = "Built-in", name, path, classID;
  uint32_t type = 0, subtype = 0, manufacturer = 0;
  std::vector<std::byte> state;
  std::vector<uint32_t> inputs, outputs;
  // Native-unit baselines applied after the opaque preset. Stable IDs make
  // control edits publishable without serializing or replacing active DSP.
  std::map<uint32_t,double> parameters;
  // Host bypass belongs to the shared recipe. Every use keeps processing and
  // reveals latency-aligned dry audio; it never replaces the vendor preset.
  bool bypass = false;
  // Logical port identities are pinned to physical bus/channel slices.
  std::string audioLayout;
  bool operator==(const GraphPluginRecipe &) const = default;
};
enum class SignalNodeKind : uint8_t { Input, Output, Plugin, LFO, Follower, Random, NoteEnvelope, MIDI, Amount, Automation };
struct SignalPatternEnvelope {
  uint64_t pattern = 0;
  bool enabled = true;
  std::vector<AutomationPoint> points;
  bool operator==(const SignalPatternEnvelope &) const = default;
};
// Editing one pattern must preserve unrelated envelope order. Re-sorting the
// collection can turn an identical get/set into history and discard pending Redo.
inline void replaceSignalEnvelope(std::vector<SignalPatternEnvelope> &envelopes, SignalPatternEnvelope lane) {
  const auto found=std::find_if(envelopes.begin(),envelopes.end(),[&](const auto &e){return e.pattern==lane.pattern;});
  if(lane.points.empty()) {if(found!=envelopes.end()) envelopes.erase(found);}
  else if(found!=envelopes.end()) *found=std::move(lane);
  else envelopes.push_back(std::move(lane));
}
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
  // Suppress every outgoing contribution while clocks, gates and history advance.
  bool muted = false;
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
  bool quantized = false; // Explicit target-step mode; quantize the final summed value once.
  bool operator==(const SignalModulation &) const = default;
};
// A processing boundary, not a bus or a second processor instance. The flat
// node/edge identities remain authoritative for DSP, automation and ports.
// Nested groups own direct nodes; a node has at most one immediate owner.
struct SignalGroup {
  uint64_t id=0,parent=0;
  std::string name;
  double x=0,y=0;
  std::vector<uint64_t> nodes;
  bool bypass=false;
  std::vector<SignalGroupDryRoute> dryRoutes;
  bool operator==(const SignalGroup &) const = default;
};
struct SignalDefinition {
  uint64_t id = 0;
  uint16_t number = 1;
  std::string name;
  std::vector<SignalNode> nodes;
  std::vector<SignalAudioEdge> audio;
  std::vector<SignalModulation> modulation;
  std::vector<SignalGroup> groups;
  SignalPresentation presentation;
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
// Song-level boundaries retain rack instance identities. They never own or
// replace processors; the mixer remains the authoritative audio topology.
struct SignalSongGroup {
  uint64_t id=0,parent=0;
  std::string name;
  double x=0,y=0;
  std::vector<std::string> nodes; // canonical "plugin:<instance ID>" or "source:n<ID>" keys
  bool bypass=false;
  std::vector<SignalSongGroupDryRoute> dryRoutes;
  bool operator==(const SignalSongGroup &) const = default;
};
// Song-level controls target existing rack instances; they never turn a rack
// processor into a recipe or replace its stable parameter identity/state.
struct SignalSongSource {
  SignalNode node;
  // Follower input is either a mixer bus tap or a processor output. An empty
  // input is a disconnected, silent source. Bus taps can be pre/post fader.
  uint64_t audioBus=0;
  std::string audioPlugin;
  uint32_t output=0;
  bool preFader=false;
  // Note sources can follow one channel or instrument; both zero means all
  // notes. These are stable document identities, never mutable slot numbers.
  uint64_t noteTarget=0,noteInstrument=0;
  double amount=1;
  bool operator==(const SignalSongSource &) const = default;
};
struct SignalSongModulation {
  uint64_t source=0;
  std::string plugin;
  uint32_t parameter=0;
  // Add normalized contributions to the host's existing manual/automation
  // baseline, then clamp once. A new connection starts at zero depth.
  double minimum=0,maximum=0;
  bool enabled=true,quantized=false;
  bool operator==(const SignalSongModulation &) const = default;
};
struct SignalGraph {
  NoteRouting noteRouting;
  std::vector<SignalDefinition> library;
  std::vector<SignalAssignment> assignments;
  std::vector<SignalAssignment> instrumentAssignments; // target is stable instrument identity.
  std::vector<SignalCommand> commands;
  std::map<uint64_t, uint8_t> lanes;
  std::map<std::string,std::array<double,2>> layout;
  std::vector<SignalInputRoute> inputs;
  std::vector<SignalOutputRoute> outputs;
  std::vector<SignalSongGroup> groups;
  std::vector<SignalSongSource> songSources;
  std::vector<SignalSongModulation> songModulation;
  SignalPresentation presentation;
  bool operator==(const SignalGraph &) const = default;
  bool empty() const { return noteRouting.empty() && library.empty() && instrumentAssignments.empty() && assignments.empty() && commands.empty() && lanes.empty() && layout.empty() && inputs.empty() && outputs.empty() && groups.empty() && songSources.empty() && songModulation.empty() && presentation.empty(); }
  size_t bytes() const;
  // Callers supply stable song identities, not slot numbers.
  void validate(const std::vector<uint64_t> &targets,
                const std::map<uint64_t, uint32_t> &patternRows, const std::vector<uint64_t> &instruments = {}) const;
};
void validateSongSignalGroups(const SignalGraph &);
void groupSongSignalNodes(SignalGraph &,const std::vector<std::string> &nodes,const std::vector<uint64_t> &groups,uint64_t id,uint64_t parent,std::string name);
void ungroupSongSignalNodes(SignalGraph &,uint64_t id);
void moveSongSignalGroup(SignalGraph &,uint64_t id,double x,double y);
std::vector<std::string> songSignalGroupNodes(const SignalGraph &,uint64_t id);
void pruneSongSignalGroups(SignalGraph &,const std::vector<std::string> &available);
// Save a contiguous rack chain as an independent recipe. Active auxiliary
// ports are exposed explicitly; the original rack/group remains untouched.
SignalDefinition extractSongSignalGroup(const SignalGraph &,const MixerGraph &,uint64_t group,
    const std::vector<std::pair<std::string,GraphPluginRecipe>> &effects,
    const std::function<uint64_t()> &allocate);
// Move a selected serial effect chain onto an audio edge, healing its old
// main path. Ambiguous branches and cycles fail without changing the recipe.
void insertSignalNodes(SignalDefinition &, const std::vector<uint64_t> &, size_t edge);
// Pull a serial main path out, joining its sole predecessor and successor.
// Auxiliary/modulation cables survive detachment; remove also deletes nodes
// and all of their remaining connections. Ambiguity leaves the model intact.
struct SignalHealPath {std::optional<size_t> incoming,outgoing;};
void detachSignalNodes(SignalDefinition &, const std::vector<uint64_t> &, bool remove = false,std::optional<SignalHealPath> heal = {});
// Selection consists of sibling nodes and/or nested boundaries. Both edits
// preserve every processor, edge and binding and are atomic on rejection.
void groupSignalNodes(SignalDefinition &, const std::vector<uint64_t> &, uint64_t id, uint64_t parent, std::string name);
void ungroupSignalNodes(SignalDefinition &, uint64_t id);
void pruneSignalGroups(SignalDefinition &);
void reconcileSignalPresentation(SignalDefinition &,const SignalDefinition &previous);
// Move the boundary and all descendants together without changing DSP.
void moveSignalGroup(SignalDefinition &, uint64_t id, double x, double y);
// Extract a boundary as a standalone recipe. Caller allocates input/output
// identities and remaps all remaining identities before publishing the copy.
SignalDefinition extractSignalGroup(const SignalDefinition &, uint64_t group, uint64_t input, uint64_t output);
// Layout and labels do not invalidate prepared audio processing.
bool sameSignalProcessing(const SignalGraph &, const SignalGraph &);
// Control-thread classification: only parameter baselines may differ.
bool sameSignalParameterLayout(SignalGraph, SignalGraph);
// Fixed topology with changed source/envelope/depth/cable gain controls.
bool sameSignalControlLayout(SignalGraph, SignalGraph);
// Retain audible processing and existing note scope; allow prepared source,
// follower-tap and modulation changes without rebuilding vendor processors.
bool sameSignalSourceLayout(SignalGraph, SignalGraph);
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
