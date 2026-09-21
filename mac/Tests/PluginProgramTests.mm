#import "../Bridge/TrackerSession.h"
#include "../Audio/AudioUnitHost.hpp"
#include <cmath>
#include <dlfcn.h>
#include <iostream>
using namespace Tracker;
std::vector<PluginDescriptor> registerFixtureAUs();
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static NSDictionary *dictionary(const PluginDescriptor &d){return @{@"type":@(d.type),@"subtype":@(d.subtype),@"manufacturer":@(d.manufacturer),@"name":@(d.name.c_str()),@"format":@(d.format.c_str()),@"path":@(d.path.c_str()),@"classID":@(d.classID.c_str()),@"isInstrument":@(d.instrument)};}
int main(int argc,char **argv){@autoreleasepool{
 NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
 try {
  check(argc==2,"Local fixture path required");auto vst=NativePlugin::discoverVST3(argv[1]);auto au=registerFixtureAUs();
  void *bundle=dlopen([[@(argv[1]) stringByAppendingPathComponent:@"Contents/MacOS/ResonanceFixture"] UTF8String],RTLD_NOW|RTLD_LOCAL);
  auto mode=reinterpret_cast<void(*)(int)>(dlsym(bundle,"ResonanceFixtureProgramMode"));check(mode,"Program fixture control hook");
  auto selections=reinterpret_cast<int(*)()>(dlsym(bundle,"ResonanceFixtureProgramSelections"));check(selections,"Program selection counter");
  check(vst.size()==4,"Four independently discovered VST3 fixtures");
  NativePlugin noPrograms(PluginState{vst[0]},48000);check(noPrograms.programs().empty(),"Optional VST3 unit interface can be absent");
  for(const auto &descriptor:{vst[3],au[0],au[1]}) {
    const bool isVst=descriptor.format=="VST3";
    auto program=isVst?"vst3:0:17:2":"au:16";
    for(const auto rate:{44100u,48000u,96000u}) {
      PluginState initial{descriptor};NativePlugin plugin(initial,rate);
      auto entries=plugin.programs();check(entries.size()==(isVst?6:3),"Unit/list programs and sparse AU IDs are enumerated");
      check(entries[2].id==program&&entries[2].name=="Full"&&entries[2].loadable,"Stable program identity and display name");
      const auto before=plugin.state();bool rejected=false;try{plugin.loadProgram("missing");}catch(const std::exception&){rejected=true;}
      check(rejected&&plugin.state().state==before.state,"Invalid program leaves plugin state unchanged");
      plugin.loadProgram(program);
      if(isVst){plugin.loadProgram("vst3:7:18:1");check(std::abs(plugin.parameters()[0].value-.75)<1e-7,"Program refreshes host parameter baseline after processor changes");}
      const auto countBeforeCapture=selections();auto state=plugin.state();
      check(selections()==countBeforeCapture,"Saving VST3 state never re-sends unchanged program selectors");
      for(uint32_t block:{1u,17u,128u,4096u}) {
        NativePlugin recalled(state,rate);if(descriptor.instrument)check(recalled.midi(0x90,60,100),"Start private AU fixture note");
        std::vector<float> buffer(8192,.2f);
        for(uint32_t position=0;position<4096;) {const auto count=std::min(block,4096-position);check(recalled.process(buffer.data()+position*2,count,position),"Offline program audio processes");position+=count;}
        const auto expected=isVst?.075f:.2f;
        for(auto value:buffer)check(std::abs(value-expected)<1e-7,"Every offline output sample matches independent program gain across buffer sizes and rates");
      }
    }
    TrackerSession *session=[TrackerSession new];NSError *error=nil;
    // Process-local AU registrations cannot be seen by the out-of-process scanner.
    if(!isVst){NativePlugin initial(PluginState{descriptor},48000);const auto state=initial.state();
      NSMutableDictionary *root=[[NSPropertyListSerialization propertyListWithData:session.serializedData options:0 format:nil error:nil] mutableCopy];
      NSMutableDictionary *item=[dictionary(descriptor) mutableCopy];item[@"state"]=[NSData dataWithBytes:state.state.data() length:state.state.size()];item[@"instrument"]=@0;item[@"bypass"]=@NO;item[@"instanceID"]=@"fixture-au";root[@"plugins"]=@[item];
      item[@"instrumentAssignments"]=@[];
      [[NSPropertyListSerialization dataWithPropertyList:root format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil] writeToFile:path atomically:YES];
      check([session openPath:path error:&error],"Open private AU fixture project");
    }else check([session addPlugin:dictionary(descriptor) error:&error],"Add local VST3 program fixture");
    auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary *{NSMutableDictionary *p=[params mutableCopy];if(write)p[@"expectedRevision"]=session.automationRevision;auto reply=[session automationMethod:method params:p error:&error];if(!reply)throw std::runtime_error(error.localizedDescription.UTF8String);return reply;};
    auto reject=[&](NSDictionary *params,int code){NSString *revision=session.automationRevision;NSMutableDictionary *p=[params mutableCopy];if(!p[@"expectedRevision"])p[@"expectedRevision"]=revision;check(![session automationMethod:@"plugin.programs.load" params:p error:&error]&&error.code==code&&[revision isEqual:session.automationRevision],"Rejected program load preserves revision");};
    NSString *identity=[session snapshot:0][@"nativePlugins"][0][@"instanceID"];
    if(descriptor.instrument){for(int i=0;i<2;++i)check([session addInstrument:0 error:&error]>0,"Create aliases");call(@"plugin.instruments.set",@{@"plugin":identity,@"assignments":@[@{@"instrument":@1,@"channel":@2},@{@"instrument":@2,@"channel":@7}]},true);}
    call(@"plugin.bypass",@{@"slot":@0,@"bypass":@YES},true);
    if(descriptor.instrument)call(@"plugin.buses.set",@{@"slot":@0,@"outputs":@[@1]},true);
    call(@"automation.pattern.set",@{@"pattern":@0,@"plugin":identity,@"parameter":@7,@"points":@[@{@"position":@0,@"value":@0.4}]},true);
    id routing=call(@"plugin.instruments.get",@{@"plugin":identity})[@"data"];
    id buses=call(@"plugin.buses.get",@{@"slot":@0})[@"data"];
    id automation=call(@"automation.pattern.get",@{@"pattern":@0})[@"data"];
    id before=call(@"plugin.state.get",@{@"slot":@0})[@"data"];
    id catalog=call(@"plugin.programs.get",@{@"plugin":identity})[@"data"];
    NSMutableDictionary *load=[@{@"plugin":identity,@"program":@(program),@"expectedCatalogRevision":catalog[@"catalogRevision"],@"dryRun":@YES} mutableCopy];
    auto preview=call(@"plugin.programs.load",load,true);check(![preview[@"changed"] boolValue]&&![preview[@"data"][@"loaded"] boolValue]&&[before isEqual:call(@"plugin.state.get",@{@"slot":@0})[@"data"]],"Program preview never invokes vendor program selection");
    NSMutableDictionary *bad=[load mutableCopy];bad[@"program"]=@"missing";reject(bad,-32602);bad=[load mutableCopy];bad[@"expectedCatalogRevision"]=@"stale";reject(bad,-32001);bad=[load mutableCopy];bad[@"expectedRevision"]=@"stale";reject(bad,-32001);bad=[load mutableCopy];bad[@"typo"]=@1;reject(bad,-32602);
    if(isVst) {
      mode(1);check(![session automationMethod:@"plugin.programs.get" params:@{@"plugin":identity} error:&error]&&error.code==-32003,"Oversized catalog rejects before allocation");
      mode(2);id unavailable=call(@"plugin.programs.get",@{@"plugin":identity})[@"data"];
      check(![unavailable[@"programs"][0][@"loadable"] boolValue],"Read-only program selector is described but not loadable");
      NSMutableDictionary *blocked=[load mutableCopy];blocked[@"expectedCatalogRevision"]=unavailable[@"catalogRevision"];reject(blocked,-32602);
      mode(4);reject(load,-32001);
      mode(3);NSMutableDictionary *failed=[load mutableCopy];failed[@"dryRun"]=@NO;reject(failed,-32003);
      mode(0);check([before isEqual:call(@"plugin.state.get",@{@"slot":@0})[@"data"]],"Vendor program rejection leaves active plugin state intact");
    }
    load[@"dryRun"]=@NO;auto loaded=call(@"plugin.programs.load",load,true);check([loaded[@"changed"] boolValue]&&[loaded[@"data"][@"loaded"] boolValue],"Program apply is one guarded mutation");
    id after=call(@"plugin.state.get",@{@"slot":@0})[@"data"];
    check(![after isEqual:before]&&[routing isEqual:call(@"plugin.instruments.get",@{@"plugin":identity})[@"data"]]&&[buses isEqual:call(@"plugin.buses.get",@{@"slot":@0})[@"data"]]&&[automation isEqual:call(@"automation.pattern.get",@{@"pattern":@0})[@"data"]]&&[[session snapshot:0][@"nativePlugins"][0][@"bypass"] boolValue],"Program load preserves aliases, ports, bypass, automation and identity");
    call(@"history.undo",@{@"domain":@"plugins"},true);check([before isEqual:call(@"plugin.state.get",@{@"slot":@0})[@"data"]],"One Undo restores complete pre-program state");
    load[@"dryRun"]=@YES;call(@"plugin.programs.load",load,true);call(@"history.redo",@{@"domain":@"plugins"},true);check([after isEqual:call(@"plugin.state.get",@{@"slot":@0})[@"data"]],"Preview preserves Redo and replay restores program state");
    call(@"document.save",@{@"path":path,@"overwrite":@YES},true);check([session openPath:path error:&error]&&[after isEqual:call(@"plugin.state.get",@{@"slot":@0})[@"data"]],"Program state survives native project recall");
  }
  [NSFileManager.defaultManager removeItemAtPath:path error:nil];std::cout<<"PASS factory programs: AU/VST3 lists, unit-specific selectors, processor delivery, parameter refresh, exact offline audio, guarded API, routing preservation, history and recall\n";return 0;
 }catch(const std::exception &e){[NSFileManager.defaultManager removeItemAtPath:path error:nil];std::cerr<<e.what()<<'\n';return 1;}
}}
