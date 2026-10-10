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
