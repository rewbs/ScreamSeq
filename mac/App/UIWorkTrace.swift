import AppKit
import QuartzCore

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

/// Qualification only, main-thread owned. Recording writes fixed-size values to
/// preallocated storage; JSON and sorting happen after the measured workload.
/// These timestamps share CAMetalDisplayLink's monotonic media-time clock.
final class UIWorkTrace {
  enum Phase:String,CaseIterable {
    case tick,snapshot,pluginEdits,drainNotes,positionTimeline,telemetry,midi,mixer
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
  private var entries:[Entry]
  private var sequence=0
  var overwritten:Int {max(0,sequence-entries.count)}
  init(capacity:Int=131072) {
    entries=Array(repeating:Entry(sequence:0,phase:.tick,start:0,end:0),count:max(1,capacity))
    runLoopTrace?.attach(to:CFRunLoopGetMain())
  }
  func reset(){sequence=0;runLoopTrace?.reset()}
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
    guard active != nil else{return NSWindow(contentRect:contentRect,styleMask:styleMask,backing:backing,defer:flag)}
    return UIQualificationWindow(contentRect:contentRect,styleMask:styleMask,backing:backing,defer:flag)
  }
}

// AppKit defers native control layout beyond application callbacks. Measure
// that work in qualification windows without forcing an extra layout pass or
// changing the window class in an ordinary musician session.
private final class UIQualificationWindow:NSWindow {
  override func layoutIfNeeded(){UIWorkTrace.measure(.windowLayout){super.layoutIfNeeded()}}
}
