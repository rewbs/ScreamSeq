// Graph metadata is shared by the project format and the revision-checked API.
NSString *signalNodeKind(SignalNodeKind kind) {return @[@"input",@"output",@"plugin",@"lfo",@"follower",@"random",@"note-envelope",@"midi",@"amount"][uint8_t(kind)];}
SignalNodeKind decodeSignalNodeKind(id raw){const auto index=[@[@"input",@"output",@"plugin",@"lfo",@"follower",@"random",@"note-envelope",@"midi",@"amount"] indexOfObject:Automation::string(raw,32)];Automation::require(index!=NSNotFound,"Unknown graph node kind");return SignalNodeKind(index);}
NSString *signalCommandKind(SignalCommandKind kind){return @[@"row",@"start",@"stop",@"clear",@"amount",@"wet"][uint8_t(kind)];}
NSArray *encodeSignalActivity(const std::vector<SignalActivity> &activity){NSMutableArray *result=[NSMutableArray array];for(const auto &a:activity)[result addObject:@{@"target":nativeID(a.target),@"graph":nativeID(a.graph),@"role":@[@"row",@"persistent",@"ordinary"][a.role],@"order":@(a.order),@"tail":@(a.tail)}];return result;}
SignalCommandKind decodeSignalCommandKind(id raw){const auto index=[@[@"row",@"start",@"stop",@"clear",@"amount",@"wet"] indexOfObject:Automation::string(raw,16)];Automation::require(index!=NSNotFound,"Unknown graph command kind");return SignalCommandKind(index);}
NSDictionary *encodeSignalRecipe(const GraphPluginRecipe &p,bool includeState=true){
  NSMutableArray *inputs=[NSMutableArray array],*outputs=[NSMutableArray array];for(auto port:p.inputs)[inputs addObject:@(port)];for(auto port:p.outputs)[outputs addObject:@(port)];
  NSMutableDictionary *result=[@{@"format":@(p.format.c_str()),@"name":@(p.name.c_str()),@"path":@(p.path.c_str()),@"classID":@(p.classID.c_str()),@"type":@(p.type),@"subtype":@(p.subtype),@"manufacturer":@(p.manufacturer),@"inputs":inputs,@"outputs":outputs} mutableCopy];
  if(includeState){NSData *state=[NSData dataWithBytes:p.state.data() length:p.state.size()];result[@"state"]=[state base64EncodedStringWithOptions:0];}return result;
}
GraphPluginRecipe decodeSignalRecipe(id raw){
  using namespace Automation;auto p=object(raw);keys(p,@[@"format",@"name",@"path",@"classID",@"type",@"subtype",@"manufacturer",@"state",@"inputs",@"outputs"]);
  GraphPluginRecipe r;r.format=string(p[@"format"],16).UTF8String;r.name=string(p[@"name"]?:@"",256).UTF8String;r.path=string(p[@"path"]?:@"",4096).UTF8String;r.classID=string(p[@"classID"]?:@"",256).UTF8String;
  r.type=uint32_t(integer(p[@"type"]?:@0,0,UINT32_MAX));r.subtype=uint32_t(integer(p[@"subtype"]?:@0,0,UINT32_MAX));r.manufacturer=uint32_t(integer(p[@"manufacturer"]?:@0,0,UINT32_MAX));
  NSData *state=[[NSData alloc] initWithBase64EncodedString:string(p[@"state"]?:@"",12*1024*1024) options:0];require(state!=nil&&state.length<=8*1024*1024,"Invalid graph plugin state");const auto *bytes=static_cast<const std::byte *>(state.bytes);if(state.length)r.state.assign(bytes,bytes+state.length);
  for(id v:array(p[@"inputs"]?:@[],63))r.inputs.push_back(uint32_t(integer(v,1,63)));for(id v:array(p[@"outputs"]?:@[],63))r.outputs.push_back(uint32_t(integer(v,1,63)));return r;
}
NSDictionary *encodeSignalDefinition(const SignalDefinition &d,bool includeState=true){
  NSMutableArray *nodes=[NSMutableArray array],*audio=[NSMutableArray array],*modulation=[NSMutableArray array];
  for(const auto &n:d.nodes){NSMutableDictionary *item=[@{@"id":nativeID(n.id),@"kind":signalNodeKind(n.kind),@"name":@(n.name.c_str()),@"x":@(n.x),@"y":@(n.y),@"rate":@(n.rate),@"phase":@(n.phase),@"attack":@(n.attack),@"release":@(n.release),@"controller":@(n.controller)} mutableCopy];if(n.kind==SignalNodeKind::Plugin)item[@"plugin"]=encodeSignalRecipe(n.plugin,includeState);[nodes addObject:item];}
  for(const auto &e:d.audio)[audio addObject:@{@"source":nativeID(e.source),@"target":nativeID(e.target),@"output":@(e.output),@"input":@(e.input),@"gain":@(e.gain)}];
  for(const auto &e:d.modulation)[modulation addObject:@{@"source":nativeID(e.source),@"target":nativeID(e.target),@"parameter":@(e.parameter),@"minimum":@(e.minimum),@"maximum":@(e.maximum),@"base":@(e.base),@"enabled":@(e.enabled)}];
  return @{@"id":nativeID(d.id),@"number":@(d.number),@"name":@(d.name.c_str()),@"nodes":nodes,@"audio":audio,@"modulation":modulation};
}
SignalDefinition decodeSignalDefinition(id raw){
  using namespace Automation;auto v=object(raw);keys(v,@[@"id",@"number",@"name",@"nodes",@"audio",@"modulation"]);SignalDefinition d;d.id=decodeNativeID(v[@"id"]);d.number=uint16_t(integer(v[@"number"],1,999));d.name=string(v[@"name"],256).UTF8String;
  for(id rawNode:array(v[@"nodes"],64)){auto n=object(rawNode);keys(n,@[@"id",@"kind",@"name",@"x",@"y",@"plugin",@"rate",@"phase",@"attack",@"release",@"controller"]);SignalNode node;node.id=decodeNativeID(n[@"id"]);node.kind=decodeSignalNodeKind(n[@"kind"]);node.name=string(n[@"name"]?:@"",256).UTF8String;node.x=number(n[@"x"]?:@0,-100000,100000);node.y=number(n[@"y"]?:@0,-100000,100000);node.rate=number(n[@"rate"]?:@1,.0001,1024);node.phase=number(n[@"phase"]?:@0,0,1);node.attack=number(n[@"attack"]?:@.01,.00001,60);node.release=number(n[@"release"]?:@.1,.00001,60);node.controller=uint32_t(integer(n[@"controller"]?:@1,0,127));if(node.kind==SignalNodeKind::Plugin)node.plugin=decodeSignalRecipe(n[@"plugin"]);else require(!n[@"plugin"],"Only plugin nodes have a plugin recipe");d.nodes.push_back(std::move(node));}
  for(id rawEdge:array(v[@"audio"],256)){auto e=object(rawEdge);keys(e,@[@"source",@"target",@"input",@"output",@"gain"]);d.audio.push_back({decodeNativeID(e[@"source"]),decodeNativeID(e[@"target"]),uint32_t(integer(e[@"output"]?:@0,0,63)),uint32_t(integer(e[@"input"]?:@0,0,63)),number(e[@"gain"]?:@1,-16,16)});}
  for(id rawEdge:array(v[@"modulation"],256)){auto e=object(rawEdge);keys(e,@[@"source",@"target",@"parameter",@"minimum",@"maximum",@"base",@"enabled"]);d.modulation.push_back({decodeNativeID(e[@"source"]),decodeNativeID(e[@"target"]),uint32_t(integer(e[@"parameter"],0,UINT32_MAX)),number(e[@"minimum"]?:@0,-1,1),number(e[@"maximum"]?:@1,-1,1),number(e[@"base"]?:@0,0,1),e[@"enabled"]?boolean(e[@"enabled"]):true});}
  compileSignal(d);return d;
}
NSDictionary *encodeSignalCommand(const SignalCommand &c){return @{@"pattern":nativeID(c.pattern),@"target":nativeID(c.target),@"graph":c.graph?nativeID(c.graph):@"",@"position":@(c.position),@"column":@(c.column),@"kind":signalCommandKind(c.kind),@"amount":@(c.amount),@"wet":@(c.wet),@"tails":@(c.tails)};}
SignalCommand decodeSignalCommand(id raw){using namespace Automation;auto c=object(raw);keys(c,@[@"pattern",@"target",@"graph",@"position",@"column",@"kind",@"amount",@"wet",@"tails"]);return {decodeNativeID(c[@"pattern"]),decodeNativeID(c[@"target"]),[c[@"graph"] isEqual:@""]?0:decodeNativeID(c[@"graph"]),uint32_t(integer(c[@"position"],0,UINT32_MAX)),uint8_t(integer(c[@"column"]?:@0,0,7)),decodeSignalCommandKind(c[@"kind"]),number(c[@"amount"]?:@1,0,1),number(c[@"wet"]?:@1,0,1),c[@"tails"]?boolean(c[@"tails"]):false};}
NSDictionary *encodeSignalGraph(const SignalGraph &g,bool includeState=true){
  NSMutableArray *library=[NSMutableArray array],*assignments=[NSMutableArray array],*commands=[NSMutableArray array],*lanes=[NSMutableArray array],*inputs=[NSMutableArray array],*outputs=[NSMutableArray array],*layout=[NSMutableArray array];
  for(const auto &[key,p]:g.layout)[layout addObject:@{@"node":@(key.c_str()),@"x":@(p[0]),@"y":@(p[1])}];
  for(const auto &d:g.library)[library addObject:encodeSignalDefinition(d,includeState)];for(const auto &a:g.assignments)[assignments addObject:@{@"target":nativeID(a.target),@"graph":nativeID(a.graph),@"amount":@(a.amount),@"wet":@(a.wet)}];for(const auto &c:g.commands)[commands addObject:encodeSignalCommand(c)];for(auto [target,count]:g.lanes)[lanes addObject:@{@"target":nativeID(target),@"count":@(count)}];
  for(const auto &r:g.inputs)[inputs addObject:@{@"source":nativeID(r.source),@"target":nativeID(r.target),@"input":@(r.input),@"gainDB":@(r.gainDB),@"preFader":@(r.preFader)}];for(const auto &r:g.outputs)[outputs addObject:@{@"source":nativeID(r.source),@"target":nativeID(r.target),@"output":@(r.output)}];
  return @{@"library":library,@"assignments":assignments,@"commands":commands,@"lanes":lanes,@"inputs":inputs,@"outputs":outputs,@"layout":layout};
}
SignalGraph decodeSignalGraph(id raw){using namespace Automation;auto g=object(raw);keys(g,@[@"library",@"assignments",@"commands",@"lanes",@"inputs",@"outputs",@"layout"]);SignalGraph result;
  for(id rawPosition:array(g[@"layout"]?:@[],8192)){auto p=object(rawPosition);keys(p,@[@"node",@"x",@"y"]);require(result.layout.emplace(string(p[@"node"],256).UTF8String,std::array<double,2>{number(p[@"x"],0,100000),number(p[@"y"],0,100000)}).second,"Duplicate graph position");}
  for(id d:array(g[@"library"],128))result.library.push_back(decodeSignalDefinition(d));
  for(id rawAssignment:array(g[@"assignments"],240)){auto a=object(rawAssignment);keys(a,@[@"target",@"graph",@"amount",@"wet"]);result.assignments.push_back({decodeNativeID(a[@"target"]),decodeNativeID(a[@"graph"]),number(a[@"amount"]?:@1,0,1),number(a[@"wet"]?:@1,0,1)});}
  for(id c:array(g[@"commands"],65536))result.commands.push_back(decodeSignalCommand(c));
  for(id rawLane:array(g[@"lanes"],240)){auto l=object(rawLane);keys(l,@[@"target",@"count"]);require(result.lanes.emplace(decodeNativeID(l[@"target"]),uint8_t(integer(l[@"count"],1,8))).second,"Duplicate graph lane");}
  for(id raw:array(g[@"inputs"]?:@[],128)){auto r=object(raw);keys(r,@[@"source",@"target",@"input",@"gainDB",@"preFader"]);result.inputs.push_back({decodeNativeID(r[@"source"]),decodeNativeID(r[@"target"]),uint32_t(integer(r[@"input"],1,63)),number(r[@"gainDB"]?:@0,-96,12),r[@"preFader"]?boolean(r[@"preFader"]):false});}
  for(id raw:array(g[@"outputs"]?:@[],128)){auto r=object(raw);keys(r,@[@"source",@"target",@"output"]);result.outputs.push_back({decodeNativeID(r[@"source"]),decodeNativeID(r[@"target"]),uint32_t(integer(r[@"output"],1,63))});}return result;
}
