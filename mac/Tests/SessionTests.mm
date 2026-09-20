#include "../Audio/MidiInput.hpp"
#import "../Bridge/TrackerSession.h"
#include <AudioToolbox/AudioToolbox.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
static void require(bool value, const char *context) {
  if (!value)
    throw std::runtime_error(context);
}
int main(int argc, char **argv) {
  @autoreleasepool {
    try {
      NSString *folder = [NSTemporaryDirectory() stringByAppendingPathComponent:@"resonance-tests"];
      [[NSFileManager defaultManager] createDirectoryAtPath:folder
                                withIntermediateDirectories:YES
                                                 attributes:nil
                                                      error:nil];
      TrackerSession *session = [TrackerSession new];
      NSError *error = nil;
      require([session savePath:[folder stringByAppendingPathComponent:@"session.mptm"] error:&error], error.localizedDescription.UTF8String ?: "save module");
      auto before = [session snapshot:0];
      require([before[@"channels"] integerValue] == 8, "session snapshot");
      NSDictionary *description = @{
        @"type" : @(kAudioUnitType_Effect),
        @"subtype" : @(kAudioUnitSubType_LowPassFilter),
        @"manufacturer" : @(kAudioUnitManufacturer_Apple),
        @"name" : @"Apple: AULowpass"
      };
      require([session addPlugin:description error:&error], "isolated Audio Unit validation and add");
      auto parameters = [session pluginParameters:0];
      require(parameters.count > 0, "Audio Unit parameter discovery");
      NSDictionary *first = parameters[0];
      double value = ([first[@"min"] doubleValue] + [first[@"max"] doubleValue]) * 0.3;
      auto identifier = [first[@"id"] integerValue];
      require([session pluginParameter:0 identifier:identifier value:value record:NO error:&error], "parameter edit");
      require(![session savePath:[folder stringByAppendingPathComponent:@"would-lose-effects.mptm"] error:&error],
              "reject module save that discards native effects");
      NSString *path = [folder stringByAppendingPathComponent:@"effects.resonance"];
      const bool liveSave = argc > 1 && std::string(argv[1]) == "--device";
      if (liveSave)
        require([session playOrder:0 error:&error], "start effect project before save");
      require([session savePath:path error:&error], "save native project");
      if (liveSave)
        require(session.playing, "saving effect state does not stop playback");
      [session stop];
      TrackerSession *reopened = [TrackerSession new];
      require([reopened openPath:path error:&error], "reopen native project");
      auto after = [reopened snapshot:0];
      require([after[@"nativePlugins"] count] == 1, "effect retained");
      require([after[@"nativePlugins"][0][@"instanceID"] length] > 0, "plugin stable identity survives reopen");
      require([before[@"cells"] isEqual:after[@"cells"]], "module cells retained in project");
      auto recalled = [reopened pluginParameters:0];
      require(std::abs([recalled[0][@"value"] doubleValue] - value) < 0.01, "effect parameter persisted");
      require([TrackerSession exportData:[reopened serializedData]
                                    path:[folder stringByAppendingPathComponent:@"session-effects.wav"]
                                   error:&error],
              "offline project render");
      require([reopened bypassPlugin:0 bypass:YES error:&error], "bypass effect");
      require([reopened savePath:[folder stringByAppendingPathComponent:@"bypassed.resonance"] error:&error],
              "save bypass");
      require([reopened removePlugin:0 error:&error], "remove effect");
      require([[reopened snapshot:0][@"nativePlugins"] count] == 0, "empty graph after removal");
      // Reject malformed project properties before replacing the existing document.
      NSData *projectData = [NSData dataWithContentsOfFile:path];
      NSDictionary *project = [NSPropertyListSerialization propertyListWithData:projectData
                                                                        options:0
                                                                         format:nil
                                                                          error:nil];
      auto writeProject = [&](NSDictionary *root, NSString *name) {
        NSString *destination = [folder stringByAppendingPathComponent:name];
        NSData *data = [NSPropertyListSerialization dataWithPropertyList:root
                                                                  format:NSPropertyListBinaryFormat_v1_0
                                                                 options:0
                                                                   error:nil];
        require([data writeToFile:destination atomically:YES], "write test project");
        return destination;
      };
      for (id badBypass in @[ @[], @{}, @"invalid" ]) {
        NSMutableDictionary *bad = [project mutableCopy];
        NSMutableDictionary *effect = [project[@"plugins"][0] mutableCopy];
        effect[@"bypass"] = badBypass;
        bad[@"plugins"] = @[ effect ];
        require(![reopened openPath:writeProject(bad, @"bad.resonance") error:&error], "reject malformed effect state");
        require([[reopened snapshot:0][@"nativePlugins"] count] == 0, "failed open retains document");
      }
      for (NSArray *badPoint in
           @[ @[ @0, @0, @(NAN), @0 ], @[ @7, @0, @1, @0 ], @[ @0, @0, @1, @(-1) ], @[ @0.5, @0, @1, @0 ] ]) {
        NSMutableDictionary *bad = [project mutableCopy];
        bad[@"automation"] = @[ badPoint ];
        require(![reopened openPath:writeProject(bad, @"bad.resonance") error:&error], "reject malformed automation");
      }
      NSString *renamedProject = [folder stringByAppendingPathComponent:@"Roundtrip.screamseq"];
      require([reopened openPath:writeProject(project, @"renamed.screamseq") error:&error] && [reopened savePath:renamedProject error:&error] && [reopened openPath:renamedProject error:&error], "ScreamSeq extension loads and saves the existing native container without losing plugins");
      NSMutableDictionary *missing = [project mutableCopy];
      NSMutableDictionary *absent = [project[@"plugins"][0] mutableCopy];
      absent[@"subtype"] = @1;
      absent[@"manufacturer"] = @1;
      absent[@"name"] = @"Unavailable test AU";
      [absent removeObjectForKey:@"instanceID"]; // Legacy missing-plugin fixtures acquire distinct identities.
      missing[@"plugins"] = @[ absent, absent ];
      require([reopened openPath:writeProject(missing, @"missing.resonance") error:&error],
              "open missing-plugin project with report");
      require([[reopened snapshot:0][@"pluginError"] length] > 0, "missing plugin reported");
      require(![reopened playOrder:0 error:&error], "refuse incomplete plugin playback");
      require([reopened movePlugin:0 direction:1 error:&error], "reorder unresolved plugins safely");
      require([reopened bypassPlugin:0 bypass:YES error:&error], "retain bypass flag on unresolved plugin");
      require([reopened removePlugin:0 error:&error], "remove one of several missing plugins");
      require([[reopened snapshot:0][@"nativePlugins"] count] == 1, "remaining opaque plugin retained");
      require([reopened savePath:writeProject(missing, @"missing-preserved.resonance") error:&error],
              "save unresolved state");
      require([reopened removePlugin:0 error:&error], "remove last missing plugin");
      require([[reopened snapshot:0][@"pluginError"] length] == 0, "graph recovers after removing missing plugins");
      require([reopened undoEffectChange:&error], "restore an opaque missing effect through undo");
      require([[reopened snapshot:0][@"pluginError"] length] > 0 &&
                  [[reopened snapshot:0][@"nativePlugins"] count] == 1,
              "missing effect history preserves state and failure report");
      require([reopened redoEffectChange:&error] && [[reopened snapshot:0][@"nativePlugins"] count] == 0,
              "redo repairs the graph again");
      NSMutableDictionary *automated = [project mutableCopy];
      automated[@"automation"] = @[ @[ @0, @(identifier), @500, @48000 ] ];
      require([reopened openPath:writeProject(automated, @"automated.resonance") error:&error],
              "load automated project");
      const bool deviceCheck = argc > 1 && std::string(argv[1]) == "--device";
      if (deviceCheck)
        require([reopened playOrder:0 error:&error], "start project for noninterrupting save test");
      require([reopened pluginParameter:0 identifier:identifier value:4000 record:NO error:&error],
              "manual baseline edit after recorded automation");
      NSString *editedAutomation = [folder stringByAppendingPathComponent:@"automated-edited.resonance"];
      require([reopened savePath:editedAutomation error:&error], "save manual edit alongside automation");
      if (deviceCheck)
        require(reopened.playing, "saving automation does not stop live playback");
      require([reopened openPath:editedAutomation error:&error], "reopen manual baseline edit");
      require(std::abs([[reopened pluginParameters:0][0][@"value"] doubleValue] - 4000) < 0.01,
              "manual baseline change persisted");
      require([[reopened snapshot:0][@"automationPoints"] integerValue] == 1,
              "automation retained alongside manual edit");
      require([reopened removePlugin:0 error:&error], "remove automated effect");
      require([reopened undoEffectChange:&error] && [[reopened snapshot:0][@"automationPoints"] integerValue] == 1,
              "effect undo restores automation and its slot");
      require(std::abs([[reopened pluginParameters:0][0][@"value"] doubleValue] - 4000) < 0.01,
              "effect undo restores the saved parameter baseline");
      require([reopened redoEffectChange:&error] && [[reopened snapshot:0][@"automationPoints"] integerValue] == 0,
              "effect redo removes the matching automation");
      TrackerSession *history = [TrackerSession new];
      require([history addPlugin:description error:&error], "history add lowpass");
      require([history pluginParameter:0 identifier:identifier value:3100 record:NO error:&error],
              "history baseline parameter");
      NSMutableDictionary *highpass = [description mutableCopy];
      highpass[@"subtype"] = @(kAudioUnitSubType_HighPassFilter);
      highpass[@"name"] = @"Apple: AUHipass";
      require([history addPlugin:highpass error:&error], "history add hipass");
      require([history editPattern:0 row:3 channel:4 values:@[ @61, @1, @1, @32, @0, @0 ] error:&error],
              "pattern edit alongside effect history");
      NSData *editedCells = [history snapshot:0][@"cells"];
      NSString *lowpassID = [history snapshot:0][@"nativePlugins"][0][@"instanceID"];
      require([history movePlugin:0 direction:1 error:&error], "history reorder");
      require([lowpassID isEqual:[history snapshot:0][@"nativePlugins"][1][@"instanceID"]], "plugin identity follows reorder");
      require([history undoEffectChange:&error], "undo reorder");
      require(std::abs([[history pluginParameters:0][0][@"value"] doubleValue] - 3100) < 0.01,
              "undo reorder restores plugin identity and parameter state");
      require([[history snapshot:0][@"cells"] isEqual:editedCells], "effect undo preserves musical edits");
      require([history redoEffectChange:&error], "redo reorder");
      require([history bypassPlugin:1 bypass:YES error:&error], "history bypass");
      require([history undoEffectChange:&error] && ![[history snapshot:0][@"nativePlugins"][1][@"bypass"] boolValue],
              "undo bypass");
      require([history removePlugin:1 error:&error] && ![[history snapshot:0][@"canRedoEffect"] boolValue],
              "new chain change clears only effect redo");
      [history undo];
      require(![[history snapshot:0][@"cells"] isEqual:editedCells] &&
                  [[history snapshot:0][@"nativePlugins"] count] == 1,
              "module undo preserves the native effect graph");
      require([history undoEffectChange:&error] && [[history snapshot:0][@"nativePlugins"] count] == 2,
              "undo removal restores both effects");
      require([reopened songTitle:@"Channel test" tempo:125 speed:6 channels:12 error:&error], "resize channel count");
      require([[reopened snapshot:0][@"channels"] integerValue] == 12, "channel count changed");
      [reopened undo];
      require([[reopened snapshot:0][@"channels"] integerValue] == 8, "channel resize undo");
      [reopened redo];
      require([[reopened snapshot:0][@"channels"] integerValue] == 12, "channel resize redo");
      require([reopened addInstrument:1 error:&error] == 1, "create matching sample instruments");
      for (int kind = 0; kind < 3; ++kind) {
        NSArray *points =
            @[ @[ @0, @(kind == 0 ? 64 : 32) ], @[ @8, @(kind == 0 ? 40 : 48) ], @[ @24, @(kind == 0 ? 0 : 32) ] ];
        require([reopened instrumentSettings:1
                                      values:@{
                                        @"envelope" : @(kind),
                                        @"points" : points,
                                        @"enabled" : @YES,
                                        @"sustain" : @YES,
                                        @"sustainPoint" : @1,
                                        @"sustainEnd" : @1,
                                        @"loop" : @YES,
                                        @"loopStart" : @1,
                                        @"loopEnd" : @2,
                                        @"filter" : @(kind == 2)
                                      }
                                       error:&error],
                "edit each envelope family");
        require([[[reopened instrumentInfo:1][@"envelopes"] objectAtIndex:kind][@"points"] isEqual:points],
                "selected envelope updated");
      }
      NSArray *originalEnvelopes = [reopened instrumentInfo:1][@"envelopes"];
      require(![reopened savePath:[folder stringByAppendingPathComponent:@"lossy-envelopes.mptm"] error:&error],
              "Reject module export that drops upper instrument sample mappings");
      NSString *instrumentSong = [folder stringByAppendingPathComponent:@"envelopes.resonance"];
      require([reopened savePath:instrumentSong error:&error] && [reopened openPath:instrumentSong error:&error],
              error.localizedDescription.UTF8String ?: "save and reopen three envelope families");
      require([[reopened instrumentInfo:1][@"envelopes"] isEqual:originalEnvelopes],
              "envelope points, flags and ranges retained");
      TrackerSession *arrangement = [TrackerSession new];
      require([arrangement addPattern:64 duplicate:YES source:0 error:&error] == 1, "create second pattern");
      require([arrangement editOrder:0 pattern:1 operation:@"assign" error:&error], "assign existing pattern");
      require([arrangement editOrder:0 pattern:0 operation:@"after" error:&error], "reuse pattern in new order");
      require([[arrangement snapshot:0][@"orders"] isEqual:@[ @1, @0, @1 ]], "insert preserves references");
      require([arrangement editOrder:2 pattern:0 operation:@"up" error:&error], "move order");
      require([[arrangement snapshot:0][@"orders"] isEqual:@[ @1, @1, @0 ]], "move changes sequence");
      [arrangement undo];
      require([[arrangement snapshot:0][@"orders"] isEqual:@[ @1, @0, @1 ]], "undo order move");
      [arrangement redo];
      require([[arrangement snapshot:0][@"orders"] isEqual:@[ @1, @1, @0 ]], "redo order move");
      require(![arrangement editOrder:0 pattern:30000 operation:@"assign" error:&error], "reject missing pattern");
      require(![arrangement editOrder:0 pattern:0 operation:@"up" error:&error], "reject order underflow");
      require([[arrangement snapshot:0][@"orders"] isEqual:@[ @1, @1, @0 ]], "failed arrangement preserves song");
      NSString *arranged = [folder stringByAppendingPathComponent:@"arranged.mptm"];
      require([arrangement savePath:arranged error:&error] && [arrangement openPath:arranged error:&error],
              "save and reopen arrangement");
      require([[arrangement snapshot:0][@"orders"] isEqual:@[ @1, @1, @0 ]], "arrangement survives native writer");
      TrackerSession *sequences = [TrackerSession new];
      require([sequences openPath:[folder stringByAppendingPathComponent:@"sequences.mptm"] error:&error],
              "open multi-sequence module");
      require([[sequences snapshot:0][@"sequences"] count] == 2 &&
                  [[sequences snapshot:0][@"sequence"] integerValue] == 1,
              "sequence names and saved selection are exposed");
      require([sequences selectSequence:0 error:&error] && [[sequences snapshot:0][@"orders"] isEqual:@[ @0 ]],
              "choose first sequence");
      require([sequences selectSequence:1 error:&error] && [[sequences snapshot:0][@"orders"] isEqual:@[ @1 ]],
              "choose alternate sequence");
      require(![sequences selectSequence:20 error:&error] && [[sequences snapshot:0][@"sequence"] integerValue] == 1,
              "invalid selection preserves current sequence");
      NSString *sequenceProject = [folder stringByAppendingPathComponent:@"sequences.resonance"];
      require([sequences savePath:sequenceProject error:&error] && [sequences openPath:sequenceProject error:&error] &&
                  [[sequences snapshot:0][@"sequence"] integerValue] == 1,
              "sequence selection survives project save/reopen");
      NSDictionary *sequenceExport = [NSPropertyListSerialization propertyListWithData:[sequences serializedData]
                                                                               options:0
                                                                                format:nil
                                                                                 error:nil];
      require([sequenceExport[@"sequence"] integerValue] == 1, "export snapshot preserves selected sequence");
      require([TrackerSession exportData:[sequences serializedData]
                                    path:[folder stringByAppendingPathComponent:@"sequence-one.wav"]
                                   error:&error],
              "render selected sequence to WAV");
      if (liveSave) {
        require([sequences playOrder:0 error:&error], "play selected sequence");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        require([[sequences telemetry][@"pattern"] integerValue] == 1, "device plays selected sequence");
        [sequences stop];
      }
      Tracker::MidiInput midi;
      std::vector<std::thread> producers;
      for (int t = 0; t < 4; ++t)
        producers.emplace_back([&midi, t] {
          for (int i = 0; i < 128; ++i)
            midi.push({uint64_t(t * 128 + i), 0x90, uint8_t(i % 120), 100});
        });
      for (auto &thread : producers)
        thread.join();
      auto events = midi.drain();
      const auto contentionDrops = midi.dropped();
      require(events.size() + contentionDrops == 512, "MIDI concurrent delivery/drop accounting");
      std::array<bool, 512> seen{};
      for (const auto &event : events) {
        require(event.timestamp < seen.size() && !seen[event.timestamp], "No duplicated or corrupt MIDI event");
        seen[event.timestamp] = true;
      }
      for (int i = 0; i < 1200; ++i)
        midi.push({0, 0x80, 60, 0});
      require(midi.dropped() == contentionDrops + 176, "MIDI bounded saturation");
      require(midi.drain().size() == 1024, "MIDI queue recovers after saturation");
      for (int i = 0; i < 1024; ++i)
        midi.push({0, 0x90, 60, 100});
      midi.push({0, 0x80, 60, 0}); // Lost release must not leave any queued note sounding.
      auto safe = midi.drainSafe();
      require(safe.size() == 1 && safe[0].status == 0xb0 && safe[0].note == 123,
              "Overflow discards the affected note batch and emits only panic");
      midi.push({1, 0x90, 64, 90});
      safe = midi.drainSafe();
      require(safe.size() == 1 && safe[0].status == 0x90 && safe[0].note == 64,
              "Fresh MIDI input resumes after overflow recovery");
      std::cout << "PASS isolated AU scan; project/module/state roundtrip; loss-prevention; offline effect render; "
                   "effect history/state/automation restoration; bounded concurrent MIDI queue\n";
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "FAIL: " << e.what() << '\n';
      return 1;
    }
  }
}
