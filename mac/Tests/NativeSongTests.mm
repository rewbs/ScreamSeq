#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include "editor/SampleArchive.hpp"
#include "editor/ArrangementTools.hpp"
#include "ModuleFixture.hpp"
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
      const auto masterIdentity=doc.native().masterID;
      check(masterIdentity>0 && masterIdentity<doc.native().nextID && !doc.native().mixer.active(),"Master identity is reserved before routing is materialized");
      const auto initialNative=doc.native();auto projected=initialNative;projected.ensureMixer();
      check(projected.nextID==initialNative.nextID && projected.mixer.buses.back().id==masterIdentity && doc.native()==initialNative,"Implicit graph projection never allocates or changes the document");
      doc.annotate([](NativeSong &n){n.ensureMixer();});doc.undo();
      check(doc.native().masterID==masterIdentity && !doc.native().mixer.active(),"Undo materialization retains the reserved Master");
      doc.redo();check(doc.native().mixer.buses.back().id==masterIdentity,"Redo reuses Master identity");doc.undo();
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
      {
        // Duplicates and independent block copies keep the pattern's own settings.
        auto timed = Document::demo();
        timed->transaction([](CSoundFile &s) {
          auto &pattern = s.Patterns[0];
          TempoSwing swing; swing.resize(3, TempoSwing::Unity); swing[0] += TempoSwing::Unity / 4; swing.Normalize();
          check(pattern.SetSignature(3, 12) && pattern.SetName("Verse"), "Pattern settings fixture");
          pattern.SetTempoSwing(swing); pattern.SetColor(0x336699);
        });
        auto same = [&](int copy, int rows) {
          const auto &s = timed->song(); const auto &a = s.Patterns[0], &b = s.Patterns[copy];
          return b.GetNumRows() == ROWINDEX(rows) && b.GetOverrideSignature() && b.GetRowsPerBeat() == 3 && b.GetRowsPerMeasure() == 12 &&
            b.HasTempoSwing() && b.GetTempoSwing() == a.GetTempoSwing() && b.GetName() == "Verse" && b.GetColor() == 0x336699 &&
            timed->cell(copy, 0, 0) == timed->cell(0, 0, 0) && timed->cell(copy, 16, 1) == timed->cell(0, 16, 1);
        };
        const auto duplicate = timed->addPattern(64, true, 0), shorter = timed->addPattern(32, true, 0);
        check(same(duplicate, 64) && timed->song().Patterns[duplicate] == timed->song().Patterns[0], "Pattern duplicate keeps signature, tempo swing, name and colour");
        check(same(shorter, 32), "Shorter pattern duplicate keeps settings and overlapping rows");
        timed->undo(); timed->undo();
        timed->editOrder(0, 0, "after");
        ArrangementCopy block{0, 1, 0, 4};
        auto unique = prepareArrangementCopy(*timed, block);
        check(unique.clone, "Shared pattern requires an independent copy");
        applyArrangementCopy(*timed, unique);
        check(same(unique.targetPattern, 64), "Independent block copy keeps signature, tempo swing, name and colour");
      }
      {
        // Merge replaces occupied FX 1 while retaining destination-only events;
        // overwrite copies the entire block, including empty native FX slots.
        auto fx = Document::demo();
        fx->edit({Edit{0, 0, 0, {}, {49, 1, VOLCMD_VOLUME, 38, CMD_VIBRATO, 0x34}}});
        fx->editOrder(0, 0, "after");
        fx->annotate([](NativeSong &n) {
          const auto p = n.patterns.at(0).id, t = n.tracks.at(4).id;
          n.performance.columns[t] = 2;
          n.performance.commands = {{p, t, 100, 0, 0, PatternCommandKind::PitchSet, 0, 1.0},
            {p, t, 65536, 0, 0, PatternCommandKind::PitchSet, 0, 2.0}, {p, t, 0, 0, 1, PatternCommandKind::PitchSet, 0, 3.0}};
        });
        auto commands = [&](int pattern) {
          std::vector<double> values; const auto &n = fx->native();
          for (const auto &c : n.performance.commands) if (c.pattern == n.patterns.at(pattern).id) values.push_back(c.value);
          std::sort(values.begin(), values.end()); return values;
        };
        ArrangementCopy block{0, 1, 0, 4};
        for (const std::string mode : {"merge", "overwrite"}) for (bool unique : {true, false}) {
          block.makeUnique = unique;
          block.mode = mode;
          const auto plan = prepareArrangementCopy(*fx, block);
          check(plan.clone == unique, "Block copy fixture");
          applyArrangementCopy(*fx, plan);
          const auto expected = mode == "merge" ? std::vector<double>{2.0, 3.0} : std::vector<double>{};
          check(fx->cell(plan.targetPattern, 0, 4).effect == CMD_VIBRATO && commands(plan.targetPattern) == expected,
                "Block copy must merge or overwrite destination native FX according to the selected mode");
          if (unique) check(commands(0) == std::vector<double>{1.0, 2.0, 3.0}, "Independent block copy leaves the shared pattern's commands");
          fx->native().validate(fx->song());
          fx->undo();
          check(commands(0) == std::vector<double>{1.0, 2.0, 3.0} && fx->cell(0, 0, 4) == Cell{}, "One Undo restores the replaced precise command");
        }
      }
      auto folder = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
      [[NSFileManager defaultManager] createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
      matrixAPITests(folder);
      for (auto type : {MOD_TYPE_MOD, MOD_TYPE_XM, MOD_TYPE_S3M, MOD_TYPE_IT, MOD_TYPE_MPT}) {
        NSString *input = [folder stringByAppendingPathComponent:@"source.module"];
        Test::writeDemoModule(type, input.UTF8String);
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
        {
          // Deleting plain cells changes no native data, so it need not stop playback.
          NSDictionary *preview = call(@"pattern.transform", @{@"operation": @"clear", @"scope": @"pattern", @"pattern": @0, @"dryRun": @YES}, true);
          check([preview[@"changedCells"] intValue] > 0 && ![preview[@"effectsChanged"] boolValue], "Delete over plain cells reports unchanged native data");
        }
        auto arrangement = call(@"arrangement.get", @{});
        auto original = [session snapshot:0];
        auto readMaster=[&]() -> NSString * {return [(NSArray *)call(@"mixer.get",@{@"includeImplicit":@YES})[@"buses"] lastObject][@"id"];};
        NSString *reservedMaster=readMaster(),*readRevision=session.automationRevision;
        auto graphView=call(@"graph.get",@{@"includeImplicitMixer":@YES});
        check([[(NSArray *)graphView[@"mixer"][@"buses"] lastObject][@"id"] isEqual:reservedMaster] && [readRevision isEqual:session.automationRevision],"Mixer and graph implicit reads agree without editing history");
        call(@"graph.create",@{},true);
        check([readMaster() isEqual:reservedMaster],"Unrelated graph allocations cannot change the displayed implicit Master");
        call(@"history.undo",@{@"domain":@"document"},true);
        check([readMaster() isEqual:reservedMaster],"Undo and further read projections preserve Master");
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
        check([readMaster() isEqual:reservedMaster],"Reserved implicit Master persists through native save/reopen");
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
        check([session openPath:path error:&error] && [[session snapshot:0][@"loadWarnings"] count]>0 && [[session snapshot:0][@"requiresSaveAs"] boolValue], "Malformed optional identities recover the embedded song with warnings");
        auto corrupt=[root mutableCopy];corrupt[@"module"]=[@"invalid required snapshot" dataUsingEncoding:NSUTF8StringEncoding];write(corrupt);revision=session.automationRevision;NSData *accepted=session.serializedData;
        check(![session openPath:path error:&error] && [revision isEqual:session.automationRevision] && [accepted isEqual:session.serializedData], "Corrupt required snapshot rejects atomically without replacing current song");
        check([root[@"version"] isEqual:@6], "Current native project is version 6");
        for(int old=1;old<6;++old) {
          auto oldRoot=[root mutableCopy];oldRoot[@"version"]=@(old);write(oldRoot);
          revision=session.automationRevision;
          check([session openPath:path error:&error]&&[[session snapshot:0][@"loadWarnings"] count]>0&&[[session snapshot:0][@"requiresSaveAs"] boolValue],"Historical container versions recover known content with warnings and Save As protection");
          check([readMaster() isEqual:reservedMaster],"Historical container recovery preserves stable Master identity");
        }
        for(int old=1;old<17;++old) {
          auto oldRoot=[root mutableCopy];auto metadata=[root[@"native"] mutableCopy];metadata[@"version"]=@(old);oldRoot[@"native"]=metadata;write(oldRoot);
          revision=session.automationRevision;
          check([session openPath:path error:&error]&&[[session snapshot:0][@"loadWarnings"] count]>0&&[[session snapshot:0][@"requiresSaveAs"] boolValue],"Historical native metadata versions recover known fields with explicit warnings");
          check([readMaster() isEqual:reservedMaster],"Historical native metadata recovery preserves stable Master identity");
        }
        write(root);check([session openPath:path error:&error],"Restore current fixture after historical recovery checks");
        call(@"mixer.enable", @{}, true);
        check([readMaster() isEqual:reservedMaster],"First routing edit materializes the same Master shown in the implicit view");
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
        check([compatible[@"native"][@"masterID"] isEqual:reservedMaster],"Current native metadata stores the reserved Master explicitly");
        auto conflicting=[compatible mutableCopy];auto conflictingNative=[compatible[@"native"] mutableCopy];
        conflictingNative[@"masterID"]=busID;conflicting[@"native"]=conflictingNative;write(conflicting);revision=session.automationRevision;
        NSData *conflictingBytes=[NSData dataWithContentsOfFile:path];
        check([session openPath:path error:&error]&&[[session snapshot:0][@"loadWarnings"] count]>0&&[[session snapshot:0][@"requiresSaveAs"] boolValue],"Conflicting optional Master identity recovers the embedded song with warnings");
        check([[session snapshot:0][@"cells"] isEqual:before[@"cells"]]&&! [session savePath:path error:&error]&&[[NSData dataWithContentsOfFile:path] isEqual:conflictingBytes],"Identity recovery preserves required song cells and every original source byte");
        auto currentWithoutField=[compatible mutableCopy];auto missingNative=[compatible[@"native"] mutableCopy];
        [missingNative removeObjectForKey:@"masterID"];currentWithoutField[@"native"]=missingNative;write(currentWithoutField);
        check([session openPath:path error:&error]&&[readMaster() isEqual:reservedMaster],"Current metadata without the optional field derives the existing Master identity");
        auto songControls=[compatible mutableCopy];auto controlsNative=[compatible[@"native"] mutableCopy];auto controlsGraph=[controlsNative[@"signalGraph"] mutableCopy];
        const auto sourceNumber=[controlsNative[@"nextID"] unsignedLongLongValue];NSString *sourceID=[NSString stringWithFormat:@"n%llu",sourceNumber];
        controlsNative[@"nextID"]=@(sourceNumber+1);
        controlsGraph[@"songSources"]=@[@{@"id":sourceID,@"kind":@"lfo",@"name":@"Rack motion",@"rate":@.5,@"phase":@.125,@"amount":@.75}];
        controlsGraph[@"songModulation"]=@[@{@"source":sourceID,@"plugin":@"unresolved-stable-target",@"parameter":@17,@"minimum":@(-.2),@"maximum":@.3,@"quantized":@YES}];
        controlsNative[@"signalGraph"]=controlsGraph;songControls[@"native"]=controlsNative;write(songControls);
        check([session openPath:path error:&error],"Current project loads song-level control metadata independently of reusable recipes");
        auto controlsBefore=call(@"graph.get",@{});check([controlsBefore[@"songSources"] count]==1&&[controlsBefore[@"songModulation"] count]==1,"Song-level source/target metadata appears in the graph projection");
        check([session savePath:path error:&error]&&[session openPath:path error:&error],"Song-level control metadata saves and reopens");
        auto controlsAfter=call(@"graph.get",@{});check([controlsBefore[@"songSources"] isEqual:controlsAfter[@"songSources"]]&&[controlsBefore[@"songModulation"] isEqual:controlsAfter[@"songModulation"]],"Song-level controls retain stable IDs, ranges and explicit quantization through persistence");
        auto duplicate=[controlsGraph mutableCopy];duplicate[@"songSources"]=@[controlsGraph[@"songSources"][0],controlsGraph[@"songSources"][0]];controlsNative[@"signalGraph"]=duplicate;songControls[@"native"]=controlsNative;write(songControls);revision=session.automationRevision;
        NSData *duplicateBytes=[NSData dataWithContentsOfFile:path];
        check([session openPath:path error:&error]&&[[session snapshot:0][@"loadWarnings"] count]>0&&[[session snapshot:0][@"requiresSaveAs"] boolValue],"Duplicate optional song control identities recover with explicit warnings");
        check([call(@"graph.get",@{})[@"songSources"] count]==0&&[call(@"graph.get",@{})[@"songModulation"] count]==0,"Invalid dependent source section is omitted as a whole");
        check([[session snapshot:0][@"cells"] isEqual:before[@"cells"]]&&[readMaster() isEqual:reservedMaster],"Skipping malformed sources preserves independent song cells and mixer identity");
        check(![session savePath:path error:&error]&&[[NSData dataWithContentsOfFile:path] isEqual:duplicateBytes],"Malformed optional source metadata remains protected from overwrite");
      }
      [[NSFileManager defaultManager] removeItemAtPath:folder error:nil];
      std::cout << "PASS native song identities, metadata, order/section history, no-op/invalid atomicity, all five module formats, current project roundtrip, historical recovery warnings and loss prevention\n";
      return 0;
    } catch (const std::exception &error) {
      std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
  }
}
