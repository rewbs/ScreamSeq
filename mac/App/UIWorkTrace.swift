import AppKit
import QuartzCore

// Qualification-only placement on an explicitly named, already connected display.
// It never changes display modes, the primary display, or musician preferences.
enum QualificationDisplay {
  struct Failure:LocalizedError {
    let message:String
    var errorDescription:String? {message}
  }
  static func requestedID(_ arguments:[String]) throws->UInt32? {
    let indices=arguments.indices.filter{arguments[$0]=="--ui-test-screen"}
    guard !indices.isEmpty else{return nil}
    guard indices.count==1,let index=indices.first,index+1<arguments.count,
      let id=UInt32(arguments[index+1]),id>0 else {
      throw Failure(message:"--ui-test-screen requires one positive existing display ID")
    }
    return id
  }
  static func id(_ screen:NSScreen?)->UInt32? {(screen?.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber)?.uint32Value}
  static func select(_ requested:UInt32?,screens:[NSScreen],fallback:NSScreen?) throws->NSScreen? {
    guard let requested else{return fallback}
    guard let screen=screens.first(where:{id($0)==requested}) else {
      throw Failure(message:"Requested qualification display \(requested) is not connected")
    }
    return screen
  }
  static func centeredFrame(size:NSSize,visibleFrame:NSRect)throws->NSRect {
    guard size.width>0,size.height>0,size.width<=visibleFrame.width,size.height<=visibleFrame.height else {
      throw Failure(message:"Requested qualification display cannot fit the unchanged window dimensions")
    }
    return NSRect(x:visibleFrame.midX-size.width/2,y:visibleFrame.midY-size.height/2,width:size.width,height:size.height)
  }
}

// Diagnostic only: bracket the run loop's sleep interval without adding a
// timer, scheduling work or waking either thread. The late before-wait observer
// runs after AppKit's transaction observers; the early after-wait observer runs
// before normal event processing. Scheduler traces still determine whether a
// thread was asleep or preempted between these markers.
final class QualificationRunLoopTrace {
  struct Entry {let sequence:Int,timestamp:Double,activity:UInt,depth:Int}
  static var enabled:Bool {CommandLine.arguments.contains("--ui-test") && CommandLine.arguments.contains("--ui-test-runloop-trace")}
  private let lock=NSLock(),capacity:Int
  private var entries:[Entry],sequence=0,depth=0,observers=[CFRunLoopObserver]()
  init(capacity:Int=131072) {
    self.capacity=max(1,capacity)
    entries=Array(repeating:Entry(sequence:0,timestamp:0,activity:0,depth:0),count:max(1,capacity))
  }
  func attach(to loop:CFRunLoop) {
    guard observers.isEmpty else{return}
    for (activities,order) in [(CFRunLoopActivity([.entry,.afterWaiting]),CFIndex.min),
      (CFRunLoopActivity([.beforeWaiting,.exit]),CFIndex.max)] {
      if let observer=CFRunLoopObserverCreateWithHandler(nil,activities.rawValue,true,order,{[weak self] _,activity in self?.record(activity)}) {
        observers.append(observer);CFRunLoopAddObserver(loop,observer,.commonModes)
      }
    }
  }
  func detach(){for observer in observers{CFRunLoopObserverInvalidate(observer)};observers=[]}
  private func record(_ activity:CFRunLoopActivity) {
    let time=CACurrentMediaTime()
    lock.lock();defer{lock.unlock()}
    if activity == .entry{depth+=1}
    entries[sequence % capacity]=Entry(sequence:sequence,timestamp:time,activity:activity.rawValue,depth:depth);sequence+=1
    if activity == .exit{depth=max(0,depth-1)}
  }
  func reset(){lock.lock();sequence=0;lock.unlock()}
  func snapshot()->(entries:[Entry],overwritten:Int) {
    lock.lock();defer{lock.unlock()}
    return((max(0,sequence-capacity)..<sequence).map{entries[$0 % capacity]},max(0,sequence-capacity))
  }
  deinit{detach()}
}

/// Opt-in native-button attribution. The bounded ring is main-thread owned;
/// titles/state are copied after drawing so they are outside the measured span.
final class QualificationButtonDrawTrace {
  static let enabled=CommandLine.arguments.contains("--ui-test") && CommandLine.arguments.contains("--ui-test-button-trace")
  struct Entry {
    let sequence:Int,identity:UInt,title:String,context:String,start:Double,end:Double
    let enabled:Bool,highlighted:Bool,state:Int,frame:NSRect
  }
  private var entries:[Entry],sequence=0
  var overwritten:Int {max(0,sequence-entries.count)}
  init(capacity:Int=32768) {
    entries=Array(repeating:Entry(sequence:0,identity:0,title:"",context:"",start:0,end:0,enabled:false,highlighted:false,state:0,frame:.zero),count:max(1,capacity))
  }
  func reset(){sequence=0}
  func record(_ button:NSButton,start:Double,end:Double) {
    var context="main",ancestor=button.superview,depth=0
    while let view=ancestor,depth<16 {
      if let panel=view as? WorkspacePanel {context=panel.id;break}
      ancestor=view.superview;depth+=1
    }
    let label=button.title.isEmpty ? (button.accessibilityLabel() ?? "Untitled button") : button.title
    entries[sequence % entries.count]=Entry(sequence:sequence,identity:UInt(bitPattern:ObjectIdentifier(button)),title:String(label.prefix(128)),context:String(context.prefix(64)),start:start,end:end,enabled:button.isEnabled,highlighted:button.isHighlighted,state:button.state.rawValue,frame:button.frame)
    sequence+=1
  }
  func snapshot()->[Entry] {(max(0,sequence-entries.count)..<sequence).map{entries[$0 % entries.count]}}
}

