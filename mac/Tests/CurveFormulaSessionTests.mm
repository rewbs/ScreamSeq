#import "../Bridge/TrackerSession.h"
#include <cmath>
#include <iostream>
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main(){@autoreleasepool{try{
  TrackerSession *session=[TrackerSession new];
  auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary * {auto p=[params mutableCopy];if(write)p[@"expectedRevision"]=session.automationRevision;NSError *e=nil;auto r=[session automationMethod:method params:p error:&e];if(!r)throw std::runtime_error(std::string(method.UTF8String)+": "+e.localizedDescription.UTF8String);return r;};
  call(@"plugin.add",@{@"descriptor":[session builtInPlugins][0]},true);
  auto plugin=[session snapshot:0][@"nativePlugins"][0][@"instanceID"];
  NSArray *points=@[@{@"position":@0,@"value":@0.2,@"curve":@"scripted",@"formula":@"mix(start,end,t^2)"},@{@"position":@256,@"value":@0.8,@"curve":@"scripted",@"formula":@"start*(1-t)"}];
  auto preview=call(@"automation.formula.preview",@{@"points":points,@"rows":@2,@"samples":@5});
  check(std::abs([preview[@"data"][@"values"][1][1] doubleValue]-.35)<1e-12,"UI/API preview uses exact script evaluator");
  auto revision=session.automationRevision;check([preview[@"changed"] boolValue]==NO,"Preview has no history side effects");
  auto set=call(@"automation.pattern.set",@{@"pattern":@0,@"plugin":plugin,@"parameter":@1,@"points":points},true);
  check([set[@"changed"] boolValue],"Store scripted lane");
  auto saved=[session serializedData];auto metadata=[NSPropertyListSerialization propertyListWithData:saved options:0 format:nil error:nil];
  check([metadata[@"native"][@"version"] intValue]==11,"Project identifies scripted automation format");
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"resonance-formula-%@.resonance",NSUUID.UUID.UUIDString]];
  check([saved writeToFile:path atomically:YES],"Write test project");TrackerSession *reopened=[TrackerSession new];NSError *error=nil;check([reopened openPath:path error:&error],"Reopen formula project");
  auto r=[reopened automationMethod:@"automation.pattern.get" params:@{@"pattern":@0} error:&error];check([r[@"data"][@"lanes"][0][@"points"] isEqual:points],"Formula text survives project round trip");
  auto before=session.automationRevision;auto invalid=[points mutableCopy];invalid[0]=@{@"position":@0,@"value":@0.2,@"curve":@"scripted",@"formula":@"unknown(t)"};
  check(![session automationMethod:@"automation.pattern.set" params:@{@"expectedRevision":before,@"pattern":@0,@"plugin":plugin,@"parameter":@1,@"points":invalid} error:&error],"Invalid expression rejected");check([before isEqual:session.automationRevision],"Invalid formula mutation is atomic");
  auto lane=call(@"automation.pattern.get",@{@"pattern":@0})[@"data"][@"lanes"][0][@"id"];
  auto clip=call(@"automation.pattern.copy",@{@"lane":lane,@"start":@0,@"end":@512})[@"data"];
  call(@"automation.pattern.transform",@{@"lane":lane,@"operation":@"paste",@"start":@1024,@"options":@{@"clip":clip}},true);
  check([call(@"automation.pattern.get",@{@"pattern":@0})[@"data"][@"lanes"][0][@"points"][2][@"formula"] isEqual:@"mix(start,end,t^2)"],"Clipboard retains executable formula text");
  call(@"history.undo",@{@"domain":@"document"},true);call(@"history.undo",@{@"domain":@"document"},true);check([session.automationRevision isEqual:revision]==NO,"Undo remains revision guarded");
  check([call(@"automation.pattern.get",@{@"pattern":@0})[@"data"][@"lanes"] count]==0,"Undo removes scripted lane");call(@"history.redo",@{@"domain":@"document"},true);
  check([call(@"automation.pattern.get",@{@"pattern":@0})[@"data"][@"lanes"][0][@"points"] isEqual:points],"Redo restores formula");
  call(@"transport.loop",@{@"enabled":@YES},true);check([call(@"transport.get",@{})[@"data"][@"loop"] boolValue],"API loop preference is observable while stopped");
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
  std::cout<<"Script previews, persistence, clipboard, Undo, atomic validation and transport API passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}}
