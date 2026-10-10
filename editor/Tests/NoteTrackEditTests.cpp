#include "editor/TrackerDocument.hpp"
#include "editor/TrackLayout.hpp"
#include <functional>
#include <iostream>
#include <limits>

using namespace Tracker;
using namespace OpenMPT;
namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void rejects(const std::function<void()> &operation) {
  try { operation(); } catch (const std::invalid_argument &) { return; }
  throw std::runtime_error("Invalid note-track edit was accepted");
}
template<typename Native> auto &bus(Native &native, uint64_t id) {
  auto found = std::find_if(native.mixer.buses.begin(), native.mixer.buses.end(),
    [&](const auto &value) { return value.id == id; });
  check(found != native.mixer.buses.end(), "Expected stable bus identity is absent");
  return *found;
}
void groupingAndHistory() {
  auto doc = Document::demo();
  const auto original = doc->native(); const auto bytes = doc->snapshotData(); const auto revision = doc->revision;
  const auto edit = GroupNoteTrack{{0, 1}, "Chords", {}};
  const auto prepared = prepareNoteTrackEdit(original, doc->song(), edit);
  check(prepared.changed && prepared.mixerChanged && prepared.appendedColumns == 0,
    "Grouping must classify the newly materialized routing");
  check(doc->native() == original && doc->revision == revision && doc->snapshotData() == bytes,
    "Candidate preparation changed the live song, allocator or history");
  check(prepared.native.tracks == original.tracks && prepared.native.noteTracks.size() == 1 &&
    prepared.native.noteTracks[0].columns == std::vector<uint64_t>{original.tracks.at(0).id, original.tracks.at(1).id},
    "Grouping changed stable columns or their order");
  check(prepareNoteTrackEdit(original, doc->song(), edit).native == prepared.native,
    "Repeated dry preparation consumed identities");
  doc->annotate([&](NativeSong &native) { native = prepared.native; });
  check(doc->snapshotData() == bytes, "Grouping changed embedded pattern or instrument data");
  doc->undo(); check(doc->native() == original, "One Undo did not restore the complete original native song");
  doc->redo(); check(doc->native() == prepared.native, "Redo changed grouping identities");
  const auto ungrouped = prepareNoteTrackEdit(doc->native(), doc->song(), UngroupNoteTrack{prepared.affected});
  check(ungrouped.changed && !ungrouped.mixerChanged && ungrouped.native.noteTracks.empty() &&
    ungrouped.native.mixer == prepared.native.mixer && ungrouped.native.tracks == original.tracks,
    "Ungroup removed processing, routing or column identities");
  for (const auto channels : {std::vector<uint16_t>{}, {1, 0}, {0, 2}, {0, 0}, {127}})
    rejects([&] { prepareNoteTrackEdit(original, doc->song(), GroupNoteTrack{channels, "Invalid", {}}); });
  rejects([&] { prepareNoteTrackEdit(doc->native(), doc->song(), edit); });
  rejects([&] { prepareNoteTrackEdit(original, doc->song(), UngroupNoteTrack{prepared.affected}); });
  rejects([&] { prepareNoteTrackEdit(original, doc->song(), GroupNoteTrack{{0}, std::string(257, 'x'), {}}); });
  check(doc->native() == prepared.native, "Rejected preparation changed the accepted document");
}
void outputAdmission() {
  auto doc = Document::demo(); auto native = doc->native(); native.ensureMixer();
  const auto first = native.tracks.at(0).id, second = native.tracks.at(1).id;
  const auto master = native.masterID;
  // Zero is a real disconnected destination, not an uninitialized common output.
  for (bool firstDisconnected : {false, true}) {
    auto mixed = native;
    bus(mixed, firstDisconnected ? first : second).output = 0;
    mixed.validate(doc->song()); const auto before = mixed;
    rejects([&] { prepareNoteTrackEdit(mixed, doc->song(), GroupNoteTrack{{0, 1}, "Ambiguous", {}}); });
    check(mixed == before, "Destination rejection changed routing or IDs");
    auto explicitRoute = prepareNoteTrackEdit(mixed, doc->song(), GroupNoteTrack{{0, 1}, "Chosen", master});
    check(bus(explicitRoute.native, explicitRoute.affected).output == master &&
      bus(explicitRoute.native, first).output == explicitRoute.affected &&
      bus(explicitRoute.native, second).output == explicitRoute.affected,
      "Explicit group destination did not govern both selected columns");
    for (const auto &old : mixed.mixer.buses) {
      auto expected = old;
      if (old.id == first || old.id == second) expected.output = explicitRoute.affected;
      check(bus(explicitRoute.native, old.id) == expected, "Grouping rewrote an unrelated control, route or insert");
    }
  }
  rejects([&] { prepareNoteTrackEdit(native, doc->song(), GroupNoteTrack{{0, 1}, "Invalid", first}); });
  rejects([&] { prepareNoteTrackEdit(native, doc->song(), GroupNoteTrack{{0, 1}, "Missing", NativeSong::maximumID - 1}); });
}
void columnMutes() {
  for (bool imported : {false, true}) {
    auto doc = Document::demo();
    doc->transaction([&](CSoundFile &song) { song.ChnSettings[0].dwFlags.set(CHN_MUTE, imported); });
    const auto original = doc->native(); const auto column = original.tracks.at(0).id;
    const auto unchanged = prepareNoteTrackEdit(original, doc->song(), SetNoteColumnMute{column, imported});
    check(!unchanged.changed && unchanged.native == original, "Imported mute state should need no native override");
    const auto overridden = prepareNoteTrackEdit(original, doc->song(), SetNoteColumnMute{column, !imported});
    check(overridden.changed && !overridden.mixerChanged && overridden.native.columnMutes.at(column) == !imported &&
      effectiveColumnMute(overridden.native, doc->song(), 0) == !imported,
      "Column mute failed to override the imported state independently of mixer controls");
    check(!prepareNoteTrackEdit(overridden.native, doc->song(), SetNoteColumnMute{column, !imported}).changed,
      "Repeated column mute was not a no-op");
    const auto restored = prepareNoteTrackEdit(overridden.native, doc->song(), SetNoteColumnMute{column, imported});
    check(restored.changed && restored.native == original, "Restoring imported mute failed to remove the native override");
    rejects([&] { prepareNoteTrackEdit(original, doc->song(), SetNoteColumnMute{0, true}); });
  }
}
void appendAndCapacity() {
  for (auto type : {MOD_TYPE_MOD, MOD_TYPE_XM, MOD_TYPE_S3M, MOD_TYPE_IT, MOD_TYPE_MPT}) {
    for (bool active : {false, true}) {
      auto doc = Document::demo(type);
      if (active) doc->annotate([](NativeSong &native) { native.ensureMixer(); });
      const auto original = doc->native(); const auto count = doc->song().GetNumChannels();
      const auto prepared = prepareNoteTrackEdit(original, doc->song(), CreateNoteTrack{2, "New columns", {}});
      check(prepared.changed && prepared.mixerChanged && prepared.appendedColumns == 2 &&
        prepared.native.tracks.size() == original.tracks.size() + 2, "Create did not prepare exactly the requested columns");
      for (const auto &[channel, entry] : original.tracks)
        check(prepared.native.tracks.at(channel) == entry, "Append changed an existing column identity");
      for (const auto &old : original.mixer.buses) {
        check(bus(prepared.native, old.id) == old, "Append changed existing bus controls or routing");
      }
      doc->transaction([&](CSoundFile &song, NativeSong &native) {
        Document::resizeChannels(song, int(count + prepared.appendedColumns)); native = prepared.native;
      });
      check(doc->song().GetNumChannels() == count + 2 && doc->native() == prepared.native,
        "Structural adoption did not retain the prepared identities");
      doc->undo(); check(doc->song().GetNumChannels() == count && doc->native() == original, "Append needed more than one Undo");
      doc->redo(); check(doc->native() == prepared.native, "Append Redo changed stable IDs");
      for (unsigned invalid : {0u, 128u, std::numeric_limits<unsigned>::max()})
        rejects([&] { prepareNoteTrackEdit(original, doc->song(), CreateNoteTrack{invalid, "Invalid", {}}); });
      const unsigned maximum = std::min<unsigned>(127, doc->song().GetModSpecifications().channelsMax);
      rejects([&] { prepareNoteTrackEdit(doc->native(), doc->song(), CreateNoteTrack{maximum - unsigned(doc->song().GetNumChannels()) + 1, "Too many", {}}); });
    }
  }
}
}
int main() {
  try {
    groupingAndHistory(); outputAdmission(); columnMutes(); appendAndCapacity();
    std::cout << "PASS shared note-track candidates, destinations, column mute and structural history\n"; return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
