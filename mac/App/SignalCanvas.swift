import AppKit

struct SignalCanvasNode {
  var id: String, title: String, detail: String, kind: String
  var x: Double, y: Double
  var activity: String? = nil
  var rect: NSRect { NSRect(x:x,y:y,width:180,height:66) }
}
struct SignalCanvasEdge {
  var source: String, target: String, label: String
  var modulation = false
}
final class SignalCanvas: NSView {
  var emptyMessage="Create a subgraph to start connecting sound."
  var nodes = [SignalCanvasNode]()
  var edges = [SignalCanvasEdge]() {didSet{needsDisplay=true}}
  var selected: String? {didSet{needsDisplay=true}}
  var onSelect: ((String)->Void)?, onMove: ((String,Double,Double)->Void)?, onConnect: ((String,String)->Void)?
  var onOpen: ((String)->Void)?
  private var dragging: String?, origin = NSPoint.zero, original = NSPoint.zero, wiring: String?, pointer = NSPoint.zero
  override var isFlipped: Bool {true}
  override var acceptsFirstResponder: Bool {true}
  override init(frame: NSRect) {
    super.init(frame:frame);setAccessibilityRole(.group);setAccessibilityLabel("Audio and modulation graph")
    setAccessibilityHelp("Tab selects nodes. Arrow keys move the selected node. Return opens its controls. Drag from a node's right port to another node's left port to connect.")
  }
  required init?(coder:NSCoder){fatalError()}
  func update(_ nodes:[SignalCanvasNode],edges:[SignalCanvasEdge]) {
    self.nodes=nodes;self.edges=edges
    frame.size=NSSize(width:max(900,(nodes.map{$0.rect.maxX}.max() ?? 0)+100),height:max(500,(nodes.map{$0.rect.maxY}.max() ?? 0)+100))
    setAccessibilityValue(nodes.map{node in node.title+" → "+edges.filter{$0.source==node.id}.map{$0.label}.joined(separator:", ")}.joined(separator:"; "))
    needsDisplay=true
  }
  private func label(_ text:String,_ rect:NSRect,_ color:NSColor,_ size:CGFloat=12,_ weight:NSFont.Weight = .regular){
    let paragraph=NSMutableParagraphStyle();paragraph.lineBreakMode = .byTruncatingTail
    (text as NSString).draw(in:rect,withAttributes:[.font:NSFont.systemFont(ofSize:size,weight:weight),.foregroundColor:color,.paragraphStyle:paragraph])
  }
  private func wire(_ from:NSPoint,_ to:NSPoint,_ mod:Bool,emphasized:Bool=true){
    let path=NSBezierPath();path.move(to:from);let distance=max(50,abs(to.x-from.x)*0.5)
    path.curve(to:to,controlPoint1:NSPoint(x:from.x+distance,y:from.y),controlPoint2:NSPoint(x:to.x-distance,y:to.y))
    let color=(mod ? Theme.gold : Theme.accent).withAlphaComponent(emphasized ? 0.9 : 0.22)
    color.setStroke();path.lineWidth=emphasized ? 2 : 1
    if mod{path.setLineDash([5,4],count:2,phase:0)};path.stroke()
    let arrow=NSBezierPath();arrow.move(to:to);arrow.line(to:NSPoint(x:to.x-8,y:to.y-4));arrow.line(to:NSPoint(x:to.x-8,y:to.y+4));arrow.close();color.setFill();arrow.fill()
  }
  override func draw(_ dirty:NSRect){
    Theme.bg.setFill();dirty.fill()
    Theme.border.withAlphaComponent(0.25).setFill()
    for x in stride(from:max(0,Int(dirty.minX)/24*24),to:Int(dirty.maxX),by:24){for y in stride(from:max(0,Int(dirty.minY)/24*24),to:Int(dirty.maxY),by:24){NSRect(x:x,y:y,width:1,height:1).fill()}}
    for edge in edges {guard let a=nodes.first(where:{$0.id==edge.source}),let b=nodes.first(where:{$0.id==edge.target})else{continue}
      let from=NSPoint(x:a.rect.maxX,y:a.rect.midY),to=NSPoint(x:b.rect.minX,y:b.rect.midY);wire(from,to,edge.modulation,emphasized:selected==nil || selected==edge.source || selected==edge.target)
      if !edge.label.isEmpty{label(edge.label,NSRect(x:from.x+12,y:from.y-18,width:max(40,to.x-from.x-24),height:15),edge.modulation ? Theme.gold : Theme.muted,10)}
    }
    if let wiring,let node=nodes.first(where:{$0.id==wiring}){wire(NSPoint(x:node.rect.maxX,y:node.rect.midY),pointer,false)}
    for node in nodes {
      let path=NSBezierPath(roundedRect:node.rect,xRadius:7,yRadius:7);Theme.raised.setFill();path.fill();(selected==node.id ? Theme.accent : Theme.border).setStroke();path.lineWidth=selected==node.id ? 2 : 1;path.stroke()
      label(node.title,NSRect(x:node.x+13,y:node.y+12,width:154,height:20),Theme.text,13,.semibold)
      label(node.detail,NSRect(x:node.x+13,y:node.y+38,width:154,height:16),Theme.muted,10)
      if let activity=node.activity{(activity=="tail" ? Theme.gold : Theme.accent).setFill();NSBezierPath(ovalIn:NSRect(x:node.rect.maxX-12,y:node.y+5,width:6,height:6)).fill()}
      for x in [node.rect.minX,node.rect.maxX]{(node.kind=="modulation" ? Theme.gold : Theme.accent).setFill();NSBezierPath(ovalIn:NSRect(x:x-4,y:node.rect.midY-4,width:8,height:8)).fill()}
    }
    if nodes.isEmpty{label(emptyMessage,NSRect(x:24,y:30,width:500,height:30),Theme.muted,15)}
  }
  override func mouseDown(with event:NSEvent){
    window?.makeFirstResponder(self);let point=convert(event.locationInWindow,from:nil);pointer=point
    guard let node=nodes.reversed().first(where:{$0.rect.insetBy(dx:-10,dy:-5).contains(point)})else{selected=nil;return}
    selected=node.id;onSelect?(node.id)
    if event.clickCount==2{onOpen?(node.id);return}
    if abs(point.x-node.rect.maxX)<12&&abs(point.y-node.rect.midY)<14{wiring=node.id;return}
    dragging=node.id;origin=point;original=NSPoint(x:node.x,y:node.y)
  }
  override func mouseDragged(with event:NSEvent){
    pointer=convert(event.locationInWindow,from:nil)
    if let dragging,let index=nodes.firstIndex(where:{$0.id==dragging}){nodes[index].x=max(8,original.x+pointer.x-origin.x);nodes[index].y=max(8,original.y+pointer.y-origin.y)}
    needsDisplay=true
  }
  override func mouseUp(with event:NSEvent){
    let point=convert(event.locationInWindow,from:nil)
    if let wiring,let node=nodes.first(where:{abs($0.rect.minX-point.x)<18&&abs($0.rect.midY-point.y)<22}),node.id != wiring{onConnect?(wiring,node.id)}
    if let dragging,let node=nodes.first(where:{$0.id==dragging}),node.x != original.x || node.y != original.y {onMove?(dragging,node.x,node.y)}
    wiring=nil;dragging=nil;needsDisplay=true
  }
  override func keyDown(with event:NSEvent){
    if event.keyCode==48{guard !nodes.isEmpty else{return};let current=nodes.firstIndex(where:{$0.id==selected}) ?? -1;let next=(current+(event.modifierFlags.contains(.shift) ? nodes.count-1 : 1)+nodes.count)%nodes.count;selected=nodes[next].id;onSelect?(nodes[next].id);scrollToVisible(nodes[next].rect.insetBy(dx:-20,dy:-20));return}
    guard let selected,let index=nodes.firstIndex(where:{$0.id==selected})else{super.keyDown(with:event);return}
    if event.keyCode==36{onOpen?(selected);return}
    let step=event.modifierFlags.contains(.shift) ? 24.0 : 4.0
    switch event.keyCode{case 123:nodes[index].x=max(8,nodes[index].x-step);case 124:nodes[index].x+=step;case 125:nodes[index].y+=step;case 126:nodes[index].y=max(8,nodes[index].y-step);default:super.keyDown(with:event);return}
    onMove?(selected,nodes[index].x,nodes[index].y);needsDisplay=true
  }
}
