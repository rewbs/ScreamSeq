#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include "editor/SongTiming.hpp"
#include "editor/SampleArchive.hpp"
#include <iostream>
using namespace Tracker;
using namespace OpenMPT;
static void check(bool result,const char *message){if(!result)throw std::runtime_error(message);}
int main(){@autoreleasepool{try{
  NSString *folder=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  [NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
  for(auto type:{MOD_TYPE_MOD,MOD_TYPE_XM,MOD_TYPE_S3M,MOD_TYPE_IT,MOD_TYPE_MPT}) {
    auto doc=Document::demo(type);NSString *path=[folder stringByAppendingPathComponent:@"source.module"];doc->save(path.UTF8String);
    TrackerSession *session=[TrackerSession new];NSError *error=nil;
    check([session openPath:path error:&error],"Open source module");
    auto call=[&](NSString *method,NSDictionary *p,bool mutation=false){auto params=[p mutableCopy];if(mutation)params[@"expectedRevision"]=session.automationRevision;
      NSDictionary *result=[session automationMethod:method params:params error:&error];if(!result)throw std::runtime_error(error.localizedDescription.UTF8String);return result;};
    auto before=call(@"document.timing.get",@{})[@"data"];
    const auto patch=@{@"mode":@"modern",@"tempo":@127.125,@"speed":@7,@"rowsPerBeat":@4,@"rowsPerMeasure":@12,@"groove":@[@1.5,@0.5,@1.25,@0.75]};
    call(@"document.timing.set",patch,true);auto edited=call(@"document.timing.get",@{})[@"data"];
    NSData *saved=[session serializedData];NSString *native=[folder stringByAppendingPathComponent:@"saved.resonance"];
    [saved writeToFile:native atomically:YES];check([session openPath:native error:&error],"Reopen timed native project");
    check([call(@"document.timing.get",@{})[@"data"] isEqual:edited],"Five-format native project retains all timing fields");
    NSDictionary *root=[NSPropertyListSerialization propertyListWithData:saved options:0 format:nil error:nil];NSData *bytes=root[@"module"];
    Document restored({static_cast<const std::byte *>(bytes.bytes),static_cast<const std::byte *>(bytes.bytes)+bytes.length});
    check(songTiming(restored.song()).sequences[0].tempo==1271250,"Project's actual renderer payload has fractional timing");
    NSString *wav=[folder stringByAppendingPathComponent:@"before.wav"],*reopened=[folder stringByAppendingPathComponent:@"after.wav"];
    check([TrackerSession exportData:saved path:wav error:&error] && [TrackerSession exportData:[session serializedData] path:reopened error:&error],"Timed project renders to silent offline WAV");
    check([[NSData dataWithContentsOfFile:wav] isEqual:[NSData dataWithContentsOfFile:reopened]],"Native reopen preserves complete WAV output sample-for-sample");
    auto parts=splitSongSnapshot({static_cast<const std::byte *>(bytes.bytes),bytes.length});
    if(!parts.timing.empty()) {
      auto malformed=std::vector<std::byte>(parts.timing.begin(),parts.timing.end());malformed[0]=std::byte{255};
      auto damaged=packSongSnapshot(parts.module,parts.samples,malformed);auto bad=[root mutableCopy];bad[@"module"]=[NSData dataWithBytes:damaged.data() length:damaged.size()];
      NSString *badPath=[folder stringByAppendingPathComponent:@"bad.resonance"];
      [[NSPropertyListSerialization dataWithPropertyList:bad format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil] writeToFile:badPath atomically:YES];
      NSString *revision=session.automationRevision;
      check(![session openPath:badPath error:&error]&&[revision isEqual:session.automationRevision],"Invalid timing project does not replace the working song");
    }
    call(@"document.timing.set",@{@"tempo":@140.56789},true);
    call(@"history.undo",@{@"domain":@"document"},true);
    check([call(@"document.timing.get",@{})[@"data"] isEqual:edited],"One Undo restores previous timing after reopen");
    (void)before;
  }
  [NSFileManager.defaultManager removeItemAtPath:folder error:nil];
  std::cout<<"PASS timing sessions: five-format project save/reopen, actual complete WAV equality, fractional payload, one-step Undo and atomic malformed-project rejection\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}}
