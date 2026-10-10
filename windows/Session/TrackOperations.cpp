#include "common/stdafx.h"
#include "TrackOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Project/NativeMetadata.hpp"
#include "editor/TrackerDocument.hpp"
#include <cmath>

namespace ScreamSeq {
namespace {
void need(bool okay, const char *message) { if (!okay) throw Api::ApiError(-32602, message); }
void keys(const Json &p, std::initializer_list<const char *> allowed) {
  need(p.is_object(), "Expected track parameters");
  for (auto it = p.begin(); it != p.end(); ++it)
    need(std::any_of(allowed.begin(), allowed.end(), [&](const auto key) { return it.key() == key; }), "Unknown track parameter");
}
unsigned integer(const Json &value, unsigned low, unsigned high) {
  need(value.is_number() && !value.is_boolean(), "Expected a track integer, not a boolean");
  const auto number = value.get<double>();
  need(std::isfinite(number) && number >= low && number <= high && std::floor(number) == number,
    "Track integer outside its range");
  return unsigned(number);
}
bool boolean(const Json &value) { need(value.is_boolean(), "Expected a track boolean"); return value.get<bool>(); }
std::string id(uint64_t value) { return "n" + std::to_string(value); }
uint64_t identity(const Json &value) {
  const auto text = Project::validatedNativeText(value, 32);
  need(text.size() > 1 && text[0] == 'n' && text[1] != '0', "Invalid track identity");
  uint64_t result = 0;
  for (size_t i = 1; i < text.size(); ++i) {
    need(text[i] >= '0' && text[i] <= '9', "Invalid track identity");
    const auto digit = unsigned(text[i] - '0');
    // Match the existing Mac public identity decoder's bound.
    need(result < Tracker::NativeSong::maximumID / 10, "Track identity exceeds its range");
    result = result * 10 + digit;
  }
  need(result != 0, "Invalid track identity"); return result;
}
}
Json noteTrackLayout(const Tracker::NativeSong &native, const OpenMPT::CSoundFile &song) {
  auto columns = Json::array(), tracks = Json::array(), destinations = Json::array();
  for (const auto &[channel, entry] : native.tracks) {
    Json item = {{"id", id(entry.id)}, {"name", entry.name}, {"annotation", entry.annotation}, {"color", entry.color},
      {"channel", channel}, {"mute", Tracker::effectiveColumnMute(native, song, channel)}, {"track", nullptr}, {"noteColumn", 0}};
    for (const auto &track : native.noteTracks) {
      const auto found = std::find(track.columns.begin(), track.columns.end(), entry.id);
      if (found != track.columns.end()) { item["track"] = id(track.bus); item["noteColumn"] = found - track.columns.begin(); break; }
    }
    columns.push_back(std::move(item));
  }
  for (const auto &track : native.noteTracks) {
    const auto bus = std::find_if(native.mixer.buses.begin(), native.mixer.buses.end(), [&](const auto &value) { return value.id == track.bus; });
    need(bus != native.mixer.buses.end(), "Note track mixer bus is missing");
    auto ids = Json::array(), channels = Json::array();
    for (auto column : track.columns) {
      ids.push_back(id(column));
      for (const auto &[channel, entry] : native.tracks) if (entry.id == column) { channels.push_back(channel); break; }
    }
    tracks.push_back({{"id", id(track.bus)}, {"name", bus->name}, {"color", bus->color},
      {"columns", ids}, {"channels", channels}, {"output", id(bus->output)}});
  }
  for (const auto &bus : native.mixer.buses) if (bus.kind != Tracker::MixerBusKind::Track)
    destinations.push_back({{"id", id(bus.id)}, {"name", bus.name}});
  return {{"destinations", destinations}, {"columns", columns}, {"noteTracks", tracks},
    {"maximumColumns", std::min<unsigned>(127, song.GetModSpecifications().channelsMax)}};
}
TrackOperations::TrackOperations(Tracker::Document &document, std::function<void()> stop, TrackHostHooks host)
  : document_(document), stop_(std::move(stop)), host_(std::move(host)) {}
std::vector<std::string> TrackOperations::reads() { return {"track.get"}; }
std::vector<std::string> TrackOperations::writes() { return {"track.group", "track.create", "track.ungroup", "track.column.set"}; }
Json TrackOperations::invoke(const std::string &method, const Json &p) {
  try {
    const auto &song = document_.song();
    if (method == "track.get") { keys(p, {}); return noteTrackLayout(document_.native(), song); }
    const auto supported = writes();
    if (std::find(supported.begin(), supported.end(), method) == supported.end()) throw Api::ApiError(-32601, "Unknown track method");
    need(document_.editable(), "This document is read-only");
    const bool dry = p.contains("dryRun") ? boolean(p.at("dryRun")) : false;
    const auto output = [&]() -> std::optional<uint64_t> { return p.contains("output") ? std::optional<uint64_t>(identity(p.at("output"))) : std::nullopt; };
    const auto name = [&] { return Project::validatedNativeText(p.value("name", Json("")), 256); };
    const Tracker::NoteTrackEdit edit = [&]() -> Tracker::NoteTrackEdit {
      if (method == "track.create") {
        keys(p, {"columns", "name", "output", "dryRun"});
        return Tracker::CreateNoteTrack{integer(p.at("columns"), 1, 127), name(), output()};
      }
      if (method == "track.group") {
        keys(p, {"channels", "name", "output", "dryRun"});
        const auto &raw = p.at("channels"); need(raw.is_array() && raw.size() <= 127, "Expected at most 127 note columns");
        std::vector<uint16_t> channels;
        for (const auto &channel : raw) channels.push_back(uint16_t(integer(channel, 0, song.GetNumChannels() - 1)));
        return Tracker::GroupNoteTrack{std::move(channels), name(), output()};
      }
      if (method == "track.ungroup") { keys(p, {"track", "dryRun"}); return Tracker::UngroupNoteTrack{identity(p.at("track"))}; }
      keys(p, {"column", "mute", "dryRun"}); return Tracker::SetNoteColumnMute{identity(p.at("column")), boolean(p.at("mute"))};
    }();
    auto prepared = Tracker::prepareNoteTrackEdit(document_.native(), song, edit);
    if (host_.validateCandidate) host_.validateCandidate(prepared);
    Json result = {{"wouldChange", prepared.changed}, {"affectedID", id(prepared.affected)},
      {"layout", noteTrackLayout(prepared.native, song)}, {"appendedColumns", prepared.appendedColumns}, {"preservesRoutingOnUngroup", true}};
    if (prepared.changed && !dry) {
      const bool muteChanged = document_.native().columnMutes != prepared.native.columnMutes;
      const auto publish = muteChanged && host_.prepareColumnMutes ? host_.prepareColumnMutes(document_.native(), prepared.native) : std::function<void()>{};
      if ((prepared.appendedColumns || prepared.mixerChanged) && stop_) stop_();
      if (prepared.appendedColumns) document_.transaction([&](OpenMPT::CSoundFile &next, Tracker::NativeSong &native) {
        Tracker::Document::resizeChannels(next, int(next.GetNumChannels() + prepared.appendedColumns)); native = std::move(prepared.native);
      });
      else document_.annotate([&](Tracker::NativeSong &native) { native = std::move(prepared.native); }, publish);
    }
    return result;
  } catch (const std::invalid_argument &error) { throw Api::ApiError(-32602, error.what()); }
    catch (const std::out_of_range &error) { throw Api::ApiError(-32602, error.what()); }
    catch (const Json::exception &error) { throw Api::ApiError(-32602, error.what()); }
}
}
