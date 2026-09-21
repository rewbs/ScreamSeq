import AppKit

struct SignalCanvasPort: Equatable {
  var number: UInt32 = 0
  var label = ""
  var modulation = false
}
struct SignalCanvasNode {
  var id: String, title: String, detail: String, kind: String
  var x: Double, y: Double
  var activity: String? = nil
  var inputs = [SignalCanvasPort()], outputs = [SignalCanvasPort()]
  var portRows: Int { max(inputs.count, outputs.count) }
  var rect: NSRect { NSRect(x:x,y:y,width:180,height:portRows > 1 ? 54+Double(portRows)*18 : 66) }
  func portPoint(_ port: SignalCanvasPort, output: Bool) -> NSPoint {
    let list = output ? outputs : inputs
    let row = list.firstIndex(where:{$0.number == port.number && $0.modulation == port.modulation}) ?? 0
    return NSPoint(x:output ? rect.maxX : rect.minX,y:portRows > 1 ? y+61+Double(row)*18 : rect.midY)
  }
}
struct SignalCanvasEdge {
  var source: String, target: String, label: String
  var modulation = false
  var output: UInt32 = 0, input: UInt32 = 0
  var enabled = true
}
final class SignalCanvas: NSView {
  var emptyMessage="Create a subgraph to start connecting sound."
  var nodes = [SignalCanvasNode]()
  var edges = [SignalCanvasEdge]() { didSet { rebuildGeometry() } }
  var selected: String? {didSet{needsDisplay=true}}
  var selectedEdge: Int? {didSet{needsDisplay=true}}
  var onSelect: ((String)->Void)?, onMove: ((String,Double,Double)->Void)?, onConnect: ((String,String)->Void)?
  var onConnectPorts: ((String,String,UInt32,UInt32,Bool)->Void)?
  var onSelectEdge: ((Int)->Void)?, onDelete: (()->Void)?, onZoom: ((Double)->Void)?
  var onOpen: ((String)->Void)?
  private struct Wire {var path:NSBezierPath;var samples:[NSPoint];var from:NSPoint;var to:NSPoint;var bounds:NSRect}
  private var wires=[Wire]()
  private var dragging: String?, origin = NSPoint.zero, original = NSPoint.zero
  private var wiring: (String,SignalCanvasPort)?, pointer = NSPoint.zero
  override var isFlipped: Bool {true}
  override var acceptsFirstResponder: Bool {true}
  override init(frame: NSRect) {
    super.init(frame:frame);setAccessibilityElement(true);setAccessibilityRole(.group);setAccessibilityLabel("Audio and modulation graph")
    setAccessibilityHelp("Tab selects nodes; Option-Tab selects wires. Arrows move a node. Return opens controls. Delete removes the selected node or connection. Drag matching ports to connect. Plus/minus zoom. Escape cancels a wire.")
  }
  required init?(coder:NSCoder){fatalError()}
  func update(_ nodes:[SignalCanvasNode],edges:[SignalCanvasEdge]) {
    self.nodes=nodes;self.edges=edges
    if let selectedEdge,!edges.indices.contains(selectedEdge){self.selectedEdge=nil}
    resizeCanvas()
    setAccessibilityValue(nodes.map{node in node.title+" → "+edges.filter{$0.source==node.id}.map{$0.label}.joined(separator:", ")}.joined(separator:"; "))
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
    for i in wires.indices.reversed() where wires[i].bounds.contains(point) {
      let p=wires[i].samples
      for j in 1..<p.count{let a=p[j-1],b=p[j],dx=b.x-a.x,dy=b.y-a.y,t=max(0,min(1,((point.x-a.x)*dx+(point.y-a.y)*dy)/max(1e-9,dx*dx+dy*dy)))
        if hypot(point.x-a.x-t*dx,point.y-a.y-t*dy)<7{return i}}
    }
    return nil
  }
  private func label(_ text:String,_ rect:NSRect,_ color:NSColor,_ size:CGFloat=12,_ weight:NSFont.Weight = .regular){
    let paragraph=NSMutableParagraphStyle();paragraph.lineBreakMode = .byTruncatingTail
    (text as NSString).draw(in:rect,withAttributes:[.font:NSFont.systemFont(ofSize:size,weight:weight),.foregroundColor:color,.paragraphStyle:paragraph])
  }
  private func paint(_ wire:Wire,_ mod:Bool,emphasized:Bool=true,enabled:Bool=true,selected:Bool=false){
    let color=(selected ? Theme.gold : mod ? Theme.gold : Theme.accent).withAlphaComponent(!enabled ? 0.16 : emphasized ? 0.9 : 0.22)
    color.setStroke();wire.path.lineWidth=selected ? 3.5 : emphasized ? 2 : 1
    if mod{wire.path.setLineDash([5,4],count:2,phase:0)}else{wire.path.setLineDash([],count:0,phase:0)};wire.path.stroke()
    let arrow=NSBezierPath();arrow.move(to:wire.to);arrow.line(to:NSPoint(x:wire.to.x-8,y:wire.to.y-4));arrow.line(to:NSPoint(x:wire.to.x-8,y:wire.to.y+4));arrow.close();color.setFill();arrow.fill()
  }
  override func draw(_ dirty:NSRect){
    Theme.bg.setFill();dirty.fill();Theme.border.withAlphaComponent(0.25).setFill()
    for x in stride(from:max(0,Int(dirty.minX)/24*24),to:Int(dirty.maxX),by:24){for y in stride(from:max(0,Int(dirty.minY)/24*24),to:Int(dirty.maxY),by:24){NSRect(x:x,y:y,width:1,height:1).fill()}}
    for (i,edge) in edges.enumerated() where wires.indices.contains(i) && wires[i].bounds.intersects(dirty) {
      let wire=wires[i];paint(wire,edge.modulation,emphasized:selected==nil || selected==edge.source || selected==edge.target,enabled:edge.enabled,selected:selectedEdge==i)
      if !edge.label.isEmpty{let p=wire.samples[16];label(edge.label,NSRect(x:p.x-60,y:p.y-18,width:140,height:15),edge.modulation ? Theme.gold : Theme.muted,10)}
    }
    if let (id,port)=wiring,let node=nodes.first(where:{$0.id==id}){paint(geometry(node.portPoint(port,output:true),pointer),port.modulation)}
    for node in nodes where node.rect.insetBy(dx:-12,dy:-4).intersects(dirty) {
      let path=NSBezierPath(roundedRect:node.rect,xRadius:7,yRadius:7);Theme.raised.setFill();path.fill();(selected==node.id ? Theme.accent : Theme.border).setStroke();path.lineWidth=selected==node.id ? 2 : 1;path.stroke()
      label(node.title,NSRect(x:node.x+13,y:node.y+12,width:154,height:20),Theme.text,13,.semibold)
      label(node.detail,NSRect(x:node.x+13,y:node.y+35,width:154,height:16),Theme.muted,10)
      if let activity=node.activity{(activity=="tail" ? Theme.gold : Theme.accent).setFill();NSBezierPath(ovalIn:NSRect(x:node.rect.maxX-12,y:node.y+5,width:6,height:6)).fill()}
      for output in [false,true]{for port in output ? node.outputs : node.inputs{let p=node.portPoint(port,output:output);(port.modulation ? Theme.gold : Theme.accent).setFill();NSBezierPath(ovalIn:NSRect(x:p.x-4,y:p.y-4,width:8,height:8)).fill()
        if node.portRows>1 {label(port.label,NSRect(x:output ? p.x-76 : p.x+9,y:p.y-6,width:67,height:14),port.modulation ? Theme.gold : Theme.muted,9)}}}
    }
    if nodes.isEmpty{label(emptyMessage,NSRect(x:24,y:30,width:500,height:30),Theme.muted,15)}
  }
  func selectForContext(_ event:NSEvent) {
    let point=convert(event.locationInWindow,from:nil)
    if let node=nodes.reversed().first(where:{$0.rect.contains(point)}) {selected=node.id;selectedEdge=nil;onSelect?(node.id)}
    else {selected=nil;selectedEdge=edge(at:point);if let selectedEdge{onSelectEdge?(selectedEdge)}}
  }
  override func mouseDown(with event:NSEvent){
    window?.makeFirstResponder(self);let point=convert(event.locationInWindow,from:nil);pointer=point
    for node in nodes.reversed(){if let port=node.outputs.first(where:{hypot(node.portPoint($0,output:true).x-point.x,node.portPoint($0,output:true).y-point.y)<12}){selected=node.id;selectedEdge=nil;onSelect?(node.id);wiring=(node.id,port);return}}
    if let node=nodes.reversed().first(where:{$0.rect.contains(point)}){selected=node.id;selectedEdge=nil;onSelect?(node.id)
      if event.clickCount>=2{dragging=nil;onOpen?(node.id);return};dragging=node.id;origin=point;original=NSPoint(x:node.x,y:node.y);return}
    selected=nil;selectedEdge=edge(at:point);if let selectedEdge{onSelectEdge?(selectedEdge)}
  }
  override func mouseDragged(with event:NSEvent){
    pointer=convert(event.locationInWindow,from:nil)
    if let dragging,let index=nodes.firstIndex(where:{$0.id==dragging}){nodes[index].x=max(8,original.x+pointer.x-origin.x);nodes[index].y=max(8,original.y+pointer.y-origin.y);rebuildGeometry()}
    needsDisplay=true
  }
  override func mouseUp(with event:NSEvent){
    let point=convert(event.locationInWindow,from:nil)
    if let (from,port)=wiring{outer:for node in nodes where node.id != from{for target in node.inputs where target.modulation==port.modulation{
      let p=node.portPoint(target,output:false);if hypot(p.x-point.x,p.y-point.y)<18{if let onConnectPorts{onConnectPorts(from,node.id,port.number,target.number,port.modulation)}else{onConnect?(from,node.id)};break outer}}}}
    if let dragging,let node=nodes.first(where:{$0.id==dragging}),node.x != original.x || node.y != original.y {onMove?(dragging,node.x,node.y)}
    wiring=nil;dragging=nil;resizeCanvas();needsDisplay=true
  }
  override func keyDown(with event:NSEvent){
    if event.keyCode==53{if let dragging,let i=nodes.firstIndex(where:{$0.id==dragging}){nodes[i].x=original.x;nodes[i].y=original.y;rebuildGeometry()};wiring=nil;dragging=nil;selectedEdge=nil;needsDisplay=true;return}
    if event.keyCode==51 || event.keyCode==117{onDelete?();return}
    if event.characters=="+" || event.characters=="="{onZoom?(1.2);return};if event.characters=="-"{onZoom?(1/1.2);return}
    if event.keyCode==48{
      if event.modifierFlags.contains(.option){guard !edges.isEmpty else{return};let current=selectedEdge ?? (event.modifierFlags.contains(.shift) ? 0 : -1);selectedEdge=(current+(event.modifierFlags.contains(.shift) ? edges.count-1 : 1)+edges.count)%edges.count;selected=nil;onSelectEdge?(selectedEdge!);scrollToVisible(wires[selectedEdge!].bounds);return}
      guard !nodes.isEmpty else{return};let current=nodes.firstIndex(where:{$0.id==selected}) ?? (event.modifierFlags.contains(.shift) ? 0 : -1);let next=(current+(event.modifierFlags.contains(.shift) ? nodes.count-1 : 1)+nodes.count)%nodes.count;selected=nodes[next].id;selectedEdge=nil;onSelect?(nodes[next].id);scrollToVisible(nodes[next].rect.insetBy(dx:-20,dy:-20));return
    }
    guard let selected,let index=nodes.firstIndex(where:{$0.id==selected})else{super.keyDown(with:event);return}
    if event.keyCode==36{onOpen?(selected);return}
    let step=event.modifierFlags.contains(.shift) ? 24.0 : 4.0
    switch event.keyCode{case 123:nodes[index].x=max(8,nodes[index].x-step);case 124:nodes[index].x+=step;case 125:nodes[index].y+=step;case 126:nodes[index].y=max(8,nodes[index].y-step);default:super.keyDown(with:event);return}
    rebuildGeometry();resizeCanvas();onMove?(selected,nodes[index].x,nodes[index].y)
  }
}
