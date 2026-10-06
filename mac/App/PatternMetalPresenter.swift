import AppKit
import MetalKit

/// AppKit owns geometry; the render run loop owns drawable acquisition and
/// presentation. A single pending snapshot request bounds main-thread work.
/// Snapshot age is measured independently of presentation cadence.
struct PatternRenderSnapshot {
  let bytes:Data,count:Int,size:SIMD2<Float>,clear:MTLClearColor,generation:Int
  let preparedAt:CFTimeInterval
}
struct PatternRenderTiming {
  let timestamp:CFTimeInterval,deadline:CFTimeInterval,snapshotAgeMS:Double
  let generation:Int,starved:Bool
}
// Accessed under the presenter's lock. A queued recovery must recheck the
// latest snapshot: the ordinary UI tick may have refreshed it in the meantime.
struct PatternSnapshotRecovery {
  let interval:Double
  private(set) var pending:UInt64?
  private var serial:UInt64=0
  init(interval:Double){self.interval=interval}
  private func due(now:Double,preparedAt:Double?,external:Bool)->Bool {
    !external || preparedAt == nil || now-preparedAt!>interval
  }
  mutating func enqueue(now:Double,preparedAt:Double?,external:Bool,active:Bool)->UInt64? {
    guard active,pending==nil,due(now:now,preparedAt:preparedAt,external:external) else{return nil}
    serial &+= 1;pending=serial;return serial
  }
  func shouldRefresh(_ token:UInt64,now:Double,preparedAt:Double?,external:Bool,active:Bool)->Bool {
    pending==token && active && due(now:now,preparedAt:preparedAt,external:external)
  }
  mutating func finish(_ token:UInt64){if pending==token{pending=nil}}
  mutating func cancel(){pending=nil}
}
// Qualification-only, bounded correlation of one drawable through its complete
// lifetime. GPU/presentation callbacks can arrive in either order. Keep their
// timestamps together instead of inferring compositor behaviour from percentiles
// of unrelated callback arrays. No serialization or main-thread work occurs here.
final class PatternFrameTrace {
  struct Entry {
    let sequence:Int, generation:Int
    let callback:Double, deadline:Double, presentationTarget:Double, geometryPrepared:Double
    var committed=0.0, scheduled=0.0, gpuStart=0.0, gpuEnd=0.0, completed=0.0, presented=0.0, presentationCallback=0.0
  }
  private let lock=NSLock(),capacity:Int
  private var entries:[Entry?],sequence=0
  init(capacity:Int=8192){self.capacity=max(1,capacity);entries=Array(repeating:nil,count:max(1,capacity))}
  func begin(generation:Int,callback:Double,deadline:Double,presentationTarget:Double,geometryPrepared:Double)->Int {
    lock.lock();defer{lock.unlock()}
    let index=sequence;sequence+=1
    entries[index % capacity]=Entry(sequence:index,generation:generation,callback:callback,deadline:deadline,presentationTarget:presentationTarget,geometryPrepared:geometryPrepared)
    return index
  }
  func update(_ index:Int,_ edit:(inout Entry)->Void) {
    lock.lock();defer{lock.unlock()}
    guard entries[index % capacity]?.sequence==index else{return}
    edit(&entries[index % capacity]!)
  }
  func snapshot(generation:Int)->[Entry] {
    lock.lock();defer{lock.unlock()}
    return entries.compactMap{$0}.filter{$0.generation==generation}.sorted{$0.sequence<$1.sequence}
  }
}
final class PatternMetalPresenter:NSObject,CAMetalDisplayLinkDelegate {
  private let lock=NSLock()
  private var latest:PatternRenderSnapshot?,freeBuffers:[MTLBuffer]
  private var stopped=false,paused=false,externalSnapshots=false
  private var recovery:PatternSnapshotRecovery
  private var runLoop:CFRunLoop?,thread:Thread?
  private var displayLink:CAMetalDisplayLink? // Render-thread owned.
  private let queue:MTLCommandQueue,pipeline:MTLRenderPipelineState,atlas:MTLTexture
  private let request:()->PatternRenderSnapshot?
  private let refreshHost:(()->Void)?
  private let timing:(PatternRenderTiming)->Void
  private let submitted:(Int,Double)->Void,completed:(Int,Double)->Void,presented:(Int,Double)->Void
  let frameTrace=CommandLine.arguments.contains("--ui-test") ? PatternFrameTrace():nil
  let runLoopTrace=QualificationRunLoopTrace.enabled ? QualificationRunLoopTrace():nil
  init(layer:CAMetalLayer,frameRate:Float=60,bufferLength:Int,queue:MTLCommandQueue,pipeline:MTLRenderPipelineState,atlas:MTLTexture,
       snapshot:@escaping()->PatternRenderSnapshot?,refreshHost:(()->Void)?=nil,timing:@escaping(PatternRenderTiming)->Void,
       submitted:@escaping(Int,Double)->Void,completed:@escaping(Int,Double)->Void,presented:@escaping(Int,Double)->Void) {
    // Each presenter owns its pool. A view moving between windows can start
    // another presenter before the old presenter's GPU commands retire.
    self.freeBuffers=(0..<3).map{_ in queue.device.makeBuffer(length:bufferLength,options:.storageModeShared)!}
    self.queue=queue;self.pipeline=pipeline;self.atlas=atlas
    self.request=snapshot;self.refreshHost=refreshHost;self.timing=timing;self.submitted=submitted;self.completed=completed;self.presented=presented
    self.recovery=PatternSnapshotRecovery(interval:1/Double(max(60,min(120,frameRate))))
    super.init()
    latest=snapshot()
    let worker=Thread{[self] in
      autoreleasepool {
        let loop=CFRunLoopGetCurrent()!
        runLoopTrace?.attach(to:loop)
        let keepAlive=Port()
        RunLoop.current.add(keepAlive,forMode:.default)
        let link=CAMetalDisplayLink(metalLayer:layer)
        displayLink=link;link.delegate=self
        let frequency=max(60,min(120,frameRate))
        link.preferredFrameRateRange=CAFrameRateRange(minimum:frequency,maximum:frequency,preferred:frequency)
        link.preferredFrameLatency=2
        link.add(to:.current,forMode:.common)
        lock.lock();runLoop=loop;link.isPaused=paused;let exit=stopped;lock.unlock()
        if !exit {CFRunLoopRun()}
        runLoopTrace?.detach();link.invalidate();displayLink=nil;keepAlive.invalidate()
        lock.lock();runLoop=nil;latest=nil;thread=nil;lock.unlock()
      }
    }
    worker.name="ScreamSeq Metal presentation";worker.qualityOfService = .userInteractive;thread=worker;worker.start()
  }
  func refreshSnapshot() {
    // Called by the main UI tick after its edits/playback positions are current.
    // The display callback's coalesced request remains a fallback for other hosts.
    let next=request()
    lock.lock();externalSnapshots=true;if !stopped{latest=next};lock.unlock()
  }
  func setPaused(_ value:Bool) {
    lock.lock();guard paused != value else{lock.unlock();return};paused=value;if value{recovery.cancel()};let loop=runLoop;lock.unlock()
    if let loop {CFRunLoopPerformBlock(loop,CFRunLoopMode.commonModes.rawValue){[weak self] in
      guard let self else{return};self.lock.lock();let value=self.paused;self.lock.unlock();self.displayLink?.isPaused=value
    };CFRunLoopWakeUp(loop)}
  }
  func stop() {
    lock.lock();stopped=true;latest=nil;recovery.cancel();let loop=runLoop;lock.unlock()
    if let loop {CFRunLoopStop(loop)}
    // In-flight command buffers retain their own immutable resources. There
    // is no main-thread wait on a GPU or a render-thread-to-main dependency.
  }
  func metalDisplayLink(_ link:CAMetalDisplayLink,needsUpdate update:CAMetalDisplayLink.Update) {
    autoreleasepool {
      let now=CACurrentMediaTime()
      lock.lock()
      let active = !stopped && !paused
      let frame=latest
      let buffer=active && frame != nil ? freeBuffers.popLast():nil
      // Recover a delayed UI tick after one cadence, without adding work while
      // its snapshots are fresh. This is coalesced and checked again on main.
      let refreshToken=recovery.enqueue(now:now,preparedAt:frame?.preparedAt,external:externalSnapshots,active:active)
      lock.unlock()
      if let refreshToken {
        // Opt-in diagnostics only. Keep the request's enqueue time distinct
        // from callback time so a stale frame can be attributed to either a
        // late recovery request or delayed servicing of the main queue. Reuse
        // the run-loop diagnostic flag (and its non-clean qualification label).
        let traceRecovery=runLoopTrace != nil
        let queuedAt=traceRecovery ? CACurrentMediaTime():0
        DispatchQueue.main.async{[weak self] in
        guard let self else{return}
        let recoveryTrace=traceRecovery ? UIWorkTrace.active:nil
        let dequeuedAt=recoveryTrace == nil ? 0:CACurrentMediaTime()
        recoveryTrace?.record(.snapshotRecoveryQueue,start:queuedAt,end:dequeuedAt)
        self.lock.lock()
        let external=self.externalSnapshots
        let refresh=self.recovery.shouldRefresh(refreshToken,now:CACurrentMediaTime(),preparedAt:self.latest?.preparedAt,
          external:external,active:!self.stopped && !self.paused)
        self.lock.unlock()
        if refresh {
          // The app must advance telemetry/playhead state before publishing;
          // merely rebuilding geometry would give an old playhead a new age.
          if external,let refreshHost=self.refreshHost{refreshHost()}
          else {
            let next=self.request()
            self.lock.lock();if !self.stopped && !self.paused{self.latest=next};self.lock.unlock()
          }
        }
        self.lock.lock();self.recovery.finish(refreshToken);self.lock.unlock()
        recoveryTrace?.finish(refresh ? .snapshotRecoveryRefresh:.snapshotRecoverySkipped,start:dequeuedAt)
      }}
      guard active,let frame else{return}
      let report=PatternRenderTiming(timestamp:now,deadline:update.targetTimestamp,snapshotAgeMS:(now-frame.preparedAt)*1000,generation:frame.generation,starved:buffer==nil)
      DispatchQueue.main.async{[timing] in timing(report)}
      guard let buffer else{return}
      let release={ [self] in lock.lock();freeBuffers.append(buffer);lock.unlock() }
      guard frame.bytes.count<=buffer.length else{release();return}
      let trace=frameTrace,traceID=trace?.begin(generation:frame.generation,callback:now,deadline:update.targetTimestamp,
        presentationTarget:update.targetPresentationTimestamp,geometryPrepared:frame.preparedAt)
      frame.bytes.withUnsafeBytes{if let p=$0.baseAddress{buffer.contents().copyMemory(from:p,byteCount:$0.count)}}
      let drawable=update.drawable,pass=MTLRenderPassDescriptor()
      pass.colorAttachments[0].texture=drawable.texture;pass.colorAttachments[0].loadAction = .clear
      pass.colorAttachments[0].storeAction = .store;pass.colorAttachments[0].clearColor=frame.clear
      guard let command=queue.makeCommandBuffer(),let encoder=command.makeRenderCommandEncoder(descriptor:pass)else{release();return}
      var size=frame.size
      encoder.setRenderPipelineState(pipeline);encoder.setVertexBuffer(buffer,offset:0,index:0)
      encoder.setVertexBytes(&size,length:MemoryLayout<SIMD2<Float>>.stride,index:1)
      encoder.setFragmentTexture(atlas,index:0);encoder.drawPrimitives(type:.triangle,vertexStart:0,vertexCount:frame.count);encoder.endEncoding()
      let generation=frame.generation,deadline=update.targetTimestamp
      command.addScheduledHandler{[submitted] _ in let time=CACurrentMediaTime();if let traceID{trace?.update(traceID){$0.scheduled=time}};let late=(time-deadline)*1000;DispatchQueue.main.async{submitted(generation,late)}}
      command.addCompletedHandler{[completed] command in
        if let traceID {let time=CACurrentMediaTime();trace?.update(traceID){$0.gpuStart=command.gpuStartTime;$0.gpuEnd=command.gpuEndTime;$0.completed=time}}
        let ms=(command.gpuEndTime-command.gpuStartTime)*1000;release();DispatchQueue.main.async{completed(generation,ms)}
      }
      drawable.addPresentedHandler{[presented] draw in let time=draw.presentedTime;if let traceID {let callback=CACurrentMediaTime();trace?.update(traceID){$0.presented=time;$0.presentationCallback=callback}};DispatchQueue.main.async{presented(generation,time)}}
      // CAMetalDisplayLink assigns the drawable's presentation time. The
      // explicit atTime/afterMinimumDuration variants are invalid for it.
      command.present(drawable);command.commit()
      if let traceID {let time=CACurrentMediaTime();trace?.update(traceID){$0.committed=time}}
    }
  }
}
