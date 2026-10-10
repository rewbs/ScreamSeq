#include "TrackLayout.hpp"
#include "soundlib/mod_specifications.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace Tracker {
namespace {
void require(bool value, const char *message) { if (!value) throw std::invalid_argument(message); }
}
void ensureNativeMixer(NativeSong &native) {
  native.ensureMixer();
}
uint64_t groupNoteColumns(NativeSong &native, std::span<const uint16_t> channels,
                          const std::string &name, std::optional<uint64_t> output) {
  require(!channels.empty() && channels.size() <= 127, "Choose 1..127 adjacent note columns");
  require(name.size() <= 256 && name.find('\0') == name.npos, "Track name exceeds its limits");
  std::set<uint64_t> used;
  for (const auto &track : native.noteTracks) used.insert(track.columns.begin(), track.columns.end());
  NativeNoteTrack track;
  for (size_t i = 0; i < channels.size(); ++i) {
    require(!i || channels[i] == channels[i - 1] + 1, "Note columns must be adjacent and in channel order");
    auto found = native.tracks.find(channels[i]);
    require(found != native.tracks.end(), "Note column does not exist");
    require(!used.contains(found->second.id), "A note column already belongs to a track; ungroup it first");
    track.columns.push_back(found->second.id);
  }
  ensureNativeMixer(native);
  std::optional<uint64_t> commonOutput;
  for (auto id : track.columns) {
    auto bus = std::find_if(native.mixer.buses.begin(), native.mixer.buses.end(), [&](const auto &b) { return b.id == id; });
    require(bus != native.mixer.buses.end(), "Note column mixer bus is missing");
    require(output.has_value() || !commonOutput || bus->output == *commonOutput,
            "Columns have different outputs; choose an explicit track destination");
    commonOutput = bus->output;
  }
  const auto destination = output.value_or(*commonOutput);
  auto target = std::find_if(native.mixer.buses.begin(), native.mixer.buses.end(), [&](const auto &b) { return b.id == destination; });
  require(target != native.mixer.buses.end() && target->kind != MixerBusKind::Track, "Choose a group, return or Master destination");
  track.bus = native.makeEntity().id;
  for (auto id : track.columns)
    std::find_if(native.mixer.buses.begin(), native.mixer.buses.end(), [&](const auto &b) { return b.id == id; })->output = track.bus;
  native.mixer.buses.push_back({track.bus, destination, MixerBusKind::Group,
    name.empty() ? "Note track " + std::to_string(channels.front() + 1) : name, native.tracks.at(channels.front()).color});
  native.noteTracks.push_back(track);
  return track.bus;
}
bool effectiveColumnMute(const NativeSong &native, const OpenMPT::CSoundFile &song, uint16_t channel) {
  auto track = native.tracks.find(channel);
  if (track == native.tracks.end() || channel >= song.GetNumChannels()) return false;
  auto found = native.columnMutes.find(track->second.id);
  return found != native.columnMutes.end() ? found->second : song.ChnSettings[channel].dwFlags[OpenMPT::CHN_MUTE];
}
NoteTrackLayoutView describeNoteTracks(const NativeSong &native, const OpenMPT::CSoundFile &song) {
  NoteTrackLayoutView result;
  result.maximumColumns = std::min<unsigned>(127, song.GetModSpecifications().channelsMax);
  std::map<uint64_t, size_t> columnByID;
  result.columns.reserve(native.tracks.size());
  for (const auto &[channel, entry] : native.tracks) {
    require(columnByID.emplace(entry.id, result.columns.size()).second, "Duplicate note column identity");
    result.columns.push_back({entry, channel, effectiveColumnMute(native, song, channel), {}, 0});
  }
  std::map<uint64_t, const MixerBus *> buses;
  for (const auto &bus : native.mixer.buses) {
    require(buses.emplace(bus.id, &bus).second, "Duplicate mixer bus identity");
    if (bus.kind != MixerBusKind::Track) result.destinations.push_back({bus.id, bus.name});
  }
  result.tracks.reserve(native.noteTracks.size());
  for (const auto &track : native.noteTracks) {
    const auto bus = buses.find(track.bus);
    require(bus != buses.end(), "Note track mixer bus is missing");
    NoteTrackView item{track.bus, bus->second->output, bus->second->name, bus->second->color, track.columns, {}};
    for (auto id : track.columns) {
      const auto found = columnByID.find(id);
      require(found != columnByID.end(), "Note track column is missing");
      auto &column = result.columns[found->second];
      require(!column.track, "Note column belongs to more than one track");
      column.track = track.bus; column.noteColumn = unsigned(item.channels.size());
      item.channels.push_back(column.channel);
    }
    result.tracks.push_back(std::move(item));
  }
  return result;
}
PreparedNoteTrackEdit prepareNoteTrackEdit(const NativeSong &native,
  const OpenMPT::CSoundFile &song, const NoteTrackEdit &edit) {
  PreparedNoteTrackEdit prepared{native};
  auto &candidate = prepared.native;
  if (const auto *group = std::get_if<GroupNoteTrack>(&edit)) {
    prepared.affected = groupNoteColumns(candidate, group->channels, group->name, group->output);
  } else if (const auto *create = std::get_if<CreateNoteTrack>(&edit)) {
    const unsigned count = song.GetNumChannels();
    const unsigned maximum = std::min<unsigned>(127, song.GetModSpecifications().channelsMax);
    require(create->columns >= 1 && create->columns <= 127, "Choose 1..127 new note columns");
    require(count <= maximum && create->columns <= maximum - count,
      "New note columns exceed this module format's channel limit");
    prepared.appendedColumns = create->columns;
    std::vector<uint16_t> channels;
    for (unsigned i = 0; i < create->columns; ++i) {
      const auto channel = uint16_t(count + i);
      require(!candidate.tracks.contains(channel), "New note column identity already exists");
      candidate.tracks.emplace(channel, candidate.makeEntity());
      channels.push_back(channel);
    }
    if (candidate.mixer.active()) {
      const auto master = std::find_if(candidate.mixer.buses.begin(), candidate.mixer.buses.end(),
        [](const auto &bus) { return bus.kind == MixerBusKind::Master; });
      require(master != candidate.mixer.buses.end(), "Mixer Master is missing");
      const auto masterID = master->id;
      for (auto channel : channels)
        candidate.mixer.buses.push_back({candidate.tracks.at(channel).id, masterID,
          MixerBusKind::Track, "Column " + std::to_string(channel + 1)});
    }
    prepared.affected = groupNoteColumns(candidate, channels, create->name, create->output);
  } else if (const auto *ungroup = std::get_if<UngroupNoteTrack>(&edit)) {
    prepared.affected = ungroup->track;
    const auto found = std::find_if(candidate.noteTracks.begin(), candidate.noteTracks.end(),
      [&](const auto &track) { return track.bus == ungroup->track; });
    require(found != candidate.noteTracks.end(), "Note track does not exist");
    candidate.noteTracks.erase(found); // Routing survives removal of visual grouping.
  } else {
    const auto &mute = std::get<SetNoteColumnMute>(edit);
    prepared.affected = mute.column;
    const auto found = std::find_if(candidate.tracks.begin(), candidate.tracks.end(),
      [&](const auto &track) { return track.second.id == mute.column; });
    require(found != candidate.tracks.end() && found->first < song.GetNumChannels(), "Note column does not exist");
    if (mute.muted == song.ChnSettings[found->first].dwFlags[OpenMPT::CHN_MUTE]) candidate.columnMutes.erase(mute.column);
    else candidate.columnMutes[mute.column] = mute.muted;
  }
  if (!prepared.appendedColumns) candidate.validate(song);
  else {
    // The adapter resizes the embedded song in the same admitted transaction.
    // Validate the projected bus identities before any structural mutation.
    std::vector<uint64_t> ids;
    for (const auto &[index, track] : candidate.tracks) ids.push_back(track.id);
    candidate.mixer.validate(ids);
  }
  prepared.changed = candidate != native;
  prepared.mixerChanged = candidate.mixer != native.mixer;
  return prepared;
}
}
