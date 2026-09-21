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
struct NativeSong {
  static constexpr uint64_t maximumID = 1000000000000ULL;
  uint64_t nextID = 1;
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
  void reconcile(const OpenMPT::CSoundFile &song);
  void clonePatternAutomation(uint64_t source, uint64_t destination);
  void validate(const OpenMPT::CSoundFile &song) const;
  bool hasAnnotations() const;
  size_t bytes() const;
  bool operator==(const NativeSong &) const = default;
};
} // namespace Tracker
