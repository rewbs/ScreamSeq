#import "../Bridge/TrackerSession.h"
#include <iostream>
static void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
static NSDictionary *call(TrackerSession *session, NSString *method, NSDictionary *params, bool write=false) {
  NSMutableDictionary *request=[params mutableCopy]; if(write)request[@"expectedRevision"]=session.automationRevision;
  NSError *error=nil;auto reply=[session automationMethod:method params:request error:&error];
  if(!reply)throw std::runtime_error(error.localizedDescription.UTF8String);return reply[@"data"];
}
int main(){@autoreleasepool{try{
  NSString *directory=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  [[NSFileManager defaultManager]createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
  NSString *path=[directory stringByAppendingPathComponent:@"recovery.resonance"];
  TrackerSession *source=[TrackerSession new];NSError *error=nil;
  call(source,@"recording.start",@{@"channels":@[@0],@"instrument":@1},true);
  NSString *originalTake=source.recordingTakeID,*revision=source.automationRevision;
  check(![source savePath:path error:&error],"Normal save still guards an unfinished take");
  check([source saveRecoveryPath:path error:&error],"Recovery can snapshot an active take");
  check(source.recordingActive&&[source.recordingTakeID isEqual:originalTake]&&[source.automationRevision isEqual:revision],"Snapshot leaves live take and revision untouched");
  NSMutableDictionary *root=[NSPropertyListSerialization propertyListWithData:[NSData dataWithContentsOfFile:path] options:NSPropertyListMutableContainersAndLeaves format:nil error:&error];
  check([root[@"recoveryTake"] isKindOfClass:NSDictionary.class],"Recovery persists take metadata");
  // Supply a deterministic, previously captured take; no hardware input is used.
  NSDictionary *native=root[@"native"];
  id firstPattern=[native[@"patterns"] firstObject],firstTrack=[native[@"tracks"] firstObject];
  auto entityID=[](id item)->id {return [item isKindOfClass:NSDictionary.class]?item[@"id"]:item[1][@"id"];};
  id pattern=entityID(firstPattern),track=entityID(firstTrack);
  root[@"recoveryTake"][@"events"]=@[
    @{@"pattern":pattern,@"track":track,@"position":@1234,@"note":@61,@"instrument":@1,@"velocity":@93},
    @{@"pattern":pattern,@"track":track,@"position":@9000,@"note":@255,@"instrument":@0,@"velocity":@127}];
  auto write=[&]{NSData *data=[NSPropertyListSerialization dataWithPropertyList:root format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];check([data writeToFile:path options:NSDataWritingAtomic error:&error],"Write recovery fixture");};write();
  TrackerSession *recovered=[TrackerSession new];
  check([recovered openPath:path error:&error]&&!recovered.recordingActive&&recovered.recordingTakeID!=nil,"Recovery restores a stopped take");
  auto take=call(recovered,@"recording.get",@{});
  check([take[@"events"] count]==2&&[take[@"baseRevision"] isEqual:recovered.automationRevision],"Recovered take retains exact events and compatible song identity");
  call(recovered,@"recording.commit",@{@"take":recovered.recordingTakeID,@"replaceRows":@YES},true);
  auto notes=call(recovered,@"pattern.notes.get",@{@"pattern":@0});
  check([notes[@"events"] count]==2&&[notes[@"events"][0][@"position"] intValue]==1234,"Recovered take commits fractional positions");
  call(recovered,@"history.undo",@{@"domain":@"document"},true);
  check([call(recovered,@"pattern.notes.get",@{@"pattern":@0})[@"events"] count]==0,"Recovered take is one undoable edit");
  root[@"recoveryTake"][@"compatible"]=@NO;write();
  check([recovered openPath:path error:&error],"Incompatible take remains recoverable for inspection");
  check(![recovered automationMethod:@"recording.commit" params:@{@"expectedRevision":recovered.automationRevision,@"take":recovered.recordingTakeID} error:&error],"Incompatible recording cannot silently target a changed song");
  call(recovered,@"recording.discard",@{@"take":recovered.recordingTakeID},true);
  root[@"recoveryTake"][@"events"]=@[@{@"pattern":pattern,@"track":track,@"position":@1234,@"note":@200,@"instrument":@1,@"velocity":@93}];write();
  revision=recovered.automationRevision;
  check(![recovered openPath:path error:&error]&&[recovered.automationRevision isEqual:revision],"Malformed recovery is rejected without replacing the current document");
  [[NSFileManager defaultManager]removeItemAtPath:directory error:nil];
  std::cout<<"PASS recovery snapshot: active take isolation, exact recall, stopped restore, commit/undo, incompatible and malformed takes\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}}}
