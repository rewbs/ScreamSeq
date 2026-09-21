import AppKit

struct SampleStrokePoint: Equatable {
  let frame: Int
  let value: Double
}

final class WaveformView: NSView {
  var playbackFrames: [Double] = [] { didSet { if playbackFrames != oldValue {needsDisplay=true} } }
  var peaks: [Float] = [] { didSet { needsDisplay = true } }
  var peaksRange: Range<Int>?
  var frames = 0 { didSet {
    if frames != oldValue { cancelStroke(); clampViewport() }
    needsDisplay = true
  } }
  var loopStart = 0, loopEnd = 0
  private(set) var viewport: Range<Int>?
  var visibleRange: Range<Int> { viewport ?? 0..<max(0, frames) }
  var selection: ClosedRange<Int>? { didSet { needsDisplay = true; onSelection?(selection) } }
  var drawing = false { didSet { if !drawing { cancelStroke() }; needsDisplay = true } }
  var onSelection: ((ClosedRange<Int>?) -> Void)?
  var onClipboard: ((String) -> Void)?
  var onViewport: (() -> Void)?
  var onFinishSelection: (() -> Void)?
  var onBeginStroke: (() -> Bool)?
  var onStroke: (([SampleStrokePoint]) -> Void)?
  var onCancelStroke: (() -> Void)?
  private var anchor = 0, strokeActive = false
  private var previousPoint: SampleStrokePoint?
  private(set) var stroke = [Int: Double]()
  var precise: Bool {
    let range = visibleRange
    return !range.isEmpty && Double(range.count) <= min(4096, max(1, Double(bounds.width))) &&
      (peaksRange ?? 0..<max(0, frames)) == range && peaks.count == range.count * 2
  }
  override var acceptsFirstResponder: Bool { true }
  private func clampViewport() {
    guard let view = viewport else { return }
    if frames <= 0 { viewport = nil; return }
    let size = min(frames, max(1, view.count)), first = max(0, min(view.lowerBound, frames-size))
    viewport = first == 0 && size == frames ? nil : first..<(first+size)
  }
  func setViewport(_ range: Range<Int>?, notify: Bool = true) {
    cancelStroke(); viewport = range; clampViewport(); needsDisplay = true
    if notify { onViewport?() }
  }
  func zoom(_ factor: Double, center: Double? = nil) {
    guard frames > 0, factor.isFinite, factor > 0 else { return }
    let range = visibleRange
    let anchor = center ?? selection.flatMap { range.contains($0.lowerBound) ? Double($0.lowerBound) : nil } ?? Double(range.lowerBound + range.count/2)
    guard anchor.isFinite else { return }
    let middle = max(0, min(Double(frames), anchor))
    let size = max(1, min(frames, Int(min(Double(frames), max(1, Double(range.count)/factor)).rounded())))
    let ratio = min(1, max(0, (middle-Double(range.lowerBound))/Double(max(1,range.count))))
    let start = max(0, min(frames-size, Int((middle-Double(size)*ratio).rounded())))
    setViewport(start..<(start+size))
  }
  func zoomSelection() {
    guard let selection, frames > 0 else { return }
    if selection.upperBound > selection.lowerBound { setViewport(selection.lowerBound..<selection.upperBound) }
    else { let size=min(frames,128), start=max(0,min(frames-size,selection.lowerBound-size/2)); setViewport(start..<(start+size)) }
  }
  func panFrames(_ delta: Int) {
    let range=visibleRange
    // Bound the displacement before adding: clamping an overflowed sum is too late.
    let last=max(0,frames-range.count)
    let start=range.lowerBound+max(-range.lowerBound,min(last-range.lowerBound,delta))
    setViewport(start..<(start+range.count))
  }
  func frameAt(x: CGFloat, insertion: Bool = true) -> Int {
    let range=visibleRange
    let fraction=max(0,min(1,Double(x/max(1,bounds.width))))
    return min(insertion ? range.upperBound : max(range.lowerBound,range.upperBound-1), range.lowerBound+Int(fraction*Double(range.count)))
  }
  private func x(_ frame: Double) -> CGFloat {
    CGFloat((frame-Double(visibleRange.lowerBound))/Double(max(1,visibleRange.count)))*bounds.width
  }
  private func samplePoint(_ event: NSEvent) -> SampleStrokePoint {
    let point=convert(event.locationInWindow,from:nil)
    return SampleStrokePoint(frame:frameAt(x:point.x,insertion:false),value:max(-1,min(1,Double((point.y-bounds.midY)/max(1,bounds.height*0.44)))))
  }
  func beginStroke(_ point: SampleStrokePoint) -> Bool {
    guard drawing, precise, visibleRange.contains(point.frame), point.value.isFinite, onBeginStroke?() == true else { return false }
    stroke.removeAll(keepingCapacity:true); previousPoint=nil; strokeActive=true; extendStroke(point); return true
  }
  func extendStroke(_ point: SampleStrokePoint) {
    guard strokeActive, visibleRange.contains(point.frame), point.value.isFinite else { return }
    let point=SampleStrokePoint(frame:point.frame,value:max(-1,min(1,point.value)))
    if let previousPoint, previousPoint.frame != point.frame {
      for frame in min(previousPoint.frame,point.frame)...max(previousPoint.frame,point.frame) {
        let amount=Double(frame-previousPoint.frame)/Double(point.frame-previousPoint.frame)
        stroke[frame]=previousPoint.value+(point.value-previousPoint.value)*amount
      }
    } else { stroke[point.frame]=point.value }
    previousPoint=point; needsDisplay=true
  }
  func finishStroke() {
    guard strokeActive else { return }; strokeActive=false; previousPoint=nil
    onStroke?(stroke.keys.sorted().map { SampleStrokePoint(frame:$0,value:stroke[$0]!) })
  }
  func cancelStroke() {
    let hadStroke=strokeActive || !stroke.isEmpty
    strokeActive=false; previousPoint=nil; stroke.removeAll(keepingCapacity:true); needsDisplay=true
    if hadStroke { onCancelStroke?() }
  }
  override func draw(_ dirtyRect: NSRect) {
    Theme.bg.setFill(); bounds.fill()
    let mid=bounds.midY, height=bounds.height*0.44
    Theme.border.setStroke(); let center=NSBezierPath();center.move(to:.init(x:0,y:mid));center.line(to:.init(x:bounds.width,y:mid));center.stroke()
    if let selection, frames>0 {
      let start=max(0,min(bounds.width,x(Double(selection.lowerBound)))), end=max(0,min(bounds.width,x(Double(selection.upperBound))))
      Theme.accent.withAlphaComponent(0.15).setFill(); NSRect(x:start,y:0,width:max(0,end-start),height:bounds.height).fill()
      if selection.lowerBound==selection.upperBound, selection.lowerBound>=visibleRange.lowerBound, selection.upperBound<=visibleRange.upperBound {
        Theme.accent.setFill();NSRect(x:min(bounds.width-1,start),y:0,width:1,height:bounds.height).fill()
      }
    }
    if peaks.count>1 {
      Theme.accent.setStroke();let path=NSBezierPath();path.lineWidth=1
      let bins=peaks.count/2, range=peaksRange ?? 0..<max(0,frames), trace=precise
      for i in 0..<bins {
        let position=x(Double(range.lowerBound)+(Double(i)+0.5)*Double(range.count)/Double(bins))
        if position < -1 || position > bounds.width+1 { continue }
        let low=mid+CGFloat(peaks[i*2])*height, high=mid+CGFloat(peaks[i*2+1])*height
        path.move(to:.init(x:position,y:low));path.line(to:.init(x:position,y:max(low+1,high)))
        if trace, i>0 {
          let previous=x(Double(range.lowerBound)+(Double(i)-0.5)*Double(range.count)/Double(bins))
          for offset in 0...1 { path.move(to:.init(x:previous,y:mid+CGFloat(peaks[(i-1)*2+offset])*height));path.line(to:.init(x:position,y:mid+CGFloat(peaks[i*2+offset])*height)) }
        }
        if trace && bounds.width/CGFloat(bins)>=5 { Theme.accent.setFill(); NSBezierPath(ovalIn:NSRect(x:position-1.5,y:low-1.5,width:3,height:3)).fill() }
      };path.stroke()
    }
    if !stroke.isEmpty {
      Theme.gold.setStroke();let path=NSBezierPath();path.lineWidth=2
      for (i,frame) in stroke.keys.sorted().enumerated() {
        let point=NSPoint(x:x(Double(frame)+0.5),y:mid+CGFloat(stroke[frame]!)*height)
        if i==0 { path.move(to:point) } else { path.line(to:point) }
        Theme.gold.setFill();NSBezierPath(ovalIn:NSRect(x:point.x-2,y:point.y-2,width:4,height:4)).fill()
      };path.stroke()
    }
    if loopEnd>loopStart && frames>0 {
      Theme.gold.setStroke()
      for frame in [loopStart,loopEnd] where frame>=visibleRange.lowerBound && frame<=visibleRange.upperBound {
        let path=NSBezierPath(),position=x(Double(frame));path.move(to:.init(x:position,y:0));path.line(to:.init(x:position,y:bounds.height));path.stroke()
      }
    }
    Theme.text.withAlphaComponent(0.9).setFill()
    for frame in playbackFrames where frame>=Double(visibleRange.lowerBound) && frame<=Double(visibleRange.upperBound) {
      let position=x(frame); NSRect(x:position,y:0,width:1.5,height:bounds.height).fill()
      NSBezierPath(ovalIn:NSRect(x:position-3,y:bounds.height-7,width:6,height:6)).fill()
    }
    if frames==0 { ("Import a sample to begin" as NSString).draw(at:.init(x:24,y:mid),withAttributes:[.foregroundColor:Theme.muted,.font:NSFont.systemFont(ofSize:14)]) }
  }
  override func mouseDown(with event:NSEvent) {
    window?.makeFirstResponder(self)
    if drawing { _=beginStroke(samplePoint(event)); return }
    anchor=frameAt(x:convert(event.locationInWindow,from:nil).x); selection=anchor...anchor
  }
  override func mouseDragged(with event:NSEvent) {
    if drawing { extendStroke(samplePoint(event));return }
    let end=frameAt(x:convert(event.locationInWindow,from:nil).x);selection=min(anchor,end)...max(anchor,end)
  }
  override func mouseUp(with event:NSEvent) {
    if drawing { extendStroke(samplePoint(event));finishStroke() }
    else { onFinishSelection?() }
  }
  override func magnify(with event:NSEvent) { zoom(pow(2,Double(event.magnification)*4),center:Double(frameAt(x:convert(event.locationInWindow,from:nil).x))) }
  override func scrollWheel(with event:NSEvent) {
    if event.modifierFlags.contains(.option) { zoom(pow(2,Double(event.scrollingDeltaY)*0.02));return }
    if event.scrollingDeltaX != 0 || event.modifierFlags.contains(.shift) {
      let delta=event.scrollingDeltaX != 0 ? event.scrollingDeltaX : event.scrollingDeltaY
      let distance=Double(-delta/max(1,bounds.width)*CGFloat(visibleRange.count))
      if distance.isFinite {panFrames(Int(max(-Double(frames),min(Double(frames),distance.rounded()))))};return
    }
    super.scrollWheel(with:event)
  }
  @objc func copy(_ sender:Any?){onClipboard?("copy")}
  @objc func cut(_ sender:Any?){onClipboard?("cut")}
  @objc func paste(_ sender:Any?){onClipboard?("paste")}
  override func keyDown(with event:NSEvent) {
    if event.keyCode==53 { cancelStroke() }
    else if event.modifierFlags.contains(.command) && event.charactersIgnoringModifiers=="a" { selection=0...frames }
    else if event.modifierFlags.contains(.command),let key=event.charactersIgnoringModifiers,let action=["c":"copy","x":"cut","v":"paste"][key]{onClipboard?(action)}
    else if event.keyCode==51 || event.keyCode==117 { onClipboard?("delete") }
    else { super.keyDown(with:event) }
  }
  override func accessibilityValue()->Any? {
    let region="Visible frames \(visibleRange.lowerBound) to \(visibleRange.upperBound)"
    if let selection { return "\(region). Selected frames \(selection.lowerBound) to \(selection.upperBound)" };return region
  }
  override init(frame:NSRect){super.init(frame:frame);setAccessibilityElement(true);setAccessibilityRole(.image);setAccessibilityLabel("Sample waveform");setAccessibilityHelp("Drag to select audio. Zoom to individual frames to draw. Escape cancels a stroke. Command A selects all.")}
  required init?(coder:NSCoder){fatalError()}
}