/// Opt-in attribution of AppKit's periodic state encoding, without changing it.
/// Only the app-owned qualification window subclass records these spans.
final class QualificationWindowEncodeTrace {
  static let enabled=CommandLine.arguments.contains("--ui-test") && CommandLine.arguments.contains("--ui-test-window-state-trace")
  struct Entry {
    let sequence:Int,windowNumber:Int,title:String,className:String,kind:String,restorable:Bool,visible:Bool,start:Double,end:Double
  }
  private var entries:[Entry],sequence=0
  var overwritten:Int{max(0,sequence-entries.count)}
  init(capacity:Int=2048){entries=Array(repeating:Entry(sequence:0,windowNumber:0,title:"",className:"",kind:"",restorable:false,visible:false,start:0,end:0),count:max(1,capacity))}
  func record(_ window:NSWindow,start:Double,end:Double,kind:String="sync"){
    entries[sequence % entries.count]=Entry(sequence:sequence,windowNumber:window.windowNumber,title:String(window.title.prefix(128)),className:String(String(reflecting:type(of:window)).prefix(128)),kind:kind,restorable:window.isRestorable,visible:window.isVisible,start:start,end:end);sequence+=1
  }
  func reset(){sequence=0}
  func snapshot()->[Entry]{(max(0,sequence-entries.count)..<sequence).map{entries[$0 % entries.count]}}
  static func inventory()->[[String:Any]] {
    NSApp.windows.prefix(128).map{window in
      ["windowNumber":window.windowNumber,"title":String(window.title.prefix(128)),"class":String(String(reflecting:type(of:window)).prefix(128)),
       "isRestorable":window.isRestorable,"restorationClass":window.restorationClass.map{String(reflecting:$0)} ?? "",
       "visible":window.isVisible,"key":window.isKeyWindow,"occluded": !window.occlusionState.contains(.visible)]
    }
  }
}

/// Measures only the synchronous portion of AppKit's application encoding.
/// Main-thread owned; neither the coder nor the background queue is retained,
/// inspected or changed. In particular, this does not measure queued work.
final class QualificationApplicationEncodeTrace {
  enum Kind:String {case sync,backgroundQueueSynchronous}
  struct Entry {let sequence:Int,kind:Kind,start:Double,end:Double}
  static func isEnabled(arguments:[String])->Bool {
    arguments.contains("--ui-test") && arguments.contains("--ui-test-window-state-trace")
  }
  static let enabled=isEnabled(arguments:CommandLine.arguments)
  private var entries:[Entry],sequence=0,depth=0,generation=0
  var overwritten:Int{max(0,sequence-entries.count)}
  init(capacity:Int=2048) {
    entries=Array(repeating:Entry(sequence:0,kind:.sync,start:0,end:0),count:max(1,capacity))
  }
  func reset(){sequence=0;generation+=1}
  func snapshot()->[Entry]{(max(0,sequence-entries.count)..<sequence).map{entries[$0 % entries.count]}}
  static func measure(_ kind:Kind,trace:QualificationApplicationEncodeTrace?,
    clock:()->Double={CACurrentMediaTime()},_ forward:()->Void) {
    guard let trace else{forward();return}
    trace.measure(kind,clock:clock,forward)
  }
  private func measure(_ kind:Kind,clock:()->Double,_ forward:()->Void) {
    let outer=depth==0,epoch=generation,start=depth==0 ? clock():0
    depth+=1
    defer {
      depth-=1
      // Either public overload may call the other. Count its complete outer
      // span once. A reset during reentrant work must not restore old evidence.
      if outer,generation==epoch {
        let end=clock()
        entries[sequence % entries.count]=Entry(sequence:sequence,kind:kind,start:start,end:end)
        sequence+=1
      }
    }
    forward()
  }
}

