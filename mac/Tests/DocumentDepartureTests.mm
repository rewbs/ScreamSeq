#include "../Bridge/DocumentDeparture.hpp"
#include <cassert>
#include <iostream>
#include <thread>

@interface DepartureFixture : NSObject <TrackerDocumentDepartureDelegate>
@property NSArray<NSString *> *work;
@property BOOL injectAtGate,blocked,adopted;
@property NSUInteger captures,begins,finishes;
@end
@implementation DepartureFixture
- (NSArray<NSString *> *)retainedNativeEditorWork {
  assert(NSThread.isMainThread);++_captures;return _work?:@[];
}
- (void)documentReplacementWillBegin {
  assert(NSThread.isMainThread && !_blocked);_blocked=YES;++_begins;
  if(_injectAtGate)_work=@[@"Gate invalid text"];
}
- (void)documentReplacementDidFinish:(BOOL)adopted {
  assert(NSThread.isMainThread && _blocked);_adopted=adopted;++_finishes;
  if(!adopted)_blocked=NO;
}
@end

int main() {@autoreleasepool {
  auto fixture=[DepartureFixture new];
  fixture.work=@[@"Hidden sample draft",@"Gate invalid text"];
  try {ScreamSeq::NativeDocumentDeparture guard(fixture,@"song:1");assert(false);}
  catch(const ScreamSeq::NativeDepartureRefused &e){assert(std::string(e.what()).find("Gate invalid text")!=std::string::npos);}
  assert(fixture.captures==1 && fixture.begins==0 && fixture.finishes==0 && fixture.work.count==2);
  fixture.work=@[];fixture.injectAtGate=YES;
  try {ScreamSeq::NativeDocumentDeparture guard(fixture,@"song:1");assert(false);}
  catch(const ScreamSeq::NativeDepartureRefused &){}
  assert(fixture.begins==1 && fixture.finishes==1 && !fixture.blocked && fixture.work.count==1);
  fixture.work=@[];fixture.injectAtGate=NO;
  try {ScreamSeq::NativeDocumentDeparture guard(fixture,@"song:1");assert(fixture.blocked);throw std::runtime_error("candidate adoption failed");}
  catch(const std::runtime_error &){}
  assert(fixture.finishes==2 && !fixture.blocked && !fixture.adopted);
  // The worker must marshal both census and lifecycle calls to the UI thread.
  __block bool finished=false;
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
    {ScreamSeq::NativeDocumentDeparture guard(fixture,@"song:2");guard.adopted();}
    dispatch_async(dispatch_get_main_queue(),^{finished=true;});
  });
  const auto deadline=[NSDate dateWithTimeIntervalSinceNow:5];
  while(!finished && deadline.timeIntervalSinceNow>0)
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
  assert(finished && fixture.adopted && fixture.blocked && fixture.finishes==3);
  {ScreamSeq::NativeDocumentDeparture headless(nil,@"song:3");headless.adopted();}
  std::cout<<"Mac document departure: PASS\n";
}}
