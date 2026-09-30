import AppKit

struct ParameterTraceSample {
  let sequence:UInt64,frame:Double,value:Double,low:Double,high:Double,seconds:Double,position:Double
  let pattern:Int,order:Int,audible:Bool,source:[String:Any]
  let kind:String
  var continuous:Bool {kind=="pattern-slide" || kind=="graph" || kind=="envelope"}
  init?(_ d:[String:Any]) {
    guard let value=(d["value"] as? NSNumber)?.doubleValue,value.isFinite else{return nil}
    self.value=value;sequence=(d["sequence"] as? NSNumber)?.uint64Value ?? 0;frame=(d["frame"] as? NSNumber)?.doubleValue ?? 0
    low=(d["minimum"] as? NSNumber)?.doubleValue ?? value;high=(d["maximum"] as? NSNumber)?.doubleValue ?? value
    seconds=(d["seconds"] as? NSNumber)?.doubleValue ?? 0;position=(d["position"] as? NSNumber)?.doubleValue ?? 0
    pattern=d["pattern"] as? Int ?? -1;order=d["order"] as? Int ?? -1;audible=d["audible"] as? Bool ?? true;source=d["source"] as? [String:Any] ?? [:]
    kind=source["kind"] as? String ?? "baseline"
  }
}
func parameterOriginName(_ kind:String)->String {
  ["baseline":"Prepared value","manual":"Manual / API","plugin-editor":"Plugin interface","recorded":"Recorded automation","envelope":"Pattern envelope","pattern-set":"PS · set parameter","pattern-slide":"PL · slide parameter","graph":"Graph modulation sum","graph-source":"Graph contribution","reset":"Envelope edit / baseline reset"][kind] ?? kind
}
func parameterOriginColor(_ kind:String)->NSColor {
  switch kind {case "envelope":return .systemTeal;case "pattern-slide","pattern-set":return .systemOrange;case "recorded":return .systemPurple;case "graph":return .systemGreen;default:return Theme.muted}
}
final class ParameterTraceView:NSView {
  var samples=[ParameterTraceSample]() {didSet{needsDisplay=true}}
  var low=0.0,high=1.0,patternMode=true,span=64.0,unit=""
  var viewStart=0.0,viewEnd:Double?,selected:UInt64?
  var editRow:Double? {didSet{if oldValue != editRow{updateCursors()}}}
  var playRow:Double? {didSet{if oldValue != playRow{updateCursors()}}}
  private let editCursor=CAShapeLayer(),playCursor=CAShapeLayer()
  private static let axisAttributes:[NSAttributedString.Key:Any]=[.font:NSFont.monospacedDigitSystemFont(ofSize:10,weight:.regular),.foregroundColor:Theme.muted]
  var onSelect:((ParameterTraceSample)->Void)?
  var onHover:((String)->Void)?
  var plot:NSRect {bounds.insetBy(dx:52,dy:25)}
  override var isFlipped:Bool {true}
  override var acceptsFirstResponder:Bool {true}
  override init(frame:NSRect){
    super.init(frame:frame);wantsLayer=true
    for (cursor,color) in [(editCursor,NSColor.systemBlue),(playCursor,NSColor.systemYellow)] {
      cursor.strokeColor=color.withAlphaComponent(0.8).cgColor;cursor.lineWidth=1;cursor.isHidden=true;layer?.addSublayer(cursor)
    }
    setAccessibilityElement(true);setAccessibilityRole(.group);setAccessibilityLabel("Rendered parameter value trace");setAccessibilityHelp("Click the trace to inspect its source. Pinch or Option-scroll to zoom; scroll to pan. Gaps and faded lines indicate uncaptured or inaudible data.")
  }
  required init?(coder:NSCoder){fatalError()}
  override func layout(){super.layout();updateCursors()}
  private func updateCursors(){
    // Moving a playhead must not rerasterize the entire captured trace at 60 Hz.
    CATransaction.begin();CATransaction.setDisableActions(true)
    for (row,cursor) in [(editRow,editCursor),(playRow,playCursor)] {
      guard patternMode,let row,row.isFinite,row>=viewStart,row<=end,plot.width>0,plot.height>0 else{cursor.isHidden=true;continue}
      cursor.isHidden=false;cursor.frame=NSRect(x:location(row,low).x,y:plot.minY,width:1,height:plot.height)
      let path=CGMutablePath();path.move(to:.zero);path.addLine(to:CGPoint(x:0,y:plot.height));cursor.path=path
    }
    CATransaction.commit()
  }
  func x(_ sample:ParameterTraceSample)->Double {patternMode ? sample.position/256:sample.seconds}
  var end:Double {viewEnd ?? span}
  func fit(){viewStart=patternMode ? 0:max(0,(samples.last?.seconds ?? 30)-30);span=patternMode ? span:max(viewStart+1,samples.last?.seconds ?? 30);viewEnd=nil;needsDisplay=true}
  func zoom(_ factor:Double){guard factor.isFinite,factor>0 else{return};let width=max(0.01,(end-viewStart)/factor),center=(viewStart+end)/2;viewStart=max(0,center-width/2);viewEnd=viewStart+width;needsDisplay=true}
  override func magnify(with event:NSEvent){zoom(exp(Double(event.magnification)*2))}
  override func scrollWheel(with event:NSEvent){if event.modifierFlags.contains(.option){zoom(exp(-Double(event.scrollingDeltaY)*0.02))}else{let width=end-viewStart;viewStart=max(0,viewStart+Double(event.scrollingDeltaX+event.scrollingDeltaY)*width/500);viewEnd=viewStart+width;needsDisplay=true}}
  func location(_ x:Double,_ value:Double)->NSPoint {NSPoint(x:plot.minX+(x-viewStart)/max(0.001,end-viewStart)*plot.width,y:plot.maxY-(value-low)/max(0.000001,high-low)*plot.height)}
  override func draw(_ dirtyRect:NSRect){
    updateCursors()
    Theme.bg.setFill();bounds.fill();let attrs=Self.axisAttributes
    guard plot.width>0,plot.height>0 else{return}
    for i in 0...4 {let f=Double(i)/4,y=plot.maxY-plot.height*f;Theme.border.setStroke();let line=NSBezierPath();line.move(to:NSPoint(x:plot.minX,y:y));line.line(to:NSPoint(x:plot.maxX,y:y));line.stroke();(String(format:"%.3g",low+(high-low)*f) as NSString).draw(at:NSPoint(x:3,y:y-6),withAttributes:attrs)
      let x=viewStart+(end-viewStart)*f;(String(format:patternMode ? "R %.1f" : "%.2fs",x) as NSString).draw(at:NSPoint(x:plot.minX+plot.width*f-15,y:plot.maxY+6),withAttributes:attrs)
    }
    NSGraphicsContext.saveGraphicsState();NSBezierPath(rect:plot).addClip();var previous:ParameterTraceSample?
    // Raster work is bounded by visible pixels while extrema remain visible.
    let width=min(4096,max(1,Int(plot.width)))
    var bins=[(ParameterTraceSample,Double,Double)?](repeating:nil,count:width+1)
    for s in samples {let at=x(s);guard at>=viewStart,at<=end else{continue};let index=min(width,max(0,Int((at-viewStart)/max(0.001,end-viewStart)*Double(width))))
      if let old=bins[index]{bins[index]=(s,min(old.1,s.low),max(old.2,s.high))}else{bins[index]=(s,s.low,s.high)}}
    var paths=[String:(NSColor,NSBezierPath,NSBezierPath)]()
    for (s,minimum,maximum) in bins.compactMap({$0}) {let at=x(s);defer{previous=s};let point=location(at,s.value)
      let key=s.kind+(s.audible ? ":on":":off")
      if paths[key]==nil {paths[key]=(parameterOriginColor(s.kind).withAlphaComponent(s.audible ? 0.95:0.3),NSBezierPath(),NSBezierPath())}
      let (_,range,path)=paths[key]!
      range.move(to:location(at,minimum));range.line(to:location(at,maximum))
      if let p=previous,x(p)<=at,p.pattern==s.pattern || !patternMode {
        path.move(to:location(x(p),p.value));if !s.continuous{path.line(to:location(at,p.value))};path.line(to:point)
      }
    }
    for key in paths.keys.sorted() {let (color,range,path)=paths[key]!;color.setStroke();range.lineWidth=1;range.stroke();path.lineWidth=1.7;path.stroke()}
    if let sample=samples.first(where:{$0.sequence==selected}) {let point=location(x(sample),sample.value);Theme.text.setFill();NSBezierPath(ovalIn:NSRect(x:point.x-3,y:point.y-3,width:6,height:6)).fill()}
    NSGraphicsContext.restoreGraphicsState()
    if samples.isEmpty {("Play the song to capture this parameter. Unplayed data is not predicted." as NSString).draw(at:NSPoint(x:plot.minX+10,y:plot.midY),withAttributes:attrs)}
  }
  override func mouseDown(with event:NSEvent){window?.makeFirstResponder(self);let p=convert(event.locationInWindow,from:nil);let at=viewStart+Double((p.x-plot.minX)/max(1,plot.width))*(end-viewStart);guard let sample=samples.min(by:{abs(x($0)-at)<abs(x($1)-at)})else{return};selected=sample.sequence;needsDisplay=true;onSelect?(sample)}
}
