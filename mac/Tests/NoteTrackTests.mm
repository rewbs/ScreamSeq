#include "../Audio/AudioUnitHost.hpp"
#include "../Audio/AudioExport.hpp"
#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include "editor/TrackLayout.hpp"
#include "soundlib/ModInstrument.h"
#include "soundlib/plugins/PlugInterface.h"
#include <iostream>
#include <fstream>
using namespace Tracker;
using namespace OpenMPT;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) { *a = *f = *l = 0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
#endif
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static std::vector<float> render(Document &doc, uint32_t rate, uint32_t block, bool withMixer) {
  Renderer renderer(doc.snapshotData(), rate);
  renderer.applyColumnMutes(doc.native(), doc.song());
  PluginChain chain({}, rate, true);
  if (withMixer) chain.attachInstruments(renderer, &doc.native());
  std::vector<float> audio(rate * 2);
  for (uint32_t pos = 0; pos < rate; pos += block) {
    const auto count = std::min(block, rate - pos);
    uint64_t a, f, l; tracker_audit_begin();
    renderer.render(audio.data() + pos * 2, count);
    const bool ok = !withMixer || chain.process(audio.data() + pos * 2, count);
    tracker_audit_end(&a, &f, &l);
    check(ok && !renderer.faulted() && a + f + l == 0, "Note track render is allocation/free/lock-free");
  }
  return audio;
}
static double difference(const std::vector<float> &a, const std::vector<float> &b) {
  double error = 0; for (size_t i = 0; i < a.size(); ++i) error = std::max(error, std::abs(double(a[i]) - b[i])); return error;
}
int main(int argc, char **argv) { @autoreleasepool { try {
  check(argc == 2, "Fixture bundle path required");
  const std::array<uint16_t, 3> columns{0, 1, 2};
  auto doc = Document::demo(); const auto original = doc->native();
  doc->annotate([&](NativeSong &n) { groupNoteColumns(n, columns, "Chords"); });
  const auto grouped = doc->native();
  check(grouped.tracks == original.tracks && grouped.noteTracks[0].columns[1] == original.tracks.at(1).id, "Grouping keeps column IDs and order");
  doc->undo(); check(doc->native().noteTracks.empty() && !doc->native().mixer.active(), "One Undo removes grouping and its new mixer");
  doc->redo(); check(doc->native().noteTracks == grouped.noteTracks, "Redo preserves group identity");
  for (auto invalid : {std::vector<uint16_t>{}, {0, 2}, {2, 1}, {0}, {127}}) {
    const auto before = doc->native(); const auto revision = doc->revision;
    try { doc->annotate([&](NativeSong &n) { groupNoteColumns(n, invalid, "Invalid"); }); check(false, "Invalid group accepted"); }
    catch (const std::invalid_argument &) {}
    check(before == doc->native() && doc->revision == revision, "Invalid group is atomic");
  }
  try { doc->annotate([](NativeSong &n) { n.mixer.buses[0].output = n.mixer.buses[1].output = n.mixer.buses[3].output; }); check(false, "Grouped column output changed"); }
  catch (const std::invalid_argument &) {}
  doc->annotate([](NativeSong &n) { n.columnMutes[n.tracks.at(0).id] = true; });
  doc->transaction([](CSoundFile &song) { Document::resizeChannels(song, 2); });
  check(doc->native().noteTracks[0].columns.size() == 2 && doc->native().columnMutes.size() == 1, "Shrinking preserves surviving group columns and overrides");
  doc->undo(); check(doc->native().noteTracks[0].columns.size() == 3, "Undo restores removed group columns");
  for (uint32_t rate : {44100, 48000, 96000}) {
    auto song = Document::demo();
    const auto legacy = render(*song, rate, 128, false);
    song->annotate([](NativeSong &n) { ensureNativeMixer(n); });
    const auto reference = render(*song, rate, 128, true);
    song->annotate([&](NativeSong &n) { groupNoteColumns(n, columns, "Shared"); });
    std::cout << "Unity " << rate << " native-group=" << difference(reference, render(*song, rate, 17, true)) << " legacy-to-mixer=" << difference(legacy, reference) << '\n';
    // Enabling the existing native mixer changes core subdivision to 32 samples;
    // its float/fixed conversion is checked separately from the grouping delta.
    check(difference(legacy, reference) < 2e-6, "Existing mixer conversion stays below -114 dBFS peak error");
    check(difference(reference, render(*song, rate, 17, true)) < 2e-7, "Grouping preserves sample playback at unity");
    song->annotate([](NativeSong &n) { n.columnMutes[n.tracks.at(0).id] = true; });
    const auto muted = render(*song, rate, 512, true);
    song->annotate([](NativeSong &n) { n.columnMutes.clear(); });
    song->transaction([](CSoundFile &s) { for (auto &p : s.Patterns) if (p.IsValid()) for (ROWINDEX r = 0; r < p.GetNumRows(); ++r) *p.GetpModCommand(r, 0) = {}; });
    check(difference(muted, render(*song, rate, 17, true)) < 2e-7, "Persistent column mute matches removal of only its notes");
    // Create real sustained NNA voices, then mute one parent at a callback boundary.
    auto nna = Document::demo();
    nna->transaction([](CSoundFile &s) {
      for (auto &p : s.Patterns) if (p.IsValid()) for (auto &cell : p) cell.Clear();
      s.m_nInstruments = 1; s.Instruments[1] = new ModInstrument(SAMPLEINDEX(1));
      s.Instruments[1]->nNNA = NewNoteAction::Continue;
      auto &sample = s.GetSample(1); sample.SetLoop(0, sample.nLength, true, false, s);
      for (int c = 0; c < 2; ++c) for (int row = 0; row < 3; ++row) { auto &cell = *s.Patterns[0].GetpModCommand(row, c); cell.note = 49 + c * 7 + row; cell.instr = 1; }
    });
    Renderer player(nna->snapshotData(), rate); std::array<float, 1024> buffer{};
    for (uint32_t pos = 0; pos < rate / 3; pos += 512) player.render(buffer.data(), 512);
    size_t first = 0, second = 0;
    for (const auto &voice : player.song().m_PlayState.BackgroundChannels(player.song())) {
      if (voice.nMasterChn == 1 && voice.nLength) ++first;
      if (voice.nMasterChn == 2 && voice.nLength) ++second;
    }
    check(first && second, "Fixture has sustained voices in both columns");
    player.mute(0, true); uint64_t a, f, l; tracker_audit_begin(); player.render(buffer.data(), 17); tracker_audit_end(&a, &f, &l);
    check(a + f + l == 0, "Live column mute does no callback allocation/free/locking");
    for (const auto &voice : player.song().m_PlayState.BackgroundChannels(player.song())) {
      if (voice.nMasterChn == 1 && voice.nLength) check(voice.dwFlags[CHN_MUTE], "Column mute includes sustained voices");
      if (voice.nMasterChn == 2 && voice.nLength) check(!voice.dwFlags[CHN_MUTE], "Other column stays audible");
    }
    player.mute(0, false); player.render(buffer.data(), 17);
    for (const auto &voice : player.song().m_PlayState.BackgroundChannels(player.song()))
      if (voice.nMasterChn == 1 && voice.nLength) check(!voice.dwFlags[CHN_MUTE], "Unmute restores surviving sample voices");
  }

  // A shared VST3 instrument must retain the other column's notes, including
  // identical pitches. The fixture counts note-on/off pairs independently.
  auto descriptors = NativePlugin::discoverVST3(argv[1]);
  for (bool samePitch : {false, true}) {
    auto source = Document::demo();
    source->transaction([&](CSoundFile &s) {
      for (auto &p : s.Patterns) if (p.IsValid()) for (auto &cell : p) cell.Clear();
      s.m_nInstruments = 1; s.Instruments[1] = new ModInstrument(SAMPLEINDEX(1)); s.Instruments[1]->nNNA = NewNoteAction::Continue;
      for (int c = 0; c < 2; ++c) for (int row = 0; row < 3; ++row) { auto &cell = *s.Patterns[0].GetpModCommand(row,c); cell.note = 49 + row + (samePitch ? 0 : c * 7); cell.instr = 1; }
    });
    PluginState synth{descriptors.at(1)}; synth.instrument = 1; synth.instanceID = "column-synth";
    Renderer player(source->snapshotData(), 48000); PluginChain chain({synth}, 48000, true); chain.attachInstruments(player, &source->native());
    std::array<float, 1024> buffer{};
    auto block = [&]() {
      uint64_t a, f, l; tracker_audit_begin(); player.render(buffer.data(), 512); const bool ok = chain.process(buffer.data(),512); tracker_audit_end(&a,&f,&l);
      check(ok && !player.faulted() && a + f + l == 0, "Shared instrument column mute keeps the callback allocation/free/lock-free");
    };
    for (int i = 0; i < 32; ++i) block();
    auto *plugin = player.song().m_MixPlugins[0].pMixPlugin;
    size_t backgroundNotes = 0;
    for (CHANNELINDEX c = player.song().GetNumChannels(); c < MAX_CHANNELS; ++c) {
      auto &v = player.song().m_PlayState.Chn[c]; if (v.nMasterChn == 1 && plugin->IsNotePlaying(v.lastMidiNoteWithoutArp, c)) ++backgroundNotes;
    }
    check(backgroundNotes > 0, "Plugin fixture has sustained background notes");
    player.mute(0,true); block();
    check(std::abs(buffer.back()) > .01, "Muting one column does not silence its shared instrument");
    for (CHANNELINDEX c = 0; c < MAX_CHANNELS; ++c) {
      auto &v = player.song().m_PlayState.Chn[c];
      if (c == 0 || (c >= player.song().GetNumChannels() && v.nMasterChn == 1))
        check(!plugin->IsNotePlaying(v.lastMidiNoteWithoutArp,c), "Muted column releases both foreground and sustained plugin notes");
    }
    player.mute(1,true); for (int i = 0; i < 4; ++i) block();
    check(std::abs(buffer.back()) < 1e-8, "Both columns muted leave no stuck plugin notes");
    player.preview({61,1,100,true}); block();
    check(std::abs(buffer.back()) > .01, "Manual audition is independent of column mute");
    player.mute(0,false); block();player.mute(0,true);block();
    check(std::abs(buffer.back()) > .01, "Column mute does not stop a manual preview voice");
    player.preview({61,1,0,false});block();
  }
  // Explicit false must override imported muted state before the first callback.
  for (auto type : {MOD_TYPE_S3M, MOD_TYPE_IT, MOD_TYPE_MPT}) {
    auto source = Document::demo(type); const auto clear = render(*source,48000,128,false);
    source->transaction([](CSoundFile &s) { s.ChnSettings[0].dwFlags.set(CHN_MUTE); });
    source->annotate([](NativeSong &n) { n.columnMutes[n.tracks.at(0).id] = false; });
    check(difference(clear,render(*source,48000,128,false)) < 2e-7, "Native unmute overrides imported channel mute before playback");
  }
  NSString *folder = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  [NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
  {
    auto source = Document::demo();
    source->annotate([&](NativeSong &n) { groupNoteColumns(n,columns,"Export"); n.columnMutes[n.tracks.at(0).id] = true; });
    NSString *path = [folder stringByAppendingPathComponent:@"muted.wav"];
    exportProjectAudio(source->snapshotData(),{},{},path.UTF8String,0,&source->native());
    const auto expected = render(*source,48000,512,true); std::vector<float> actual(expected.size());
    std::ifstream file(path.UTF8String,std::ios::binary);file.seekg(44);file.read(reinterpret_cast<char *>(actual.data()),actual.size()*sizeof(float));
    check(bool(file) && difference(expected,actual) < 2e-7,"Actual offline WAV export applies shared grouping and persistent column mutes");
  }
  for (auto type : {MOD_TYPE_MOD, MOD_TYPE_XM, MOD_TYPE_S3M, MOD_TYPE_IT, MOD_TYPE_MPT}) {
    auto source = Document::demo(type); NSString *module = [folder stringByAppendingPathComponent:@"source.module"]; source->save(module.UTF8String);
    auto session = [TrackerSession new]; NSError *error = nil;
    check([session openPath:module error:&error], "Open fixture module");
    auto call = [&](NSString *method, NSDictionary *params) -> NSDictionary * {
      NSMutableDictionary *p = [params mutableCopy]; if (![method hasSuffix:@".get"]) p[@"expectedRevision"] = session.automationRevision;
      auto response = [session automationMethod:method params:p error:&error];
      if (!response) throw std::runtime_error(error.localizedDescription.UTF8String); return response;
    };
    auto before = [session snapshot:0];
    auto preview = call(@"track.group", @{@"channels": @[@0, @1], @"name": @"Chord 🎹", @"dryRun": @YES});
    check(![preview[@"changed"] boolValue] && [preview[@"data"][@"wouldChange"] boolValue], "Group preview is read-only");
    call(@"track.group", @{@"channels": @[@0, @1], @"name": @"Chord 🎹"});
    call(@"track.column.set", @{@"column": before[@"tracks"][0][@"id"], @"mute": @YES});
    NSDictionary *layout = call(@"track.get", @{})[@"data"];
    check([layout[@"columns"][0][@"mute"] boolValue] && ![layout[@"columns"][1][@"mute"] boolValue], "Independent column mute");
    NSString *path = [folder stringByAppendingPathComponent:@"native.resonance"];
    check([session savePath:path error:&error] && [session openPath:path error:&error], "Native note track roundtrip");
    check([layout isEqual:call(@"track.get", @{})[@"data"]], "Column IDs, grouping and mute persist in all five formats");
    check([[session snapshot:0][@"cells"] isEqual:before[@"cells"]], "Grouping preserves pattern data");
    check(![session savePath:module error:&error], "Reject metadata-losing module export");
    NSMutableDictionary *root = [[NSPropertyListSerialization propertyListWithData:[NSData dataWithContentsOfFile:path] options:NSPropertyListMutableContainers format:nil error:nil] mutableCopy];
    check([root[@"native"][@"version"] intValue] == 17, "Column features use native metadata v5");
    root[@"native"][@"version"] = @4;
    [[NSPropertyListSerialization dataWithPropertyList:root format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil] writeToFile:path atomically:YES];
    auto revision = session.automationRevision;
    check(![session openPath:path error:&error] && [revision isEqual:session.automationRevision], "Reject column fields in legacy metadata atomically");
  }
  [NSFileManager.defaultManager removeItemAtPath:folder error:nil];
  std::cout << "PASS note tracks: stable grouping, atomic rejection, structural Undo, five-format native roundtrip, column mutes, unity audio at three rates and callback audit\n";
  return 0;
} catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } } }
