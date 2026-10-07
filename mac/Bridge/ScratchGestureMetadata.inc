NSDictionary *encodeScratchGesture(uint16_t id,const ScratchGesture &gesture) {
  NSMutableArray *motion=[NSMutableArray array],*fader=[NSMutableArray array];
  for(const auto &point:gesture.motion)[motion addObject:encodeAutomationPoint(point)];
  for(const auto &point:gesture.fader)[fader addObject:encodeAutomationPoint(point)];
  return @{@"id":@(id),@"name":@(gesture.name.c_str()),@"motion":motion,@"fader":fader};
}
ScratchGesture decodeScratchGesture(NSDictionary *item) {
  using namespace Automation;
  keys(item,@[@"id",@"name",@"motion",@"fader"]);
  ScratchGesture result;result.name=string(item[@"name"],256).UTF8String;
  for(id point:array(item[@"motion"],maximumScratchPoints))result.motion.push_back(decodeAutomationPoint(point,scratchCycleUnits));
  for(id point:array(item[@"fader"],maximumScratchPoints))result.fader.push_back(decodeAutomationPoint(point,scratchCycleUnits));
  validateScratchGesture(result);return result;
}
NSArray *encodeScratchGestures(const NativeSong &native) {
  NSMutableArray *result=[NSMutableArray array];
  for(const auto &[id,gesture]:native.scratchGestures)[result addObject:encodeScratchGesture(id,gesture)];
  return result;
}
NSUInteger scratchGestureUses(const NativeSong &native,uint16_t id) {
  return std::count_if(native.performance.commands.begin(),native.performance.commands.end(),[&](const auto &c){
    return c.kind==PatternCommandKind::Native&&c.native==NativePatternOp::Scratch&&c.arguments[0]==id;
  });
}
NSArray *scratchGestureSummaries(const NativeSong &native) {
  NSMutableArray *result=[NSMutableArray array];
  for(const auto &[id,gesture]:native.scratchGestures)[result addObject:@{@"id":@(id),@"name":@(gesture.name.c_str()),@"uses":@(scratchGestureUses(native,id))}];
  return result;
}
