#import "../Bridge/TrackerSession.h"
#include "../Audio/AudioUnitHost.hpp"
#include "../Plugins/PluginInventory.hpp"
#include "FixtureTrust.hpp"
#include <iostream>
#include <stdexcept>
using namespace Tracker;
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static NSMutableDictionary *tree(TrackerSession *session){return [NSPropertyListSerialization propertyListWithData:[session serializedData] options:NSPropertyListMutableContainersAndLeaves format:nil error:nil];}
static NSMutableDictionary *recipe(NSDictionary *root){
  for(NSDictionary *graph in root[@"native"][@"signalGraph"][@"library"])
    for(NSMutableDictionary *node in graph[@"nodes"])if(node[@"plugin"] && [node[@"plugin"][@"format"] isEqual:@"VST3"])return node[@"plugin"];
  throw std::runtime_error("Missing graph fixture");
}
static void save(NSDictionary *root,NSString *path){check([[NSPropertyListSerialization dataWithPropertyList:root format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil] writeToFile:path atomically:YES],"Save fixture");}
int main(int argc,const char **argv){@autoreleasepool{
  NSString *folder=[PluginTrust::canonical(NSTemporaryDirectory()) stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  auto files=NSFileManager.defaultManager;
  try{
    check(argc==2,"Pass VST3 fixture");trustFixtureArguments(argc,argv);
    [files createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
    PluginTrust::setStorePath([folder stringByAppendingPathComponent:@"trust/trusted.json"]);
    PluginInventory::setStorePath([folder stringByAppendingPathComponent:@"discovery/plugins.json"]);
    PluginLocations::setStorePath([folder stringByAppendingPathComponent:@"verified/modules.json"]);
    NSString *first=[folder stringByAppendingPathComponent:@"Original.vst3"],*second=[folder stringByAppendingPathComponent:@"Moved 音.vst3"];
    for(NSString *path in @[first,second])check([files copyItemAtPath:PluginTrust::canonical(@(argv[1])) toPath:path error:nil],"Copy fixture bundle");
    PluginTrust::trust(first);
    auto descriptors=NativePlugin::discoverVST3(first.UTF8String);check(!descriptors.empty(),"Discover native fixture");
    const auto &d=descriptors.at(0);
    NSDictionary *descriptor=@{@"type":@(d.type),@"subtype":@(d.subtype),@"manufacturer":@(d.manufacturer),@"name":@(d.name.c_str()),@"format":@"VST3",@"path":first,@"classID":@(d.classID.c_str()),@"isInstrument":@(d.instrument)};
    TrackerSession *session=[TrackerSession new];NSError *error=nil;
    auto call=[&](NSString *method,NSDictionary *params)->NSDictionary *{
      NSMutableDictionary *p=[params mutableCopy];if(![method hasSuffix:@".get"]&&!p[@"expectedRevision"])p[@"expectedRevision"]=session.automationRevision;
      auto result=[session automationMethod:method params:p error:&error];
      if(!result)throw std::runtime_error(std::string(method.UTF8String)+": "+error.localizedDescription.UTF8String);return result;
    };
    call(@"plugin.add",@{@"descriptor":descriptor});
    call(@"plugin.parameters.set",@{@"slot":@0,@"values":@[@{@"id":@7,@"value":@0.37}]});
    NSString *graph=call(@"graph.create",@{@"name":@"Location repair"})[@"data"][@"graph"];
    call(@"graph.node.add",@{@"graph":graph,@"kind":@"plugin",@"slot":@0});
    NSString *node=nil;for(NSDictionary *definition in call(@"graph.get",@{})[@"data"][@"library"])
      for(NSDictionary *item in definition[@"nodes"])if([item[@"kind"] isEqual:@"plugin"])node=item[@"id"];
    check(node!=nil,"Graph node identity");
    auto broken=tree(session);NSString *plugin=broken[@"plugins"][0][@"instanceID"];
    NSString *missing=@"C:/Old Windows Plugins/Fixture.vst3";
    broken[@"plugins"][0][@"path"]=missing;broken[@"plugins"][0][@"futureLocationField"]=@{@"keep":@"opaque"};
    recipe(broken)[@"path"]=missing;
    NSString *project=[folder stringByAppendingPathComponent:@"missing.screamseq"];save(broken,project);
    check([session openPath:project error:&error],"Open missing rack and graph project");
    auto original=tree(session);NSString *revision=session.automationRevision;
    auto info=call(@"plugin.path.get",@{@"plugin":plugin})[@"data"];
    check([info[@"descriptor"][@"path"] isEqual:missing]&&![info[@"moduleVerified"] boolValue]&&![info[@"candidates"] count],"Report original saved path with no invented verified candidates");
    auto refused=[&](NSString *method,NSDictionary *fields,NSInteger code,bool receipt){
      NSMutableDictionary *p=[fields mutableCopy];if(![method hasSuffix:@".get"]&&!p[@"expectedRevision"])p[@"expectedRevision"]=session.automationRevision;
      auto before=tree(session);NSString *beforeRevision=session.automationRevision;NSError *failure=nil;
      check(![session automationMethod:method params:p error:&failure]&&failure.code==code,"Expected path operation refusal");
      if(receipt)check([failure.userInfo[@"writeOutcome"] isEqual:@"notCommitted"],"Preflight refusal explicitly reports notCommitted");
      check([tree(session) isEqual:before]&&[session.automationRevision isEqual:beforeRevision],"Refusal leaves all saved state and history unchanged");
    };
    refused(@"plugin.path.scan",@{@"plugin":plugin,@"path":second,@"expectedRevision":@"stale"},-32001,false);
    refused(@"plugin.path.set",@{@"plugin":plugin,@"path":second,@"expectedModuleSHA256":@"bad"},-32602,true);
    auto scan=call(@"plugin.path.scan",@{@"plugin":plugin,@"path":second});
    check(![scan[@"changed"] boolValue]&&[session.automationRevision isEqual:revision]&&[tree(session) isEqual:original],"Explicit isolated scan changes no song state");
    auto candidate=scan[@"data"][@"candidates"][0];NSString *hash=candidate[@"moduleSHA256"];
    NSMutableDictionary *fields=[@{@"plugin":plugin,@"path":second,@"expectedModuleSHA256":hash} mutableCopy];
    refused(@"plugin.path.set",[fields dictionaryWithValuesForKeys:@[@"plugin",@"path"]],-32602,true);
    auto bad=[fields mutableCopy];bad[@"dryRun"]=@1;refused(@"plugin.path.set",bad,-32602,true);
    bad=[fields mutableCopy];bad[@"expectedModuleSHA256"]=[@"0" stringByPaddingToLength:64 withString:@"0" startingAtIndex:0];refused(@"plugin.path.set",bad,-32602,true);
    auto dry=[fields mutableCopy];dry[@"dryRun"]=@YES;auto preview=call(@"plugin.path.set",dry);
    check(![preview[@"changed"] boolValue]&&[preview[@"data"][@"wouldChange"] boolValue]&&[tree(session) isEqual:original],"Dry run preserves opaque plugin and graph data");
    check([call(@"plugin.path.set",fields)[@"changed"] boolValue],"Repair rack location");
    auto repaired=tree(session);auto expected=[original[@"plugins"] mutableCopy];expected[0]=[expected[0] mutableCopy];expected[0][@"path"]=PluginTrust::canonical(second);
    check([repaired[@"plugins"] isEqual:expected]&&[repaired[@"native"] isEqual:original[@"native"]]&&[repaired[@"automation"] isEqual:original[@"automation"]],"Rack reconnect changes only its path, retaining state, unknown data and all graph/automation content");
    check(![call(@"plugin.path.set",fields)[@"changed"] boolValue],"Repeated reconnect is a no-op");
    call(@"history.undo",@{});check([tree(session) isEqual:original],"One Undo restores the entire original missing-path project");
    call(@"history.redo",@{});check([tree(session) isEqual:repaired],"Redo restores repaired rack path");
    NSMutableDictionary *graphFields=[@{@"graph":graph,@"node":node,@"path":second,@"expectedModuleSHA256":hash} mutableCopy];
    auto beforeGraph=tree(session);check([call(@"graph.plugin.path.set",graphFields)[@"changed"] boolValue],"Repair graph recipe location");
    auto afterGraph=tree(session);auto expectedNative=[NSPropertyListSerialization propertyListWithData:[NSPropertyListSerialization dataWithPropertyList:beforeGraph format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil] options:NSPropertyListMutableContainersAndLeaves format:nil error:nil];
    recipe(expectedNative)[@"path"]=PluginTrust::canonical(second);
    check([afterGraph isEqual:expectedNative],"Graph reconnect changes only the selected recipe path");
    call(@"history.undo",@{});check([tree(session) isEqual:beforeGraph],"Graph reconnect has one chronological Undo");
    call(@"history.redo",@{});check([tree(session) isEqual:afterGraph],"Graph path Redo retains complete saved sound");
    NSString *saved=[folder stringByAppendingPathComponent:@"repaired.screamseq"];check([session savePath:saved error:&error],"Save repaired project");
    TrackerSession *reopened=[TrackerSession new];check([reopened openPath:saved error:&error]&&[tree(reopened) isEqual:afterGraph],"Repairs persist across reopen");[reopened shutdown];
    // Automatic runtime resolution must not make explicit acceptance a no-op:
    // the original cross-platform hint still belongs to the saved document.
    auto installed=[descriptor mutableCopy];installed[@"path"]=PluginTrust::canonical(second);
    PluginInventory inventory;inventory.load(true,[&]()->NSArray<NSDictionary *> *{return @[installed];});
    check([session openPath:project error:&error],"Reopen with automatic class resolution");
    auto automatic=tree(session);check([automatic[@"plugins"][0][@"path"] isEqual:missing],"Automatic resolution preserves stored hint");
    check([call(@"plugin.path.set",fields)[@"changed"] boolValue],"Explicitly accepting an already resolved path still creates Undo");
    call(@"history.undo",@{});check([tree(session) isEqual:automatic],"Undo restores original stored path even when runtime locations are equal");
    call(@"history.redo",@{});check([tree(session)[@"plugins"][0][@"path"] isEqual:PluginTrust::canonical(second)],"Redo reaccepts equal runtime path");
    auto autoGraph=tree(session);check([call(@"graph.plugin.path.set",graphFields)[@"changed"] boolValue],"Explicit graph acceptance tracks its original hint");
    call(@"history.undo",@{});check([tree(session) isEqual:autoGraph],"Graph Undo restores hint with equal runtime paths");
    [session shutdown];PluginInventory::setStorePath(nil);PluginLocations::setStorePath(nil);PluginTrust::setStorePath(nil);[files removeItemAtPath:folder error:nil];
    std::cout<<"PASS plugin paths: real isolated scan, strict/stale/hash/dry-run admission, exact rack/graph preservation, one-step Undo/Redo, automatic resolution hints and save/reopen\n";return 0;
  }catch(const std::exception &e){PluginInventory::setStorePath(nil);PluginLocations::setStorePath(nil);PluginTrust::setStorePath(nil);[files removeItemAtPath:folder error:nil];std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}}
