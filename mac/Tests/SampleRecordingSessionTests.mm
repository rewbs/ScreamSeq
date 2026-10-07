#import "../Bridge/TrackerSession.h"
#include <iostream>
#include <stdexcept>
static void check(bool result, const char *message) { if(!result) throw std::runtime_error(message); }
int main() { @autoreleasepool { try {
  TrackerSession *session = [TrackerSession new]; NSError *error = nil;
  auto call = [&](NSString *method, NSDictionary *params) -> NSDictionary * {
    error = nil; return [session automationMethod:method params:params error:&error];
  };
  auto idle = call(@"sample.recording.get", @{});
  check(idle && [idle[@"data"][@"take"] isEqual:@""] && ![idle[@"data"][@"capturing"] boolValue], "Sample recording starts idle without opening a microphone");
  check(!call(@"sample.recording.get", @{@"unknown": @YES}) && error.code == -32602, "Recording reads reject unknown fields");
  NSString *revision = session.automationRevision;
  for(NSString *method in @[@"sample.recording.get", @"sample.recording.stop", @"sample.recording.discard"])
    check(!call(method, @{@"take": @"retired-take"}) && error.code == -32001, "Stale transient take IDs reject without requiring song revision");
  check(!call(@"sample.recording.start", @{@"expectedRevision": @"stale"}) && error.code == -32001,
        "Recording start rejects a stale document before device or permission work");
  for(NSDictionary *values in @[@{@"channels": @YES}, @{@"channels": @3}, @{@"firstChannel": @-1},
                               @{@"maxSeconds": @0}, @{@"maxSeconds": @301}, @{@"unknown": @1}]) {
    NSMutableDictionary *params = [values mutableCopy]; params[@"expectedRevision"] = revision;
    check(!call(@"sample.recording.start", params) && error.code == -32602, "Invalid capture settings reject before opening hardware");
  }
  check(!call(@"sample.recording.commit", @{@"take": @"retired-take", @"expectedRevision": revision}) && error.code == -32001,
        "Missing captured take cannot modify the song");
  check([revision isEqual:session.automationRevision] && [call(@"sample.recording.get", @{})[@"data"][@"take"] length] == 0,
        "Rejected recording commands preserve song revision and idle state");
  [session shutdown];
  std::cout << "PASS sample recording API: idle read, strict fields, numeric validation, transient take guard and stale start/commit; no microphone opened\n";
  return 0;
} catch(const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; } } }
