#pragma once
#include "NativeSong.hpp"
#include <optional>
#include <span>
#include <variant>
namespace Tracker {
void ensureNativeMixer(NativeSong &native);
uint64_t groupNoteColumns(NativeSong &native, std::span<const uint16_t> channels,
                          const std::string &name, std::optional<uint64_t> output = {});
bool effectiveColumnMute(const NativeSong &native, const OpenMPT::CSoundFile &song, uint16_t channel);

// Owned read projection shared by native API serializers and UI snapshots.
// Stable IDs stay numeric here; JSON/Foundation and native controls stay in the
// adapters. This also accepts a prepared append before its song is resized.
struct NoteTrackColumnView {
  NativeEntity entity;
  uint16_t channel = 0;
  bool muted = false;
  std::optional<uint64_t> track;
  unsigned noteColumn = 0;
  bool operator==(const NoteTrackColumnView &) const = default;
};
struct NoteTrackView {
  uint64_t id = 0, output = 0;
  std::string name;
  uint32_t color = 0;
  std::vector<uint64_t> columns;
  std::vector<uint16_t> channels;
  bool operator==(const NoteTrackView &) const = default;
};
struct NoteTrackDestinationView {
  uint64_t id = 0;
  std::string name;
  bool operator==(const NoteTrackDestinationView &) const = default;
};
struct NoteTrackLayoutView {
  std::vector<NoteTrackColumnView> columns;
  std::vector<NoteTrackView> tracks;
  std::vector<NoteTrackDestinationView> destinations;
  unsigned maximumColumns = 0;
  bool operator==(const NoteTrackLayoutView &) const = default;
};
NoteTrackLayoutView describeNoteTracks(const NativeSong &native, const OpenMPT::CSoundFile &song);

// Portable edit intent and candidate only. Native adapters own request decoding,
// revision admission, plugin capacity, history and renderer publication.
struct GroupNoteTrack {
  std::vector<uint16_t> channels;
  std::string name;
  std::optional<uint64_t> output;
};
struct CreateNoteTrack {
  unsigned columns = 1;
  std::string name;
  std::optional<uint64_t> output;
};
struct UngroupNoteTrack { uint64_t track = 0; };
struct SetNoteColumnMute { uint64_t column = 0; bool muted = false; };
using NoteTrackEdit = std::variant<GroupNoteTrack, CreateNoteTrack, UngroupNoteTrack, SetNoteColumnMute>;
struct PreparedNoteTrackEdit {
  NativeSong native;
  uint64_t affected = 0;
  unsigned appendedColumns = 0;
  bool changed = false;
  bool mixerChanged = false;
};
PreparedNoteTrackEdit prepareNoteTrackEdit(const NativeSong &native,
  const OpenMPT::CSoundFile &song, const NoteTrackEdit &edit);
}
