#pragma once
#include "common/stdafx.h"
#include "soundlib/Sndfile.h"
#include <map>
#include <string>
#include <vector>
#include "MusicalAutomation.hpp"
#include "MixerGraph.hpp"
#include "PatternPerformance.hpp"
#include "PreciseNotes.hpp"
#include "SignalGraph.hpp"
#include "EnvelopeBank.hpp"

namespace Tracker {
// Stable, project-local identities and UTF-8 metadata. Module playback stays in
// CSoundFile; native features never rely on a mutable order index as identity.
struct NativeEntity {
  uint64_t id = 0;
  std::string name, annotation;
  uint32_t color = 0;
  bool operator==(const NativeEntity &) const = default;
};
struct NativeSequence {
  NativeEntity info;
  std::vector<NativeEntity> orders; // name marks a section beginning at this slot
  bool operator==(const NativeSequence &) const = default;
};
struct NativeNoteTrack {
  uint64_t bus = 0; // Shared processing group; identity/name/color live in the mixer.
  std::vector<uint64_t> columns; // Stable native channel identities, in visual order.
  bool operator==(const NativeNoteTrack &) const = default;
};
// Semantic cable identities survive filtering, grouping and route reordering.
// Fixed rack-chain wires are intentionally absent: deleting them requires a
// detached-processor ownership model, not an implicit reassignment to Master.
enum class SongConnectionKind { Output, Send, GraphInput, GraphOutput, PluginInput, PluginOutput, FollowerInput, Modulation, Note };
struct SongConnectionRef {
  SongConnectionKind kind;
  uint64_t source = 0, target = 0;
  std::string plugin;
  uint32_t port = 0; // Parameter ID for Modulation; audio port otherwise.
  bool preFader = false;
};
struct NativeSong {
  static constexpr uint64_t maximumID = 1000000000000ULL;
  uint64_t nextID = 1;
  // Reserved even before routing is materialized, so graph reads and later
  // unrelated allocations cannot change the identity displayed for Master.
  uint64_t masterID = 0;
  std::map<uint16_t, NativeEntity> patterns, tracks, samples, instruments;
  std::vector<NativeSequence> sequences;
  std::vector<MusicalAutomationLane> automation;
  MixerGraph mixer;
  SignalGraph signal;
  PatternPerformance performance;
  std::vector<PreciseNote> preciseNotes;
  std::vector<NativeNoteTrack> noteTracks;
  std::map<uint64_t, bool> columnMutes; // Overrides; absence preserves imported mute state.
  std::vector<EnvelopeTemplate> envelopeBank;
  std::vector<EnvelopeLink> envelopeLinks;
  NativeEntity makeEntity();
  void reserveMasterIdentity(); // Initial construction/current-format decoding.
  // Materialize the ordinary channel → master topology on the first edit.
  void ensureMixer();
  // Remove structural references when a rack instance is deleted. Musical
  // automation and pattern bindings remain unresolved until Undo restores it.
  void removePluginRoutes(const std::string &instance);
  void reconcile(const OpenMPT::CSoundFile &song);
  void clonePatternAutomation(uint64_t source, uint64_t destination);
  void validate(const OpenMPT::CSoundFile &song) const;
  void prepareEffects(OpenMPT::CSoundFile &song) const; // Control thread only.
  bool hasAnnotations() const;
  size_t bytes() const;
  bool operator==(const NativeSong &) const = default;
};
// Validate the entire batch before replacing song. instrumentPlugins identifies
// live instrument instances whose unrecorded main output defaults to Master.
void removeSongConnections(NativeSong &song, const std::vector<SongConnectionRef> &connections,
                           const std::vector<std::string> &instrumentPlugins = {});
} // namespace Tracker
