import AppKit

struct GraphParameterRange:Equatable {
  let source:String,target:String,parameter:UInt32,name:String
  var minimum:Double,maximum:Double,enabled:Bool
}

// One independent source contribution range. Endpoint values are normalized
// additions to the base; they may be reversed. A gesture publishes once.
final class GraphParameterRangeView:NSView {
  var range:GraphParameterRange
  var onCommit:((Double,Double)->Void)?,onEdit:(()->Void)?,onGesture:((Bool)->Void)?
  private var start:(Double,Double,Double,Int)?
  override var acceptsFirstResponder:Bool{true}
  override var isFlipped:Bool{true}
  init(_ range:GraphParameterRange){self.range=range;super.init(frame:.zero);setAccessibilityElement(true);setAccessibilityRole(.slider);setAccessibilityLabel(range.name+" modulation range");updateAccessibility()}
  required init?(coder:NSCoder){fatalError()}
  var bar:NSRect{NSRect(x:88,y:7,width:max(20,bounds.width-96),height:8)}
  func x(_ value:Double)->CGFloat{bar.minX+CGFloat((value+1)/2)*bar.width}
  private func value(_ x:CGFloat)->Double{max(-1,min(1,Double((x-bar.minX)/bar.width)*2-1))}
  private func updateAccessibility(){
    let values=String(format:"Source 0: %+.3f, source 1: %+.3f%@",range.minimum,range.maximum,range.enabled ? "":"; disabled")
    setAccessibilityValue(values)
    toolTip=range.name+" · "+values+". Normalized additive range −1…+1. Hollow handle = source 0; filled = source 1. Drag an endpoint or shift the segment. Shift-drag picks the hollow handle when they overlap. Esc cancels; click the name or Return to edit this connection."
  }
  override func draw(_ dirtyRect:NSRect){
    let color=range.enabled ? Theme.gold:Theme.muted
    let name=range.name+(range.enabled ? "":" (off)")
    (name as NSString).draw(in:NSRect(x:2,y:3,width:82,height:18),withAttributes:[.font:NSFont.systemFont(ofSize:9),.foregroundColor:color])
    Theme.raised.setFill();NSBezierPath(roundedRect:bar,xRadius:4,yRadius:4).fill()
    Theme.muted.withAlphaComponent(0.4).setStroke();let zero=NSBezierPath();zero.move(to:NSPoint(x:x(0),y:bar.minY-2));zero.line(to:NSPoint(x:x(0),y:bar.maxY+2));zero.stroke()
    let a=x(range.minimum),b=x(range.maximum)
    color.withAlphaComponent(0.65).setFill();NSBezierPath(roundedRect:NSRect(x:min(a,b),y:bar.minY,width:max(2,abs(a-b)),height:bar.height),xRadius:3,yRadius:3).fill()
    color.setStroke();for (index,p) in [a,b].enumerated(){let handle=NSBezierPath(ovalIn:NSRect(x:p-3,y:bar.midY-4,width:6,height:8));if index==1{color.setFill();handle.fill()}else{Theme.bg.setFill();handle.fill();handle.stroke()}}
    if window?.firstResponder === self {color.withAlphaComponent(0.45).setStroke();NSBezierPath(roundedRect:bounds.insetBy(dx:1,dy:1),xRadius:3,yRadius:3).stroke()}
  }
  override func mouseDown(with event:NSEvent){
    let point=convert(event.locationInWindow,from:nil)
    guard point.x>=bar.minX-6 else{onEdit?();return}
    window?.makeFirstResponder(self)
    let a=abs(point.x-x(range.minimum)),b=abs(point.x-x(range.maximum))
    let handle=(a<8 || b<8) ? (event.modifierFlags.contains(.shift) || a<b ? 0:1):2
    begin(at:value(point.x),handle:handle)
  }
  func begin(at value:Double,handle:Int){guard start==nil else{return};start=(range.minimum,range.maximum,value,handle);onGesture?(true)}
  func drag(to value:Double){guard let (a,b,origin,handle)=start,value.isFinite else{return}
    if handle==0{range.minimum=max(-1,min(1,value))}
    else if handle==1{range.maximum=max(-1,min(1,value))}
    else{let delta=max(-1-min(a,b),min(1-max(a,b),value-origin));range.minimum=a+delta;range.maximum=b+delta}
    updateAccessibility();needsDisplay=true
  }
  func finish(cancelled:Bool=false){guard let (a,b,_,_)=start else{return};start=nil
    let proposed=(range.minimum,range.maximum);range.minimum=a;range.maximum=b
    if !cancelled && (a != proposed.0 || b != proposed.1){onCommit?(proposed.0,proposed.1)}
    onGesture?(false);updateAccessibility();needsDisplay=true
  }
  override func mouseDragged(with event:NSEvent){drag(to:value(convert(event.locationInWindow,from:nil).x))}
  override func mouseUp(with event:NSEvent){if start != nil{drag(to:value(convert(event.locationInWindow,from:nil).x));finish()}}
  override func keyDown(with event:NSEvent){
    if event.keyCode==53{finish(cancelled:true);return}
    if event.keyCode==36{onEdit?();return}
    if event.keyCode==123 || event.keyCode==124 {let step=event.modifierFlags.contains(.shift) ? 0.001:0.01;begin(at:0,handle:2);drag(to:event.keyCode==123 ? -step:step);finish();return}
    super.keyDown(with:event)
  }
  override func menu(for event:NSEvent)->NSMenu?{let menu=NSMenu();menu.addItem(ContextAction("Edit this source connection…",key:"↩"){[weak self] in self?.onEdit?()});return menu}
}

extension SignalGraphEditor {
  func configureParameterRanges(){
    let controls=graphID==nil ? rackControls:pluginControls.parametersView
    let target=graphID==nil ? selectedID.flatMap{songNodePlugin[$0]}:selectedNode?["kind"] as? String=="plugin" ? selectedID:nil
    let graph=graphID,document=projectionDocument,context=viewContext,capturedRevision=revision
    let sourceNodes=graph==nil ? songSources:nodes
    let edges=graph==nil ? songModulation:definition?["modulation"] as? [[String:Any]] ?? []
    let ranges: [GraphParameterRange]=edges.compactMap { edge in
      guard let target,target==(edge[graph==nil ? "plugin":"target"] as? String),let source=edge["source"] as? String,let parameter=(edge["parameter"] as? NSNumber)?.uint32Value else{return nil}
      let node=sourceNodes.first{$0["id"] as? String==source}
      return .init(source:source,target:target,parameter:parameter,name:node?["name"] as? String ?? source,minimum:(edge["minimum"] as? NSNumber)?.doubleValue ?? 0,maximum:(edge["maximum"] as? NSNumber)?.doubleValue ?? 0,enabled:edge["enabled"] as? Bool != false)
    }
    controls.onRangeCommit={[weak self] range,minimum,maximum in
      guard let self else{return}
      guard self.projectionDocument==document,self.viewContext==context,self.graphID==graph,self.revision==capturedRevision else{self.status.stringValue="The graph changed; this modulation range edit was cancelled";self.inspect();return}
      if graph==nil {self.mutate("graph.song.modulation.set",["source":range.source,"plugin":range.target,"parameter":range.parameter,"minimum":minimum,"maximum":maximum])}
      else {self.updateDefinition{definition in var edges=definition["modulation"] as? [[String:Any]] ?? [];guard let i=edges.firstIndex(where:{$0["source"] as? String==range.source && $0["target"] as? String==range.target && ($0["parameter"] as? NSNumber)?.uint32Value==range.parameter})else{return};edges[i]["minimum"]=minimum;edges[i]["maximum"]=maximum;definition["modulation"]=edges}}
    }
    controls.onRangeEdit={[weak self] range in
      guard let self,self.projectionDocument==document,self.viewContext==context,self.graphID==graph else{return}
      if graph==nil{self.inspectSongModulation(source:range.source,plugin:range.target,parameter:range.parameter,editConnection:true)}
      else {let audioCount=(self.definition?["audio"] as? [[String:Any]] ?? []).count;let mods=self.definition?["modulation"] as? [[String:Any]] ?? [];guard let i=mods.firstIndex(where:{$0["source"] as? String==range.source && $0["target"] as? String==range.target && ($0["parameter"] as? NSNumber)?.uint32Value==range.parameter}),let visible=self.definitionEdgeIndices.firstIndex(of:audioCount+i)else{return};self.selectConnection(visible);self.revealCableAmount();self.focusConnectionValue(self.maximum)}
    }
    controls.updateRanges(ranges)
  }
}