// Selected before AppController is constructed, only for an explicitly
// instrumented qualification process. Never override shared or force encoding.
final class UIQualificationApplication:NSApplication {
  override func encodeRestorableState(with coder:NSCoder) {
    QualificationApplicationEncodeTrace.measure(.sync,trace:UIWorkTrace.active?.applicationEncodeTrace) {
      super.encodeRestorableState(with:coder)
    }
  }
  override func encodeRestorableState(with coder:NSCoder,backgroundQueue queue:OperationQueue) {
    QualificationApplicationEncodeTrace.measure(.backgroundQueueSynchronous,trace:UIWorkTrace.active?.applicationEncodeTrace) {
      super.encodeRestorableState(with:coder,backgroundQueue:queue)
    }
  }
}

/// Qualification only, main-thread owned. Recording writes fixed-size values to
/// preallocated storage; JSON and sorting happen after the measured workload.
/// These timestamps share CAMetalDisplayLink's monotonic media-time clock.
final class UIWorkTrace {
  enum Phase:String,CaseIterable {
    case tick,snapshot,pluginEdits,drainNotes,positionTimeline,telemetry,midi,mixer
    // Emitted only with --ui-test-runloop-trace. Queue spans deliberately
    // overlap other work: they measure waiting to enter main, not CPU work.
    case snapshotRecoveryQueue,snapshotRecoveryRefresh,snapshotRecoverySkipped
    case patternGraphRefresh,graphTelemetry,graphSignalRead,graphSignalDisplay,workspaceContext,signalScope,graphDraw
    case qualificationTick,qualificationProgress
    case windowLayout
  }
  struct Entry {
    let sequence:Int,phase:Phase,start:Double,end:Double
    var durationMS:Double {(end-start)*1000}
  }
  static let active:UIWorkTrace? = CommandLine.arguments.contains("--ui-test") ? UIWorkTrace():nil
  let runLoopTrace=QualificationRunLoopTrace.enabled ? QualificationRunLoopTrace():nil
  let buttonDrawTrace=QualificationButtonDrawTrace.enabled ? QualificationButtonDrawTrace():nil
  let windowEncodeTrace=QualificationWindowEncodeTrace.enabled ? QualificationWindowEncodeTrace():nil
  let applicationEncodeTrace=QualificationApplicationEncodeTrace.enabled ? QualificationApplicationEncodeTrace():nil
  private var entries:[Entry]
  private var sequence=0
  var overwritten:Int {max(0,sequence-entries.count)}
  init(capacity:Int=131072) {
    entries=Array(repeating:Entry(sequence:0,phase:.tick,start:0,end:0),count:max(1,capacity))
    runLoopTrace?.attach(to:CFRunLoopGetMain())
  }
  func reset(){sequence=0;runLoopTrace?.reset();buttonDrawTrace?.reset();windowEncodeTrace?.reset();applicationEncodeTrace?.reset()}
  func record(_ phase:Phase,start:Double,end:Double) {
    entries[sequence % entries.count]=Entry(sequence:sequence,phase:phase,start:start,end:end)
    sequence+=1
  }
  func finish(_ phase:Phase,start:Double){record(phase,start:start,end:CACurrentMediaTime())}
  func snapshot()->[Entry] {
    (max(0,sequence-entries.count)..<sequence).map{entries[$0 % entries.count]}
  }
  static func measure<T>(_ phase:Phase,_ work:() throws->T) rethrows->T {
    guard let trace=active else{return try work()}
    let start=CACurrentMediaTime();defer{trace.finish(phase,start:start)}
    return try work()
  }
  static func window(contentRect:NSRect,styleMask:NSWindow.StyleMask,backing:NSWindow.BackingStoreType,defer flag:Bool)->NSWindow {
    let window=active == nil ? NSWindow(contentRect:contentRect,styleMask:styleMask,backing:backing,defer:flag):
      UIQualificationWindow(contentRect:contentRect,styleMask:styleMask,backing:backing,defer:flag)
    return window
  }
}

// AppKit defers native control layout beyond application callbacks. Measure
// that work in qualification windows without forcing an extra layout pass or
// changing the window class in an ordinary musician session.
private final class UIQualificationWindow:NSWindow {
  private var stateEncodingDepth=0
  override func layoutIfNeeded(){UIWorkTrace.measure(.windowLayout){super.layoutIfNeeded()}}
  private func measureStateEncoding(_ kind:String,_ work:()->Void){
    guard QualificationWindowEncodeTrace.enabled,let trace=UIWorkTrace.active?.windowEncodeTrace else{work();return}
    let outer=stateEncodingDepth==0,started=CACurrentMediaTime();stateEncodingDepth+=1
    defer{stateEncodingDepth-=1;if outer{let ended=CACurrentMediaTime();trace.record(self,start:started,end:ended,kind:kind)}}
    work()
  }
  override func encodeRestorableState(with coder:NSCoder){measureStateEncoding("sync"){super.encodeRestorableState(with:coder)}}
  override func encodeRestorableState(with coder:NSCoder,backgroundQueue queue:OperationQueue){
    // This spans only the main-thread call; it does not time deferred work.
    measureStateEncoding("backgroundQueue"){super.encodeRestorableState(with:coder,backgroundQueue:queue)}
  }
}
