// A VST3 path stored in a project is a hint, never permission to load code.
#import "../Bridge/TrackerSession.h"
#include "FixtureTrust.hpp"
#include "../Audio/AudioUnitHost.hpp"
#include "../Plugins/PluginInventory.hpp"
#include <iostream>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace Tracker;
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static NSDictionary *describe(const PluginDescriptor &d, NSString *path) {
  return @{@"format": @(d.format.c_str()), @"name": @(d.name.c_str()), @"type": @(d.type), @"subtype": @(d.subtype),
    @"manufacturer": @(d.manufacturer), @"path": path, @"classID": @(d.classID.c_str()), @"isInstrument": @(d.instrument)};
}
// Stands in for vendor or Foundation code raising inside an API call.
@interface RaisingParameters : NSDictionary
@end
@implementation RaisingParameters
- (NSUInteger)count { return 1; }
- (NSEnumerator *)keyEnumerator { return @[@"slot"].objectEnumerator; }
- (id)objectForKey:(id)key { [NSException raise:NSInvalidArgumentException format:@"Raised while reading %@", key]; return nil; }
@end
static bool imageLoaded(NSString *fragment) {
  for (uint32_t i = 0; i < _dyld_image_count(); ++i)
    if (const char *name = _dyld_get_image_name(i); name && [@(name) containsString:fragment])
      return true;
  return false;
}
static NSMutableDictionary *readProject(NSString *path) {
  return [NSPropertyListSerialization propertyListWithData:[NSData dataWithContentsOfFile:path]
    options:NSPropertyListMutableContainersAndLeaves format:nil error:nil];
}
static void writeProject(NSDictionary *root, NSString *path) {
  NSData *data = [NSPropertyListSerialization dataWithPropertyList:root format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
  check([data writeToFile:path options:NSDataWritingAtomic error:nil], "Write project fixture");
}
static NSMutableDictionary *graphRecipe(NSDictionary *root) {
  for (NSMutableDictionary *definition in root[@"native"][@"signalGraph"][@"library"])
    for (NSMutableDictionary *node in definition[@"nodes"])
      if (node[@"plugin"] && [node[@"plugin"][@"format"] isEqual:@"VST3"]) return node[@"plugin"];
  throw std::runtime_error("Project has no graph plugin recipe");
}
int main(int argc, const char **argv) {
  @autoreleasepool {
    NSFileManager *files = NSFileManager.defaultManager;
    NSString *folder = [PluginTrust::canonical(NSTemporaryDirectory()) stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    try {
      check(argc == 2, "Pass local VST3 fixture path");
      trustFixtureArguments(argc, argv);
      NSString *fixturePath = PluginTrust::canonical(@(argv[1]));
      NSString *untrustedFolder = [folder stringByAppendingPathComponent:@"Untrusted-Location"];
      NSString *trustedFolder = [folder stringByAppendingPathComponent:@"Chosen"];
      NSString *pickedFolder = [folder stringByAppendingPathComponent:@"Picked"];
      for (NSString *item in @[untrustedFolder, trustedFolder, pickedFolder])
        check([files createDirectoryAtPath:item withIntermediateDirectories:YES attributes:nil error:nil], "Create test directory");
      NSString *untrusted = [untrustedFolder stringByAppendingPathComponent:@"Hinted.vst3"];
      check([files copyItemAtPath:fixturePath toPath:untrusted error:nil], "Copy fixture to an untrusted location");
      // Command-line hosts persist nothing by default; this test uses its own store.
      check(PluginTrust::storePath() == nil && [PluginTrust::defaultStorePath() hasSuffix:@"/Library/Application Support/Resonance/Plugins/trusted-plugins-v1.json"],
            "The default store is durable application data and is unused by test executables");
      NSString *store = [[folder stringByAppendingPathComponent:@"Store"] stringByAppendingPathComponent:@"trusted-plugins-v1.json"];
      PluginTrust::setStorePath(store);
      auto stored = [&]() -> NSArray * {
        NSData *data = [NSData dataWithContentsOfFile:store];
        id root = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
        return [root isKindOfClass:NSDictionary.class] ? root[@"bundles"] : nil;
      };
      auto tamper = [&](id root) {
        NSData *data = [root isKindOfClass:NSData.class] ? root : [NSJSONSerialization dataWithJSONObject:root options:0 error:nil];
        check([data writeToFile:store atomically:YES] && chmod(store.fileSystemRepresentation, 0600) == 0, "Write store fixture");
      };
      const auto fixture = NativePlugin::discoverVST3(argv[1]);
      NSString *classID = @(fixture.at(0).classID.c_str());

      // Location rules, without loading anything.
      check(!PluginTrust::trusted(untrusted, nil) && !PluginTrust::resolve(untrusted, classID, nil), "A project location is untrusted by default");
      check([TrackerSession trustPluginLocation:trustedFolder] && ![TrackerSession trustPluginLocation:[folder stringByAppendingPathComponent:@"absent"]], "Trust requires an existing location");
      NSString *escaping = [trustedFolder stringByAppendingPathComponent:@"../Untrusted-Location/Hinted.vst3"];
      check([files fileExistsAtPath:escaping] && !PluginTrust::trusted(escaping, nil), "Parent references cannot leave a trusted folder");
      NSString *link = [trustedFolder stringByAppendingPathComponent:@"Link.vst3"];
      check(symlink(untrusted.fileSystemRepresentation, link.fileSystemRepresentation) == 0 && !PluginTrust::trusted(link, nil), "Symbolic links resolve to their real location");
      check(!PluginTrust::trusted(@"relative/Plugin.vst3", nil) && !PluginTrust::trusted([untrustedFolder stringByAppendingPathComponent:@"Absent.vst3"], nil) &&
            !PluginTrust::trusted(untrustedFolder, nil), "Relative, absent and non-bundle locations are rejected");
      NSArray *inventory = @[@{@"format": @"VST3", @"name": @"Fixture", @"path": fixturePath, @"classID": classID.lowercaseString,
        @"type": @0, @"subtype": @0, @"manufacturer": @0, @"isInstrument": @NO}];
      check([PluginTrust::resolve(untrusted, classID, inventory) isEqual:fixturePath], "An untrusted hint resolves its class ID through the inventory");
      check(PluginTrust::trusted(fixturePath, inventory) && !PluginTrust::resolve(untrusted, @"00000000000000000000000000000000", inventory), "Only the inventory's own bundles and classes are accepted");

      // A user-initiated add trusts the bundle it names; saving and reopening then works.
      NSError *error = nil;
      TrackerSession *session = [TrackerSession new];
      auto call = [&](TrackerSession *target, NSString *method, NSDictionary *params) -> NSDictionary * {
        NSMutableDictionary *p = [params mutableCopy]; if (![method hasSuffix:@".get"]) p[@"expectedRevision"] = target.automationRevision;
        auto reply = [target automationMethod:method params:p error:&error];
        if (!reply) throw std::runtime_error(error.localizedDescription.UTF8String);
        return reply[@"data"];
      };
      check([session addPlugin:describe(fixture.at(0), fixturePath) error:&error], "Add fixture effect");
      check([session pluginParameter:0 identifier:7 value:.37 record:NO error:&error], "Edit fixture parameter");
      NSString *graph = call(session, @"graph.create", @{@"name": @"Trust"})[@"graph"];
      call(session, @"graph.node.add", @{@"graph": graph, @"kind": @"plugin", @"slot": @0});
      NSString *project = [folder stringByAppendingPathComponent:@"trusted.screamseq"];
      check([session savePath:project error:&error], "Save project");
      TrackerSession *reopened = [TrackerSession new];
      check([reopened openPath:project error:&error] && [[reopened snapshot:0][@"pluginError"] length] == 0 &&
            [[reopened snapshot:0][@"nativePlugins"][0][@"path"] isEqual:fixturePath], "A trusted location reopens normally");

      // The same project naming an untrusted bundle with a valid class ID.
      NSMutableDictionary *root = readProject(project);
      NSData *savedState = [root[@"plugins"][0][@"state"] copy], *graphState = [graphRecipe(root)[@"state"] copy];
      check(savedState.length > 0, "Project carries plugin state");
      root[@"plugins"][0][@"path"] = untrusted;
      NSString *rackOnly = [folder stringByAppendingPathComponent:@"rack.screamseq"];
      writeProject(root, rackOnly);
      graphRecipe(root)[@"path"] = untrusted;
      NSString *hostile = [folder stringByAppendingPathComponent:@"hostile.screamseq"];
      writeProject(root, hostile);
      root[@"plugins"][0][@"path"] = fixturePath;
      NSString *graphOnly = [folder stringByAppendingPathComponent:@"graph.screamseq"];
      writeProject(root, graphOnly);

      for (NSString *candidate in @[rackOnly, hostile]) {
        TrackerSession *victim = [TrackerSession new];
        check([victim openPath:candidate error:&error], "A project with a missing plugin still opens");
        NSDictionary *status = [victim snapshot:0];
        check([status[@"pluginError"] containsString:untrusted] && [status[@"pluginError"] containsString:@(fixture.at(0).name.c_str())],
              "The missing plugin is reported with its name and stored path");
        check([status[@"nativePlugins"] count] == 1 && [status[@"nativePlugins"][0][@"path"] length] == 0, "The untrusted path is not retained as a loadable location");
        check(![victim playOrder:0 error:&error] && [error.localizedDescription containsString:untrusted], "Playback preparation is rejected");
        NSString *resaved = [folder stringByAppendingPathComponent:@"resaved.screamseq"];
        check([victim savePath:resaved error:&error], "A project with a missing plugin can be saved");
        NSDictionary *saved = readProject(resaved);
        check([saved[@"plugins"][0][@"path"] isEqual:untrusted] && [saved[@"plugins"][0][@"classID"] isEqual:root[@"plugins"][0][@"classID"]] &&
              [saved[@"plugins"][0][@"state"] isEqual:savedState], "Saving preserves the missing plugin's hint, identity and opaque state");
        if (candidate == hostile)
          check([graphRecipe(saved)[@"path"] isEqual:untrusted] && [graphRecipe(saved)[@"state"] isEqual:graphState], "Saving preserves the missing graph recipe");
        check(![TrackerSession exportData:[NSData dataWithContentsOfFile:candidate] path:[folder stringByAppendingPathComponent:@"out.wav"] error:&error] &&
              [error.localizedDescription containsString:untrusted], "Offline export rejects the missing plugin");
        check(!imageLoaded(@"Untrusted-Location"), "The untrusted bundle was never loaded");
      }
      {
        TrackerSession *victim = [TrackerSession new];
        check([victim openPath:graphOnly error:&error] && [[victim snapshot:0][@"pluginError"] length] == 0, "Rack plugins of a graph-only failure still load");
        bool reported = false;
        for (NSString *issue in [victim snapshot:0][@"issues"]) reported = reported || [issue containsString:untrusted];
        check(reported, "The missing graph plugin is reported");
        check(![victim playOrder:0 error:&error] && [error.localizedDescription containsString:untrusted], "Playback rejects a missing graph plugin");
        NSString *node = nil;
        for (NSDictionary *definition in call(victim, @"graph.get", @{})[@"library"])
          for (NSDictionary *item in definition[@"nodes"]) if ([item[@"kind"] isEqual:@"plugin"]) node = item[@"id"];
        check(node && ![victim automationMethod:@"graph.plugin.get" params:@{@"graph": graph, @"node": node} error:&error] &&
              [error.localizedDescription containsString:untrusted], "Graph recipe inspection cannot instantiate it");
        NSMutableDictionary *recipe = [describe(fixture.at(0), untrusted) mutableCopy];
        [recipe removeObjectForKey:@"isInstrument"];
        check(![victim automationMethod:@"graph.node.add" params:@{@"expectedRevision": victim.automationRevision, @"graph": graph, @"kind": @"plugin", @"plugin": recipe} error:&error] &&
              error.code == -32602, "An API graph recipe cannot name an untrusted bundle");
        check(!imageLoaded(@"Untrusted-Location"), "The untrusted graph bundle was never loaded");
      }

      // Explicit trust survives a new launch; nothing else reaches the store.
      {
        NSString *kept = [[folder stringByAppendingPathComponent:@"Kept"] stringByAppendingPathComponent:@"Kept.vst3"];
        check([files createDirectoryAtPath:kept.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil] &&
              [files copyItemAtPath:fixturePath toPath:kept error:nil], "Copy fixture to a user-chosen location");
        TrackerSession *adding = [TrackerSession new];
        check([adding addPlugin:describe(fixture.at(0), kept) error:&error], "Add a plugin from a non-standard folder");
        NSString *keptProject = [folder stringByAppendingPathComponent:@"kept.screamseq"];
        check([adding savePath:keptProject error:&error], "Save project naming the chosen bundle");
        struct stat info{}, directory{};
        check(lstat(store.fileSystemRepresentation, &info) == 0 && S_ISREG(info.st_mode) && (info.st_mode & 0777) == 0600 &&
              lstat(store.stringByDeletingLastPathComponent.fileSystemRepresentation, &directory) == 0 && (directory.st_mode & 0777) == 0700,
              "The store is a private file in a private directory");
        check([stored() containsObject:kept] && [stored() containsObject:PluginTrust::canonical(fixturePath)], "Explicit adds are recorded by canonical path");
        for (NSString *entry in stored())
          check(![entry hasPrefix:trustedFolder] && ![entry isEqual:trustedFolder], "In-process trust is never written to the store");
        NSData *before = [NSData dataWithContentsOfFile:store];
        PluginTrust::resetProcessTrust();
        check(PluginTrust::trusted(kept, nil) && !PluginTrust::trusted(untrusted, nil), "Stored trust is honoured after the in-process set is cleared");
        TrackerSession *relaunched = [TrackerSession new];
        check([relaunched openPath:keptProject error:&error] && [[relaunched snapshot:0][@"pluginError"] length] == 0 &&
              [[relaunched snapshot:0][@"nativePlugins"][0][@"path"] isEqual:kept], "A fresh session loads the persisted bundle");
        TrackerSession *victim = [TrackerSession new];
        check([victim openPath:hostile error:&error] && [[victim snapshot:0][@"pluginError"] containsString:untrusted] &&
              [victim savePath:[folder stringByAppendingPathComponent:@"hostile-resaved.screamseq"] error:&error], "A hostile project still opens as missing");
        check([before isEqual:[NSData dataWithContentsOfFile:store]] && ![stored() containsObject:untrusted], "A project file cannot add store entries");

        // Tampered entries: only a path that is still its own canonical location counts.
        tamper(@{@"version": @1, @"bundles": @[link, escaping, @"relative/Plugin.vst3", untrustedFolder, @7, kept]});
        check(!PluginTrust::trusted(link, nil) && !PluginTrust::trusted(escaping, nil) && !PluginTrust::trusted(untrusted, nil) &&
              PluginTrust::trusted(kept, nil), "Symbolic-link and parent-reference entries are rejected");
        // Corrupt, oversized or exposed stores are empty stores, never errors.
        NSMutableArray *many = [NSMutableArray arrayWithObject:kept];
        for (int i = 0; i < 1024; ++i) [many addObject:[NSString stringWithFormat:@"/absent/%d.vst3", i]];
        NSMutableData *huge = [NSMutableData dataWithLength:1024 * 1024 + 1];
        memset(huge.mutableBytes, ' ', huge.length);
        for (id content in @[[@"{not json" dataUsingEncoding:NSUTF8StringEncoding], @{@"version": @2, @"bundles": @[kept]},
                             @{@"version": @1, @"bundles": @{kept: @YES}}, @{@"version": @1, @"bundles": @[kept], @"extra": @1},
                             @{@"version": @1, @"bundles": many}, huge]) {
          tamper(content);
          check(!PluginTrust::trusted(kept, nil), "A malformed or oversized store is treated as empty");
        }
        tamper(@{@"version": @1, @"bundles": @[kept]});
        check(PluginTrust::trusted(kept, nil) && chmod(store.fileSystemRepresentation, 0644) == 0 && !PluginTrust::trusted(kept, nil),
              "A store readable by others is ignored");
        tamper([@"{not json" dataUsingEncoding:NSUTF8StringEncoding]);
        TrackerSession *corrupt = [TrackerSession new];
        check([corrupt openPath:keptProject error:&error] && [[corrupt snapshot:0][@"pluginError"] containsString:kept], "A corrupt store never blocks opening a project");
        check([corrupt addPlugin:describe(fixture.at(0), kept) error:&error] && [stored() isEqual:@[kept]], "An explicit add replaces a corrupt store");
        check(!imageLoaded(@"Untrusted-Location"), "The untrusted bundle was never loaded");
      }

      // The loader itself refuses a location that is not trusted, whoever asks.
      {
        PluginState direct;
        direct.descriptor = fixture.at(0);
        direct.descriptor.path = untrusted.UTF8String;
        bool refused = false;
        try { NativePlugin plugin(direct, 48000); } catch (const std::exception &) { refused = true; }
        check(refused, "The loader refuses an untrusted bundle directly");
        refused = false;
        try { (void)NativePlugin::discoverVST3(untrusted.UTF8String); } catch (const std::exception &) { refused = true; }
        check(refused && !imageLoaded(@"Untrusted-Location"), "Discovery refuses an untrusted bundle");

        // A link that pointed at a trusted bundle when checked is repointed before loading.
        NSString *swap = [folder stringByAppendingPathComponent:@"Swap"], *good = [swap stringByAppendingPathComponent:@"Good.vst3"];
        NSString *current = [swap stringByAppendingPathComponent:@"Current.vst3"];
        check([files createDirectoryAtPath:swap withIntermediateDirectories:YES attributes:nil error:nil] && [files copyItemAtPath:fixturePath toPath:good error:nil] &&
              symlink(good.fileSystemRepresentation, current.fileSystemRepresentation) == 0 && [TrackerSession trustPluginLocation:good], "Prepare a linked bundle");
        check([PluginTrust::resolve(current, classID, nil) isEqual:good] && [PluginTrust::loadable(current) isEqual:good], "Resolution returns the canonical location that was checked");
        NSMutableDictionary *linked = readProject(project);
        linked[@"plugins"][0][@"path"] = current; graphRecipe(linked)[@"path"] = current;
        NSString *linkedProject = [folder stringByAppendingPathComponent:@"linked.screamseq"];
        writeProject(linked, linkedProject);
        TrackerSession *viaLink = [TrackerSession new];
        check([viaLink openPath:linkedProject error:&error] && [[viaLink snapshot:0][@"pluginError"] length] == 0 &&
              [[viaLink snapshot:0][@"nativePlugins"][0][@"path"] isEqual:good], "The session holds the canonical path, not the stored link");
        NSString *linkedSaved = [folder stringByAppendingPathComponent:@"linked-saved.screamseq"];
        check([viaLink savePath:linkedSaved error:&error] && [readProject(linkedSaved)[@"plugins"][0][@"path"] isEqual:current] &&
              [graphRecipe(readProject(linkedSaved))[@"path"] isEqual:current], "Saving keeps the stored hint string unchanged");
        check(unlink(current.fileSystemRepresentation) == 0 && symlink(untrusted.fileSystemRepresentation, current.fileSystemRepresentation) == 0, "Repoint the link");
        direct.descriptor.path = current.UTF8String;
        refused = false;
        try { NativePlugin plugin(direct, 48000); } catch (const std::exception &) { refused = true; }
        check(refused && PluginTrust::loadable(current) == nil, "A link repointed after the check is refused at load");
        direct.descriptor.path = good.UTF8String;
        { NativePlugin plugin(direct, 48000); } // The session's canonical path still loads the bundle that was checked.
        TrackerSession *afterSwap = [TrackerSession new];
        check([afterSwap openPath:linkedProject error:&error] && [[afterSwap snapshot:0][@"pluginError"] containsString:current], "Reopening through the repointed link reports a missing plugin");
        check(!imageLoaded(@"Untrusted-Location"), "The repointed target was never loaded");
      }

      // Consent: an existing bundle outside the trusted locations is offered, never assumed.
      {
        NSString *asked = [[folder stringByAppendingPathComponent:@"Asked"] stringByAppendingPathComponent:@"Asked.vst3"];
        check([files createDirectoryAtPath:asked.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil] &&
              [files copyItemAtPath:fixturePath toPath:asked error:nil], "Copy fixture to a location needing consent");
        NSMutableDictionary *waiting = readProject(project);
        NSData *rackState = [waiting[@"plugins"][0][@"state"] copy], *recipeState = [graphRecipe(waiting)[@"state"] copy];
        waiting[@"plugins"][0][@"path"] = asked; graphRecipe(waiting)[@"path"] = asked;
        NSString *waitingProject = [folder stringByAppendingPathComponent:@"waiting.screamseq"];
        writeProject(waiting, waitingProject);
        TrackerSession *asking = [TrackerSession new];
        check([asking openPath:waitingProject error:&error] && [[asking snapshot:0][@"pluginError"] containsString:asked], "The project opens with its plugins missing");
        NSArray *unresolved = asking.unresolvedPluginLocations;
        check(unresolved.count == 2 && [unresolved[0][@"canonicalPath"] isEqual:asked] && [unresolved[0][@"storedPath"] isEqual:asked] &&
              [unresolved[0][@"classID"] isEqual:classID] && [unresolved[0][@"name"] length] > 0 && [unresolved[0][@"kind"] isEqual:@"rack"] &&
              [unresolved[1][@"kind"] isEqual:@"graph"], "Waiting rack and graph plugins are listed with their canonical paths");
        check([call(asking, @"plugin.locations.get", @{})[@"unresolved"] isEqual:unresolved], "The agent API lists the same entries");
        check(!PluginTrust::trusted(asked, nil) && ![stored() containsObject:asked] && !imageLoaded(@"/Asked/"), "Listing neither trusts nor loads");
        NSString *revision = asking.automationRevision;
        check(![asking trustPluginLocations:@[fixturePath] error:&error] && ![asking trustPluginLocations:@[[asked stringByAppendingString:@"/../Asked.vst3"]] error:&error] &&
              ![asking trustPluginLocations:@[] error:&error] && ![asking automationMethod:@"plugin.locations.trust" params:@{@"expectedRevision": revision, @"canonicalPaths": @[untrusted]} error:&error] &&
              [revision isEqual:asking.automationRevision] && !PluginTrust::trusted(asked, nil) && !PluginTrust::trusted(untrusted, nil),
              "Only canonical paths this document is waiting for can be approved");
        NSDictionary *granted = call(asking, @"plugin.locations.trust", @{@"canonicalPaths": @[asked]});
        check([granted[@"unresolved"] count] == 0 && [granted[@"pluginError"] length] == 0 && asking.unresolvedPluginLocations.count == 0 &&
              [[asking snapshot:0][@"pluginError"] length] == 0 && [[asking snapshot:0][@"nativePlugins"][0][@"path"] isEqual:asked], "Approved plugins resolve in place");
        bool restored = false;
        for (NSDictionary *parameter in [asking pluginParameters:0])
          if ([parameter[@"id"] intValue] == 7) restored = std::abs([parameter[@"value"] doubleValue] - .37) < 1e-4;
        check(restored, "The approved plugin keeps its saved state");
        NSString *node = nil;
        for (NSDictionary *definition in call(asking, @"graph.get", @{})[@"library"])
          for (NSDictionary *item in definition[@"nodes"]) if ([item[@"kind"] isEqual:@"plugin"]) node = item[@"id"];
        check(node && [asking automationMethod:@"graph.plugin.get" params:@{@"graph": graph, @"node": node} error:&error] != nil, "The approved graph plugin loads");
        NSString *approvedSaved = [folder stringByAppendingPathComponent:@"approved.screamseq"];
        check([asking savePath:approvedSaved error:&error], "Save after approval");
        NSDictionary *written = readProject(approvedSaved);
        check([written[@"plugins"][0][@"path"] isEqual:asked] && [graphRecipe(written)[@"path"] isEqual:asked] &&
              [graphRecipe(written)[@"state"] isEqual:recipeState] && [written[@"plugins"][0][@"state"] length] > 0 && rackState.length > 0,
              "Saving keeps the stored hints and plugin state");
        check([stored() containsObject:asked] && ![stored() containsObject:untrusted], "Approval is remembered for later launches");
        PluginTrust::resetProcessTrust();
        TrackerSession *later = [TrackerSession new];
        check([later openPath:waitingProject error:&error] && [[later snapshot:0][@"pluginError"] length] == 0 && later.unresolvedPluginLocations.count == 0,
              "A later launch loads the approved location without asking");
        check(!imageLoaded(@"Untrusted-Location"), "Approving one location never loads another");
      }

      // A pending manual edit that cannot be committed must not block saving.
      NSString *picked = [pickedFolder stringByAppendingPathComponent:@"Picked.vst3"];
      check([files copyItemAtPath:fixturePath toPath:picked error:nil], "Copy fixture to a chosen location");
      TrackerSession *editing = [TrackerSession new];
      check([editing addPlugin:describe(fixture.at(0), picked) error:&error], "A bundle the user adds directly is trusted");
      NSString *first = [folder stringByAppendingPathComponent:@"manual.screamseq"];
      check([editing savePath:first error:&error], "Save baseline");
      NSData *baseline = readProject(first)[@"plugins"][0][@"state"];
      check([editing pluginParameter:0 identifier:7 value:.81 record:NO error:&error], "Queue a manual edit");
      check([files removeItemAtPath:picked error:nil], "Remove the plugin while an edit is pending");
      check([editing savePath:first error:&error], "Saving succeeds although the edit cannot be committed");
      // The stopped live instance still supplies its own state; the slot is never emptied.
      check(baseline.length > 0 && [readProject(first)[@"plugins"][0][@"state"] length] > 0, "Plugin state is retained");
      bool warned = false;
      for (NSString *issue in [editing snapshot:0][@"issues"]) warned = warned || [issue containsString:@"manual plugin parameter edit"];
      check(warned, "The dropped edit is reported");
      check([editing savePath:first error:&error] && [editing recoveryData:&error] != nil, "The failure is not sticky");

      // Objective-C exceptions stop at the API boundary.
      error = nil;
      check(![editing automationMethod:@"plugin.parameters.get" params:[RaisingParameters new] error:&error] && [error.localizedDescription hasPrefix:@"Internal error"], "A raised exception becomes an API error");

      [files removeItemAtPath:folder error:nil];
      std::cout << "PASS plugin trust: hints, canonical locations, inventory resolution, missing rack/graph plugins, preserved state, persistent explicit trust, tampered and corrupt stores, canonical loading, loader enforcement, consent, recoverable manual edits, API exception boundary\n";
      return 0;
    } catch (const std::exception &e) {
      [files removeItemAtPath:folder error:nil];
      std::cerr << e.what() << "\n";
      return 1;
    }
  }
}
