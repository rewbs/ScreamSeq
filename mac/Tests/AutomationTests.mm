#import "../Bridge/TrackerSession.h"
#include <AudioToolbox/AudioToolbox.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int main() {
  @autoreleasepool {
    try {
      TrackerSession *session = [TrackerSession new];
      auto call = [&](NSString *method, NSDictionary *params, bool mutation = false) -> NSDictionary * {
        NSMutableDictionary *p = [params mutableCopy];
        if (mutation)
          p[@"expectedRevision"] = session.automationRevision;
        NSError *error = nil;
        auto result = [session automationMethod:method params:p error:&error];
        if (!result)
          throw std::runtime_error(error.localizedDescription.UTF8String);
        check([NSJSONSerialization isValidJSONObject:result], "Every API result is JSON serializable");
        return result;
      };
      auto reject = [&](NSString *method, NSDictionary *params, NSInteger code) {
        NSError *error = nil;
        NSString *before = session.automationRevision;
        check(![session automationMethod:method params:params error:&error] && error.code == code,
              "Reject invalid API request with expected code");
        check([before isEqual:session.automationRevision], "Rejected request keeps revision");
      };
      NSDictionary *before = call(@"pattern.get", @{@"pattern" : @0})[@"data"];
      NSString *revision = session.automationRevision;
      NSArray *cells = @[
        @{
          @"pattern" : @0,
          @"row" : @7,
          @"channel" : @2,
          @"note" : @49,
          @"instrument" : @2,
          @"volumeCommand" : @1,
          @"volume" : @4
        },
        @{
          @"pattern" : @0,
          @"row" : @9,
          @"channel" : @2,
          @"note" : @49,
          @"instrument" : @2,
          @"volumeCommand" : @1,
          @"volume" : @64
        }
      ];
      auto preview = call(@"pattern.apply", @{@"cells" : cells, @"dryRun" : @YES}, true);
      check(![preview[@"changed"] boolValue] && [revision isEqual:session.automationRevision],
            "Dry run preserves revision/history");
      call(@"pattern.apply", @{@"cells" : cells}, true);
      check(![revision isEqual:session.automationRevision], "Committed batch advances revision");
      reject(@"pattern.apply", @{@"expectedRevision" : revision, @"cells" : cells}, -32001);
      call(@"history.undo", @{@"domain" : @"document"}, true);
      check([before isEqual:call(@"pattern.get", @{@"pattern" : @0})[@"data"]], "Pattern batch is one undo step");
      call(@"history.redo", @{@"domain" : @"document"}, true);
      for (id value in @[ @(-1), @256, @1.5, @YES, @"49", NSNull.null, @[], @{} ]) {
        reject(
            @"pattern.apply", @{
              @"expectedRevision" : session.automationRevision,
              @"cells" : @[
                @{@"pattern" : @0,
                  @"row" : @0,
                  @"channel" : @0,
                  @"note" : value}
              ]
            },
            -32602);
      }
      auto atomicBefore = call(@"pattern.get", @{@"pattern" : @0});
      reject(
          @"pattern.apply", @{
            @"expectedRevision" : session.automationRevision,
            @"cells" : @[ cells[0],
                          @{@"pattern" : @0,
                            @"row" : @65535,
                            @"channel" : @0,
                            @"note" : @49} ]
          },
          -32602);
      check([atomicBefore isEqual:call(@"pattern.get", @{@"pattern" : @0})], "Invalid batch cannot partially apply");
      reject(@"pattern.get", @{@"pattern" : @9999}, -32602);
      reject(@"does.not.exist", @{}, -32601);
      NSDictionary *info = call(@"sample.get", @{@"sample" : @1})[@"data"];
      NSDictionary *pcm = call(@"sample.pcm.get", @{@"sample" : @1, @"frames" : @32})[@"data"];
      call(@"sample.patch", @{@"sample" : @1, @"values" : @{@"volume" : @17}}, true);
      NSDictionary *patched = call(@"sample.get", @{@"sample" : @1})[@"data"];
      check([patched[@"volume"] intValue] == 17 && [info[@"rate"] isEqual:patched[@"rate"]],
            "Sample patch preserves unspecified settings");
      call(
          @"sample.pcm.set", @{
            @"sample" : @0,
            @"format" : pcm[@"format"],
            @"channels" : pcm[@"channels"],
            @"rate" : pcm[@"rate"],
            @"data" : pcm[@"data"]
          },
          true);
      call(@"history.undo", @{@"domain" : @"document"}, true);
      check([pcm isEqual:call(
                             @"sample.pcm.get",
                             @{@"sample" : @1,
                               @"frames" : @32})[@"data"]],
            "PCM import/undo preserves original sample");
      call(
          @"sample.pcm.set", @{
            @"sample" : @1,
            @"format" : pcm[@"format"],
            @"channels" : pcm[@"channels"],
            @"rate" : pcm[@"rate"],
            @"data" : pcm[@"data"]
          },
          true);
      check([call(
                @"sample.get",
                @{@"sample" : @1})[@"data"][@"frames"] intValue] == 32,
            "Existing sample PCM replaced");
      call(@"history.undo", @{@"domain" : @"document"}, true);
      check([pcm isEqual:call(
                             @"sample.pcm.get",
                             @{@"sample" : @1,
                               @"frames" : @32})[@"data"]],
            "PCM replacement undo restores bytes and frame count");
      NSNumber *instrument = call(@"instrument.create", @{@"sample" : @2}, true)[@"data"][@"instrument"];
      call(
          @"instrument.patch", @{
            @"instrument" : instrument,
            @"values" : @{
              @"name" : @"API drums",
              @"envelope" : @0,
              @"points" : @[ @[ @0, @64 ], @[ @12, @32 ] ],
              @"enabled" : @YES
            }
          },
          true);
      check([call(@"instrument.get", @{@"instrument" : instrument})[@"data"][@"name"] isEqual:@"API drums"],
            "Instrument metadata and envelope edit");
      NSDictionary *descriptor = @{
        @"type" : @(kAudioUnitType_Effect),
        @"subtype" : @(kAudioUnitSubType_LowPassFilter),
        @"manufacturer" : @(kAudioUnitManufacturer_Apple),
        @"name" : @"Apple Lowpass"
      };
      call(@"plugin.add", @{@"descriptor" : descriptor}, true);
      NSArray *parameters = call(@"plugin.parameters.get", @{@"slot" : @0})[@"data"];
      NSDictionary *parameter = parameters[0];
      NSNumber *identifier = parameter[@"id"];
      double value = ([parameter[@"min"] doubleValue] + [parameter[@"max"] doubleValue]) / 2;
      NSString *state = call(@"plugin.state.get", @{@"slot" : @0})[@"data"][@"data"];
      call(
          @"plugin.parameters.set",
          @{@"slot" : @0,
            @"values" : @[
              @{@"id" : identifier,
                @"value" : @(value)}
            ]},
          true);
      call(
          @"automation.replaceLane", @{
            @"slot" : @0,
            @"id" : identifier,
            @"points" : @[
              @{@"frame" : @4800,
                @"value" : @(value)},
              @{@"frame" : @0,
                @"value" : parameter[@"min"]}
            ]
          },
          true);
      check([call(@"automation.get", @{})[@"data"][@"total"] intValue] == 2, "Programmatic automation lane");
      call(@"history.undo", @{@"domain" : @"plugins"}, true);
      check([call(@"automation.get", @{})[@"data"][@"total"] intValue] == 0, "Automation lane undo");
      call(@"plugin.state.set", @{@"slot" : @0, @"data" : state}, true);
      NSError *error = nil;
      NSString *prior = session.automationRevision;
      check([session pluginParameter:0 identifier:[identifier integerValue] value:value record:NO error:&error],
            "Native parameter edit");
      check(![prior isEqual:session.automationRevision], "Native plugin edits invalidate API revision");
      prior = session.automationRevision;
      check([session editPattern:0 row:1 channel:0 values:@[ @61, @1, @1, @40, @0, @0 ] error:&error],
            "Native pattern edit");
      check(![prior isEqual:session.automationRevision], "Native pattern edits invalidate API revision");
      NSString *fixture = [NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent
          stringByAppendingPathComponent:@"test-plugins/ResonanceFixture.vst3"];
      call(
          @"plugin.add", @{
            @"descriptor" : @{
              @"type" : @0,
              @"subtype" : @0,
              @"manufacturer" : @0,
              @"name" : @"API VST3 instrument",
              @"format" : @"VST3",
              @"path" : fixture,
              @"classID" : @"5245534F4E414E43494E535452550001",
              @"isInstrument" : @YES
            }
          },
          true);
      call(@"plugin.assign", @{@"slot" : @1, @"instrument" : instrument}, true);
      call(@"plugin.parameters.set", @{@"slot" : @1, @"values" : @[ @{@"id" : @7, @"value" : @0.27} ]}, true);
      NSString *vstState = call(@"plugin.state.get", @{@"slot" : @1})[@"data"][@"data"];
      call(@"plugin.parameters.set", @{@"slot" : @1, @"values" : @[ @{@"id" : @7, @"value" : @0.72} ]}, true);
      call(@"plugin.state.set", @{@"slot" : @1, @"data" : vstState}, true);
      check(std::abs([call(
                         @"plugin.parameters.get",
                         @{@"slot" : @1})[@"data"][0][@"value"] doubleValue] -
                     0.27) < 1e-5,
            "VST3 opaque state restores its parameter");
      reject(
          @"plugin.parameters.set", @{
            @"expectedRevision" : session.automationRevision,
            @"slot" : @1,
            @"values" : @[
              @{@"id" : @7,
                @"value" : @0.8},
              @{@"id" : @999999,
                @"value" : @0.3}
            ]
          },
          -32602);
      check(std::abs([call(
                         @"plugin.parameters.get",
                         @{@"slot" : @1})[@"data"][0][@"value"] doubleValue] -
                     0.27) < 1e-5,
            "Invalid parameter batch cannot partially apply");
      call(
          @"automation.replaceLane",
          @{@"slot" : @0,
            @"id" : identifier,
            @"points" : @[
              @{@"frame" : @0,
                @"value" : @(value)}
            ]},
          true);
      call(
          @"automation.replaceLane",
          @{@"slot" : @1,
            @"id" : @7,
            @"points" : @[
              @{@"frame" : @100,
                @"value" : @0.9}
            ]},
          true);
      check([call(@"automation.get", @{})[@"data"][@"total"] intValue] == 2,
            "Editing another plugin lane preserves the first lane");
      NSString *path = [NSTemporaryDirectory()
          stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
      check([session savePath:path error:&error], "Save API mutations");
      TrackerSession *reopened = [TrackerSession new];
      check([reopened openPath:path error:&error], "Reopen API mutations");
      check([[session snapshot:0][@"cells"] isEqual:[reopened snapshot:0][@"cells"]], "API pattern changes persist");
      check([reopened snapshot:0][@"nativePlugins"][1][@"instrument"] != nil &&
                [[reopened snapshot:0][@"automationPoints"] intValue] == 2,
            "API plugin assignment and automation persist");
      [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
      prior = session.automationRevision;
      [session newSong:YES];
      check(![prior isEqual:session.automationRevision], "Document replacement has a fresh identity");
      check(!session.playing, "API tests never start audio");
      std::cout
          << "PASS automation API: atomic/dry-run patterns, validation, optimistic revisions, native edits, "
             "undo, PCM, instruments, AU/VST3 state/parameters, automation and project persistence; no audio output\n";
    } catch (const std::exception &e) {
      std::cerr << e.what() << '\n';
      return 1;
    }
  }
}
