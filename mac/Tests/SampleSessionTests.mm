#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include <filesystem>
#include <iostream>
#include <thread>
static void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  @autoreleasepool {
    try {
      TrackerSession *session = [TrackerSession new];
      auto call = [&](NSString *method, NSDictionary *params, bool write = true) {
        NSMutableDictionary *p = [params mutableCopy];
        if (write)
          p[@"expectedRevision"] = session.automationRevision;
        NSError *error = nil;
        auto r = [session automationMethod:method params:p error:&error];
        if (!r)
          throw std::runtime_error(error.localizedDescription.UTF8String);
        return r;
      };
      const bool device = argc > 1 && std::string(argv[1]) == "--device";
      if (device) {
        auto doc = Tracker::Document::demo();
        doc->song().m_nDefaultGlobalVolume = 0;
        NSString *path =
            [NSTemporaryDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"sample-session-%@.mptm",
                                                                                              NSUUID.UUID.UUIDString]];
        doc->save(path.UTF8String);
        NSError *error = nil;
        check([session openPath:path error:&error], "Load silent playback fixture");
        std::filesystem::remove(path.UTF8String);
        check([session playOrder:0 error:&error], "Start silent sample fixture");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        check(session.playing && [[session telemetry][@"callbacks"] unsignedLongLongValue] > 0,
              "Actual CoreAudio callbacks running");
      }
      auto revision = session.automationRevision;
      auto preview = call(
          @"sample.process",
          @{@"sample" : @1,
            @"operation" : @"silence",
            @"start" : @1,
            @"end" : @200,
            @"dryRun" : @YES});
      check(![preview[@"changed"] boolValue] && [revision isEqual:session.automationRevision],
            "Preview retains revision");
      check(!device || session.playing, "Preview does not stop real playback");
      auto noop = call(@"sample.process", @{@"sample" : @1, @"operation" : @"gain", @"gainDB" : @0});
      check(![noop[@"changed"] boolValue] && [revision isEqual:session.automationRevision], "No-op retains revision");
      noop = call(@"sample.process", @{@"sample" : @1, @"operation" : @"trim"});
      check(![noop[@"changed"] boolValue], "Full-range trim no-op");
      NSError *error = nil;
      check(![session automationMethod:@"sample.process"
                                params:@{
                                  @"sample" : @1,
                                  @"operation" : @"silence",
                                  @"start" : @1,
                                  @"end" : @1,
                                  @"expectedRevision" : session.automationRevision
                                }
                                 error:&error] &&
                error.code == -32602,
            "Invalid selection rejected");
      check(!device || session.playing, "No-op and errors do not stop real playback");
      call(@"sample.waveform.get", @{@"sample" : @1, @"bins" : @1024}, false);
      check(!device || session.playing, "Waveform read keeps playback");
      call(@"sample.snap.get", @{@"sample" : @1, @"positions" : @[@0, @13, @17]}, false);
      call(@"sample.snap.get", @{@"sample" : @1, @"positions" : @[@0, @13, @17], @"mode" : @"grid", @"step" : @8}, false);
      check((!device || session.playing) && [revision isEqual:session.automationRevision],
            "Zero-crossing and grid snapping preserve actual playback and revision");
      NSString *saved = [NSTemporaryDirectory()
          stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
      auto savePreview = call(@"document.save", @{@"path" : saved, @"dryRun" : @YES});
      check(![savePreview[@"changed"] boolValue] && ![NSFileManager.defaultManager fileExistsAtPath:saved],
            "Save preview leaves revision and filesystem unchanged");
      auto savedResult = call(@"document.save", @{@"path" : saved});
      check(![savedResult[@"changed"] boolValue] && ![savedResult[@"playbackStopped"] boolValue] &&
                (!device || session.playing),
            "Native saving preserves actual transport and document revision");
      [[NSFileManager defaultManager] removeItemAtPath:saved error:nil];
      NSDictionary *original = call(@"sample.pcm.get", @{@"sample" : @1}, false)[@"data"];
      NSData *originalBytes = [[NSData alloc] initWithBase64EncodedString:original[@"data"] options:0];
      check(originalBytes.length >= 2, "Drawing transport fixture has PCM");
      const auto *bytes = static_cast<const uint8_t *>(originalBytes.bytes);
      const bool sixteen = [original[@"format"] isEqual:@"s16le"];
      const int first = sixteen ? static_cast<int16_t>(uint16_t(bytes[0]) | uint16_t(bytes[1]) << 8)
                               : static_cast<int8_t>(bytes[0]);
      const double firstValue = double(first) / (sixteen ? 32768.0 : 128.0);
      auto drawNoop = call(@"sample.draw", @{@"sample" : @1, @"channels" : @"left",
                                            @"points" : @[@{@"frame" : @0, @"value" : @(firstValue)}]});
      check(![drawNoop[@"changed"] boolValue] && [revision isEqual:session.automationRevision] &&
                (!device || session.playing), "An identical drawn point preserves revision and playback");
      NSDictionary *drawParams = @{@"sample" : @1, @"channels" : @"left",
                                    @"points" : @[@{@"frame" : @0, @"value" : @(firstValue == 0.5 ? -0.5 : 0.5)}]};
      NSMutableDictionary *drawPreviewParams = [drawParams mutableCopy];
      drawPreviewParams[@"dryRun"] = @YES;
      auto drawPreview = call(@"sample.draw", drawPreviewParams);
      check(![drawPreview[@"changed"] boolValue] && [drawPreview[@"data"][@"changedFrames"] intValue] == 1 &&
                (!device || session.playing), "Drawing preview preserves actual playback");
      check(![session automationMethod:@"sample.draw"
                                params:@{@"sample" : @1, @"expectedRevision" : session.automationRevision,
                                         @"points" : @[@{@"frame" : @0, @"value" : @2}]}
                                 error:&error] && error.code == -32602 && (!device || session.playing),
            "Rejected drawing leaves playback running");
      auto drawn = call(@"sample.draw", drawParams);
      check([drawn[@"changed"] boolValue] && !session.playing &&
                [drawn[@"playbackStopped"] boolValue] == device, "Actual drawing stops playback before PCM mutation");
      if (device)
        check([session playOrder:0 error:&error], "Restart before drawing undo");
      call(@"history.undo", @{@"domain" : @"document"});
      check(!session.playing && [original isEqual:call(@"sample.pcm.get", @{@"sample" : @1}, false)[@"data"]],
            "Drawing undo stops playback and restores exact source PCM");
      if (device)
        check([session playOrder:0 error:&error], "Restart before drawing redo");
      call(@"history.redo", @{@"domain" : @"document"});
      check(!session.playing, "Drawing redo stops playback");
      call(@"history.undo", @{@"domain" : @"document"});
      if (device)
        check([session playOrder:0 error:&error], "Restart before copy-to-new checks");
      auto copyPreview = call(@"sample.copyToNew", @{@"sample" : @1, @"start" : @3, @"end" : @17, @"dryRun" : @YES});
      check(![copyPreview[@"changed"] boolValue] && (!device || session.playing),
            "Copy-to-new preview preserves actual playback");
      auto newSample = call(@"sample.copyToNew", @{@"sample" : @1, @"start" : @3, @"end" : @17});
      check([newSample[@"changed"] boolValue] && !session.playing &&
                [newSample[@"playbackStopped"] boolValue] == device,
            "Copy-to-new stops playback before changing sample inventory");
      if (device)
        check([session playOrder:0 error:&error], "Restart before copy undo");
      call(@"history.undo", @{@"domain" : @"document"});
      check(!session.playing && [original isEqual:call(
                                                      @"sample.pcm.get",
                                                      @{@"sample" : @1}, false)[@"data"]],
            "Copy undo stops playback and leaves source audio unchanged");
      if (device)
        check([session playOrder:0 error:&error], "Restart before copy redo");
      call(@"history.redo", @{@"domain" : @"document"});
      check(!session.playing, "Copy redo stops playback");
      call(@"history.undo", @{@"domain" : @"document"});
      revision = session.automationRevision;
      if (device)
        check([session playOrder:0 error:&error], "Restart before clipboard checks");
      auto copied = call(@"sample.clipboard.copy", @{@"sample" : @1});
      NSString *clipboardID = copied[@"data"][@"clipboardId"];
      auto clipboard = call(@"sample.clipboard.get", @{@"frames" : @65536}, false);
      check(![copied[@"changed"] boolValue] && [revision isEqual:session.automationRevision] &&
                [clipboard[@"data"][@"data"] isEqual:original[@"data"]],
            "Copy and clipboard reads preserve document revision and exact PCM");
      noop = call(@"sample.paste", @{@"sample" : @1, @"at" : @0, @"mode" : @"overwrite", @"clipboardId" : clipboardID});
      check(![noop[@"changed"] boolValue] && (!device || session.playing), "Identical overwrite preserves playback");
      preview = call(@"sample.paste", @{@"sample" : @1, @"at" : @13, @"clipboardId" : clipboardID, @"dryRun" : @YES});
      check(![preview[@"changed"] boolValue] && [preview[@"data"][@"insertedFrames"] unsignedIntValue] > 0 &&
                (!device || session.playing),
            "Structural paste preview preserves playback");
      auto cutPreview = call(@"sample.cut", @{@"sample" : @1, @"start" : @1, @"end" : @20, @"dryRun" : @YES});
      check(![cutPreview[@"changed"] boolValue] &&
                [call(@"sample.clipboard.get", @{}, false)[@"data"][@"clipboardId"] isEqual:clipboardID],
            "Cut preview does not replace the clipboard");
      call(@"sample.clipboard.copy", @{@"sample" : @1});
      check(![session automationMethod:@"sample.paste"
                                params:@{
                                  @"sample" : @1,
                                  @"at" : @0,
                                  @"clipboardId" : clipboardID,
                                  @"expectedRevision" : session.automationRevision
                                }
                                 error:&error] &&
                error.code == -32001 && (!device || session.playing),
            "Stale clipboard identity cannot stop playback or apply a different clip");
      clipboardID = call(@"sample.clipboard.get", @{}, false)[@"data"][@"clipboardId"];
      auto splice = call(@"sample.paste", @{@"sample" : @1, @"at" : @13, @"clipboardId" : clipboardID});
      check([splice[@"changed"] boolValue] && !session.playing && [splice[@"playbackStopped"] boolValue] == device,
            "Structural paste stops playback before changing the allocation");
      if (device)
        check([session playOrder:0 error:&error], "Restart before structural undo");
      call(@"history.undo", @{@"domain" : @"document"});
      check(!session.playing && [original isEqual:call(
                                                      @"sample.pcm.get",
                                                      @{@"sample" : @1}, false)[@"data"]],
            "Structural undo stops playback and restores exact PCM");
      if (device)
        check([session playOrder:0 error:&error], "Restart before structural redo");
      call(@"history.redo", @{@"domain" : @"document"});
      check(!session.playing, "Structural redo stops playback");
      call(@"history.undo", @{@"domain" : @"document"});
      if (device)
        check([session playOrder:0 error:&error], "Restart before fixed-length edit");
      auto edited = call(@"sample.process", @{@"sample" : @1, @"operation" : @"silence", @"start" : @1, @"end" : @200});
      check([edited[@"changed"] boolValue] && !session.playing, "Actual PCM edit stops playback");
      check([edited[@"playbackStopped"] boolValue] == device, "Response reports actual playback stop");
      if (device)
        check([session playOrder:0 error:&error], "Restart before undo");
      call(@"history.undo", @{@"domain" : @"document"});
      check(!session.playing, "PCM undo stops playback");
      check([original isEqual:call(
                                  @"sample.pcm.get",
                                  @{@"sample" : @1}, false)[@"data"]],
            "Session undo restores exact PCM");
      if (device)
        check([session playOrder:0 error:&error], "Restart before redo");
      call(@"history.redo", @{@"domain" : @"document"});
      check(!session.playing, "PCM redo stops playback");
      std::vector<int16_t> constant(512, 12000);
      NSData *constantBytes = [NSData dataWithBytes:constant.data() length:constant.size() * sizeof(int16_t)];
      call(@"sample.pcm.set", @{@"sample":@1, @"format":@"s16le", @"channels":@2, @"rate":@22050,
                                @"data":[constantBytes base64EncodedStringWithOptions:0]});
      call(@"sample.patch", @{@"sample":@1, @"values":@{@"loop":@YES, @"loopStart":@64, @"loopEnd":@256}});
      NSDictionary *loopBefore = call(@"sample.get", @{@"sample":@1}, false)[@"data"];
      revision = session.automationRevision;
      if (device) check([session playOrder:0 error:&error], "Start silent crossfade transport fixture");
      auto crossfadePreview = call(@"sample.crossfade", @{@"sample":@1, @"mode":@"overlap", @"frames":@32, @"dryRun":@YES});
      check(![crossfadePreview[@"changed"] boolValue] && [crossfadePreview[@"data"][@"loopChanged"] boolValue] &&
            [revision isEqual:session.automationRevision] && (!device || session.playing),
            "Crossfade preview retains revision and real playback");
      auto crossfadeNoop = call(@"sample.crossfade", @{@"sample":@1, @"mode":@"preserve", @"frames":@32});
      check(![crossfadeNoop[@"changed"] boolValue] && [revision isEqual:session.automationRevision] && (!device || session.playing),
            "Exact no-op crossfade keeps actual playback");
      check(![session automationMethod:@"sample.crossfade" params:@{@"sample":@1, @"frames":@1,
                @"expectedRevision":session.automationRevision} error:&error] && error.code == -32602 && (!device || session.playing),
            "Invalid crossfade does not interrupt actual playback");
      auto crossfaded = call(@"sample.crossfade", @{@"sample":@1, @"mode":@"overlap", @"frames":@32});
      check([crossfaded[@"changed"] boolValue] && [crossfaded[@"data"][@"changedFrames"] intValue] == 0 &&
            !session.playing && [crossfaded[@"playbackStopped"] boolValue] == device,
            "A geometry-only crossfade stops playback before moving the live loop");
      call(@"document.save", @{@"path":saved});
      [[NSFileManager defaultManager] removeItemAtPath:saved error:nil];
      if (device) check([session playOrder:0 error:&error], "Restart before crossfade undo");
      call(@"history.undo", @{@"domain":@"document"});
      check(!session.playing && [loopBefore isEqual:call(@"sample.get", @{@"sample":@1}, false)[@"data"]],
            "Crossfade Undo after native saving stops playback and restores exact loop settings");
      if (device) check([session playOrder:0 error:&error], "Restart before crossfade redo");
      call(@"history.redo", @{@"domain":@"document"});
      check(!session.playing && [call(@"sample.get", @{@"sample":@1}, false)[@"data"][@"loopStart"] intValue] == 96 &&
            [call(@"sample.pcm.get", @{@"sample":@1}, false)[@"data"][@"data"] isEqual:[constantBytes base64EncodedStringWithOptions:0]],
            "Crossfade Redo stops playback, retains exact constant PCM and restores the moved loop");
      NSDictionary *savedLoops = call(@"sample.get", @{@"sample":@1}, false)[@"data"];
      NSDictionary *sustainLoop = @{@"enabled":@YES, @"start":@32, @"end":@192, @"pingpong":@YES};
      if (device) check([session playOrder:0 error:&error], "Restart before loop editing");
      revision = session.automationRevision;
      auto loopsPreview = call(@"sample.loops.set", @{@"sample":@1, @"sustain":sustainLoop, @"dryRun":@YES});
      check(![loopsPreview[@"changed"] boolValue] && [revision isEqual:session.automationRevision] && (!device || session.playing), "Loop preview preserves actual playback");
      auto loopsNoop = call(@"sample.loops.set", @{@"sample":@1, @"normal":@{@"enabled":@YES, @"start":@96, @"end":@256}});
      check(![loopsNoop[@"changed"] boolValue] && (!device || session.playing), "Unchanged loops preserve actual playback");
      check(![session automationMethod:@"sample.loops.set" params:@{@"sample":@1, @"sustain":@{@"enabled":@YES, @"start":@32, @"end":@257}, @"expectedRevision":revision} error:&error] && error.code == -32602 && (!device || session.playing), "Invalid loops preserve actual playback");
      auto loopsApplied = call(@"sample.loops.set", @{@"sample":@1, @"sustain":sustainLoop});
      check([loopsApplied[@"changed"] boolValue] && !session.playing && [loopsApplied[@"playbackStopped"] boolValue] == device, "Loop changes stop before changing playback geometry");
      if (device) check([session playOrder:0 error:&error], "Restart before loop undo");
      call(@"history.undo", @{@"domain":@"document"});
      check(!session.playing && [savedLoops isEqual:call(@"sample.get", @{@"sample":@1}, false)[@"data"]], "Loop Undo stops and restores both exact loops");
      if (device) check([session playOrder:0 error:&error], "Restart before loop redo");
      call(@"history.redo", @{@"domain":@"document"});
      check(!session.playing && [call(@"sample.get", @{@"sample":@1}, false)[@"data"][@"sustainPingpong"] boolValue] && [call(@"sample.pcm.get", @{@"sample":@1}, false)[@"data"][@"data"] isEqual:[constantBytes base64EncodedStringWithOptions:0]], "Loop Redo stops, restores sustain loop and preserves exact PCM");
      NSDictionary *reverseNormal = @{@"enabled":@YES, @"start":@96, @"end":@256, @"reverse":@YES};
      NSDictionary *reverseSustain = @{@"enabled":@YES, @"start":@32, @"end":@192, @"reverse":@YES};
      call(@"sample.loops.set", @{@"sample":@1, @"normal":reverseNormal, @"sustain":reverseSustain});
      auto reversedLoops = call(@"sample.get", @{@"sample":@1}, false)[@"data"];
      check([reversedLoops[@"reverseLoop"] boolValue] && [reversedLoops[@"sustainReverse"] boolValue], "Both reverse modes reach the session");
      if (device) check([session playOrder:0 error:&error], "Start silent native reverse-loop playback");
      auto reverseNoop = call(@"sample.loops.set", @{@"sample":@1, @"normal":reverseNormal, @"sustain":reverseSustain});
      check(![reverseNoop[@"changed"] boolValue] && (!device || session.playing), "Reverse-loop no-op retains actual playback");
      call(@"history.undo", @{@"domain":@"document"});
      check(!session.playing && ![call(@"sample.get", @{@"sample":@1}, false)[@"data"][@"reverseLoop"] boolValue], "Reverse-loop Undo stops and clears native traversal");
      if (device) check([session playOrder:0 error:&error], "Restart before reverse-loop Redo");
      call(@"history.redo", @{@"domain":@"document"});
      check(!session.playing && [reversedLoops isEqual:call(@"sample.get", @{@"sample":@1}, false)[@"data"]], "Reverse-loop Redo restores exact modes safely");
      [session stop];
      NSDictionary *retainedClipboard = call(@"sample.clipboard.get", @{@"frames" : @65536}, false)[@"data"];
      [session newSong:NO];
      check([retainedClipboard isEqual:call(
                                           @"sample.clipboard.get",
                                           @{@"frames" : @65536}, false)[@"data"]],
            "Starting another song preserves this session's clipboard and identity");
      TrackerSession *separateSession = [TrackerSession new];
      auto separateClipboard = [separateSession automationMethod:@"sample.clipboard.get" params:@{} error:&error];
      check(separateClipboard && ![separateClipboard[@"data"][@"available"] boolValue],
            "A new session has an independent empty clipboard");
      std::cout << "PASS sample session processing/clipboard preview/no-op/error/read revisions, fixed/structural "
                   "edit/undo/redo"
                << (device ? " with silent physical CoreAudio playback" : "") << '\n';
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "FAIL " << e.what() << '\n';
      return 1;
    }
  }
}
