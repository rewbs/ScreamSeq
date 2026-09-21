// Included in TrackerSession.mm's anonymous namespace.
NSString *nativeID(uint64_t identifier) {
  return [NSString stringWithFormat:@"n%llu", (unsigned long long)identifier];
}
uint64_t decodeNativeID(id value) {
  NSString *text = Automation::string(value, 32);
  Automation::require([text hasPrefix:@"n"] && text.length > 1, "Invalid native identity");
  uint64_t result = 0;
  for (NSUInteger i = 1; i < text.length; ++i) {
    unichar ch = [text characterAtIndex:i];
    Automation::require(ch >= '0' && ch <= '9' && result < NativeSong::maximumID / 10, "Invalid native identity");
    result = result * 10 + ch - '0';
  }
  Automation::require(result > 0 && [nativeID(result) isEqual:text], "Invalid native identity");
  return result;
}
NSDictionary *nativeEntity(const NativeEntity &e) {
  return @{@"id": nativeID(e.id), @"name": @(e.name.c_str()), @"annotation": @(e.annotation.c_str()), @"color": @(e.color)};
}
NativeEntity decodeNativeEntity(id raw) {
  using namespace Automation;
  auto value = object(raw);
  keys(value, @[@"id", @"name", @"annotation", @"color"]);
  return {decodeNativeID(value[@"id"]), string(value[@"name"], 256).UTF8String,
          string(value[@"annotation"], 4096).UTF8String, uint32_t(integer(value[@"color"], 0, 0xffffff))};
}
NSDictionary *encodeMusicalLane(const MusicalAutomationLane &lane) {
  NSMutableArray *points = [NSMutableArray array];
  for (const auto &point : lane.points)
    [points addObject:point.formula.source().empty() ? @[@(point.position), @(point.value), @(uint8_t(point.curve))] : @[@(point.position), @(point.value), @(uint8_t(point.curve)), @(point.formula.source().c_str())]];
  return @{@"id": nativeID(lane.id), @"pattern": nativeID(lane.pattern), @"plugin": @(lane.plugin.c_str()),
           @"parameter": @(lane.parameter), @"enabled": @(lane.enabled), @"points": points};
}
NSString *automationCurveName(AutomationCurve curve) {
  return @[@"step", @"linear", @"smooth", @"exponential", @"logarithmic", @"step-next", @"exponential-reverse", @"logarithmic-reverse", @"scripted"][uint8_t(curve)];
}
AutomationCurve decodeAutomationCurve(id value) {
  NSArray *names = @[@"step", @"linear", @"smooth", @"exponential", @"logarithmic", @"step-next", @"exponential-reverse", @"logarithmic-reverse", @"scripted"];
  NSUInteger index = [names indexOfObject:Automation::string(value, 20)];
  Automation::require(index != NSNotFound, "Unknown automation curve");
  return AutomationCurve(index);
}
NSDictionary *encodeAutomationPoint(const AutomationPoint &point) {
  NSMutableDictionary *item = [@{@"position": @(point.position), @"value": @(point.value), @"curve": automationCurveName(point.curve)} mutableCopy];
  if(!point.formula.source().empty()) item[@"formula"] = @(point.formula.source().c_str());
  return item;
}
AutomationPoint decodeAutomationPoint(id raw, uint32_t maximum) {
  using namespace Automation; auto point = object(raw); keys(point, @[@"position", @"value", @"curve", @"formula"]);
  AutomationPoint result{uint32_t(integer(point[@"position"],0,maximum)),number(point[@"value"],0,1),decodeAutomationCurve(point[@"curve"] ?: @"linear")};
  if(point[@"formula"]) result.formula = CurveFormula(string(point[@"formula"],2048).UTF8String);
  require(result.curve != AutomationCurve::Scripted || !result.formula.source().empty(), "Scripted points require a formula");
  return result;
}
MusicalAutomationLane decodeMusicalLane(id raw) {
  using namespace Automation;
  auto value = object(raw);
  keys(value, @[@"id", @"pattern", @"plugin", @"parameter", @"enabled", @"points"]);
  MusicalAutomationLane lane;
  lane.id = decodeNativeID(value[@"id"]); lane.pattern = decodeNativeID(value[@"pattern"]);
  lane.plugin = string(value[@"plugin"], 128).UTF8String;
  lane.parameter = uint32_t(integer(value[@"parameter"], 0, UINT32_MAX));
  lane.enabled = boolean(value[@"enabled"]);
  for (id rawPoint in array(value[@"points"], 4096)) {
    auto point = array(rawPoint, 4);
    require(point.count == 3 || point.count == 4, "Automation points require position, value and curve");
    lane.points.push_back({uint32_t(integer(point[0], 0, UINT32_MAX)), number(point[1], 0, 1),
                          AutomationCurve(integer(point[2], 0, 8)), point.count == 4 ? CurveFormula(string(point[3], 2048).UTF8String) : CurveFormula{}});
  }
  return lane;
}
NSString *mixerBusKind(MixerBusKind kind) { return @[@"track", @"group", @"return", @"master"][uint8_t(kind)]; }
MixerBusKind decodeMixerKind(id value) {
  NSUInteger kind = [@[@"track", @"group", @"return", @"master"] indexOfObject:Automation::string(value, 16)];
  Automation::require(kind != NSNotFound, "Unknown mixer bus kind"); return MixerBusKind(kind);
}
NSDictionary *encodeMixer(const MixerGraph &graph) {
  NSMutableArray *buses = [NSMutableArray array], *instruments = [NSMutableArray array], *sidechains = [NSMutableArray array];
  for (const auto &bus : graph.buses) {
    NSMutableArray *inserts = [NSMutableArray array], *sends = [NSMutableArray array];
    for (const auto &plugin : bus.inserts) [inserts addObject:@(plugin.c_str())];
    for (const auto &send : bus.sends) [sends addObject:@{@"target": nativeID(send.target), @"gainDB": @(send.gainDB),
      @"preFader": @(send.preFader), @"enabled": @(send.enabled)}];
    NSMutableDictionary *encoded = [@{@"id": nativeID(bus.id), @"kind": mixerBusKind(bus.kind), @"name": @(bus.name.c_str()),
      @"color": @(bus.color), @"output": bus.output ? nativeID(bus.output) : @"",
      @"preGainDB": @(bus.preGainDB), @"gainDB": @(bus.gainDB), @"pan": @(bus.pan), @"width": @(bus.width),
      @"timingMS": @(bus.timingMS), @"mute": @(bus.mute), @"solo": @(bus.solo), @"inserts": inserts, @"sends": sends} mutableCopy];
    encoded[@"prePan"] = @(bus.prePan);
    [buses addObject:encoded];
  }
  for (const auto &source : graph.instruments) [instruments addObject:@{@"plugin": @(source.plugin.c_str()),
    @"target": source.target ? nativeID(source.target) : @"", @"output": @(source.output)}];
  for (const auto &side : graph.sidechains) [sidechains addObject:@{@"source": nativeID(side.source), @"plugin": @(side.plugin.c_str()),
    @"input": @(side.input), @"gainDB": @(side.gainDB), @"preFader": @(side.preFader), @"enabled": @(side.enabled)}];
  return @{@"buses": buses, @"instruments": instruments, @"sidechains": sidechains};
}
MixerGraph decodeMixer(id raw) {
  using namespace Automation;
  auto value = object(raw); keys(value, @[@"buses", @"instruments", @"sidechains"]);
  MixerGraph graph;
  for (id rawBus in array(value[@"buses"], 240)) {
    auto v = object(rawBus);
    keys(v, @[@"id", @"kind", @"name", @"color", @"output", @"preGainDB", @"prePan", @"gainDB", @"pan", @"width", @"timingMS", @"mute", @"solo", @"inserts", @"sends"]);
    MixerBus bus; bus.id = decodeNativeID(v[@"id"]); bus.kind = decodeMixerKind(v[@"kind"]);
    bus.name = string(v[@"name"], 256).UTF8String; bus.color = uint32_t(integer(v[@"color"], 0, 0xffffff));
    bus.output = [v[@"output"] isEqual:@""] ? 0 : decodeNativeID(v[@"output"]);
    bus.preGainDB = number(v[@"preGainDB"], -96, 24); bus.gainDB = number(v[@"gainDB"], -96, 24);
    bus.prePan = number(v[@"prePan"], -1, 1);
    bus.pan = number(v[@"pan"], -1, 1); bus.width = number(v[@"width"], 0, 2); bus.timingMS = number(v[@"timingMS"], -500, 500);
    bus.mute = boolean(v[@"mute"]); bus.solo = boolean(v[@"solo"]);
    for (id insert in array(v[@"inserts"], 32)) bus.inserts.push_back(string(insert, 128).UTF8String);
    for (id rawSend in array(v[@"sends"], 16)) {
      auto send = object(rawSend); keys(send, @[@"target", @"gainDB", @"preFader", @"enabled"]);
      bus.sends.push_back({decodeNativeID(send[@"target"]), number(send[@"gainDB"], -96, 12), boolean(send[@"preFader"]), boolean(send[@"enabled"])});
    }
    graph.buses.push_back(std::move(bus));
  }
  for (id rawSource in array(value[@"instruments"], 128)) {
    auto source = object(rawSource); keys(source, @[@"plugin", @"target", @"output"]);
    graph.instruments.push_back({string(source[@"plugin"], 128).UTF8String, [source[@"target"] isEqual:@""] ? 0 : decodeNativeID(source[@"target"]), uint32_t(integer(source[@"output"], 0, 63))});
  }
  for (id rawSide in array(value[@"sidechains"], 128)) {
    auto side = object(rawSide); keys(side, @[@"source", @"plugin", @"input", @"gainDB", @"preFader", @"enabled"]);
    graph.sidechains.push_back({decodeNativeID(side[@"source"]), string(side[@"plugin"], 128).UTF8String,
      uint32_t(integer(side[@"input"], 1, 63)), number(side[@"gainDB"], -96, 12), boolean(side[@"preFader"]), boolean(side[@"enabled"])});
  }
  return graph;
}
#include "EnvelopeBankMetadata.inc"
#include "PatternPerformanceMetadata.inc"
#include "SignalGraphMetadata.inc"
NSDictionary *encodePreciseNote(const PreciseNote &note) {
  NSMutableDictionary *item=[@{@"position":@(note.position),@"instrument":@(note.instrument),@"note":@(note.note),@"velocity":@(note.velocity)} mutableCopy];
  if(note.effect) {item[@"effect"]=@(note.effect);item[@"parameter"]=@(note.parameter);}
  return item;
}
NSDictionary *encodeNativeSong(const NativeSong &native) {
  auto map = [](const auto &items) {
    NSMutableArray *result = [NSMutableArray array];
    for (const auto &[index, e] : items) [result addObject:@[@(index), nativeEntity(e)]];
    return result;
  };
  NSMutableArray *sequences = [NSMutableArray array];
  for (const auto &sequence : native.sequences) {
    NSMutableArray *orders = [NSMutableArray array];
    for (const auto &slot : sequence.orders) [orders addObject:nativeEntity(slot)];
    [sequences addObject:@{@"info": nativeEntity(sequence.info), @"orders": orders}];
  }
  NSMutableArray *automation = [NSMutableArray array];
  for (const auto &lane : native.automation) [automation addObject:encodeMusicalLane(lane)];
  NSMutableArray *noteTracks = [NSMutableArray array], *mutes = [NSMutableArray array];
  for (const auto &track : native.noteTracks) {
    NSMutableArray *columns = [NSMutableArray array];
    for (auto id : track.columns) [columns addObject:nativeID(id)];
    [noteTracks addObject:@{@"bus": nativeID(track.bus), @"columns": columns}];
  }
  for (const auto &[id, muted] : native.columnMutes) [mutes addObject:@[nativeID(id), @(muted)]];
  NSMutableDictionary *result = [@{@"version": @17, @"nextID": @(native.nextID), @"patterns": map(native.patterns),
           @"tracks": map(native.tracks), @"samples": map(native.samples), @"instruments": map(native.instruments),
           @"sequences": sequences, @"automation": automation, @"mixer": encodeMixer(native.mixer)} mutableCopy];
  result[@"envelopeBank"]=encodeEnvelopeBank(native);
  result[@"signalGraph"] = encodeSignalGraph(native.signal);
  {
    NSMutableArray *notes=[NSMutableArray array];
    for(const auto &note:native.preciseNotes) {
      NSMutableDictionary *item=[encodePreciseNote(note) mutableCopy];item[@"pattern"]=nativeID(note.pattern);item[@"track"]=nativeID(note.track);[notes addObject:item];
    }
    result[@"preciseNotes"]=notes;
  }
  result[@"performance"] = encodePatternPerformance(native.performance);
  { result[@"noteTracks"] = noteTracks; result[@"columnMutes"] = mutes; }
  return result;
}
NativeSong decodeNativeSong(id raw) {
  using namespace Automation;
  auto value = object(raw);
  keys(value, @[@"version", @"nextID", @"patterns", @"tracks", @"samples", @"instruments", @"sequences", @"automation", @"mixer", @"noteTracks", @"columnMutes", @"performance", @"preciseNotes", @"signalGraph", @"envelopeBank"]);
  integer(value[@"version"], 17, 17);
  NativeSong result;
  result.nextID = integer(value[@"nextID"], 1, NativeSong::maximumID);
  auto map = [&](NSString *key, auto &out) {
    for (id item in array(value[key], 65536)) {
      auto pair = array(item, 2);
      require(pair.count == 2, "Invalid indexed metadata");
      require(out.emplace(uint16_t(integer(pair[0], 0, UINT16_MAX)), decodeNativeEntity(pair[1])).second,
              "Duplicate metadata index");
    }
  };
  map(@"patterns", result.patterns); map(@"tracks", result.tracks);
  map(@"samples", result.samples); map(@"instruments", result.instruments);
  for (id item in array(value[@"sequences"], 256)) {
    auto sequence = object(item);
    keys(sequence, @[@"info", @"orders"]);
    NativeSequence entry{decodeNativeEntity(sequence[@"info"]), {}};
    for (id slot in array(sequence[@"orders"], 65536)) entry.orders.push_back(decodeNativeEntity(slot));
    result.sequences.push_back(std::move(entry));
  }
  for (id lane in array(value[@"automation"], 256)) result.automation.push_back(decodeMusicalLane(lane));
  result.mixer = decodeMixer(value[@"mixer"]);
  {
    for (id rawTrack in array(value[@"noteTracks"], 127)) {
      auto track = object(rawTrack); keys(track, @[@"bus", @"columns"]);
      NativeNoteTrack entry{decodeNativeID(track[@"bus"]), {}};
      for (id column in array(track[@"columns"], 127)) entry.columns.push_back(decodeNativeID(column));
      result.noteTracks.push_back(std::move(entry));
    }
    for (id rawMute in array(value[@"columnMutes"], 127)) {
      auto pair = array(rawMute, 2); require(pair.count == 2, "Invalid column mute override");
      require(result.columnMutes.emplace(decodeNativeID(pair[0]), boolean(pair[1])).second, "Duplicate column mute override");
    }
  }
  for(id rawNote:array(value[@"preciseNotes"],maximumPreciseNotes)) {
    auto n=object(rawNote);keys(n,@[@"pattern",@"track",@"position",@"instrument",@"note",@"velocity",@"effect",@"parameter"]);
    result.preciseNotes.push_back({decodeNativeID(n[@"pattern"]),decodeNativeID(n[@"track"]),uint32_t(integer(n[@"position"],0,UINT32_MAX)),
      uint16_t(integer(n[@"instrument"],0,255)),uint8_t(integer(n[@"note"],1,255)),uint8_t(integer(n[@"velocity"],1,127)),
      uint8_t(integer(n[@"effect"]?:@0,0,255)),uint8_t(integer(n[@"parameter"]?:@0,0,255))});
  }
  result.signal=decodeSignalGraph(value[@"signalGraph"]);
  decodeEnvelopeBank(result,value[@"envelopeBank"]);
  result.performance=decodePatternPerformance(value[@"performance"]);
  return result;
}
