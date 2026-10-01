import AppKit

extension InterfaceTests {
  static func graphConnectionPreviewChecks() throws {
    func port(_ node:String,_ output:Bool,_ mod:Bool=false,_ number:UInt32=0)->GraphBoundaryPort {.init(node:node,number:number,output:output,modulation:mod)}
    let nodes:[[String:Any]]=[["id":"in","kind":"input"],["id":"a","kind":"plugin"],["id":"b","kind":"plugin"],["id":"follow","kind":"follower"],["id":"lfo","kind":"lfo"],["id":"out","kind":"output"]]
    var definition:[String:Any]=["id":"g","nodes":nodes,"audio":[["source":"in","target":"a"],["source":"a","target":"b"],["source":"a","target":"follow"],["source":"b","target":"out"]],"modulation":[["source":"follow","target":"b","parameter":UInt32.max,"enabled":true]]]
    let preview=GraphConnectionPreview();preview.prepare(definition)
    let context=["song","g","r1"]
    for _ in 0..<1000 {
      try require(preview.reason(from:port("b",true),to:port("a",false),replacing:nil,context:context)?.contains("feedback cycle")==true,"Audio hover diagnoses the full recipe feedback path")
    }
    try require(preview.evaluations==1,"Repeated pointer positions reuse a bounded canonical-intent cache instead of revalidating or calling the API")
    try require(preview.reason(from:port("follow",true,true),to:port("a",false,true,11),replacing:nil,context:context)?.contains("feedback cycle")==true,"Control dependencies participate in the same feedback diagnosis as audio")
    try require(preview.reason(from:port("b",true),to:port("a",false,true,11),replacing:nil,context:context)?.contains("feedback cycle")==true,"An offered audio-to-parameter follower would add the source-to-target dependency and cannot hide a feedback loop")
    try require(preview.reason(from:port("in",true),to:port("b",false,true,UInt32.max),replacing:nil,context:context)==nil,"A new audio follower on a forward dependency is a valid preview; creation still goes through the explicit offer")
    try require(preview.reason(from:port("follow",true,true),to:port("a",false,true,11),replacing:4,context:context)?.contains("feedback cycle")==true,"Rewiring an enabled modulation cable diagnoses the replacement topology")
    definition["modulation"]=[["source":"follow","target":"b","parameter":UInt32.max,"enabled":false]];preview.prepare(definition)
    try require(preview.reason(from:port("follow",true,true),to:port("a",false,true,11),replacing:4,context:context)==nil,"A disabled modulation cable remains disabled on rewire, matching the shared compiler's dependency policy")
    try require(preview.reason(from:port("b",true),to:port("a",false),replacing:1,context:context)==nil,"Preview removes the original audio edge before assessing a replacement, so a safe reroute is not falsely called a cycle")
    try require(preview.reason(from:port("a",true),to:port("b",false),replacing:nil,context:context)?.contains("already connected")==true,"A duplicate audio cable explains its rejection before release")
    let count=preview.evaluations;_ = preview.reason(from:port("a",true),to:port("b",false),replacing:nil,context:["song","g","r2"])
    try require(preview.evaluations==count+1,"A document revision change retires cached hover verdicts")

    let editor=SignalGraphEditor(frame:.zero);editor.graphID="g";editor.update(["library":[definition]])
    editor.boundaryPorts[port("group",true)]=GraphRealPort(node:"b",number:0)
    var calls=0;editor.onRequest={_,_,_ in calls+=1}
    try require(editor.previewCable(port("group",true),port("a",false),replacing:nil)?.contains("feedback cycle")==true && calls==0,"Projected group ports resolve to actual recipe endpoints without a hover request")
    editor.graphID=nil
    try require(editor.previewCable(port("group",true),port("a",false),replacing:nil)==nil && calls==0,"Song mixer ownership and live-transition validation stay backend-authoritative")

    let host=NSWindow(contentRect:NSRect(x:0,y:0,width:900,height:500),styleMask:[.borderless],backing:.buffered,defer:false)
    let canvas=SignalCanvas(frame:NSRect(x:0,y:0,width:900,height:500));host.contentView=canvas
    let audio=SignalCanvasPort(label:"Audio"),control=SignalCanvasPort(label:"Control",modulation:true),parameter=SignalCanvasPort(number:UInt32.max,label:"Parameter",modulation:true,signal:.parameter)
    let a=SignalCanvasNode(id:"a",title:"A",detail:"",kind:"audio",x:40,y:80,inputs:[audio],outputs:[audio,control])
    let b=SignalCanvasNode(id:"b",title:"B",detail:"",kind:"audio",x:490,y:80,inputs:[audio,parameter],outputs:[audio])
    canvas.update([a,b],edges:[])
    var hint="",writes=0,adds=0,converters=0,identity="r1",blocked=false
    canvas.onCableHint={hint=$0};canvas.onConnectPorts={_,_,_,_,_ in writes+=1};canvas.onRewire={_,_,_,_,_,_ in writes+=1};canvas.onAddConnected={_,_,_,_ in adds+=1};canvas.onAudioParameterDrop={_,_,_,_,_ in converters+=1}
    canvas.cableOrigin={.init(context:[identity],port:$0)}
    canvas.validateCable={_,_,_ in blocked ? "This would create a feedback cycle":nil}
    func mouse(_ type:NSEvent.EventType,_ point:NSPoint)->NSEvent {NSEvent.mouseEvent(with:type,location:canvas.convert(point,to:nil),modifierFlags:[],timestamp:0,windowNumber:host.windowNumber,context:nil,eventNumber:1,clickCount:1,pressure:1)!}
    func drag(_ start:NSPoint,_ end:NSPoint){canvas.mouseDown(with:mouse(.leftMouseDown,start));canvas.mouseDragged(with:mouse(.leftMouseDragged,end));canvas.mouseUp(with:mouse(.leftMouseUp,end))}
    drag(a.portPoint(control,output:true),b.portPoint(audio,output:false))
    try require(hint.contains("Audio sockets accept audio") && writes==0 && adds==0 && converters==0,"An incompatible socket remains a visible rejection on release rather than becoming an Add or silent cancellation")
    blocked=true;drag(a.portPoint(audio,output:true),b.portPoint(audio,output:false))
    try require(hint.contains("feedback cycle") && writes==0 && adds==0,"A locally rejected cycle makes no partial edit or Add invocation")
    blocked=false;drag(a.portPoint(audio,output:true),b.portPoint(parameter,output:false))
    try require(converters==1 && writes==0,"Valid audio-to-parameter drags retain the explicit follower conversion callback")
    let start=a.portPoint(audio,output:true),end=b.portPoint(audio,output:false)
    canvas.mouseDown(with:mouse(.leftMouseDown,start));identity="r2";canvas.mouseDragged(with:mouse(.leftMouseDragged,end));canvas.mouseUp(with:mouse(.leftMouseUp,end))
    try require(hint.contains("socket layout changed") && writes==0,"A graph or alias change during drag cancels the captured intention without retargeting it")
    canvas.mouseDown(with:mouse(.leftMouseDown,start));identity="r3";canvas.mouseUp(with:mouse(.leftMouseUp,NSPoint(x:820,y:400)))
    try require(adds==0,"A stale gesture released on blank canvas cannot add a node at a retired source")
    canvas.update([a,b],edges:[SignalCanvasEdge(source:"a",target:"b",label:"")]);canvas.selectedEdge=0
    let original=canvas.edges.map{$0.source+"→"+$0.target};blocked=true
    if let handle=canvas.wireHandle(0,source:false){drag(handle,a.portPoint(audio,output:false))}
    try require(canvas.edges.map{$0.source+"→"+$0.target}==original && writes==0 && adds==0,"Rejected endpoint rewiring preserves the original connection")
    print("PASS cached recipe drag preview: mixed audio/control cycles, rewires, disabled controls, converter path, exact aliases, no hover requests, typed/stale rejection")
  }
}
