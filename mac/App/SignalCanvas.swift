import AppKit

enum SignalPortSignal { case audio, sidechain, control, parameter, events
  var color:NSColor {switch self {case .audio:return Theme.accent;case .sidechain:return NSColor(calibratedRed:0.37,green:0.63,blue:0.85,alpha:1);case .control,.parameter:return Theme.gold;case .events:return NSColor(calibratedRed:0.71,green:0.56,blue:0.9,alpha:1)}}
}
struct SignalCanvasPort: Equatable {
  var number: UInt32 = 0
  var label = ""
  var modulation = false
  var active = true
  var signal:SignalPortSignal = .audio
  var channels:Int? = nil
  var unavailable:String? = nil
  var signalType:SignalPortSignal {modulation ? (signal == .parameter ? .parameter:.control):signal}
}
struct SignalCanvasNode {
  var id: String, title: String, detail: String, kind: String
  var x: Double, y: Double
  var activity: String? = nil
  // Instance identity remains visible when semantic zoom hides live detail.
  var role: String? = nil
  var bypassed=false
  var inputs = [SignalCanvasPort()], outputs = [SignalCanvasPort()]
  var portRows: Int { max(inputs.count, outputs.count) }
  var rect: NSRect { NSRect(x:x,y:y,width:180,height:portRows > 0 ? 58+Double(portRows)*20 : 66) }
  func portPoint(_ port: SignalCanvasPort, output: Bool) -> NSPoint {
    let list = output ? outputs : inputs
    let row = list.firstIndex(where:{$0.number == port.number && $0.modulation == port.modulation}) ?? 0
    return NSPoint(x:output ? rect.maxX : rect.minX,y:y+61+Double(row)*20)
  }
}
struct SignalCanvasEdge {
  var source: String, target: String, label: String
  var modulation = false
  var output: UInt32 = 0, input: UInt32 = 0
  var enabled = true
  var amount:Double? = nil
  var amountRange:ClosedRange<Double> = -1...1
  var amountUnit=""
}
final class SignalCanvas: NSView {
  var emptyMessage="Create a subgraph to start connecting sound."
  var nodes = [SignalCanvasNode]()
  var signalReadings=GraphSignalReadings() {didSet{needsDisplay=true}}
  var onScope:((String?,Bool)->Void)?
  var onListen:((String)->Void)?,onStopListening:(()->Void)?,onChooseListen:(()->Void)?
  var onBypass:(()->Void)?
  var onGroup:((Bool)->Void)?
  var listeningPort:String? {didSet{if oldValue != listeningPort{needsDisplay=true}}}
  var observedEdgePort:((Int)->GraphPortReading?)?
  private var scopeHeld=false,hoverPoint:NSPoint?
  private let listenIcon=NSImage(systemSymbolName:"headphones",accessibilityDescription:"Listen here")?.withSymbolConfiguration(NSImage.SymbolConfiguration(paletteColors:[Theme.text]))
  private let activeListenIcon=NSImage(systemSymbolName:"headphones",accessibilityDescription:"Stop listening")?.withSymbolConfiguration(NSImage.SymbolConfiguration(paletteColors:[Theme.gold]))
  private var signalTracking:NSTrackingArea?
  var edges = [SignalCanvasEdge]() { didSet { rebuildGeometry() } }
  var selection=Set<String>()
  var selected: String? {didSet{if let selected {if !selection.contains(selected){selection=[selected]}}else{selection=[]};needsDisplay=true}}
  func selectNodes(_ ids:Set<String>,primary:String?=nil) {selection=ids;selected=primary ?? nodes.first{ids.contains($0.id)}?.id}
  var onMoveNodes: (([(String,Double,Double)])->Void)?
  var onInsertNodes: (([String],Int,[(String,Double,Double)])->Void)?
  var onDetachNodes: (([String],[(String,Double,Double)],Bool)->Void)?
  var onCutEdges: (([Int])->Void)?
  var cutTool=false {didSet{needsDisplay=true}}
  private var cutStroke=[NSPoint](),cutEdges=Set<Int>(),detachDragging=false
  var insertionHint: (([String],Int)->String?)?
  var addingMainInput=false
  var selectedEdge: Int? {didSet{needsDisplay=true}}
  var onSelect: ((String)->Void)?, onMove: ((String,Double,Double)->Void)?, onConnect: ((String,String)->Void)?
  var onConnectPorts: ((String,String,UInt32,UInt32,Bool)->Void)?
  var onSelectEdge: ((Int)->Void)?, onDelete: (()->Void)?, onZoom: ((Double)->Void)?
  var onOpen: ((String)->Void)?
  var onAddConnected:((String,SignalCanvasPort,Bool,NSPoint)->Void)?
  var onAdd: ((NSPoint)->Void)?, onParent:(()->Void)?, onFit:(()->Void)?, onFrame:(()->Void)?
  var onEditEdge: ((Int)->Void)?
  var onAmount:((Int,Double)->Void)?
  private var amountDrag:(index:Int,start:NSPoint,initial:Double,value:Double)?
  var onRewire: ((Int,String,String,UInt32,UInt32,Bool)->Void)?
  var onCableHint: ((String)->Void)?
  var describeCable: ((String,String,UInt32,UInt32,Bool)->String)?
  private struct Wire {var path:NSBezierPath;var samples:[NSPoint];var from:NSPoint;var to:NSPoint;var bounds:NSRect}
  private var wires=[Wire]()
  private var nudges=[(String,Double,Double)](),nudgeWork:DispatchWorkItem?
  var nudgeDelay=0.35
  var hasPendingNudge:Bool {!nudges.isEmpty}
  func commitNudge(){
    nudgeWork?.cancel();nudgeWork=nil
    let saved=nudges;nudges=[];guard !saved.isEmpty else{return}
    if let onMoveNodes{onMoveNodes(saved)}else{for(id,x,y) in saved{onMove?(id,x,y)}}
  }
  private var dragging: String?, origin = NSPoint.zero
  private var originals=[String:NSPoint]()
  private var marquee:NSRect?,marqueeStart:NSPoint?,marqueeBase=Set<String>()
  private var insertionEdge:Int?
  var isEditing:Bool {isWiring || dragging != nil || marqueeStart != nil || !cutStroke.isEmpty || amountDrag != nil}
  private struct Cable {var node:String;var port:SignalCanvasPort;var output:Bool;var edge:Int?}
  private var wiring:Cable?, pointer = NSPoint.zero
  var isWiring:Bool {wiring != nil}
  private var dropPort:(String,SignalCanvasPort)?
  // Screen-space targets overlap at overview zoom. Choose the nearest actual
  // socket instead of the first port; otherwise a Detector drop hits Main in.
  func socket(at point:NSPoint,output:Bool?=nil,modulation:Bool?=nil)->(node:String,port:SignalCanvasPort,output:Bool)? {
    let radius=12/max(0.3,enclosingScrollView?.magnification ?? 1)
    var best:(String,SignalCanvasPort,Bool)?,distance=radius
    for node in nodes.reversed() {for direction in [true,false] where output==nil || output==direction {
      for port in direction ? node.outputs:node.inputs where modulation==nil || modulation==port.modulation {
        let p=node.portPoint(port,output:direction),d=hypot(p.x-point.x,p.y-point.y)
        if d<distance {distance=d;best=(node.id,port,direction)}
      }
    }};return best
  }
  private func port(at point:NSPoint,output:Bool,modulation:Bool)->(String,SignalCanvasPort)? {
    socket(at:point,output:output,modulation:modulation).map{($0.node,$0.port)}
  }
  func wireHandle(_ index:Int,source:Bool)->NSPoint? {
    guard wires.indices.contains(index)else{return nil}
    let p=source ? wires[index].from:wires[index].to
    let scale=max(0.3,enclosingScrollView?.magnification ?? 1)
    // Stagger the handles vertically: adjacent cards can be less than 20 screen
    // points apart when zoomed out. Both handles must remain distinct from each
    // other and from the sockets themselves.
    return NSPoint(x:p.x+(source ? 12:-12)/scale,y:p.y+(source ? -14:14)/scale)
  }
  override var isFlipped: Bool {true}
  override var acceptsFirstResponder: Bool {true}
  override init(frame: NSRect) {
    super.init(frame:frame);wantsLayer=true;setAccessibilityElement(true);setAccessibilityRole(.group);setAccessibilityLabel("Audio and modulation graph")
    setAccessibilityHelp("Tab selects nodes; Option-Tab selects wires. Shift/Command-click or drag empty space to select several nodes. Arrows move the selection. Drop selected effects on a highlighted wire to insert them. Return opens controls; M toggles plugin bypass; L listens here; hold Q for scope. Delete removes the selected node or connection. Drag input or output sockets to connect. Sockets add connections; select a wire and drag its round handles to reroute. Plus/minus zoom. Escape cancels a wire.")
  }
  required init?(coder:NSCoder){fatalError()}
  override func updateTrackingAreas() {
    super.updateTrackingAreas();if let signalTracking{removeTrackingArea(signalTracking)}
    let area=NSTrackingArea(rect:.zero,options:[.mouseMoved,.activeInKeyWindow,.inVisibleRect],owner:self,userInfo:nil);addTrackingArea(area);signalTracking=area
  }
  override func mouseMoved(with event:NSEvent) {
    guard !isEditing else{return};let point=convert(event.locationInWindow,from:nil)
    hoverPoint=point
    if let socket=socket(at:point) {
      let port=socket.port.modulation ? nil:signalReadings.port(socket.node,output:socket.output,number:socket.port.number)
      toolTip=port?.summary(active:signalReadings.active) ?? (signalReadings.active ? "Measurement unavailable for this port":"Stopped")
    }else if let node=nodes.last(where:{$0.rect.contains(point)}) {
      let values=signalReadings.nodePorts(node.id)
      toolTip=values.isEmpty ? "Measurement unavailable for this node":values.map{$0.summary(active:signalReadings.active)}.joined(separator:"\n")
    }else if let index=edge(at:point),let port=observedEdgePort?(index){toolTip="Observed host port · "+port.summary(active:signalReadings.active)+" · hold Q for scope, ⇧Q for spectrum"}
    else{toolTip=nil}
  }
  func scopeTarget(at point:NSPoint?)->String? {
    if let point,let socket=socket(at:point),!socket.port.modulation{return signalReadings.port(socket.node,output:socket.output,number:socket.port.number)?.key}
    if let index=point.flatMap({edge(at:$0)}) ?? selectedEdge,edges.indices.contains(index),!edges[index].modulation {
      return observedEdgePort?(index)?.key
    }
    let node=point.flatMap{p in nodes.last{$0.rect.contains(p)}?.id} ?? selected
    return node.flatMap{signalReadings.primaryPort($0,output:true)?.key}
  }
  override func keyUp(with event:NSEvent) {if [123,124,125,126].contains(event.keyCode){commitNudge();return};if scopeHeld,event.charactersIgnoringModifiers?.lowercased()=="q"{scopeHeld=false;onScope?(nil,false)}else{super.keyUp(with:event)}}
  func cancelGesture() {
    amountDrag=nil
    for i in nodes.indices {if let p=originals[nodes[i].id]{nodes[i].x=p.x;nodes[i].y=p.y}}
    rebuildGeometry();wiring=nil;dropPort=nil;dragging=nil;insertionEdge=nil
    marquee=nil;marqueeStart=nil;originals=[:];selectedEdge=nil
    cutStroke=[];cutEdges=[];cutTool=false;detachDragging=false;needsDisplay=true
  }
  override func resignFirstResponder()->Bool {commitNudge();if scopeHeld{scopeHeld=false;onScope?(nil,false)};return super.resignFirstResponder()}
  func update(_ nodes:[SignalCanvasNode],edges:[SignalCanvasEdge]) {
    self.nodes=nodes;self.edges=edges
    if let selectedEdge,!edges.indices.contains(selectedEdge){self.selectedEdge=nil}
    selection.formIntersection(Set(nodes.map(\.id)))
    resizeCanvas()
    setAccessibilityValue(nodes.map{node in node.title+(node.role.map{" · "+$0} ?? "")+" → "+edges.filter{$0.source==node.id}.map{$0.label}.joined(separator:", ")}.joined(separator:"; "))
    needsDisplay=true
  }
  private func resizeCanvas(){frame.size=NSSize(width:max(900,(nodes.map{$0.rect.maxX}.max() ?? 0)+100),height:max(500,(nodes.map{$0.rect.maxY}.max() ?? 0)+100))}
  private func geometry(_ from:NSPoint,_ to:NSPoint)->Wire {
    let distance=max(50,abs(to.x-from.x)*0.5),a=NSPoint(x:from.x+distance,y:from.y),b=NSPoint(x:to.x-distance,y:to.y)
    let path=NSBezierPath();path.move(to:from);path.curve(to:to,controlPoint1:a,controlPoint2:b)
    let samples=(0...32).map{i -> NSPoint in let t=Double(i)/32,u=1-t;return NSPoint(x:u*u*u*from.x+3*u*u*t*a.x+3*u*t*t*b.x+t*t*t*to.x,y:u*u*u*from.y+3*u*u*t*a.y+3*u*t*t*b.y+t*t*t*to.y)}
    return Wire(path:path,samples:samples,from:from,to:to,bounds:path.bounds.insetBy(dx:-12,dy:-24))
  }
  private func rebuildGeometry(){
    let lookup=Dictionary(nodes.map{($0.id,$0)},uniquingKeysWith:{_,last in last})
    wires=edges.map{edge in guard let a=lookup[edge.source],let b=lookup[edge.target]else{return geometry(.zero,.zero)}
      return geometry(a.portPoint(SignalCanvasPort(number:edge.output,modulation:edge.modulation),output:true),b.portPoint(SignalCanvasPort(number:edge.input,modulation:edge.modulation),output:false))}
    needsDisplay=true
  }
  func edge(at point:NSPoint)->Int? {
    let tolerance=8/max(0.3,enclosingScrollView?.magnification ?? 1)
    var nearest:Int?,distance=tolerance
    for i in wires.indices.reversed() {
      guard wires[i].path.bounds.insetBy(dx:-tolerance,dy:-tolerance).contains(point) else{continue}
      let p=wires[i].samples
      for j in 1..<p.count{let a=p[j-1],b=p[j],dx=b.x-a.x,dy=b.y-a.y,t=max(0,min(1,((point.x-a.x)*dx+(point.y-a.y)*dy)/max(1e-9,dx*dx+dy*dy)))
        let candidate=hypot(point.x-a.x-t*dx,point.y-a.y-t*dy)
        if candidate<distance {nearest=i;distance=candidate}}
    }
    // Dense fan-in puts several curves inside one screen-space hit target.
    // Choose the closest curve, not the last connection in storage order.
    // Labels remain an alternate target, but never steal an actual wire hit.
    if let nearest{return nearest}
    for i in wires.indices.reversed() {
      let mid=wires[i].samples[16]
      if !edges[i].label.isEmpty && NSRect(x:mid.x-60,y:mid.y-18,width:140,height:15).contains(point) {return i}
    }
    return nil
  }
  func amountBadge(_ index:Int)->NSRect? {
    guard wires.indices.contains(index),edges[index].amount != nil else{return nil}
    let scale=max(0.3,enclosingScrollView?.magnification ?? 1)
    // Prefer the cable midpoint, then nearby points along it. The control must
    // remain usable when another card occupies the middle of a long send.
    func badge(_ sample:Int,_ offset:CGFloat)->NSRect {
      let point=wires[index].samples[sample]
      return NSRect(x:max(0,min(bounds.width-86/scale,point.x-43/scale)),
                    y:max(0,min(bounds.height-21/scale,point.y+offset/scale)),width:86/scale,height:21/scale)
    }
    for offset:CGFloat in [-28,8,-54,34,-80,60] {
      for sample in [16,12,20,8,24,4,28] {
        let rect=badge(sample,offset)
        if !nodes.contains(where:{$0.rect.insetBy(dx:-6/scale,dy:-6/scale).intersects(rect)}){return rect}
      }
    }
    return badge(16,-28)
  }
  private func amountLabel(_ index:Int)->String {
    guard let value=amountDrag?.index==index ? amountDrag?.value:edges[index].amount else{return ""}
    return edges[index].amountUnit=="dB" && value<=(-96) ? "−∞ dB":String(format:"%.3g",value)+" "+edges[index].amountUnit
  }
  private func label(_ text:String,_ rect:NSRect,_ color:NSColor,_ size:CGFloat=12,_ weight:NSFont.Weight = .regular){
    let paragraph=NSMutableParagraphStyle();paragraph.lineBreakMode = .byTruncatingTail
    (text as NSString).draw(in:rect,withAttributes:[.font:NSFont.systemFont(ofSize:size,weight:weight),.foregroundColor:color,.paragraphStyle:paragraph])
  }
  private func paint(_ wire:Wire,_ signal:SignalPortSignal,emphasized:Bool=true,enabled:Bool=true,selected:Bool=false){
    let color=signal.color.withAlphaComponent(!enabled ? 0.2 : emphasized ? 0.9 : 0.5)
    wire.path.setLineDash([],count:0,phase:0)
    if selected {Theme.text.withAlphaComponent(0.3).setStroke();wire.path.lineWidth=5;wire.path.stroke()}
    color.setStroke();wire.path.lineWidth=(emphasized ? 2 : 1.3)/max(0.6,enclosingScrollView?.magnification ?? 1)
    if signal == .control || signal == .parameter {wire.path.setLineDash([5,4],count:2,phase:0)}
    else if signal == .events {wire.path.setLineDash([1,4],count:2,phase:0)}
    wire.path.stroke()
    let arrow=NSBezierPath();arrow.move(to:wire.to);arrow.line(to:NSPoint(x:wire.to.x-8,y:wire.to.y-4));arrow.line(to:NSPoint(x:wire.to.x-8,y:wire.to.y+4));arrow.close();color.setFill();arrow.fill()
  }
  func edgeSignal(_ edge:SignalCanvasEdge)->SignalPortSignal {
    if edge.modulation{return .control}
    return nodes.first{$0.id==edge.target}?.inputs.first{$0.number==edge.input && !$0.modulation}?.signalType ?? .audio
  }
  override func draw(_ dirty:NSRect){
    Theme.bg.setFill();dirty.fill();Theme.border.withAlphaComponent(0.25).setFill()
    let grid=NSBezierPath()
    for x in stride(from:max(0,Int(dirty.minX)/24*24),to:Int(dirty.maxX),by:24){for y in stride(from:max(0,Int(dirty.minY)/24*24),to:Int(dirty.maxY),by:24){grid.appendRect(NSRect(x:x,y:y,width:1,height:1))}}
    grid.fill()
    for (i,edge) in edges.enumerated() where wires.indices.contains(i) && wires[i].bounds.intersects(dirty) {
      let wire=wires[i];if wiring?.edge==i {continue};paint(wire,edgeSignal(edge),emphasized:selection.isEmpty || selection.contains(edge.source) || selection.contains(edge.target),enabled:edge.enabled,selected:selectedEdge==i || insertionEdge==i)
      if selectedEdge != i || edge.amount==nil, !edge.label.isEmpty{let p=wire.samples[16];label(edge.label,NSRect(x:p.x-60,y:p.y-18,width:140,height:15),edge.modulation ? Theme.gold : Theme.muted,10)}
    }
    if let selectedEdge,wiring==nil {for source in [true,false] {if let p=wireHandle(selectedEdge,source:source) {let scale=max(0.3,enclosingScrollView?.magnification ?? 1);let socket=source ? wires[selectedEdge].from:wires[selectedEdge].to;let stem=NSBezierPath();stem.move(to:socket);stem.line(to:p);stem.lineWidth=1/scale;Theme.gold.setStroke();stem.stroke();let radius=5/scale;let handle=NSBezierPath(ovalIn:NSRect(x:p.x-radius,y:p.y-radius,width:radius*2,height:radius*2));Theme.bg.setFill();handle.fill();Theme.gold.setStroke();handle.lineWidth=2;handle.stroke()}}}
    if let cable=wiring,let node=nodes.first(where:{$0.id==cable.node}) {
      let fixed=node.portPoint(cable.port,output:cable.output)
      paint(cable.output ? geometry(fixed,pointer):geometry(pointer,fixed),cable.port.signalType)
      if let (id,port)=dropPort,let target=nodes.first(where:{$0.id==id}) {let p=target.portPoint(port,output:!cable.output);Theme.gold.setStroke();let ring=NSBezierPath(ovalIn:NSRect(x:p.x-8,y:p.y-8,width:16,height:16));ring.lineWidth=2;ring.stroke()}
    }
    for node in nodes where node.rect.insetBy(dx:-12,dy:-4).intersects(dirty) {
      let path=NSBezierPath(roundedRect:node.rect,xRadius:7,yRadius:7);Theme.raised.setFill();path.fill();(selection.contains(node.id) ? Theme.accent : Theme.border).setStroke();path.lineWidth=selection.contains(node.id) ? 2 : 1;path.stroke()
      let scale=max(0.3,enclosingScrollView?.magnification ?? 1),overview=scale<0.65
      let outputs=signalReadings.nodePorts(node.id,output:true)
      label(node.title,NSRect(x:node.x+10,y:node.y+9,width:outputs.isEmpty ? 160:142,height:overview && node.role==nil ? 42:23),Theme.text,max(13,9/scale),.semibold)
      if !outputs.isEmpty {
        let isListening=outputs.contains{$0.key==listeningPort}
        let badge=listenBadge(node)
        (isListening ? Theme.gold.withAlphaComponent(0.25):Theme.bg).setFill();NSBezierPath(roundedRect:badge,xRadius:3,yRadius:3).fill()
        (isListening ? activeListenIcon:listenIcon)?.draw(in:badge.insetBy(dx:3,dy:3),from:.zero,operation:.sourceOver,fraction:1,respectFlipped:true,hints:nil)
      }
      if node.bypassed {
        let pass=NSBezierPath();pass.move(to:NSPoint(x:node.rect.minX+6,y:node.rect.maxY-6));pass.line(to:NSPoint(x:node.rect.maxX-6,y:node.rect.maxY-6));pass.lineWidth=2;pass.setLineDash([4,3],count:2,phase:0);Theme.gold.withAlphaComponent(0.7).setStroke();pass.stroke()
      }
      if !overview {label(node.detail,NSRect(x:node.x+13,y:node.y+35,width:154,height:16),Theme.muted,10)}
      else if let role=node.role {label(role,NSRect(x:node.x+10,y:node.y+35,width:160,height:24),Theme.muted,max(10,8/scale))}
      if !overview {
        for output in [false,true] {
          guard let meter=signalReadings.primaryPort(node.id,output:output),meter.measured else{continue}
          let rect=NSRect(x:node.x+(output ? 92:10),y:node.y+53,width:76,height:3)
          Theme.border.setFill();rect.fill()
          if signalReadings.active {
            let level=max(0,min(1,(20*log10(max(1e-6,meter.peak))+60)/60))
            (meter.clipped || meter.invalid ? NSColor.systemRed:Theme.accent).setFill();NSRect(x:rect.minX,y:rect.minY,width:rect.width*level,height:rect.height).fill()
          }
          if meter.clipped || meter.invalid {NSColor.systemRed.setFill();NSRect(x:rect.maxX-3,y:rect.minY-1,width:3,height:5).fill()}
        }
      }
      if let activity=node.activity{(activity=="tail" ? Theme.gold : Theme.accent).setFill();NSBezierPath(ovalIn:NSRect(x:node.rect.maxX-12,y:node.y+5,width:6,height:6)).fill()}
      for output in [false,true]{for port in output ? node.outputs : node.inputs{let p=node.portPoint(port,output:output);let signal=port.signalType,color=signal.color.withAlphaComponent(port.unavailable==nil ? 1:0.35),r:CGFloat=4/max(0.6,scale)
        let socket:NSBezierPath
        if signal == .control || signal == .parameter {socket=NSBezierPath();socket.move(to:NSPoint(x:p.x,y:p.y-r));socket.line(to:NSPoint(x:p.x+r,y:p.y));socket.line(to:NSPoint(x:p.x,y:p.y+r));socket.line(to:NSPoint(x:p.x-r,y:p.y));socket.close()}
        else if signal == .events {socket=NSBezierPath(rect:NSRect(x:p.x-r,y:p.y-r,width:r*2,height:r*2))}
        else {socket=NSBezierPath(ovalIn:NSRect(x:p.x-r,y:p.y-r,width:r*2,height:r*2))}
        color.setStroke();socket.lineWidth=1.5;socket.stroke();if port.active && signal != .parameter {color.setFill();socket.fill()}
        if !overview || signal == .sidechain {label(overview ? "SC" : (port.label.isEmpty ? (output ? "Main out":"Main in") : port.label),NSRect(x:output ? p.x-86:p.x+9,y:p.y-6,width:77,height:18),signal == .audio ? Theme.muted:color,max(9,overview ? 8/scale:9))}}}
    }
    // Selected controls are above cards, including the crowded-layout fallback.
    if let i=selectedEdge,edges.indices.contains(i),let badge=amountBadge(i),badge.intersects(dirty) {
      Theme.raised.setFill();NSBezierPath(roundedRect:badge,xRadius:4,yRadius:4).fill();edgeSignal(edges[i]).color.setStroke();NSBezierPath(roundedRect:badge,xRadius:4,yRadius:4).stroke()
      label(amountLabel(i),badge.insetBy(dx:5,dy:2),Theme.text,11/max(0.3,enclosingScrollView?.magnification ?? 1))
    }
    if let marquee {Theme.accent.withAlphaComponent(0.08).setFill();marquee.fill();Theme.accent.setStroke();NSBezierPath(rect:marquee).stroke()}
    if let first=cutStroke.first {let path=NSBezierPath();path.move(to:first);for p in cutStroke.dropFirst(){path.line(to:p)};NSColor.systemRed.setStroke();path.lineWidth=2/max(0.3,enclosingScrollView?.magnification ?? 1);path.stroke()}
    if nodes.isEmpty{label(emptyMessage,NSRect(x:24,y:30,width:500,height:30),Theme.muted,15)}
  }
  func selectForContext(_ event:NSEvent) {
    let point=convert(event.locationInWindow,from:nil)
    if let node=nodes.reversed().first(where:{$0.rect.contains(point)}) {if !selection.contains(node.id){selectNodes([node.id])};selectedEdge=nil;onSelect?(node.id)}
    else {selected=nil;selectedEdge=edge(at:point);if let selectedEdge{onSelectEdge?(selectedEdge)}}
  }
  private func listenBadge(_ node:SignalCanvasNode)->NSRect {NSRect(x:node.rect.maxX-24,y:node.y+7,width:18,height:18)}
  override func mouseDown(with event:NSEvent){
    commitNudge()
    window?.makeFirstResponder(self);let point=convert(event.locationInWindow,from:nil);pointer=point
    if cutTool {cutStroke=[point];cutEdges=[];return}
    if let node=nodes.reversed().first(where:{$0.rect.contains(point)}),!signalReadings.nodePorts(node.id,output:true).isEmpty,
       listenBadge(node).contains(point) || event.modifierFlags.intersection([.control,.shift,.command,.option]) == [.control,.shift] {
      selectNodes([node.id]);selectedEdge=nil;onSelect?(node.id);if let port=signalReadings.primaryPort(node.id,output:true){onListen?(port.key)}else{onChooseListen?()};return
    }
    if let index=selectedEdge,edges.indices.contains(index) {
      if let badge=amountBadge(index),badge.contains(point),let value=edges[index].amount {
        if event.clickCount>=2{onEditEdge?(index);return}
        amountDrag=(index,point,value,value);onCableHint?("Drag amount · Shift for precision · double-click for exact entry · Esc cancels");return
      }
      for source in [true,false] {if let handle=wireHandle(index,source:source),hypot(handle.x-point.x,handle.y-point.y)<8/max(0.3,enclosingScrollView?.magnification ?? 1) {
        let edge=edges[index];wiring=Cable(node:source ? edge.target:edge.source,port:SignalCanvasPort(number:source ? edge.input:edge.output,modulation:edge.modulation),output:!source,edge:index)
        onCableHint?("Drag to a matching socket · Esc or empty space cancels");return
      }}
    }
    if let hit=socket(at:point) {
      let id=hit.node,port=hit.port,output=hit.output
      if let reason=port.unavailable {onCableHint?(reason);return}
      addingMainInput=event.modifierFlags.contains(.option)
      selected=id;selectedEdge=nil;onSelect?(id)
      wiring=Cable(node:id,port:port,output:output,edge:nil);onCableHint?("Drag to add a connection · Option-drag an effect’s Main in to sum another channel · Esc cancels");return
    }

    if let node=nodes.reversed().first(where:{$0.rect.contains(point)}) {
      if !event.modifierFlags.intersection([.shift,.command]).isEmpty {
        var next=selection;if next.contains(node.id){next.remove(node.id)}else{next.insert(node.id)};selectNodes(next,primary:next.contains(node.id) ? node.id:nil)
      }else if !selection.contains(node.id){selectNodes([node.id])}
      selectedEdge=nil;if let selected{onSelect?(selected)}
      if event.clickCount>=2{dragging=nil;onOpen?(node.id);return}
      guard selection.contains(node.id)else{return};dragging=node.id;origin=point;detachDragging=event.modifierFlags.contains(.option)
      originals=Dictionary(uniqueKeysWithValues:nodes.filter{selection.contains($0.id)}.map{($0.id,NSPoint(x:$0.x,y:$0.y))});return
    }
    let hit=edge(at:point)
    if let hit {selected=nil;selectedEdge=hit;onSelectEdge?(hit);if event.clickCount>=2{onEditEdge?(hit)}}
    else {marqueeStart=point;marqueeBase=event.modifierFlags.intersection([.shift,.command]).isEmpty ? []:selection;selectNodes(marqueeBase);selectedEdge=nil}
  }
  private func positions()->[(String,Double,Double)] {nodes.filter{selection.contains($0.id)}.map{($0.id,$0.x,$0.y)}}
  private func notifyMove(){let p=positions();if let onMoveNodes{onMoveNodes(p)}else{for(id,x,y)in p{onMove?(id,x,y)}}}
  override func mouseDragged(with event:NSEvent){
    pointer=convert(event.locationInWindow,from:nil)
    if let drag=amountDrag,edges.indices.contains(drag.index) {
      let scale=max(0.3,enclosingScrollView?.magnification ?? 1),fine=event.modifierFlags.contains(.shift) ? 0.1:1.0
      let delta=(pointer.x-drag.start.x+drag.start.y-pointer.y)*scale*fine
      let edge=edges[drag.index],step=edge.amountUnit=="dB" ? 0.5:0.005
      amountDrag?.value=max(edge.amountRange.lowerBound,min(edge.amountRange.upperBound,drag.initial+delta*step))
      onCableHint?(amountLabel(drag.index)+" · release commits · Esc cancels");needsDisplay=true;return
    }
    if let previous=cutStroke.last {
      cutEdges.formUnion(crossedEdges(from:previous,to:pointer));if cutStroke.count<4096{cutStroke.append(pointer)}else{cutStroke[cutStroke.count-1]=pointer}
      onCableHint?("Cut \(cutEdges.count) crossed connection(s) · release commits · Esc cancels");needsDisplay=true;return
    }
    if let cable=wiring {
      dropPort=port(at:pointer,output:!cable.output,modulation:cable.port.modulation)
      if let (id,port)=dropPort {
        let a=cable.output ? cable.node:id,b=cable.output ? id:cable.node,out=cable.output ? cable.port.number:port.number,input=cable.output ? port.number:cable.port.number
        onCableHint?(port.unavailable ?? describeCable?(a,b,out,input,cable.port.modulation) ?? "Release to connect")
      }
    }
    if let start=marqueeStart {
      let rect=NSRect(x:min(start.x,pointer.x),y:min(start.y,pointer.y),width:abs(pointer.x-start.x),height:abs(pointer.y-start.y))
      marquee=rect;selectNodes(marqueeBase.union(nodes.filter{$0.rect.intersects(rect)}.map(\.id)))
    }
    if dragging != nil {
      let dx=max(8-(originals.values.map(\.x).min() ?? 8),min(99900-(originals.values.map(\.x).max() ?? 0),pointer.x-origin.x))
      let dy=max(8-(originals.values.map(\.y).min() ?? 8),min(99900-(originals.values.map(\.y).max() ?? 0),pointer.y-origin.y))
      for i in nodes.indices{if let p=originals[nodes[i].id]{nodes[i].x=p.x+dx;nodes[i].y=p.y+dy}};rebuildGeometry()
      insertionEdge=nil
      let center=nodes.first{$0.id==dragging}.map{NSPoint(x:$0.rect.midX,y:$0.rect.midY)} ?? pointer
      if detachDragging {onCableHint?("Pull out and reconnect the old main path · release commits · Esc cancels")}
      else if let e=edge(at:pointer) ?? edge(at:center),!edges[e].modulation,!selection.contains(edges[e].source),!selection.contains(edges[e].target),let hint=insertionHint?(nodes.filter{selection.contains($0.id)}.map(\.id),e) {insertionEdge=e;onCableHint?(hint)}
      else{onCableHint?("Move \(selection.count) node(s) · drop an effect chain on a highlighted audio wire to insert · Esc cancels")}
    }
    needsDisplay=true
  }
  override func mouseUp(with event:NSEvent){
    let point=convert(event.locationInWindow,from:nil)
    if let drag=amountDrag {amountDrag=nil;if drag.value != drag.initial{onAmount?(drag.index,drag.value)};needsDisplay=true;return}
    if let previous=cutStroke.last {
      cutEdges.formUnion(crossedEdges(from:previous,to:point));let indices=cutEdges.sorted()
      cutStroke=[];cutEdges=[];cutTool=false;needsDisplay=true
      if !indices.isEmpty{onCutEdges?(indices)};return
    }
    if let cable=wiring,let (id,port)=port(at:point,output:!cable.output,modulation:cable.port.modulation),id != cable.node {
      let a=cable.output ? cable.node:id,b=cable.output ? id:cable.node,out=cable.output ? cable.port.number:port.number,input=cable.output ? port.number:cable.port.number
      if let reason=port.unavailable {onCableHint?(reason)}
      else if let edge=cable.edge {onRewire?(edge,a,b,out,input,cable.port.modulation)}
      else if let onConnectPorts {onConnectPorts(a,b,out,input,cable.port.modulation)}else{onConnect?(a,b)}
    }else if wiring != nil{onCableHint?("Connection unchanged · drag cancelled")}
    let addCable=wiring.flatMap{c -> Cable? in
      guard c.edge==nil,!nodes.contains(where:{$0.rect.insetBy(dx:-14,dy:-14).contains(point)}) else{return nil};return c
    }

    let moved=dragging != nil && nodes.contains{n in guard let p=originals[n.id]else{return false};return n.x != p.x || n.y != p.y}
    let insertion=insertionEdge,positions=positions(),ids=nodes.filter{selection.contains($0.id)}.map(\.id),detach=detachDragging
    wiring=nil;dropPort=nil;dragging=nil;insertionEdge=nil;marquee=nil;marqueeStart=nil;originals=[:]
    detachDragging=false
    if moved {if detach {onDetachNodes?(ids,positions,false)}else if let insertion,let onInsertNodes{onInsertNodes(ids,insertion,positions)}else{notifyMove()}}
    if let selected{onSelect?(selected)};resizeCanvas();needsDisplay=true
    if let cable=addCable {onAddConnected?(cable.node,cable.port,cable.output,point)}
  }
  override func keyDown(with event:NSEvent){
    if ![123,124,125,126].contains(event.keyCode){commitNudge()}
    if !isEditing {
      let flags=event.modifierFlags.intersection([.command,.control,.option,.shift])
      if (flags == .control || flags == [.control,.option]),event.charactersIgnoringModifiers?.lowercased()=="g",(flags == .control ? GraphCommand.groupSelection:GraphCommand.ungroup).usesCanvasDefault(){onGroup?(flags.contains(.option));return}
      if (flags.isEmpty || flags == .shift),event.charactersIgnoringModifiers?.lowercased()=="q",(flags == .shift ? GraphCommand.spectrum:GraphCommand.scope).usesCanvasDefault() {
        if !event.isARepeat {if let port=scopeTarget(at:hoverPoint){scopeHeld=true;onScope?(port,flags == .shift)}else{onCableHint?("Measurement unavailable here · select a host audio port")}};return
      }
      if flags == .shift,event.charactersIgnoringModifiers?.lowercased()=="a",GraphCommand.add.usesCanvasDefault() {onAdd?(pointer == .zero ? NSPoint(x:visibleRect.minX+40,y:visibleRect.minY+40) : pointer);return}
      if flags == .option,event.keyCode==126,GraphCommand.parent.usesCanvasDefault() {onParent?();return}
      if flags.isEmpty,event.keyCode==115,GraphCommand.fit.usesCanvasDefault() {onFit?();return}
      if flags.isEmpty,event.charactersIgnoringModifiers?.lowercased()=="f",GraphCommand.frameSelection.usesCanvasDefault() {onFrame?();return}
      if flags.isEmpty,event.charactersIgnoringModifiers?.lowercased()=="m",GraphCommand.bypass.usesCanvasDefault() {onBypass?();return}
      if flags.isEmpty,event.charactersIgnoringModifiers?.lowercased()=="l",GraphCommand.listen.usesCanvasDefault() {
        if let selected,let port=signalReadings.primaryPort(selected,output:true){onListen?(port.key)}else{onChooseListen?()};return
      }
      if event.keyCode==53,!isEditing,listeningPort != nil {onStopListening?();return}
      if flags == .control,event.charactersIgnoringModifiers=="x",GraphCommand.deleteHeal.usesCanvasDefault() {onDetachNodes?(nodes.filter{selection.contains($0.id)}.map(\.id),[],true);return}
    }
    if event.keyCode==53 {cancelGesture();onCableHint?("Drag cancelled · song unchanged");return}
    if event.modifierFlags.contains(.command),event.charactersIgnoringModifiers=="a" {selectNodes(Set(nodes.map(\.id)));selectedEdge=nil;if let selected{onSelect?(selected)};return}
    if isEditing {return} // Only Escape may alter an unfinished cable gesture.
    if event.keyCode==51 || event.keyCode==117{onDelete?();return}
    let flags=event.modifierFlags.intersection([.command,.control,.option,.shift])
    if (flags.isEmpty || flags == .shift),(event.characters=="+" || event.characters=="="),GraphCommand.zoomIn.usesCanvasDefault(){onZoom?(1.2);return}
    if flags.isEmpty,event.characters=="-",GraphCommand.zoomOut.usesCanvasDefault(){onZoom?(1/1.2);return}
    if event.keyCode==48{
      if event.modifierFlags.contains(.option){guard !edges.isEmpty else{return};let current=selectedEdge ?? (event.modifierFlags.contains(.shift) ? 0 : -1);selectedEdge=(current+(event.modifierFlags.contains(.shift) ? edges.count-1 : 1)+edges.count)%edges.count;selected=nil;onSelectEdge?(selectedEdge!);scrollToVisible(wires[selectedEdge!].bounds);return}
      let reverse=event.modifierFlags.contains(.shift)
      let next=(nodes.firstIndex(where:{$0.id==selected}) ?? (reverse ? nodes.count:-1))+(reverse ? -1:1)
      guard nodes.indices.contains(next)else{if reverse{window?.selectPreviousKeyView(self)}else{window?.selectNextKeyView(self)};return}
      selected=nodes[next].id;selectedEdge=nil;onSelect?(nodes[next].id);scrollToVisible(nodes[next].rect.insetBy(dx:-20,dy:-20));return
    }
    if event.keyCode==36,let selectedEdge {onEditEdge?(selectedEdge);return}
    guard let selected,nodes.contains(where:{$0.id==selected})else{super.keyDown(with:event);return}
    if event.keyCode==36{onOpen?(selected);return}
    guard flags.isEmpty || flags == .shift else{super.keyDown(with:event);return}
    let step=flags == .shift ? 24.0 : 4.0
    let dx:Double,dy:Double
    switch event.keyCode{case 123:dx = -step;dy=0;case 124:dx=step;dy=0;case 125:dx=0;dy=step;case 126:dx=0;dy = -step;default:super.keyDown(with:event);return}
    let chosen=nodes.filter{selection.contains($0.id)},x=max(8-(chosen.map(\.x).min() ?? 8),dx),y=max(8-(chosen.map(\.y).min() ?? 8),dy)
    for i in nodes.indices where selection.contains(nodes[i].id){nodes[i].x+=x;nodes[i].y+=y}
    rebuildGeometry();resizeCanvas();nudges=positions();nudgeWork?.cancel()
    let work=DispatchWorkItem{[weak self] in self?.commitNudge()};nudgeWork=work
    DispatchQueue.main.asyncAfter(deadline:.now()+nudgeDelay,execute:work)
  }
  func crossedEdges(from a:NSPoint,to b:NSPoint)->[Int] {
    func crossing(_ c:NSPoint,_ d:NSPoint)->Bool {
      let x=b.x-a.x,y=b.y-a.y,u=d.x-c.x,v=d.y-c.y,den=x*v-y*u
      guard abs(den)>1e-9 else{return false}
      let t=((c.x-a.x)*v-(c.y-a.y)*u)/den,s=((c.x-a.x)*y-(c.y-a.y)*x)/den
      return (0...1).contains(t) && (0...1).contains(s)
    }
    return wires.indices.filter{i in (1..<wires[i].samples.count).contains{crossing(wires[i].samples[$0-1],wires[i].samples[$0])}}
  }
  override func rightMouseDown(with event:NSEvent) {
    guard event.modifierFlags.contains(.control) else{super.rightMouseDown(with:event);return}
    cutTool=true;mouseDown(with:event)
  }
  override func rightMouseDragged(with event:NSEvent) {if !cutStroke.isEmpty {mouseDragged(with:event)}else{super.rightMouseDragged(with:event)}}
  override func rightMouseUp(with event:NSEvent) {if !cutStroke.isEmpty {mouseUp(with:event)}else{super.rightMouseUp(with:event)}}
}
