#import "../Bridge/TrackerSession.h"
#include <iostream>
#include <stdexcept>

static void check(bool condition,const char *message) {if(!condition)throw std::runtime_error(message);}
@interface SessionDepartureFixture : NSObject <TrackerDocumentDepartureDelegate>
@property NSArray<NSString *> *work;
@property NSUInteger captures,begins,finishes;
@property BOOL adopted;
@end
@implementation SessionDepartureFixture
- (NSArray<NSString *> *)retainedNativeEditorWork {++_captures;return _work?:@[];}
- (void)documentReplacementWillBegin {++_begins;}
- (void)documentReplacementDidFinish:(BOOL)adopted {++_finishes;_adopted=adopted;}
@end

int main() {@autoreleasepool {try {
  auto session=[TrackerSession new];auto fixture=[SessionDepartureFixture new];
  session.documentDepartureDelegate=fixture;
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];
  NSError *error=nil;
  check([session savePath:path error:&error],"Create a private native fixture");
  NSString *original=session.automationRevision;
  fixture.work=@[@"Gate invalid text"];
  check(![session createSong:NO error:&error],"New must refuse retained work");
  check(error.code==-32002 && [error.userInfo[@"writeOutcome"] isEqual:@"notCommitted"],"New returns definite refusal");
  check([session.automationRevision isEqual:original] && fixture.begins==0,"Refused New preserves the current document");
  error=nil;
  check(![session openPath:path error:&error],"Open must refuse a prepared file with retained work");
  check(error.code==-32002 && [error.userInfo[@"writeOutcome"] isEqual:@"notCommitted"],"Open returns definite refusal");
  check([session.automationRevision isEqual:original] && fixture.work.count==1 && fixture.begins==0,"Refused Open preserves both document and editor");
  const auto captures=fixture.captures;
  check(![session openPath:[path stringByAppendingString:@".missing"] error:&error],"Missing file must fail");
  check(fixture.captures==captures && fixture.begins==0,"File preparation precedes native admission");
  fixture.work=@[];
  check([session openPath:path error:&error],"Clean Open succeeds");
  check(![session.automationRevision isEqual:original] && fixture.begins==1 && fixture.finishes==1 && fixture.adopted,"Open reports adopted identity through the native boundary");
  original=session.automationRevision;
  check([session createSong:NO error:&error],"Clean New succeeds");
  check(![session.automationRevision isEqual:original] && fixture.begins==2 && fixture.finishes==2 && fixture.adopted,"New uses the same adoption boundary");
  [session shutdown];[[NSFileManager defaultManager] removeItemAtPath:path error:nil];
  std::cout<<"PASS Mac session departure: prepared Open/New refusal, preserved document and draft, clean adoption\n";
  return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
