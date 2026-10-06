#import "../Bridge/TrackerSession.h"
#include <iostream>
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static NSDictionary *call(TrackerSession *s,NSString *method,NSDictionary *params,bool write=false){
  auto p=[params mutableCopy];if(write)p[@"expectedRevision"]=s.automationRevision;NSError *error=nil;
  auto reply=[s automationMethod:method params:p error:&error];if(!reply)throw std::runtime_error(error.localizedDescription.UTF8String);return reply[@"data"];
}
static void reject(TrackerSession *s,NSString *method,NSDictionary *params){
  auto revision=s.automationRevision;auto before=call(s,@"scratch.gestures.get",@{});auto p=[params mutableCopy];p[@"expectedRevision"]=revision;NSError *error=nil;
  check(![s automationMethod:method params:p error:&error],"Invalid scratch edit must reject");check([revision isEqual:s.automationRevision]&&[before isEqual:call(s,@"scratch.gestures.get",@{})],"Rejected edit preserves library and history");
}
int main(){@autoreleasepool{try{
  auto s=[TrackerSession new];auto initial=call(s,@"scratch.gestures.get",@{});check([initial[@"presets"] count]>=7&&[initial[@"gestures"] count]==0,"Scratch factory palette does not mutate a new song");
  auto revision=s.automationRevision;
  auto dry=call(s,@"scratch.gestures.set",@{@"preset":@"baby",@"dryRun":@YES},true);check([dry[@"id"] intValue]==1&&[revision isEqual:s.automationRevision],"First free phrase dry run is nonmutating");
  call(s,@"scratch.gestures.set",@{@"preset":@"baby"},true);
  auto first=call(s,@"scratch.gestures.get",@{});revision=s.automationRevision;
  call(s,@"scratch.gestures.set",@{@"id":@1,@"preset":@"baby"},true);check([revision isEqual:s.automationRevision],"Repeated exact phrase update is no-op");
  call(s,@"scratch.gestures.set",@{@"preset":@"crab",@"name":@"Crab variation"},true);
  check([call(s,@"scratch.gestures.get",@{})[@"gestures"] count]==2,"Create independent phrase");
  call(s,@"history.undo",@{},true);check([first isEqual:call(s,@"scratch.gestures.get",@{})],"Phrase create uses one global Undo");call(s,@"history.redo",@{},true);
  const auto command=@{@"kind":@"native",@"native":@"scratch",@"parameters":@{@"gesture":@1,@"beats":@1.25,@"travelMs":@173.456789,@"repeats":@3,@"reverse":@NO},@"offset":@1234};
  call(s,@"pattern.effect.set",@{@"pattern":@0,@"row":@0,@"channel":@0,@"column":@0,@"command":command},true);
  call(s,@"pattern.effect.set",@{@"pattern":@0,@"row":@8,@"channel":@1,@"column":@0,@"command":command},true);
  check([call(s,@"scratch.gestures.get",@{})[@"gestures"][0][@"uses"] intValue]==2,"Phrase reports both linked pattern uses");
  const auto target=@{@"pattern":@0,@"row":@0,@"channel":@0,@"column":@0};
  auto beforeClone=call(s,@"scratch.gestures.get",@{}),beforeCloneEffects=call(s,@"pattern.effects.get",@{@"pattern":@0});revision=s.automationRevision;
  call(s,@"scratch.gestures.clone",@{@"id":@1,@"target":target,@"dryRun":@YES},true);
  check([revision isEqual:s.automationRevision]&&[beforeClone isEqual:call(s,@"scratch.gestures.get",@{})],"Dry clone and assign leaves no copy or history");
  reject(s,@"scratch.gestures.clone",@{@"id":@2,@"target":target});
  reject(s,@"scratch.gestures.clone",@{@"id":@1,@"target":@{@"pattern":@0,@"row":@1,@"channel":@0,@"column":@0}});
  reject(s,@"scratch.gestures.clone",@{@"id":@1,@"target":@{@"pattern":@0,@"row":@0,@"channel":@0}});
  reject(s,@"scratch.gestures.clone",@{@"id":@1,@"target":@{@"pattern":@0,@"row":@0,@"channel":@YES,@"column":@0}});
  auto copiedID=call(s,@"scratch.gestures.clone",@{@"id":@1,@"target":target,@"name":@"Unique baby"},true)[@"id"];
  auto afterClone=call(s,@"scratch.gestures.get",@{}),afterCloneEffects=call(s,@"pattern.effects.get",@{@"pattern":@0});
  auto expectedCommands=[beforeCloneEffects[@"commands"] mutableCopy];auto expectedCommand=[expectedCommands[0] mutableCopy],expectedParameters=[expectedCommand[@"parameters"] mutableCopy];expectedParameters[@"gesture"]=copiedID;expectedCommand[@"parameters"]=expectedParameters;expectedCommands[0]=expectedCommand;
  check([afterCloneEffects[@"commands"] isEqual:expectedCommands],"Make unique preserves exact timing, parameters and the other linked cell");
  check([afterClone[@"gestures"] count]==3&&[afterClone[@"gestures"][0][@"uses"] intValue]==1,"Make unique leaves other references linked to the original");
  call(s,@"history.undo",@{},true);check([beforeClone isEqual:call(s,@"scratch.gestures.get",@{})]&&[beforeCloneEffects isEqual:call(s,@"pattern.effects.get",@{@"pattern":@0})],"One global Undo restores both phrase bank and captured cell");
  call(s,@"history.redo",@{},true);check([afterClone isEqual:call(s,@"scratch.gestures.get",@{})]&&[afterCloneEffects isEqual:call(s,@"pattern.effects.get",@{@"pattern":@0})],"One Redo restores both copy and assignment");call(s,@"history.undo",@{},true);
  reject(s,@"scratch.gestures.remove",@{@"id":@1});
  for(NSDictionary *bad in @[@{@"id":@YES},@{@"id":@256},@{@"id":@1,@"name":@""},@{@"id":@1,@"motion":@[]},@{@"id":@1,@"unknown":@1},@{@"preset":@"missing"},@{@"id":@1,@"fader":@[@{@"position":@0,@"value":@YES},@{@"position":@65536,@"value":@1}]}])reject(s,@"scratch.gestures.set",bad);
  auto fader=@[@{@"position":@0,@"value":@1,@"curve":@"scripted",@"formula":@"0.5 + 0.5 * sin(t * pi * 8)"},@{@"position":@65536,@"value":@1,@"curve":@"linear"}];
  auto old=call(s,@"scratch.gestures.get",@{});
  auto preview=call(s,@"automation.formula.preview",@{@"rows":@257,@"span":@65537,@"end":@65536,@"samples":@3,@"scratchBeats":@2,
    @"points":@[@{@"position":@0,@"value":@0,@"curve":@"scripted",@"formula":@"beat/4"},@{@"position":@65536,@"value":@.5}]});
  check([preview[@"values"][1][1] doubleValue]==.25,"Scratch formula preview uses actual per-cycle beat duration");
  call(s,@"scratch.gestures.set",@{@"id":@1,@"fader":fader},true);auto edited=call(s,@"scratch.gestures.get",@{});
  check([old[@"gestures"][1] isEqual:edited[@"gestures"][1]],"Editing one phrase preserves another");
  call(s,@"history.undo",@{},true);check([old isEqual:call(s,@"scratch.gestures.get",@{})],"Curve edit Undo restores all linked uses");call(s,@"history.redo",@{},true);
  auto effects=call(s,@"pattern.effects.get",@{@"pattern":@0});NSError *error=nil;
  auto path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];
  check([s savePath:path error:&error],"Save scratch phrases");auto reopened=[TrackerSession new];check([reopened openPath:path error:&error],"Reopen scratch phrases");
  check([edited isEqual:call(reopened,@"scratch.gestures.get",@{})]&&[effects isEqual:call(reopened,@"pattern.effects.get",@{@"pattern":@0})],"Curves, scripts, links and precise timing roundtrip exactly");
  check([[reopened snapshot:0][@"loadWarnings"] count]==0,"Current scratch project opens cleanly");
  auto pasted=[TrackerSession new];call(pasted,@"scratch.gestures.set",@{@"id":@1,@"preset":@"crab"},true);
  auto copied=[edited[@"gestures"][0] mutableCopy];[copied removeObjectForKey:@"uses"];
  auto clipboardEffect=[command mutableCopy];[clipboardEffect removeObjectForKey:@"offset"];clipboardEffect[@"position"]=@1234;clipboardEffect[@"channel"]=@0;clipboardEffect[@"column"]=@0;
  auto paste=@{@"pattern":@0,@"startRow":@0,@"startChannel":@0,@"rows":@1,@"channels":@1,@"cells":@[@[@0,@0,@0,@0,@0,@0]],@"effects":@[clipboardEffect],@"scratchGestures":@[copied]};
  call(pasted,@"pattern.paste",paste,true);
  auto pastedBank=call(pasted,@"scratch.gestures.get",@{})[@"gestures"];
  check([pastedBank count]==2&&[pastedBank[0][@"name"] isEqual:@"Crab"]&&[pastedBank[1][@"fader"] isEqual:copied[@"fader"]],"Clipboard collision preserves destination phrase and imports exact source curves");
  check([call(pasted,@"pattern.effects.get",@{@"pattern":@0})[@"commands"][0][@"parameters"][@"gesture"] intValue]==2,"Clipboard remaps SK reference");
  call(pasted,@"history.undo",@{},true);check([call(pasted,@"scratch.gestures.get",@{})[@"gestures"] count]==1,"Clipboard phrase import and cell paste share one Undo");
  call(pasted,@"pattern.effect.set",@{@"pattern":@0,@"row":@0,@"channel":@0,@"column":@0,@"command":@{@"kind":@"note-cut"}},true);
  auto mixed=[paste mutableCopy];mixed[@"mode"]=@"mix";mixed[@"fields"]=@[@"effect"];auto mixRevision=pasted.automationRevision;
  call(pasted,@"pattern.paste",mixed,true);check([mixRevision isEqual:pasted.automationRevision]&&[call(pasted,@"scratch.gestures.get",@{})[@"gestures"] count]==1,"Skipped mixed SK paste imports no unused phrase or Undo");[pasted shutdown];
  auto bytes=[NSData dataWithContentsOfFile:path];auto damaged=[NSPropertyListSerialization propertyListWithData:bytes options:NSPropertyListMutableContainersAndLeaves format:nil error:&error];
  check([damaged isKindOfClass:NSMutableDictionary.class],"Native wrapper fixture");
  // The wrapper's metadata key is deliberately discovered from the current save.
  NSMutableDictionary *native=nil;for(NSString *key in damaged){id value=damaged[key];if([value isKindOfClass:NSMutableDictionary.class]&&value[@"scratchGestures"])native=value;}
  check(native!=nil,"Find current native metadata");native[@"scratchGestures"][0][@"motion"]=@[];
  auto badPath=[path stringByAppendingString:@".screamseq"];
  auto damagedBytes=[NSPropertyListSerialization dataWithPropertyList:damaged format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];check([damagedBytes writeToFile:badPath atomically:YES]&&[reopened openPath:badPath error:&error],"Best effort damaged phrase load");
  check([call(reopened,@"scratch.gestures.get",@{})[@"gestures"] count]==1&&[call(reopened,@"pattern.effects.get",@{@"pattern":@0})[@"commands"] count]==0,"Recovery preserves independent phrase and drops only invalid references");
  check([[reopened snapshot:0][@"loadWarnings"] count]>0,"Recovery explains missing scratch data");
  auto crowded=[NSPropertyListSerialization propertyListWithData:bytes options:NSPropertyListMutableContainersAndLeaves format:nil error:&error];
  auto crowdedNative=crowded[@"native"];auto tail=[copied mutableCopy];tail[@"id"]=@255;NSMutableArray *entries=[NSMutableArray array];
  for(unsigned i=1;i<255;++i)[entries addObject:@{@"id":@(i),@"name":@"Broken",@"motion":@[],@"fader":@[]}];[entries addObject:tail];crowdedNative[@"scratchGestures"]=entries;
  for(NSMutableDictionary *effect in crowdedNative[@"performance"][@"commands"])if([effect[@"native"] isEqual:@"scratch"])effect[@"parameters"][@"gesture"]=@255;
  auto crowdedBytes=[NSPropertyListSerialization dataWithPropertyList:crowded format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
  check([crowdedBytes writeToFile:badPath atomically:YES]&&[reopened openPath:badPath error:&error],"Recover bank containing many damaged entries");
  check([call(reopened,@"scratch.gestures.get",@{})[@"gestures"][0][@"id"] intValue]==255&&[call(reopened,@"pattern.effects.get",@{@"pattern":@0})[@"commands"] count]==2,"Bad phrase entries cannot exhaust independent command recovery");
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];[[NSFileManager defaultManager] removeItemAtPath:badPath error:nil];[s shutdown];[reopened shutdown];
  std::cout<<"PASS scratch phrase API, shared uses, no-op, dry-run, strict atomic rejection, Undo/Redo, exact persistence and best-effort recovery\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
