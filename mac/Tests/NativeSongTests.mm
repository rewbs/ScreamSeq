#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include "editor/SampleArchive.hpp"
#include "editor/ArrangementTools.hpp"
#include <iostream>
#include <stdexcept>
using namespace Tracker;
using namespace OpenMPT;
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static void matrixAPITests(NSString *folder) {
  auto document=Document::demo();document->editOrder(0,0,"after");
  NSString *input=[folder stringByAppendingPathComponent:@"matrix.mptm"];document->save(input.UTF8String);
  TrackerSession *session=[TrackerSession new];NSError *error=nil;check([session openPath:input error:&error],"Open matrix API fixture");
  auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary * {
    auto request=[params mutableCopy];if(write)request[@"expectedRevision"]=session.automationRevision;
    auto response=[session automationMethod:method params:request error:&error];
    if(!response)throw std::runtime_error(error.localizedDescription.UTF8String);return response;
  };
  call(@"pattern.notes.set",@{@"pattern":@0,@"events":@[@{@"channel":@4,@"position":@17,@"note":@61,@"instrument":@1},
    @{@"channel":@4,@"position":@(2*65536),@"note":@255}]},true);
  call(@"pattern.effects.set",@{@"pattern":@0,@"columns":@[@{@"channel":@4,@"count":@2}],
    @"commands":@[@{@"channel":@4,@"position":@65536,@"column":@1,@"kind":@"tracker",@"effect":@(CMD_PANNING8),@"parameter":@32}]},true);
  NSDictionary *original=call(@"arrangement.matrix",@{})[@"data"];
  NSDictionary *source=original[@"orders"][0][@"blocks"][4];
  check([source[@"events"] intValue]==3&&[source[@"notes"] intValue]==1&&[source[@"preciseEvents"] intValue]==2&&
    [source[@"nativeFxEvents"] intValue]==1&&[source[@"trackerEvents"] intValue]==0,"Mac matrix missed native-only notes/FX");
  auto params=[@{@"sourceOrder":@0,@"targetOrder":@1,@"sourceChannel":@4,@"targetChannel":@5} mutableCopy];
  params[@"dryRun"]=@YES;NSString *revision=session.automationRevision;auto preview=call(@"arrangement.copyBlock",params,true);
  check(![preview[@"changed"] boolValue]&&[revision isEqual:session.automationRevision]&&[preview[@"data"][@"wouldChange"] boolValue]&&
    [preview[@"data"][@"changedCells"] intValue]==0&&[preview[@"data"][@"clonesPattern"] boolValue],"Mac native-only preview mutated/skipped");
  [params removeObjectForKey:@"dryRun"];auto applied=call(@"arrangement.copyBlock",params,true);check([applied[@"changed"] boolValue],"Mac adapter skipped native-only copy");
  NSDictionary *matrix=call(@"arrangement.matrix",@{})[@"data"];
  check([matrix[@"orders"][1][@"blocks"][5][@"events"] intValue]==3&&[matrix[@"orders"][0] isEqual:original[@"orders"][0]],"Mac copied event summary or source alias changed");
  check([matrix[@"orders"][1][@"id"] isEqual:original[@"orders"][1][@"id"]],"Mac independent copy replaced occurrence ID");
  call(@"history.undo",@{@"domain":@"document"},true);check([original isEqual:call(@"arrangement.matrix",@{})[@"data"]],"Mac matrix copy was not one Undo");
  params[@"targetOrder"]=@0;params[@"targetChannel"]=@4;revision=session.automationRevision;
  auto noop=call(@"arrangement.copyBlock",params,true);check(![noop[@"changed"] boolValue]&&![noop[@"data"][@"wouldChange"] boolValue]&&[revision isEqual:session.automationRevision],"Mac native no-op changed revision");
  call(@"history.redo",@{@"domain":@"document"},true);check([matrix isEqual:call(@"arrangement.matrix",@{})[@"data"]],"Mac no-op consumed matrix Redo");
  NSString *saved=[folder stringByAppendingPathComponent:@"matrix.resonance"];
  check([session savePath:saved error:&error]&&[session openPath:saved error:&error]&&[matrix isEqual:call(@"arrangement.matrix",@{})[@"data"]],"Mac matrix native persistence");
  check([call(@"arrangement.matrix",@{@"startOrder":matrix[@"totalOrders"]})[@"data"][@"orders"] count]==0,"Mac matrix end page changed");
  for(NSDictionary *bad in @[@{@"orderCount":@0},@{@"channelCount":@0},@{@"channelCount":@33},@{@"startOrder":@YES}]) {
    NSError *failure=nil;check(![session automationMethod:@"arrangement.matrix" params:bad error:&failure]&&failure.code==-32602,"Mac matrix malformed page accepted");
  }
  std::cout<<"PASS Mac matrix API native-only density/copy, dry-run/no-op, alias identities, Undo/Redo and persistence\n";
}
int main() {
  @autoreleasepool {
    try {
      Document doc;
      auto firstID = doc.native().sequences[0].orders[0].id;
      auto patternID = doc.native().patterns.at(0).id;
      doc.annotate([](NativeSong &n) { n.sequences[0].orders[0].name = "Intro"; n.patterns.at(0).annotation = "Shared notes"; });
      doc.editOrder(0, 0, "before");
      check(doc.native().sequences[0].orders[1].id == firstID, "Insertion preserves original order identity");
      const auto insertedID = doc.native().sequences[0].orders[0].id;
      check(insertedID != firstID, "Repeated pattern receives a distinct order identity");
      doc.editOrder(1, 0, "up");
      check(doc.native().sequences[0].orders[0].name == "Intro", "Section follows order move");
      doc.removeOrder(0);
      check(doc.native().sequences[0].orders[0].id == insertedID, "Remove preserves remaining slots");
      doc.undo(); check(doc.native().sequences[0].orders[0].id == firstID, "Undo restores deleted identity");
      doc.redo(); doc.undo(); doc.undo(); doc.undo();
      check(doc.native().sequences[0].orders.size() == 1 && doc.native().sequences[0].orders[0].id == firstID,
            "Undo complete order editing history");
      doc.editOrder(0, 0, "after");
      check(doc.native().sequences[0].orders[1].id > insertedID, "Discarded redo identities are never reused");
      check(doc.native().patterns.at(0).id == patternID, "Order edits preserve pattern identity");
      doc.transaction([](CSoundFile &s) { Document::resizeChannels(s, 12); });
      const auto extraTrack = doc.native().tracks.at(11).id;
      doc.undo(); check(doc.native().tracks.size() == 8, "Undo restores metadata shape");
      doc.redo(); check(doc.native().tracks.at(11).id == extraTrack, "Redo restores track identity");
      const auto revision = doc.revision;
      auto metadata = doc.native();
      try { doc.annotate([](NativeSong &n) { n.tracks.at(0).id = n.tracks.at(1).id; }); check(false, "Must reject duplicate ID"); }
      catch (const std::invalid_argument &) {}
      check(doc.revision == revision && doc.native() == metadata, "Invalid metadata edit is atomic");
      auto blocks = Document::demo();
      blocks->editOrder(0, 0, "after");
      const auto destinationID = blocks->native().sequences[0].orders[1].id;
      ArrangementCopy copy{0, 1, 0, 4};
      auto plan = prepareArrangementCopy(*blocks, copy);
      check(plan.clone && plan.edits.size() == 16 && blocks->song().Order()[1] == 0,
            "Block preview is read-only and detects shared patterns");
      applyArrangementCopy(*blocks, plan);
      check(blocks->song().Order()[1] == plan.targetPattern && blocks->cell(0, 0, 4) == Cell{} &&
            blocks->cell(plan.targetPattern, 0, 4) == blocks->cell(0, 0, 0), "Independent block paste preserves other pattern uses");
      check(blocks->native().sequences[0].orders[1].id == destinationID &&
            blocks->native().patterns.at(0).id != blocks->native().patterns.at(plan.targetPattern).id,
            "Independent paste preserves slot identity and creates pattern identity");
      blocks->undo();
      check(blocks->song().Order()[1] == 0 && blocks->native().patterns.size() == 1, "One undo removes cloned pattern and edits");
      blocks->redo(); blocks->undo();
      copy.makeUnique = false;
      plan = prepareArrangementCopy(*blocks, copy);
      applyArrangementCopy(*blocks, plan);
      check(blocks->song().Order()[1] == 0 && blocks->cell(0, 0, 4).note != 0, "Explicit shared pattern editing");
      blocks->undo();
      blocks->addPattern(32, false, 0); copy.targetOrder = 2;
      try { prepareArrangementCopy(*blocks, copy); check(false, "Reject unequal block lengths by default"); }
      catch (const std::invalid_argument &) {}
      copy.clip = true; plan = prepareArrangementCopy(*blocks, copy);
      check(plan.edits.size() == 8, "Explicit clipping copies overlapping rows");
      blocks->annotate([](NativeSong &n) { n.tracks.at(0).name = "Changed"; });
      try { applyArrangementCopy(*blocks, plan); check(false, "Reject stale prepared block"); }
      catch (const std::invalid_argument &) {}
      auto folder = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
      [[NSFileManager defaultManager] createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
      matrixAPITests(folder);
      for (auto type : {MOD_TYPE_MOD, MOD_TYPE_XM, MOD_TYPE_S3M, MOD_TYPE_IT, MOD_TYPE_MPT}) {
        auto source = Document::demo(type);
        NSString *input = [folder stringByAppendingPathComponent:@"source.module"];
        source->save(input.UTF8String);
        TrackerSession *session = [TrackerSession new];
        NSError *error = nil;
        check([session openPath:input error:&error], "Open supported module");
        auto call = [&](NSString *method, NSDictionary *p, bool mutation = false) -> NSDictionary * {
          NSMutableDictionary *params = [p mutableCopy];
          if (mutation) params[@"expectedRevision"] = session.automationRevision;
          auto response = [session automationMethod:method params:params error:&error];
          if (!response) throw std::runtime_error(error.localizedDescription.UTF8String);
          return response[@"data"];
        };
        auto arrangement = call(@"arrangement.get", @{});
        auto original = [session snapshot:0];
        NSString *slot = arrangement[@"orders"][0][@"id"];
        call(@"song.annotate", @{@"id": slot, @"name": @"Intro 🎹", @"color": @0x52cdb4}, true);
        NSString *revision = session.automationRevision;
        call(@"song.annotate", @{@"id": slot, @"name": @"Intro 🎹"}, true);
        check([revision isEqual:session.automationRevision], "No-op annotation preserves revision and history");
        call(@"song.annotate", @{@"id": original[@"patterns"][0][@"id"], @"name": @"Theme", @"annotation": @"Build into the chorus"}, true);
        call(@"song.annotate", @{@"id": original[@"tracks"][0][@"id"], @"name": @"Lead"}, true);
        check(![session savePath:[folder stringByAppendingPathComponent:@"loss.module"] error:&error], "Reject metadata-losing module save");
        NSString *path = [folder stringByAppendingPathComponent:@"song.resonance"];
        check([session savePath:path error:&error], "Save version 4 project");
        auto before = [session snapshot:0];
        check([session openPath:path error:&error], error.localizedDescription.UTF8String ?: "Reopen native metadata");
        auto after = [session snapshot:0];
        for (NSString *key in @[@"orderMetadata", @"patterns", @"tracks", @"samples", @"instruments", @"cells"])
          check([before[key] isEqual:after[key]], "Project metadata/identities/cells survive reopen");
        NSArray *sections = call(@"arrangement.get", @{})[@"sections"];
        check([sections count] == 1 && [sections[0][@"name"] isEqual:@"Intro 🎹"], "Named section persisted");
        call(@"order.edit", @{@"order": @0, @"pattern": @0, @"operation": @"before"}, true);
        check([call(@"arrangement.get", @{})[@"sections"][0][@"firstOrder"] intValue] == 1, "API section follows inserted order");
        call(@"history.undo", @{@"domain": @"document"}, true);
        check([call(@"arrangement.get", @{})[@"sections"][0][@"firstOrder"] intValue] == 0, "API undo restores section range");
        NSData *data = [NSData dataWithContentsOfFile:path];
        NSMutableDictionary *root = [[NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:nil] mutableCopy];
        auto write = [&](NSDictionary *value) {
          NSData *bytes = [NSPropertyListSerialization dataWithPropertyList:value format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
          [bytes writeToFile:path atomically:YES];
        };
        NSMutableDictionary *bad = [root mutableCopy];
        NSMutableDictionary *native = [root[@"native"] mutableCopy];
        native[@"patterns"] = @[]; bad[@"native"] = native; write(bad);
        revision = session.automationRevision;
        check(![session openPath:path error:&error] && [revision isEqual:session.automationRevision], "Reject malformed metadata without replacing current song");
        check([root[@"version"] isEqual:@6], "Current native project is version 6");
        for(int old=1;old<6;++old) {
          auto oldRoot=[root mutableCopy];oldRoot[@"version"]=@(old);write(oldRoot);
          revision=session.automationRevision;
          check(![session openPath:path error:&error]&&[revision isEqual:session.automationRevision],"Old native project versions are rejected without changing the current song");
        }
        for(int old=1;old<17;++old) {
          auto oldRoot=[root mutableCopy];auto metadata=[root[@"native"] mutableCopy];metadata[@"version"]=@(old);oldRoot[@"native"]=metadata;write(oldRoot);
          revision=session.automationRevision;
          check(![session openPath:path error:&error]&&[revision isEqual:session.automationRevision],"Historical native metadata is rejected atomically");
        }
        call(@"mixer.enable", @{}, true);
        NSString *busID = call(@"mixer.get", @{})[@"buses"][0][@"id"];
        call(@"mixer.bus.set", @{@"bus": busID, @"prePan": @0.375}, true);
        check([session savePath:path error:&error] && [session openPath:path error:&error], "Input balance saves/reopens over every source format");
        check([call(@"mixer.get", @{})[@"buses"][0][@"prePan"] doubleValue] == .375, "Every format retains native input balance exactly");
        call(@"document.patch", @{@"title": @"Changed"}, true);
        call(@"history.undo", @{@"domain": @"document"}, true);
        check([call(@"mixer.get", @{})[@"buses"][0][@"prePan"] doubleValue] == .375, "Structural Undo preserves input balance alongside source-format snapshots");
        call(@"mixer.bus.set", @{@"bus": busID, @"prePan": @0}, true);
        auto compatible = [NSPropertyListSerialization propertyListWithData:[session serializedData] options:0 format:nil error:nil];
        check([compatible[@"native"][@"version"] intValue] == 17, "Clearing input balance retains the same current metadata format");
      }
      [[NSFileManager defaultManager] removeItemAtPath:folder error:nil];
      std::cout << "PASS native song identities, metadata, order/section history, no-op/invalid atomicity, all five module formats, current project roundtrip, historical version rejection and loss prevention\n";
      return 0;
    } catch (const std::exception &error) {
      std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
  }
}
