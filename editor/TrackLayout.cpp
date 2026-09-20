#include "TrackLayout.hpp"
#include <set>
#include <stdexcept>
namespace Tracker {
namespace {
void require(bool value, const char *message) { if (!value) throw std::invalid_argument(message); }
}
void ensureNativeMixer(NativeSong &native) {
  if (native.mixer.active()) return;
  const auto master = native.makeEntity().id;
  for (const auto &[channel, track] : native.tracks)
    native.mixer.buses.push_back({track.id, master, MixerBusKind::Track,
      track.name.empty() ? "Track " + std::to_string(channel + 1) : track.name, track.color});
  native.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
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
  uint64_t commonOutput = 0;
  for (auto id : track.columns) {
    auto bus = std::find_if(native.mixer.buses.begin(), native.mixer.buses.end(), [&](const auto &b) { return b.id == id; });
    require(bus != native.mixer.buses.end(), "Note column mixer bus is missing");
    require(output.has_value() || !commonOutput || bus->output == commonOutput,
            "Columns have different outputs; choose an explicit track destination");
    commonOutput = bus->output;
  }
  const auto destination = output.value_or(commonOutput);
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
}
