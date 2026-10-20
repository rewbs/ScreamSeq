#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include "editor/SongTiming.hpp"
#include "../Bridge/NativePropertyTree.hpp"
#include <cmath>
#include <iostream>

using namespace Tracker;
using namespace OpenMPT;

static void check(bool condition, const char *message) {
  if(!condition) throw std::runtime_error(message);
}
static NSDictionary *call(TrackerSession *session, NSString *method, NSDictionary *params, bool write = false) {
  NSMutableDictionary *request = [params mutableCopy];
  if(write) request[@"expectedRevision"] = session.automationRevision;
  NSError *error = nil;
  NSDictionary *reply = [session automationMethod:method params:request error:&error];
  if(!reply) throw std::runtime_error(error.localizedDescription.UTF8String ?: "Session API failed");
  return reply[@"data"];
}
static NSMutableDictionary *decode(NSData *bytes) {
  NSError *error = nil;
  id value = [NSPropertyListSerialization propertyListWithData:bytes options:NSPropertyListMutableContainersAndLeaves format:nil error:&error];
  check([value isKindOfClass:NSDictionary.class], "Fixture project must be a dictionary");
  return value;
}
static NSData *encode(NSDictionary *value) {
  NSError *error = nil;
  NSData *bytes = [NSPropertyListSerialization dataWithPropertyList:value format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
  check(bytes != nil, "Encode recovery fixture");
  return bytes;
}
static NSData *writeProject(NSDictionary *value, NSString *path) {
  NSData *bytes = encode(value);
  NSError *error = nil;
  check([bytes writeToFile:path options:NSDataWritingAtomic error:&error], "Write recovery fixture");
  return bytes;
}
static void open(TrackerSession *session, NSString *path) {
  NSError *error = nil;
  if(![session openPath:path error:&error]) throw std::runtime_error(error.localizedDescription.UTF8String ?: "Project open failed");
}
static NSArray *commands(TrackerSession *session, NSInteger pattern) {
  return call(session, @"pattern.effects.get", @{@"pattern": @(pattern)})[@"commands"];
}
static void warnings(TrackerSession *session, NSString *path) {
  NSDictionary *snapshot = [session snapshot:0];
  NSArray *items = snapshot[@"loadWarnings"];
  check([items isKindOfClass:NSArray.class] && items.count > 0, "Recovered metadata must provide load warnings");
  for(id item in items) check([item isKindOfClass:NSString.class] && [item length] > 0, "Each recovery warning is readable text");
  check([snapshot[@"requiresSaveAs"] boolValue], "Recovered project must require Save As");
  check([snapshot[@"loadSourcePath"] isEqual:path], "Recovery retains the source path for overwrite protection");
}
static void clean(TrackerSession *session) {
  NSDictionary *snapshot = [session snapshot:0];
  check([snapshot[@"loadWarnings"] isKindOfClass:NSArray.class] && [snapshot[@"loadWarnings"] count] == 0, "Current saved project reopens without recovery warnings");
  check(![snapshot[@"requiresSaveAs"] boolValue], "Current saved project does not require Save As");
}
static void saveCopy(TrackerSession *session, NSString *source, NSData *original, NSString *copy, bool retainedExtensions=false) {
  NSError *error = nil;
  check(![session savePath:source error:&error], "Recovered source cannot be overwritten by ordinary Save");
  NSString *revision = session.automationRevision;
  check(![session automationMethod:@"document.save" params:@{@"path": source, @"overwrite": @YES, @"expectedRevision": revision} error:&error], "Explicit API overwrite still protects the recovered source");
  check([revision isEqual:session.automationRevision], "Refused source overwrite does not mutate history");
  check([[NSData dataWithContentsOfFile:source] isEqual:original], "Refused overwrite preserves every original byte");
  check([session savePath:copy error:&error], "Recovered project can be saved to a new copy");
  check([[NSData dataWithContentsOfFile:source] isEqual:original], "Saving a copy leaves every original byte untouched");
  check(![[session snapshot:0][@"requiresSaveAs"] boolValue], "Successful Save As clears the overwrite guard");
  TrackerSession *reopened = [TrackerSession new];
  open(reopened, copy);
  if(retainedExtensions)warnings(reopened,copy);else clean(reopened);
  for(NSInteger pattern : {0, 1}) check([commands(session, pattern) isEqual:commands(reopened, pattern)], "Recovery copy preserves every accepted command exactly");
}

static NSData *baseline(NSString *directory) {
  Document document(MOD_TYPE_MPT);
  const int second = document.addPattern(32, false, 0);
  check(second == 1, "Recovery fixture has a second pattern");
  document.transaction([](CSoundFile &song) {
    auto timing = songTiming(song);
    timing.mode = TempoMode::Modern;
    timing.rowsPerBeat = 6;
    timing.rowsPerMeasure = 24;
    timing.groove.clear();
    applySongTiming(song, timing);
    check(song.Patterns[0].SetSignature(8, 32), "Set pattern override to eight rows per beat");
  });
  NSString *module = [directory stringByAppendingPathComponent:@"signature-source.mptm"];
  document.save(module.UTF8String);
  TrackerSession *source = [TrackerSession new];
  open(source, module);
  for(NSInteger pattern : {0, 1}) {
    call(source, @"pattern.effect.set", @{@"pattern": @(pattern), @"row": @0, @"channel": @0, @"column": @0,
      @"command": @{@"kind": pattern ? @"nudge-reverse" : @"nudge-forward", @"value": @.7123456789, @"durationBeats": @1.0, @"offset": @1234}}, true);
  }
  call(source, @"pattern.effect.set", @{@"pattern": @0, @"row": @2, @"channel": @0, @"column": @0,
    @"command": @{@"kind": @"note-cut", @"offset": @2345}}, true);
  NSData *data = source.serializedData;
  NSDictionary *root = decode(data);
  NSData *snapshot = root[@"module"];
  const auto *begin = static_cast<const std::byte *>(snapshot.bytes);
  Document checkSnapshot(std::vector<std::byte>(begin, begin + snapshot.length));
  check(checkSnapshot.song().m_nDefaultRowsPerBeat == 6 && checkSnapshot.song().Patterns[0].GetOverrideSignature() && checkSnapshot.song().Patterns[0].GetRowsPerBeat() == 8 && !checkSnapshot.song().Patterns[1].GetOverrideSignature(), "Embedded snapshot retains default and per-pattern beat signatures");
  return data;
}

static void legacyNudges(NSData *baselineBytes, NSString *directory) {
  NSMutableDictionary *root = decode(baselineBytes);
  for(NSMutableDictionary *command in root[@"native"][@"performance"][@"commands"]) {
    const auto kind = [command[@"kind"] unsignedIntegerValue];
    if(kind == uint8_t(PatternCommandKind::NudgeForward) || kind == uint8_t(PatternCommandKind::NudgeReverse)) {
      [command removeObjectForKey:@"durationBeats"];
      command[@"duration"] = kind == uint8_t(PatternCommandKind::NudgeForward) ? @(3 * 65536 + 1234) : @(6 * 65536 + 2345);
    }
  }
  NSString *source = [directory stringByAppendingPathComponent:@"legacy-nudges.screamseq"];
  NSData *original = writeProject(root, source);
  TrackerSession *session = [TrackerSession new];
  open(session, source);
  warnings(session, source);
  check([[NSData dataWithContentsOfFile:source] isEqual:original], "Opening legacy nudges never rewrites their source");
  const double expected[] = {(3 + 1234. / 65536) / 8, (6 + 2345. / 65536) / 6};
  for(NSInteger pattern : {0, 1}) {
    NSDictionary *command = commands(session, pattern).firstObject;
    check(std::abs([command[@"durationBeats"] doubleValue] - expected[pattern]) < 1e-14, "Legacy duration migrates with the actual pattern override or song signature");
    check(command[@"duration"] == nil, "Migrated nudge exposes only canonical beat duration");
    check([command[@"position"] unsignedIntegerValue] == 1234 && std::abs([command[@"value"] doubleValue] - .7123456789) < 1e-14, "Migration leaves onset and strength intact");
  }
  check(commands(session, 0).count == 2 && [commands(session, 0)[1][@"kind"] isEqual:@"note-cut"], "Migration preserves unrelated valid commands");
  saveCopy(session, source, original, [directory stringByAppendingPathComponent:@"migrated-copy.screamseq"]);
}

static void partialRecovery(NSData *baselineBytes, NSString *directory) {
  NSMutableDictionary *root = decode(baselineBytes);
  root[@"version"] = @999;
  root[@"futureContainerField"] = @{@"ignored": @YES};
  root[@"native"][@"version"] = @999;
  root[@"native"][@"futureNativeField"] = @42;
  root[@"native"][@"performance"][@"futurePerformanceField"] = @"known fields survive";
  NSMutableArray *items = root[@"native"][@"performance"][@"commands"];
  items[0][@"futureCommandField"] = @YES;
  NSMutableDictionary *unknown = [items[0] mutableCopy];
  unknown[@"kind"] = @999;
  unknown[@"position"] = @200000;
  [items addObject:unknown];
  NSMutableDictionary *malformed = [items[0] mutableCopy];
  malformed[@"value"] = @"not numeric";
  malformed[@"position"] = @300000;
  [items addObject:malformed];
  NSString *source = [directory stringByAppendingPathComponent:@"future-partial.screamseq"];
  NSData *original = writeProject(root, source);
  TrackerSession *session = [TrackerSession new];
  open(session, source);
  warnings(session, source);
  check(commands(session, 0).count == 2 && commands(session, 1).count == 1, "Unknown and malformed commands are skipped individually; all good siblings remain");
  check(std::abs([commands(session, 0)[0][@"durationBeats"] doubleValue] - 1) < 1e-14, "Unknown versions and fields retain known command precision");
  NSString *copy=[directory stringByAppendingPathComponent:@"future-recovered-copy.screamseq"];
  saveCopy(session, source, original, copy, true);
  NSDictionary *saved=decode([NSData dataWithContentsOfFile:copy]);
  check([saved[@"futureContainerField"] isEqual:root[@"futureContainerField"]],"Recovery keeps opaque root extension active");
  NSDictionary *archived=saved[@"screamseqLoadRecovery"][@"sourceSections"];
  check([archived[@"native"] isEqual:root[@"native"]] && !archived[@"module"],
    "Recovery retains exact original invalid native sections inactive without a second module");
  check(saved[@"native"][@"futureNativeField"]==nil &&
    [saved[@"native"][@"performance"][@"commands"] count]==3,
    "Recovery archive must not reactivate rejected commands");

  NSString *revision = session.automationRevision;
  NSData *before = session.serializedData;
  NSError *error = nil;
  check(![session automationMethod:@"pattern.effect.set" params:@{@"expectedRevision": revision, @"pattern": @0, @"row": @4, @"channel": @0, @"column": @0,
    @"command": @{@"kind": @"nudge-forward", @"value": @.5, @"durationBeats": @1, @"unknownField": @1}} error:&error], "File recovery must not make API command mutations permissive");
  check([revision isEqual:session.automationRevision] && [before isEqual:session.serializedData], "Strict API rejection leaves the recovered document unchanged");

  NSDictionary *beforeSnapshot = [session snapshot:0];
  for(bool missing : {false, true}) {
    NSMutableDictionary *broken = decode(baselineBytes);
    if(missing) [broken removeObjectForKey:@"module"];
    else broken[@"module"] = [@"invalid snapshot" dataUsingEncoding:NSUTF8StringEncoding];
    NSString *badPath = [directory stringByAppendingPathComponent:missing ? @"missing-snapshot.screamseq" : @"corrupt-snapshot.screamseq"];
    NSData *badBytes = writeProject(broken, badPath);
    check(![session openPath:badPath error:&error], "Required snapshot corruption must reject the entire open");
    check([revision isEqual:session.automationRevision] && [before isEqual:session.serializedData], "Failed project open preserves the current document and revision atomically");
    NSDictionary *after = [session snapshot:0];
    for(NSString *key in @[@"loadWarnings", @"requiresSaveAs", @"loadSourcePath"]) check([beforeSnapshot[key] isEqual:after[key]], "Failed open preserves recovery state atomically");
    check([[NSData dataWithContentsOfFile:badPath] isEqual:badBytes], "Failed open never rewrites the broken source");
  }
}

static void malformedPluginContainers(NSData *baselineBytes, NSString *directory) {
  NSUInteger index=0;
  for(id invalid in @[@42, @"not a plugin array"]) {
    NSMutableDictionary *root=decode(baselineBytes);root[@"plugins"]=invalid;
    NSString *source=[directory stringByAppendingPathComponent:[NSString stringWithFormat:@"malformed-plugins-%lu.screamseq",(unsigned long)index]];
    NSData *original=writeProject(root,source);
    TrackerSession *session=[TrackerSession new];open(session,source);warnings(session,source);
    check([[session snapshot:0][@"nativePlugins"] count]==0,"Malformed plugin container is omitted safely");
    check(commands(session,0).count==2&&commands(session,1).count==1,"Malformed plugin container retains independent valid native commands");
    check([[NSData dataWithContentsOfFile:source] isEqual:original],"Opening malformed plugin container preserves original bytes");
    saveCopy(session,source,original,[directory stringByAppendingPathComponent:[NSString stringWithFormat:@"plugins-copy-%lu.screamseq",(unsigned long)index++]]);
  }
}

static void opaqueMetadata(NSData *baselineBytes,NSString *directory) {
  using namespace ScreamSeq;
  auto original=NativePropertyTree::decode(baselineBytes);
  Project::Value sentinel={{"integer",int64_t(9007199254740993LL)},{"real",1.0},{"negativeZero",-0.0},
    {"data",Project::Value::binary({0,255,1})},{"text","music \xF0\x9F\x8E\xB5"}};
  sentinel["date"]=Project::Value::binary({0x3f,0xf0,0,0,0,0,0,0},uint64_t(Project::OpaqueType::Date));
  sentinel["uid"]=Project::Value::binary({0,1,0,0,0,0,0,1},uint64_t(Project::OpaqueType::UID));
  sentinel["wideInteger"]=Project::Value::binary(std::vector<uint8_t>(16,0xff),uint64_t(Project::OpaqueType::WideInteger));
  sentinel["nan32"]=Project::Value::binary({0x7f,0xc0,0,7},uint64_t(Project::OpaqueType::Real32NaN));
  original["futureRoot"]=sentinel;original["native"]["futureNative"]=sentinel;
  original["native"]["tracks"][0][1]["futureTrack"]=sentinel;
  NSString *source=[directory stringByAppendingPathComponent:@"opaque-typed.screamseq"];
  NSData *bytes=NativePropertyTree::encode(original);
  check([bytes writeToFile:source atomically:YES],"Write typed opaque fixture");
  TrackerSession *session=[TrackerSession new];open(session,source);warnings(session,source);
  auto checkExtensions=[&](NSData *data) {
    auto tree=NativePropertyTree::decode(data);
    check(Project::sameStoredValue(tree.at("futureRoot"),sentinel) &&
      Project::sameStoredValue(tree.at("native").at("futureNative"),sentinel) &&
      Project::sameStoredValue(tree.at("native").at("tracks")[0][1].at("futureTrack"),sentinel),
      "Native session edit/save/reopen changed exact opaque types or payloads");
    check(!tree.contains("screamseqLoadRecovery"),"Supported typed extensions must remain active, not be archived as lost");
    return tree;
  };
  const auto before=checkExtensions(session.serializedData);
  call(session,@"document.patch",@{@"title":@"Opaque metadata survives unrelated edits"},true);
  const auto edited=checkExtensions(session.serializedData);
  check(edited.at("module")!=before.at("module"),"Unrelated musical edit must persist in snapshot");
  NSString *revision=session.automationRevision;
  NSError *error=nil;
  NSString *failed=[directory stringByAppendingPathComponent:@"missing-parent/copy.screamseq"];
  check(![session savePath:failed error:&error],"Missing destination parent must fail Save");
  check([revision isEqual:session.automationRevision] &&
    Project::sameStoredValue(checkExtensions(session.serializedData),edited),"Failed Save changed document/preservation state");
  warnings(session,source);
  NSString *copy=[directory stringByAppendingPathComponent:@"opaque-copy.screamseq"];
  check([session savePath:copy error:&error],"Save typed opaque project copy");
  check(Project::sameStoredValue(checkExtensions([NSData dataWithContentsOfFile:copy]),edited),"Save copy differs from accepted edited tree");
  call(session,@"history.undo",@{@"domain":@"all"},true);
  check(Project::sameStoredValue(checkExtensions(session.serializedData),before),"Undo after Save lost original opaque metadata");
  call(session,@"history.redo",@{@"domain":@"all"},true);
  check(Project::sameStoredValue(checkExtensions(session.serializedData),edited),"Redo after Save changed opaque metadata");
  auto broken=original;broken["module"]=Project::Value::binary({1,2,3});
  NSString *bad=[directory stringByAppendingPathComponent:@"opaque-broken.screamseq"];
  check([NativePropertyTree::encode(broken) writeToFile:bad atomically:YES],"Write broken snapshot fixture");
  revision=session.automationRevision;
  check(![session openPath:bad error:&error] && [revision isEqual:session.automationRevision] &&
    Project::sameStoredValue(checkExtensions(session.serializedData),edited),"Failed Open replaced captured opaque tree");
  TrackerSession *reopened=[TrackerSession new];open(reopened,copy);
  check(Project::sameStoredValue(checkExtensions(reopened.serializedData),edited),"Reopened typed project changed known or opaque fields");
  check([[NSData dataWithContentsOfFile:source] isEqual:bytes],"All operations must leave original source bytes untouched");
  [session newSong:NO];
  check(!NativePropertyTree::decode(session.serializedData).contains("futureRoot"),"New document inherited prior opaque metadata");

  // XML remains admitted; its parsed typed values are promoted to binary once.
  NSMutableDictionary *xml=decode(baselineBytes);
  xml[@"futureXML"]=@{@"bytes":[NSData dataWithBytes:"abc" length:3],@"number":@(9007199254740993LL),@"text":@"music \U0001F3B5"};
  NSData *xmlBytes=[NSPropertyListSerialization dataWithPropertyList:xml format:NSPropertyListXMLFormat_v1_0 options:0 error:&error];
  NSString *xmlPath=[directory stringByAppendingPathComponent:@"opaque-xml.screamseq"];
  check(xmlBytes && [xmlBytes writeToFile:xmlPath atomically:YES],"Write XML import fixture");
  open(session,xmlPath);
  check([decode(session.serializedData)[@"futureXML"] isEqual:xml[@"futureXML"]],"XML typed extension import changed values");
  check([[NSData dataWithContentsOfFile:xmlPath] isEqual:xmlBytes],"XML import changed source file");

  auto unavailable=NativePropertyTree::decode(baselineBytes);
  const Project::Value rackState=Project::Value::binary({0,255,42,0,128});
  unavailable["plugins"]=Project::Value::array({{{"format","VST3"},{"type",0},{"subtype",0},{"manufacturer",0},
    {"name","Unavailable preservation fixture"},{"path","/ScreamSeqMissing/Preservation.vst3"},
    {"classID","00112233445566778899AABBCCDDEEFF"},{"isInstrument",false},{"bypass",false},
    {"instanceID","preservation-missing-plugin"},{"instrument",0},{"instrumentAssignments",Project::Value::array()},
    {"audioLayout",""},{"auxiliaryInputs",Project::Value::array()},{"auxiliaryOutputs",Project::Value::array()},
    {"state",rackState},{"futurePlugin",sentinel}}});
  NSString *pluginPath=[directory stringByAppendingPathComponent:@"opaque-missing-plugin.screamseq"];
  NSData *pluginBytes=NativePropertyTree::encode(unavailable);
  check([pluginBytes writeToFile:pluginPath atomically:YES],"Write unavailable plugin fixture");
  open(session,pluginPath);
  call(session,@"document.patch",@{@"title":@"Missing plugin state survives"},true);
  NSString *pluginCopy=[directory stringByAppendingPathComponent:@"opaque-missing-plugin-copy.screamseq"];
  check([session savePath:pluginCopy error:&error],"Save copy with unavailable plugin");
  open(reopened,pluginCopy);
  const auto pluginTree=NativePropertyTree::decode(reopened.serializedData);
  const auto &retained=pluginTree.at("plugins").at(0);
  check(retained.at("state")==rackState && retained.at("instanceID")=="preservation-missing-plugin" &&
    retained.at("path")==unavailable.at("plugins")[0].at("path") &&
    Project::sameStoredValue(retained.at("futurePlugin"),sentinel),
    "Missing provider changed opaque state, stable identity, original location hint or plugin extension");
  check([[NSData dataWithContentsOfFile:pluginPath] isEqual:pluginBytes],"Missing plugin edit/save changed original source");
}

static void moduleExportRecovery(NSString *directory) {
  // Container-version recovery alone has no native musical loss to block
  // export, but the original source must stay protected afterwards.
  Document fresh(MOD_TYPE_IT,4);Document imported(fresh.serialize());
  const auto module=imported.moduleExportData();
  NSString *input=[directory stringByAppendingPathComponent:@"export-baseline.it"];
  check([[NSData dataWithBytes:module.data() length:module.size()] writeToFile:input atomically:YES],
    "Write export recovery baseline");
  TrackerSession *session=[TrackerSession new];open(session,input);
  NSMutableDictionary *root=decode(session.serializedData);root[@"version"]=@999;
  NSString *source=[directory stringByAppendingPathComponent:@"export-protected.screamseq"];
  NSData *original=writeProject(root,source);open(session,source);warnings(session,source);
  NSString *output=[directory stringByAppendingPathComponent:@"export-recovered.it"];
  NSString *revision=session.automationRevision;NSDictionary *before=[session snapshot:0];
  check([call(session,@"document.exportModule",@{@"path":output},true)[@"written"] boolValue],
    "Recovered module-compatible project can export a separate file");
  check([session.automationRevision isEqual:revision]&&[[session snapshot:0] isEqual:before],
    "Export cleared recovery source protection or changed the song");
  NSError *error=nil;
  check(![session savePath:source error:&error]&&[[NSData dataWithContentsOfFile:source] isEqual:original],
    "Export unlocked the original recovered source");
  root[@"futureExtension"]=[NSData dataWithBytes:"\0\xff" length:2];
  NSString *opaque=[directory stringByAppendingPathComponent:@"export-opaque.screamseq"];
  NSData *opaqueOriginal=writeProject(root,opaque);open(session,opaque);
  NSData *exportOriginal=[NSData dataWithContentsOfFile:output];
  for(NSNumber *dry in @[@YES,@NO]) {
    error=nil;revision=session.automationRevision;
    check(![session automationMethod:@"document.exportModule" params:@{@"expectedRevision":revision,
      @"path":output,@"overwrite":@YES,@"dryRun":dry} error:&error],
      "Opaque retained project fields must block module export");
    check([session.automationRevision isEqual:revision]&&
      [[NSData dataWithContentsOfFile:output] isEqual:exportOriginal]&&
      [[NSData dataWithContentsOfFile:opaque] isEqual:opaqueOriginal],
      "Rejected opaque export changed a destination, original source or revision");
  }
}
int main(int argc, char **argv) { @autoreleasepool { try {
  if(argc == 3 && std::string(argv[1]) == "--probe") {
    NSString *path = @(argv[2]);
    NSData *original = [NSData dataWithContentsOfFile:path];
    TrackerSession *session = [TrackerSession new];
    open(session, path);
    NSDictionary *snapshot = [session snapshot:0], *project = decode(session.serializedData);
    check([[NSData dataWithContentsOfFile:path] isEqual:original], "Probe must leave the original file untouched");
    NSDictionary *summary = @{@"opened": @YES, @"loadWarnings": snapshot[@"loadWarnings"] ?: @[], @"requiresSaveAs": snapshot[@"requiresSaveAs"] ?: @NO,
      @"patternCount": @([project[@"native"][@"patterns"] count]), @"commandCount": @([project[@"native"][@"performance"][@"commands"] count])};
    NSData *json = [NSJSONSerialization dataWithJSONObject:summary options:0 error:nil];
    std::cout.write(static_cast<const char *>(json.bytes), json.length); std::cout << '\n';
    return 0;
  }
  check(argc == 1, "Usage: project-recovery-tests [--probe project.screamseq]");
  NSString *directory = [NSTemporaryDirectory() stringByAppendingPathComponent:[@"screamseq-project-recovery-" stringByAppendingString:NSUUID.UUID.UUIDString]];
  check([NSFileManager.defaultManager createDirectoryAtPath:directory withIntermediateDirectories:NO attributes:nil error:nil], "Create isolated recovery test folder");
  NSData *bytes = baseline(directory);
  NSString *current = [directory stringByAppendingPathComponent:@"current.screamseq"];
  check([bytes writeToFile:current atomically:YES], "Write current baseline");
  TrackerSession *session = [TrackerSession new]; open(session, current); clean(session);
  legacyNudges(bytes, directory);
  partialRecovery(bytes, directory);
  malformedPluginContainers(bytes, directory);
  opaqueMetadata(bytes,directory);
  moduleExportRecovery(directory);
  [NSFileManager.defaultManager removeItemAtPath:directory error:nil];
  std::cout << "PASS project recovery: legacy NF/NR signatures, warnings, guarded Save As, typed extensions/XML, inactive recovery archives, Undo/Redo, failed Save/Open, strict API and byte-identical originals\n";
  return 0;
} catch(const std::exception &error) {
  std::cerr << "FAIL " << error.what() << '\n'; return 1;
} } }
