#import "../Bridge/TrackerSession.h"
#include "editor/SampleArchive.hpp"
#include "editor/TrackerDocument.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace Tracker;
using namespace OpenMPT;
static void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
static std::vector<std::byte> bytes(NSData *data) {
  const auto *first = static_cast<const std::byte *>(data.bytes);
  return {first, first + data.length};
}
int main() {
  @autoreleasepool {
    try {
      NSString *folder = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
      std::filesystem::create_directories(folder.UTF8String);
      for (auto type : {MOD_TYPE_MOD, MOD_TYPE_XM, MOD_TYPE_S3M, MOD_TYPE_IT, MOD_TYPE_MPT}) {
        auto source = Document::demo(type);
        NSString *input = [folder stringByAppendingPathComponent:@"import.module"];
        source->save(input.UTF8String);
        TrackerSession *session = [TrackerSession new];
        NSError *error = nil;
        check([session openPath:input error:&error], "Open legacy fixture");
        auto call = [&](NSString *method, NSDictionary *params, bool mutation = false) {
          NSMutableDictionary *p = [params mutableCopy];
          if (mutation)
            p[@"expectedRevision"] = session.automationRevision;
          auto result = [session automationMethod:method params:p error:&error];
          if (!result)
            throw std::runtime_error(error.localizedDescription.UTF8String);
          return result[@"data"];
        };
        const int stride = type == MOD_TYPE_MOD ? 1 : 4;
        std::vector<uint8_t> pcm(17 * stride);
        for (size_t i = 0; i < pcm.size(); ++i)
          pcm[i] = uint8_t(i * 7 + 79);
        NSData *data = [NSData dataWithBytes:pcm.data() length:pcm.size()];
        call(
            @"sample.pcm.set", @{
              @"sample" : @1,
              @"format" : stride == 1 ? @"s8" : @"s16le",
              @"channels" : stride == 1 ? @1 : @2,
              @"rate" : @8363,
              @"data" : [data base64EncodedStringWithOptions:0]
            },
            true);
        call(
            @"sample.patch",
            @{@"sample" : @1,
              @"values" : @{@"loop" : @YES, @"loopStart" : @3, @"loopEnd" : @15}},
            true);
        NSDictionary *original = call(@"sample.pcm.get", @{@"sample" : @1});
        NSString *sampleID = [session snapshot:0][@"samples"][0][@"id"];
        call(@"sample.process", @{@"sample" : @1, @"operation" : @"trim", @"start" : @2, @"end" : @13}, true);
        NSDictionary *trimmed = call(@"sample.pcm.get", @{@"sample" : @1});
        check([trimmed[@"totalFrames"] intValue] == 11, "Exact odd trim through API");
        NSData *expected = [data subdataWithRange:NSMakeRange(2 * stride, 11 * stride)];
        check([trimmed[@"data"] isEqual:[expected base64EncodedStringWithOptions:0]],
              "Trim independently matches selected source PCM");
        call(@"history.undo", @{@"domain" : @"document"}, true);
        check([original isEqual:call(@"sample.pcm.get", @{@"sample" : @1})], "API structural undo exact");
        call(@"history.redo", @{@"domain" : @"document"}, true);
        auto before = [session snapshot:0];
        NSDictionary *info = call(@"sample.get", @{@"sample" : @1});
        check([info[@"loopStart"] intValue] == 1 && [info[@"loopEnd"] intValue] == 11,
              "Trim adjusts odd loop bounds exactly");
        NSData *serialized = session.serializedData;
        NSDictionary *root = [NSPropertyListSerialization propertyListWithData:serialized
                                                                       options:0
                                                                        format:nil
                                                                         error:nil];
        check([root[@"version"] isEqual:@6],
              "Export/recovery data uses native project without requiring annotations/plugins");
        Document native(bytes(root[@"module"]));
        check(native.song().GetSample(1).nLength == 11 && native.song().GetSample(1).nLoopStart == 1 &&
                  native.song().GetSample(1).nLoopEnd == 11 &&
                  std::memcmp(native.song().GetSample(1).sampleb(), expected.bytes, expected.length) == 0,
              "Project PCM/loops match independent trim reference");
        NSString *path = [folder stringByAppendingPathComponent:@"edited.resonance"];
        call(@"document.save", @{@"path" : path, @"overwrite" : @YES}, true);
        check([session openPath:path error:&error], "Reopen exact project");
        check([trimmed isEqual:call(@"sample.pcm.get", @{@"sample" : @1})], "Native save/reopen retains exact odd PCM");
        check([sampleID isEqual:[session snapshot:0][@"samples"][0][@"id"]],
              "Save/reopen retains sample identity without labels");
        check([before[@"cells"] isEqual:[session snapshot:0][@"cells"]], "Save/reopen retains pattern data");
        check([info isEqual:call(@"sample.get", @{@"sample" : @1})], "Save/reopen retains all exposed sample settings");
        if (type == MOD_TYPE_MOD) {
          NSString *lossy = [folder stringByAppendingPathComponent:@"lossy.mod"];
          NSData *sentinel = [@"keep original" dataUsingEncoding:NSUTF8StringEncoding];
          [sentinel writeToFile:lossy atomically:YES];
          auto revision = session.automationRevision;
          check(![session automationMethod:@"document.exportModule"
                                    params:@{
                                      @"path" : lossy,
                                      @"overwrite" : @YES,
                                      @"expectedRevision" : revision
                                    }
                                     error:&error],
                "Reject lossy module export");
          check([sentinel isEqual:[NSData dataWithContentsOfFile:lossy]] &&
                    [revision isEqual:session.automationRevision],
                "Failed module export retains destination file and document revision");
        }
        // Compare the actual exported WAV against an independently populated renderer.
        // The ordinary module is used only for song/pattern settings. PCM and loop
        // positions are installed from the original API input, without archive decoding.
        NSData *wrapped = root[@"module"];
        auto parts = splitSongSnapshot({static_cast<const std::byte *>(wrapped.bytes), wrapped.length});
        Renderer reference(std::vector<std::byte>(parts.module.begin(), parts.module.end()), 48000);
        auto &sample = reference.song().GetSample(1);
        sample.FreeSample();
        sample.nLength = 11;
        sample.uFlags.set(CHN_16BIT, stride == 4);
        sample.uFlags.set(CHN_STEREO, stride == 4);
        sample.uFlags.set(CHN_LOOP | CHN_PANNING);
        sample.nLoopStart = 1;
        sample.nLoopEnd = 11;
        check(sample.AllocateSample() != 0, "Independent renderer PCM allocation");
        std::memcpy(sample.sampleb(), expected.bytes, expected.length);
        sample.PrecomputeLoops(reference.song(), false);
        NSString *wave = [folder stringByAppendingPathComponent:@"edited.wav"];
        check([TrackerSession exportData:serialized path:wave error:&error], "Export exact project audio");
        std::ifstream audio(wave.UTF8String, std::ios::binary);
        audio.seekg(44);
        std::array<float, 1024> actual{}, expectedAudio{};
        size_t frames = 0;
        while (auto count = reference.render(expectedAudio.data(), 512)) {
          audio.read(reinterpret_cast<char *>(actual.data()), count * 2 * sizeof(float));
          check(bool(audio) && std::equal(actual.begin(), actual.begin() + count * 2, expectedAudio.begin()),
                "Native WAV matches independent PCM/loop renderer exactly");
          frames += count;
        }
        check(frames > 48000 && audio.peek() == std::char_traits<char>::eof(), "Exact complete WAV duration");
        // Each clipboard mode is also checked through the production API, project
        // writer, decoder and complete WAV exporter. The reference splice is built
        // directly from the input bytes, independently of clipboard planning/history.
        for (NSString *mode in @[ @"insert", @"overwrite", @"mix", @"replace" ]) {
          check([serialized writeToFile:path atomically:YES] && [session openPath:path error:&error],
                "Restore clipboard audio baseline");
          const auto *baseline = static_cast<const uint8_t *>(expected.bytes);
          std::vector<uint8_t> referencePCM(baseline, baseline + expected.length);
          std::vector<uint8_t> inserted(pcm.begin() + 4 * stride, pcm.begin() + 9 * stride);
          const bool insert = [mode isEqual:@"insert"], replace = [mode isEqual:@"replace"],
                     mix = [mode isEqual:@"mix"];
          const size_t removed = insert ? 0 : replace ? 7 : 5;
          if (mix) {
            for (size_t i = 0; i < inserted.size(); i += stride == 1 ? 1 : 2) {
              if (stride == 1) {
                int value = int(int8_t(inserted[i])) + int(int8_t(referencePCM[2 + i]));
                inserted[i] = uint8_t(int8_t(std::clamp(value, -128, 127)));
              } else {
                int16_t a, b;
                std::memcpy(&a, inserted.data() + i, 2);
                std::memcpy(&b, referencePCM.data() + 2 * stride + i, 2);
                int16_t value = int16_t(std::clamp(int(a) + int(b), -32768, 32767));
                std::memcpy(inserted.data() + i, &value, 2);
              }
            }
          }
          referencePCM.erase(referencePCM.begin() + 2 * stride, referencePCM.begin() + (2 + removed) * stride);
          referencePCM.insert(referencePCM.begin() + 2 * stride, inserted.begin(), inserted.end());
          NSData *sourceClip = [data subdataWithRange:NSMakeRange(4 * stride, 5 * stride)];
          NSDictionary *clipboard = call(
              @"sample.clipboard.set", @{
                @"format" : stride == 1 ? @"s8" : @"s16le",
                @"channels" : stride == 1 ? @1 : @2,
                @"rate" : @8363,
                @"data" : [sourceClip base64EncodedStringWithOptions:0]
              },
              true);
          NSMutableDictionary *paste = [@{
            @"sample" : @1,
            @"at" : @2,
            @"mode" : mode,
            @"rateMode" : @"keep-frames",
            @"clipboardId" : clipboard[@"clipboardId"]
          } mutableCopy];
          if (replace)
            paste[@"end"] = @9;
          call(@"sample.paste", paste, true);
          NSData *pastedProject = session.serializedData;
          check([pastedProject writeToFile:path atomically:YES] && [session openPath:path error:&error],
                "Reopen clipboard project before audio export");
          Renderer clipboardReference(std::vector<std::byte>(parts.module.begin(), parts.module.end()), 48000);
          auto &target = clipboardReference.song().GetSample(1);
          target.FreeSample();
          target.nLength = uint32_t(referencePCM.size() / stride);
          target.uFlags.set(CHN_16BIT, stride == 4);
          target.uFlags.set(CHN_STEREO, stride == 4);
          target.uFlags.set(CHN_LOOP | CHN_PANNING);
          target.nLoopStart = 1;
          target.nLoopEnd = target.nLength;
          check(target.AllocateSample() != 0, "Independent clipboard render allocation");
          std::memcpy(target.sampleb(), referencePCM.data(), referencePCM.size());
          target.PrecomputeLoops(clipboardReference.song(), false);
          check([TrackerSession exportData:session.serializedData path:wave error:&error], "Export clipboard audio");
          std::ifstream clipboardAudio(wave.UTF8String, std::ios::binary);
          clipboardAudio.seekg(44);
          size_t clipboardFrames = 0;
          while (auto count = clipboardReference.render(expectedAudio.data(), 512)) {
            clipboardAudio.read(reinterpret_cast<char *>(actual.data()), count * 2 * sizeof(float));
            check(bool(clipboardAudio) && std::equal(actual.begin(), actual.begin() + count * 2, expectedAudio.begin()),
                  "Complete clipboard WAV matches independently spliced PCM and loop geometry exactly");
            clipboardFrames += count;
          }
          check(clipboardFrames == frames && clipboardAudio.peek() == std::char_traits<char>::eof(),
                "Clipboard audio duration is unchanged and complete");
        }
        // Bad version/container combinations and truncated sample archives never replace a live document.
        auto revision = session.automationRevision;
        NSMutableDictionary *bad = [root mutableCopy];
        for (id version : @[ @YES, @0, @1.5, @7, @3 ]) {
          bad[@"version"] = version;
          NSData *invalid = [NSPropertyListSerialization dataWithPropertyList:bad
                                                                       format:NSPropertyListBinaryFormat_v1_0
                                                                      options:0
                                                                        error:nil];
          [invalid writeToFile:path atomically:YES];
          check(![session openPath:path error:&error] && [revision isEqual:session.automationRevision],
                "Reject malformed project version without replacement");
        }
        bad[@"version"] = @6;
        auto broken = packSongSnapshot(parts.module, parts.samples.first(parts.samples.size() - 1));
        bad[@"module"] = [NSData dataWithBytes:broken.data() length:broken.size()];
        NSData *invalid = [NSPropertyListSerialization dataWithPropertyList:bad
                                                                     format:NSPropertyListBinaryFormat_v1_0
                                                                    options:0
                                                                      error:nil];
        [invalid writeToFile:path atomically:YES];
        check(![session openPath:path error:&error] && [revision isEqual:session.automationRevision],
              "Reject truncated sample archive atomically");
      }
      std::filesystem::remove_all(folder.UTF8String);
      std::cout << "PASS API trim/undo, native sample persistence and identity in five formats, checked module export, "
                   "exact independently rendered WAV and malformed project atomicity\n";
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "FAIL " << e.what() << '\n';
      return 1;
    }
  }
}
