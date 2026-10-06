import AppKit
import CoreText
import MetalKit

private struct Vertex {
  var position: SIMD2<Float>
  var uv: SIMD2<Float>
  var color: SIMD4<Float>
}
enum PatternPositionMode: String, CaseIterable {
  case rows, beats, patternTime, songTime
  var title:String {switch self {case .rows:return "ROW";case .beats:return "BEAT";case .patternTime:return "PAT TIME";case .songTime:return "SONG TIME"}}
  var width:Float {self == .rows ? 52 : self == .beats ? 92 : 116}
}
private final class PatternRulerAccessibility: NSAccessibilityElement {
  weak var owner:PatternView?
  override func accessibilityPerformPress()->Bool {owner?.cyclePositionMode();return owner != nil}
}
final class PatternView: MTKView, MTKViewDelegate {
  var model = PatternModel([:]) {
    didSet {
      model.rebuildEffectLayout()
      if let draft=inlineDraftLayout {model.includeEffectFields(channel:draft.channel,fx:draft.fx,fields:draft.fields)}
      rebuildInlineText()
      if oldValue.revisionToken != model.revisionToken { clearEffectPrefix() }
      if oldValue.pattern != model.pattern || oldValue.rows != model.rows || oldValue.channels != model.channels
        || oldValue.revisionToken.split(separator: ":").first != model.revisionToken.split(separator: ":").first
      { discardDeferredKeys() }
      rebuildPositionLabels()
      muted = model.mutedColumns
      if oldValue.pattern != model.pattern || oldValue.rows != model.rows
        || oldValue.channels != model.channels
      {
        selectionStart = nil
        selectionEnd = nil
      }
      cursorRow = min(cursorRow, max(0, model.rows - 1))
      cursorChannel = min(cursorChannel, max(0, model.channels - 1))
      firstRow = min(firstRow, max(0, model.rows - 1))
      firstChannel = min(firstChannel, max(0, model.channels - 1))
      column = min(column,model.lastField(cursorChannel))
      horizontalInset = min(horizontalInset,max(0,model.channelWidth(firstChannel)-1))
    }
  }
  var positionMode = PatternPositionMode.rows { didSet { rebuildPositionLabels();onPositionMode?() } }
  var onPositionMode:(()->Void)?
  var positionTimes=[[String:Any]]() { didSet { rebuildPositionLabels() } }
  private(set) var positionLabels=[String]()
  var gutterWidth:Float { positionMode.width }
  func cyclePositionMode() { let all=PatternPositionMode.allCases;positionMode=all[(all.firstIndex(of:positionMode)!+1)%all.count] }
  private func rebuildPositionLabels() {
    positionLabels=(0..<max(0,model.rows)).map { row in
      switch positionMode {
      case .rows:return String(format:"%03d",row)
      case .beats:return String(format:"%.3f",Double(row)/Double(max(1,model.rowsPerBeat)))
      case .patternTime,.songTime:
        guard positionTimes.indices.contains(row),let value=positionTimes[row][positionMode == .songTime ? "songSeconds" : "patternSeconds"] as? Double,value.isFinite,value>=0 else{return "--:--.---"}
        let millis=Int(min(value*1000,Double(Int.max/2)).rounded())
        return String(format:"%02d:%02d.%03d",millis/60000,(millis/1000)%60,millis%1000)
      }
    }
  }
  var cursorRow = 0, cursorChannel = 0, step = 1
  var column = 0 {didSet {if oldValue != column {parameterIndex=0}}}
  var parameterIndex=0
  var timingUnit=PatternTimingUnit(rawValue:UserDefaults.standard.string(forKey:"patternTimingUnit") ?? "") ?? .beats {didSet {rebuildInlineText()}}
  private var inlineText=[Int:[String]]()
  private func rebuildInlineText() {
    inlineText=[:]
    for (key,command) in model.performanceCommands {
      if let schema=model.commands.schema(kind:command.kind,native:command.native) {inlineText[key]=schema.inlineFields.map{$0.text($0.value(command),model:model,timing:timingUnit,rateMode:command.parameters["rateMode"] as? String)}}
    }
  }
  func setTimingUnit(_ unit:PatternTimingUnit) {
    guard nudgeEditor == nil else {onMessage?("Save or cancel the current value before changing timing units.");return}
    timingUnit=unit;UserDefaults.standard.set(unit.rawValue,forKey:"patternTimingUnit");onCursor?()
  }
  var octave = 4 { didSet { if octave != oldValue { onInputChanged?() } } }
  var instrument = 1 { didSet { if instrument != oldValue { onInputChanged?() } } }
  var onInputChanged:(()->Void)?
  var firstRow = 0, firstChannel = 0, playRow = -1, playPattern = -1
  var isFollowing = true { didSet { if oldValue != isFollowing { onFollowChanged?(isFollowing) } } }
  var onFollowChanged: ((Bool) -> Void)?
  var navigation: EditorNavigation {
    EditorNavigation(pattern: model.pattern, row: cursorRow, channel: cursorChannel, column: column, following: isFollowing)
  }
  var contextToken: String {
    let region = automationSelection
    return navigation.token + ":" + ["startRow","endRow","startChannel","endChannel"].map { String(region[$0] ?? 0) }.joined(separator: ":") + ":\(instrument):\(octave):\(step):\(parameterFields().indices.contains(selectedParameterIndex) && column>=4 && column%2==0 ? parameterFields()[selectedParameterIndex].key : "")"
  }
  func navigate(_ state: EditorNavigation, clearSelection: Bool) {
    if clearSelection { selectionStart = nil; selectionEnd = nil }
    // Stored positions can outlive a pattern that has since shrunk.
    discardDeferredKeys()
    cursorRow = max(0, min(model.rows - 1, state.row))
    cursorChannel = max(0, min(model.channels - 1, state.channel))
    column = max(0, min(model.lastField(cursorChannel), state.column))
    isFollowing = state.following; revealCursor()
  }
  typealias Edits = [(Int, Int, [UInt8])]
  var onTransform: ((@escaping (PatternModel) -> Edits) -> Void)?
  var canEdit: (() -> Bool) = { true }
  var onMessage: ((String) -> Void)?
  var onPaste: (([String: Any]) -> Void)?
  var onRowShift: (([String: Any]) -> Void)?
  var commandRevision: (() -> String)?
  var onEdit: ((Int, Int, [UInt8]) -> Void)?
  var onPreciseNotes: (() -> Void)?
  var onClearPreciseNotes: ((Int,Int)->Void)?
  var onNativeEffect: (() -> Void)?
  var nudgeEditor: PatternNudgeEditor?
  var inlineDraftLayout:(channel:Int,fx:Int,fields:[PatternEffectField])?
  var onNudgeRequest: (([String:Any], @escaping ([String:Any])->Void)->Void)?
  var onTrackerEffect: ((Int,Int,Int,Int,Int)->Void)?
  // Preserve rapid typing while an FX transaction refreshes the displayed model,
  // or while an editable document is briefly busy (read-only request, recovery
  // autosave). Replay in order so each key sees the previous key's result.
  var deferringEffectKeys = false
  static let deferredKeyLimit = 64
  private struct DeferredKey { let event: NSEvent; var released = false }
  private var deferredKeys = [DeferredKey]()
  private var replayingReleasedKey = false
  var deferredKeyCount: Int { deferredKeys.count }
  func discardDeferredKeys() { deferredKeys.removeAll() }
  // Editable but temporarily unable to accept edits: typing waits instead of being lost.
  private var waitingForDocument: Bool { model.editable && model.rows > 0 && model.channels > 0 && !canEdit() }
  private func deferKey(_ event: NSEvent) {
    if event.keyCode == 53 { discardDeferredKeys(); clearEffectPrefix(); return }
    if deferredKeys.count < Self.deferredKeyLimit { deferredKeys.append(DeferredKey(event: event)) } else { NSSound.beep() }
  }
  func finishEffectKeys(success:Bool) {
    deferringEffectKeys=false
    if !success {discardDeferredKeys();return}
    replayDeferredKeys()
  }
  // Called on the main thread when the document stops being busy.
  func replayDeferredKeys() {
    dispatchPrecondition(condition: .onQueue(.main))
    while !deferringEffectKeys && !deferredKeys.isEmpty && !waitingForDocument {
      let key=deferredKeys.removeFirst()
      // The key-up of a replayed key may already have happened: never start
      // an audition that nothing would release.
      replayingReleasedKey=key.released
      keyDown(with:key.event)
      replayingReleasedKey=false
    }
  }
  var onTypedNativeEffect: ((String) -> Void)?
  var onScratchPhrase:(()->Void)?
  private(set) var effectPrefix = ""
  private var effectPrefixTarget: EditorNavigation?
  private func clearEffectPrefix() { if !effectPrefix.isEmpty {onMessage?("")};effectPrefix="";effectPrefixTarget=nil }
  private func finishEffectPrefix() {
    let prefix=effectPrefix,target=effectPrefixTarget;clearEffectPrefix()
    guard let target,target.pattern==model.pattern,let index=model.effectLetters.firstIndex(where:{$0.uppercased()==prefix}),canEdit() else{return}
    let fx=max(0,(target.column-3)/2),existing=model.nativeCommand(target.row,target.channel,max(0,(target.column-3)/2));onTrackerEffect?(target.row,target.channel,fx,index,existing?.kind=="tracker" ? existing!.parameter : 0)
  }
  private func typeEffectCode(_ event:NSEvent) -> Bool {
    guard column>=3 && column%2==1,KeyboardSettings.isDataTyping(event) else { finishEffectPrefix();return false }
    if !effectPrefix.isEmpty && effectPrefixTarget != navigation {clearEffectPrefix()}
    if event.keyCode==53 {clearEffectPrefix();return true}
    let key=(event.charactersIgnoringModifiers ?? "").uppercased()
    let entries=model.commands.effects + model.commands.native
    if !effectPrefix.isEmpty {
      if let entry=entries.first(where:{$0.displayCode==effectPrefix+key}) {
        clearEffectPrefix()
        if entry.nativeName=="scratch",model.scratchGestures.isEmpty {onScratchPhrase?();return true}
        if let kind=entry.nativeKind {if !beginNudgeEdit(kind:kind,native:entry.nativeName) {onTypedNativeEffect?(kind)}}
        else {let old=model.nativeCommand(cursorRow,cursorChannel,effectColumn);onTrackerEffect?(cursorRow,cursorChannel,effectColumn,entry.command,((old?.kind=="tracker" ? old!.parameter : 0) & ~entry.mask) | entry.value);column += 1;revealCursor()}
        return true
      }
      if event.keyCode==51 || event.keyCode==117 {clearEffectPrefix();return true}
      finishEffectPrefix()
    }
    guard key.count==1,entries.contains(where:{$0.displayCode.count==2 && $0.displayCode.hasPrefix(key)}) else{return false}
    effectPrefix=key;effectPrefixTarget=navigation
    onMessage?("\(key)… · type the second command character, or move to keep the single-letter command · Esc cancels")
    return true
  }
  var onEffectPicker: (() -> Void)?
  var onEffectColumns: ((Int,Int) -> Void)?
  var onContextMenu: ((NSEvent) -> Void)?
  var cursorRect: NSRect { NSRect(x:CGFloat(channelX(cursorChannel)+fieldOffset(column)), y:CGFloat(headerHeight+Float(cursorRow-firstRow)*rowHeight),width:CGFloat(fieldWidth(column)),height:CGFloat(rowHeight)) }
  override func rightMouseDown(with event: NSEvent) {
    let p=convert(event.locationInWindow,from:nil), (r,c)=position(event)
    discardDeferredKeys()
    if !selected(r,c) {selectionStart=nil;selectionEnd=nil;cursorRow=r;cursorChannel=c
      let x=Float(p.x)-channelX(c)
      column=fieldAt(x,c)
      parameterIndex=parameterAt(x,c,r,effectColumn)
    };onCursor?();onContextMenu?(event)
  }
  var onClearNativeEffect: ((Int,Int,Int) -> Void)?
  var onTransport: (() -> Void)?
  var onCursor: (() -> Void)?
  var onUndo: (() -> Void)?, onRedo: (() -> Void)?
  var onAudition: ((Int, Int, Int, Bool) -> Void)?
  private var heldKeys = [UInt16: (note:Int,instrument:Int,channel:Int)]()
  var onMute: ((Int) -> Void)?
  var muted = Set<Int>()
  var headerHeight: Float { model.noteTracks.isEmpty ? 36 : 58 }
  private var metalPresenter: PatternMetalPresenter?
  var renderingPaused = false {
    didSet { metalPresenter?.setPaused(renderingPaused) }
  }
  private var commandQueue: MTLCommandQueue!
  private var pipeline: MTLRenderPipelineState!
  private var atlas: MTLTexture!
  private let vertexBufferBytes=16*1024*1024
  private var vertices = [Vertex]()
  private var selectionStart: (Int, Int)?
  private var selectionEnd: (Int, Int)?
  var playbackSelection: Range<Int>? {
    guard let a=selectionStart, let b=selectionEnd else { return nil }
    return min(a.0,b.0)..<(max(a.0,b.0)+1)
  }
  var automationSelection: [String: Int] {
    let a = selectionStart ?? (cursorRow, cursorChannel)
    let b = selectionEnd ?? a
    return [
      "startRow": min(a.0, b.0), "endRow": max(a.0, b.0),
      "startChannel": min(a.1, b.1), "endChannel": max(a.1, b.1),
    ]
  }
  private var copying = false
  private let clipboardWorker = DispatchQueue(label: "org.resonance.clipboard", qos: .userInitiated)
  var frameCount: UInt64 = 0
  var submittedFrames: UInt64 = 0
  var presentIntervals = [Double]()
  var presentationTimestamps = [Double]()
  var presentationCallbacks = 0, unpresentedDrawables = 0, outOfOrderPresentations = 0
  var cpuTimes = [Double](), gpuTimes = [Double]()
  var mainThreadTimes = [Double](), drawableWaitTimes = [Double]()
  var displayLinkLeadTimes=[Double](),renderQueueWaitTimes=[Double](),submissionLateness=[Double]()
  var displayLinkIntervals=[Double](),snapshotAges=[Double](),displayLinkCallbacks=0,bufferStarvations=0
  private var lastDisplayLinkTime=0.0
  private var metricsGeneration = 0
  private var lastPresentTime = 0.0
  var p99PresentMS: Double {
    percentile(presentIntervals, 0.99)
  }
  func percentile(_ values: [Double], _ fraction: Double) -> Double {
    let sorted = values.sorted()
    return sorted.isEmpty
      ? 0 : sorted[min(sorted.count - 1, Int(Double(sorted.count - 1) * fraction))]
  }
  func resetMetrics() {
    metricsGeneration += 1
    frameCount = 0
    submittedFrames = 0
    lastPresentTime = 0
    presentIntervals.removeAll(keepingCapacity: true)
    presentationTimestamps.removeAll(keepingCapacity: true)
    presentationCallbacks = 0; unpresentedDrawables = 0; outOfOrderPresentations = 0
    cpuTimes.removeAll(keepingCapacity: true)
    gpuTimes.removeAll(keepingCapacity: true)
    mainThreadTimes.removeAll(keepingCapacity: true)
    drawableWaitTimes.removeAll(keepingCapacity: true)
    displayLinkLeadTimes.removeAll(keepingCapacity:true);renderQueueWaitTimes.removeAll(keepingCapacity:true);submissionLateness.removeAll(keepingCapacity:true)
    displayLinkIntervals.removeAll(keepingCapacity:true);snapshotAges.removeAll(keepingCapacity:true);displayLinkCallbacks=0;bufferStarvations=0;lastDisplayLinkTime=0
    maxFrameMS = 0
    maxGPUMS = 0
  }
  func resetPresentationClock() { lastPresentTime = 0 }
  var maxFrameMS: Double = 0
  var maxGPUMS: Double = 0
  var rowHeight: Float = KeyboardSettings.rowHeight
  private var horizontalInset: Float = 0
  private var clipLeft: Float = 0
  private var clipRight:Float = .greatestFiniteMagnitude
  func channelX(_ channel: Int) -> Float { gutterWidth + model.channelOffset(channel) - model.channelOffset(firstChannel) - horizontalInset }
  func visibleChannelCount() -> Int {
    var last=firstChannel
    while last < model.channels && channelX(last) < Float(bounds.width) { last += 1 }
    return max(0,last-firstChannel)
  }
  private func normalizeHorizontalScroll() {
    while horizontalInset >= model.channelWidth(firstChannel),firstChannel < model.channels-1 { horizontalInset -= model.channelWidth(firstChannel);firstChannel += 1 }
    while horizontalInset < 0,firstChannel > 0 { firstChannel -= 1;horizontalInset += model.channelWidth(firstChannel) }
    horizontalInset=max(0,min(horizontalInset,model.channelWidth(firstChannel)-1))
  }
  var effectColumn:Int {max(0,(column-3)/2)}
  func parameterFields(row:Int?=nil,channel:Int?=nil,fx:Int?=nil)->[PatternEffectField] {
    if let editor=nudgeEditor,editor.target.row==(row ?? cursorRow),editor.target.channel==(channel ?? cursorChannel),editor.fx==(fx ?? effectColumn) {return editor.descriptors}
    guard let command=model.nativeCommand(row ?? cursorRow,channel ?? cursorChannel,fx ?? effectColumn) else{return []}
    return model.commands.schema(kind:command.kind,native:command.native)?.inlineFields ?? []
  }
  var selectedParameterIndex:Int {max(0,min(parameterIndex,parameterFields().count-1))}
  func fieldOffset(_ column:Int,channel:Int?=nil,row:Int?=nil,slot:Int?=nil)->Float {
    if column<3 {return [7,44,72][max(0,column)]}
    let ch=channel ?? cursorChannel,fx=max(0,(column-3)/2)
    return model.effectOffset(ch,fx)+(column%2==1 ? 5 : model.effectLayout(ch,fx).slotOffset(slot ?? (channel==nil ? selectedParameterIndex : 0))+3)
  }
  func fieldAt(_ x:Float,_ channel:Int)->Int {
    if x<104 {return x<43 ? 0 : x<70 ? 1 : 2}
    var fx=0
    while fx<model.effectCount(channel)-1 && x>=model.effectOffset(channel,fx+1) {fx += 1}
    return 3+fx*2+(x-model.effectOffset(channel,fx)>=30 ? 1 : 0)
  }
  func parameterAt(_ x:Float,_ channel:Int,_ row:Int,_ fx:Int)->Int {
    let layout=model.effectLayout(channel,fx),count=max(1,parameterFields(row:row,channel:channel,fx:fx).count)
    var slot=0
    while slot<count-1 && x-model.effectOffset(channel,fx)>=layout.slotOffset(slot+1) {slot += 1}
    return slot
  }
  func fieldWidth(_ column:Int,channel:Int?=nil,slot:Int?=nil)->Float {
    if column<3 {return [32,24,28][max(0,column)]}
    if column%2==1 {return 23}
    let layout=model.effectLayout(channel ?? cursorChannel,max(0,(column-3)/2))
    let index=max(0,min(slot ?? selectedParameterIndex,layout.slotWidths.count-1))
    return layout.slotWidths[index]-6
  }
  func channelHeaderLabel(_ channel:Int)->String {
    let number=Self.decimal[channel+1]
    let name=channel<model.tracks.count ? model.tracks[channel]["name"] as? String ?? "" : ""
    return name.isEmpty ? (model.noteTrackByChannel[channel]==nil ? "CH "+number : "N"+number) : number+" "+String(name.prefix(8)).uppercased()
  }
  private let notes = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
  private let normal = SIMD4<Float>(0.66, 0.72, 0.79, 1), faint = SIMD4<Float>(0.22, 0.28, 0.34, 1)
  override var acceptsFirstResponder: Bool { true }
  override var isFlipped: Bool { true }
  init() {
    super.init(frame: .zero, device: MTLCreateSystemDefaultDevice())
    guard let device else { fatalError("Metal is required") }
    colorPixelFormat = .bgra8Unorm
    clearColor = MTLClearColorMake(0.055, 0.069, 0.088, 1)
    preferredFramesPerSecond = 60
    isPaused = true // CAMetalDisplayLink owns pacing and drawable presentation deadlines.
    enableSetNeedsDisplay = false
    autoResizeDrawable = false // The custom presenter publishes backing-pixel dimensions.
    framebufferOnly = true
    delegate = self
    commandQueue = device.makeCommandQueue()
    let shader = """
      #include <metal_stdlib>
      using namespace metal;
      struct V { float2 p; float2 uv; float4 color; };
      struct O { float4 p [[position]]; float2 uv; float4 color; };
      vertex O vertexMain(const device V *v [[buffer(0)]],constant float2 &size [[buffer(1)]],uint id [[vertex_id]]) {
          O o; o.p=float4(v[id].p.x/size.x*2-1,1-v[id].p.y/size.y*2,0,1);o.uv=v[id].uv;o.color=v[id].color;return o;
      }
      fragment float4 fragmentMain(O in [[stage_in]],texture2d<float> tex [[texture(0)]]) {
          constexpr sampler s(filter::linear);float a=in.uv.x<0?1:tex.sample(s,in.uv).a;return float4(in.color.rgb,in.color.a*a);
      }
      """
    do {
      let library = try device.makeLibrary(source: shader, options: nil)
      let p = MTLRenderPipelineDescriptor()
      p.vertexFunction = library.makeFunction(name: "vertexMain")
      p.fragmentFunction = library.makeFunction(name: "fragmentMain")
      p.colorAttachments[0].pixelFormat = colorPixelFormat
      p.colorAttachments[0].isBlendingEnabled = true
      p.colorAttachments[0].sourceRGBBlendFactor = .sourceAlpha
      p.colorAttachments[0].destinationRGBBlendFactor = .oneMinusSourceAlpha
      p.colorAttachments[0].sourceAlphaBlendFactor = .one
      p.colorAttachments[0].destinationAlphaBlendFactor = .oneMinusSourceAlpha
      pipeline = try device.makeRenderPipelineState(descriptor: p)
    } catch { fatalError("Metal pipeline: \(error)") }
    vertices.reserveCapacity(200000)
    createAtlas()
    setAccessibilityElement(true)
    setAccessibilityRole(.table)
    setAccessibilityLabel("Pattern editor")
    setAccessibilityHelp(
      "Arrow keys navigate. Z through M and Q through U enter notes. Tab changes channel. Space starts or stops playback."
    )
  }
  required init(coder: NSCoder) { fatalError() }
  deinit { metalPresenter?.stop() }
  var onNeedsRefresh:(()->Void)?
  func refreshForPresentation(){if let onNeedsRefresh{onNeedsRefresh()}else{refreshRenderSnapshot()}}
  func refreshRenderSnapshot(){metalPresenter?.refreshSnapshot()}
  var qualificationFrameTrace:[PatternFrameTrace.Entry] {metalPresenter?.frameTrace?.snapshot(generation:metricsGeneration) ?? []}
  var qualificationRunLoopTrace:QualificationRunLoopTrace? {metalPresenter?.runLoopTrace}
  func shutdownRendering(){metalPresenter?.stop();metalPresenter=nil}
  override func viewDidMoveToWindow() {
    super.viewDidMoveToWindow()
    if window == nil {metalPresenter?.stop();metalPresenter=nil;return}
    wantsLayer=true
    if metalPresenter==nil,let metalLayer=layer as? CAMetalLayer {
      metalLayer.presentsWithTransaction=false
      metalPresenter=PatternMetalPresenter(layer:metalLayer,frameRate:60,bufferLength:vertexBufferBytes,queue:commandQueue,pipeline:pipeline,atlas:atlas,
        snapshot:{[weak self] in self?.prepareRenderSnapshot()},
        refreshHost:{[weak self] in self?.refreshForPresentation()},
        timing:{[weak self] value in
          guard let self,value.generation==self.metricsGeneration else{return}
          self.displayLinkCallbacks+=1;if value.starved{self.bufferStarvations+=1}
          if self.frameCount>120,self.displayLinkLeadTimes.count<216000 {
            self.displayLinkLeadTimes.append((value.deadline-value.timestamp)*1000)
            self.snapshotAges.append(value.snapshotAgeMS)
            if self.lastDisplayLinkTime>0{self.displayLinkIntervals.append((value.timestamp-self.lastDisplayLinkTime)*1000)}
          }
          self.lastDisplayLinkTime=value.timestamp
        },submitted:{[weak self] generation,lateness in
          guard let self,generation==self.metricsGeneration else{return};self.submittedFrames+=1
          if self.frameCount>120,self.submissionLateness.count<216000{self.submissionLateness.append(lateness)}
        },completed:{[weak self] generation,ms in
          guard let self,generation==self.metricsGeneration else{return};self.maxGPUMS=max(self.maxGPUMS,ms)
          if self.frameCount>120,self.gpuTimes.count<216000{self.gpuTimes.append(ms)}
        },presented:{[weak self] generation,timestamp in
          guard let self,generation==self.metricsGeneration else{return}
          self.presentationCallbacks+=1
          if timestamp<=0{self.unpresentedDrawables+=1;return}
          if self.presentationTimestamps.count<216000{self.presentationTimestamps.append(timestamp)}
          if timestamp<=self.lastPresentTime{self.outOfOrderPresentations+=1;return}
          if self.lastPresentTime>0,self.frameCount>120,self.presentIntervals.count<216000{self.presentIntervals.append((timestamp-self.lastPresentTime)*1000)}
          self.lastPresentTime=timestamp;self.frameCount+=1
        })
    }
    metalPresenter?.setPaused(renderingPaused)
  }
  override func setFrameSize(_ newSize:NSSize) {
    super.setFrameSize(newSize);synchronizeDrawableSize()
  }
  override func layout() {
    super.layout();synchronizeDrawableSize()
  }
  override func viewDidChangeBackingProperties() {
    super.viewDidChangeBackingProperties();synchronizeDrawableSize()
  }
  private func synchronizeDrawableSize() {
    // MTKView's own draw loop normally resizes its drawable. It is paused
    // because our display link owns presentation, so publish backing pixels
    // explicitly after layout (including the initial zero-sized attachment).
    let size=convertToBacking(bounds).size
    guard size.width>0,size.height>0 else{return}
    if drawableSize != size {drawableSize=size}
    // MTKView caches drawableSize even before publishing it to CAMetalLayer.
    // With no draw(in:) callback, that deferred update must be explicit too.
    if let metalLayer=layer as? CAMetalLayer,metalLayer.drawableSize != size {metalLayer.drawableSize=size}
  }
  private func createAtlas() {
    let width = 512
    let height = 256
    let context = CGContext(
      data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
      space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    context.setAllowsAntialiasing(true)
    let font = CTFontCreateWithName("SFMono-Regular" as CFString, 25, nil)
    for i in 32...126 {
      let x = ((i - 32) % 16) * 32
      let y = ((i - 32) / 16) * 40
      let attributes =
        [kCTFontAttributeName: font, kCTForegroundColorAttributeName: NSColor.white.cgColor]
        as [CFString: Any]
      let line = CTLineCreateWithAttributedString(
        NSAttributedString(
          string: String(UnicodeScalar(i)!), attributes: attributes as [NSAttributedString.Key: Any]
        ))
      context.textPosition = CGPoint(x: x + 1, y: height - y - 30)
      CTLineDraw(line, context)
    }
    let descriptor = MTLTextureDescriptor.texture2DDescriptor(
      pixelFormat: .rgba8Unorm, width: width, height: height, mipmapped: false)
    atlas = device!.makeTexture(descriptor: descriptor)
    atlas.replace(
      region: MTLRegionMake2D(0, 0, width, height), mipmapLevel: 0, withBytes: context.data!,
      bytesPerRow: width * 4)
  }
  private func quad(
    _ x: Float, _ y: Float, _ w: Float, _ h: Float, _ color: SIMD4<Float>,
    _ uv0: SIMD2<Float> = SIMD2(-1, -1), _ uv1: SIMD2<Float> = SIMD2(-1, -1)
  ) {
    guard w>0,x+w>clipLeft,x<min(Float(bounds.width),clipRight) else{return}
    let left=max(x,clipLeft),right=min(x+w,min(Float(bounds.width),clipRight))
    let u0=uv0.x < 0 ? uv0.x : uv0.x+(uv1.x-uv0.x)*(left-x)/w
    let u1=uv1.x < 0 ? uv1.x : uv0.x+(uv1.x-uv0.x)*(right-x)/w
    let x=left,w=right-left,uv0=SIMD2(u0,uv0.y),uv1=SIMD2(u1,uv1.y)
    let a = Vertex(position: SIMD2(x, y), uv: uv0, color: color)
    let b = Vertex(position: SIMD2(x + w, y), uv: SIMD2(uv1.x, uv0.y), color: color)
    let c = Vertex(position: SIMD2(x, y + h), uv: SIMD2(uv0.x, uv1.y), color: color)
    let d = Vertex(position: SIMD2(x + w, y + h), uv: uv1, color: color)
    vertices.append(a)
    vertices.append(b)
    vertices.append(c)
    vertices.append(b)
    vertices.append(d)
    vertices.append(c)
  }
  private func text(_ str: String, _ x: Float, _ y: Float, _ color: SIMD4<Float>) {
    for (j, char) in str.utf8.enumerated() where char >= 32 && char <= 126 {
      let i = Int(char) - 32
      let u = Float((i % 16) * 32) / 512
      let v = Float((i / 16) * 40) / 256
      quad(x + Float(j) * 9, y, 16, 20, color, SIMD2(u, v), SIMD2(u + 32.0 / 512, v + 40.0 / 256))
    }
  }
  private static let hexadecimal = (0...255).map { String(format: "%02X", $0) }
  private static let decimal = (0...255).map { String(format: "%02d", $0) }
  private static let rowNumbers = (0...4095).map { String(format: "%03d", $0) }
  private static let noteNames: [String] = (0...255).map { note in
    let names = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
    if note == 0 { return "..." }
    if note == 255 { return "===" }
    if note == 254 { return "^^^" }
    if note > 128 { return "~~~" }
    return names[(note - 1) % 12] + String((note - 1) / 12)
  }
  func draw(in view: MTKView) {} // MTKView supplies sizing, not a second rendering timer.
  private func prepareGeometry() {
    vertices.removeAll(keepingCapacity: true)
    let width = Float(bounds.width)
    let height = Float(bounds.height)
    quad(0, 0, width, headerHeight, SIMD4(0.09, 0.11, 0.14, 1))
    text(positionMode.title, 9, headerHeight - 28, SIMD4(0.42, 0.48, 0.55, 1))
    let visibleChannels = visibleChannelCount()
    clipLeft=gutterWidth
    let visibleRows = Int(ceil(max(0, height - headerHeight) / rowHeight))
    for track in model.noteTracks {
      guard let first = track.channels.first, let last = track.channels.last else { continue }
      let left = max(first, firstChannel), right = min(last + 1, firstChannel + visibleChannels)
      guard left < right else { continue }
      let x = channelX(left)
      let span = model.channelOffset(right)-model.channelOffset(left)
      quad(x, 0, span, 22, SIMD4(0.14, 0.23, 0.28, 1))
      text(String(track.name.prefix(max(1, Int(span / 9) - 2))).uppercased(), x + 9, 0, SIMD4(0.54, 0.87, 0.78, 1))
    }
    for v in 0..<max(0, visibleChannels) {
      let ch = firstChannel + v
      let x = channelX(ch)
      clipRight=x+model.effectOffset(ch,0)-4
      text(
        channelHeaderLabel(ch), x + 9, headerHeight - 28,
        muted.contains(ch) ? SIMD4(0.38, 0.41, 0.45, 1) : SIMD4(0.66, 0.73, 0.79, 1))
      clipRight = .greatestFiniteMagnitude
      text("+FX",x+model.channelWidth(ch)-30,headerHeight-28,SIMD4(0.43,0.88,0.76,1))
      for effect in 0..<model.effectCount(ch) {
        let fx=x+model.effectOffset(ch,effect)
        text("FX \(effect+1)",fx+7,headerHeight-28,SIMD4(0.59,0.63,0.78,1))
      }
    }
    for v in 0..<visibleRows {
      let r = firstRow + v
      if r >= model.rows { break }
      let y = headerHeight + Float(v) * rowHeight
      clipLeft=0
      if r % model.rowsPerMeasure == 0 {
        quad(0, y, width, rowHeight, SIMD4(0.095, 0.12, 0.15, 1))
      } else if r % model.rowsPerBeat == 0 {
        quad(0, y, width, rowHeight, SIMD4(0.074, 0.091, 0.115, 1))
      }
      if r == playRow && model.pattern == playPattern {
        quad(0, y, width, rowHeight, SIMD4(0.13, 0.29, 0.25, 1))
        quad(0, y, 3, rowHeight, SIMD4(0.37, 0.88, 0.72, 1))
      }
      text(
        positionLabels.indices.contains(r) ? positionLabels[r] : String(r), 9, y + 1,
        r % model.rowsPerBeat == 0 ? SIMD4(0.46, 0.55, 0.62, 1) : SIMD4(0.28, 0.35, 0.41, 1))
      clipLeft=gutterWidth
      for v in 0..<max(0, visibleChannels) {
        let ch = firstChannel + v
        let x = channelX(ch)
        if selected(r, ch) {
          quad(x + 1, y, model.channelWidth(ch) - 1, rowHeight, SIMD4(0.18, 0.25, 0.34, 0.9))
        }
        if r == cursorRow && ch == cursorChannel {
          let offset=fieldOffset(column), size=fieldWidth(column)
          quad(
            x + offset - 2, y + 1, size, rowHeight - 2, SIMD4(0.22, 0.39, 0.37, 1)
          )
          quad(
            x + offset - 2, y + rowHeight - 2, size, 1, SIMD4(0.43, 0.94, 0.78, 1)
          )
        }
        let cell = model.drawCell(r, ch)
        let precise=model.notes(r,ch)
        let preciseOnset=precise.first(where:{$0.note<128}) ?? precise.first
        let note = cell.note==0 ? (preciseOnset?.note ?? 0) : Int(cell.note)
        let shownInstrument=cell.instrument==0 ? (preciseOnset?.instrument ?? 0) : Int(cell.instrument)
        if !precise.isEmpty {text("~",x+35,y+1,SIMD4(0.77,0.57,0.96,1))}
        let noteText = Self.noteNames[note]
        text(noteText, x + 7, y + 1, note == 0 ? faint : SIMD4(0.43, 0.88, 0.76, 1))
        text(
          shownInstrument == 0 ? ".." : Self.hexadecimal[shownInstrument], x + 44, y + 1,
          shownInstrument == 0 ? faint : SIMD4(0.7, 0.76, 0.9, 1))
        text(
          cell.volumeCommand == 0
            ? "..."
            : (Int(cell.volumeCommand) < model.volumeLetters.count
              ? model.volumeLetters[Int(cell.volumeCommand)] : "?")
              + Self.decimal[Int(cell.volume)], x + 72, y + 1,
          cell.volumeCommand == 0 ? faint : model.commands.entry(command: Int(cell.volumeCommand), parameter: Int(cell.volume), volume: true)?.rgba ?? SIMD4(0.88, 0.71, 0.43, 1))
        for fx in 0..<model.effectCount(ch) {
          let command=model.nativeCommand(r,ch,fx)
          let entry=command.flatMap {model.commands.entry(command:$0.effect,parameter:$0.parameter)}
          let draft=nudgeEditor.flatMap{editor in editor.target.pattern==model.pattern && editor.target.row==r && editor.target.channel==ch && editor.fx==fx ? model.commands.schema(kind:editor.kind,native:editor.native)?.code : nil}
          let code=draft ?? command.map {$0.kind=="tracker" ? entry?.displayCode ?? "??" : model.commands.schema(kind:$0.kind,native:$0.native)?.code ?? $0.code} ?? ".."
          let values=inlineText[(r*model.channels+ch)*8+fx] ?? [command.map{$0.kind=="tracker" ? String(format:"%02X",$0.parameter) : $0.valueText} ?? ".."]
          let color=command == nil ? faint : command?.kind=="tracker" ? entry?.rgba ?? SIMD4(0.77,0.57,0.86,1) : SIMD4(0.69,0.66,0.98,1)
          text(effectPrefixTarget==navigation && !effectPrefix.isEmpty && r==cursorRow && ch==cursorChannel && column==3+fx*2 ? effectPrefix+"_" : code,x+fieldOffset(3+fx*2,channel:ch),y+1,color)
          for (slot,value) in values.enumerated() {
            let layout=model.effectLayout(ch,fx),start=x+fieldOffset(4+fx*2,channel:ch,slot:slot)
            clipRight=start+(slot<layout.slotWidths.count ? layout.slotWidths[slot]-6 : 38)
            text(value,start,y+1,color);clipRight = .greatestFiniteMagnitude
          }
        }
      }
    }
    // Backgrounds and selection span complete rows. Draw structural rails last
    // so those fills never erase the distinction between channels and FX slots.
    for v in 0..<max(0,visibleChannels) {
      let ch=firstChannel+v,x=channelX(ch)
      for effect in 0..<model.effectCount(ch) {
        let origin=x+model.effectOffset(ch,effect),layout=model.effectLayout(ch,effect)
        quad(origin,headerHeight,1,max(0,height-headerHeight),SIMD4(0.19,0.23,0.29,1))
        for slot in 1..<layout.slotWidths.count {quad(origin+layout.slotOffset(slot),headerHeight,1,max(0,height-headerHeight),SIMD4(0.12,0.16,0.21,1))}
      }
      quad(x,0,2,height,SIMD4(0.34,0.40,0.48,1))
      if v==visibleChannels-1 {quad(x+model.channelWidth(ch),0,2,height,SIMD4(0.34,0.40,0.48,1))}
    }
    clipLeft=0
  }
  // Qualification-only readback: the same geometry, shader and atlas as the
  // display path, rendered into a private texture without any window/drawable.
  func offscreenImage() -> CGImage? {
    guard Thread.isMainThread, window?.occlusionState.contains(.visible) != true,
          bounds.width > 0, bounds.height > 0, bounds.width <= 2000, bounds.height <= 2000,
          let device, let command = commandQueue.makeCommandBuffer() else { return nil }
    prepareGeometry()
    let pixelWidth = Int(bounds.width * 2), pixelHeight = Int(bounds.height * 2)
    let rowBytes = ((pixelWidth * 4 + 255) / 256) * 256
    let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: colorPixelFormat, width: pixelWidth, height: pixelHeight, mipmapped: false)
    descriptor.storageMode = .private; descriptor.usage = [.renderTarget]
    guard let texture = device.makeTexture(descriptor: descriptor),
          let readback = device.makeBuffer(length: rowBytes * pixelHeight, options: .storageModeShared) else { return nil }
    let count = min(vertices.count, vertexBufferBytes / MemoryLayout<Vertex>.stride)
    guard let geometry = vertices.withUnsafeBytes({ device.makeBuffer(bytes: $0.baseAddress!, length: count * MemoryLayout<Vertex>.stride, options: .storageModeShared) }) else { return nil }
    let pass = MTLRenderPassDescriptor()
    pass.colorAttachments[0].texture = texture; pass.colorAttachments[0].loadAction = .clear
    pass.colorAttachments[0].storeAction = .store; pass.colorAttachments[0].clearColor = clearColor
    guard let encoder = command.makeRenderCommandEncoder(descriptor: pass) else { return nil }
    var size = SIMD2(Float(bounds.width), Float(bounds.height))
    encoder.setRenderPipelineState(pipeline); encoder.setVertexBuffer(geometry, offset: 0, index: 0)
    encoder.setVertexBytes(&size, length: MemoryLayout<SIMD2<Float>>.stride, index: 1)
    encoder.setFragmentTexture(atlas, index: 0); encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: count); encoder.endEncoding()
    guard let blit = command.makeBlitCommandEncoder() else { return nil }
    blit.copy(from: texture, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(x: 0, y: 0, z: 0),
      sourceSize: MTLSize(width: pixelWidth, height: pixelHeight, depth: 1), to: readback, destinationOffset: 0,
      destinationBytesPerRow: rowBytes, destinationBytesPerImage: rowBytes * pixelHeight)
    blit.endEncoding(); command.commit(); command.waitUntilCompleted()
    guard command.status == .completed,
          let provider = CGDataProvider(data: Data(bytes: readback.contents(), count: rowBytes * pixelHeight) as CFData) else { return nil }
    return CGImage(width: pixelWidth, height: pixelHeight, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: rowBytes,
      space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue).union(.byteOrder32Little),
      provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
  }
  private func prepareRenderSnapshot()->PatternRenderSnapshot? {
    let begin=CFAbsoluteTimeGetCurrent()
    guard window?.occlusionState.contains(.visible)==true,!renderingPaused else{return nil}
    positionNudgeEditor();prepareGeometry()
    let count=min(vertices.count,vertexBufferBytes/MemoryLayout<Vertex>.stride)
    let bytes=vertices.withUnsafeBytes{Data($0.prefix(count*MemoryLayout<Vertex>.stride))}
    let ms=(CFAbsoluteTimeGetCurrent()-begin)*1000
    maxFrameMS=max(maxFrameMS,ms)
    if frameCount>120,cpuTimes.count<216000{cpuTimes.append(ms);mainThreadTimes.append(ms)}
    return PatternRenderSnapshot(bytes:bytes,count:count,size:SIMD2(Float(bounds.width),Float(bounds.height)),clear:clearColor,generation:metricsGeneration,preparedAt:CACurrentMediaTime())
  }

  func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}
  private func position(_ event: NSEvent) -> (Int, Int) {
    let p = convert(event.locationInWindow, from: nil)
    var channel=firstChannel
    while channel < model.channels-1 && Float(p.x) >= channelX(channel+1) { channel += 1 }
    return (max(0,min(model.rows-1,firstRow+Int((p.y-CGFloat(headerHeight))/CGFloat(rowHeight)))),max(0,channel))
  }
  override func mouseDown(with event: NSEvent) {
    if let nudgeEditor {nudgeEditor.commit(advance:false);return}
    finishEffectPrefix()
    discardDeferredKeys()
    window?.makeFirstResponder(self)
    let p = convert(event.locationInWindow, from: nil)
    let (r, c) = position(event)
    if p.y < CGFloat(headerHeight) {
      if p.x < CGFloat(gutterWidth) {cyclePositionMode();return}
      if p.x>=CGFloat(gutterWidth) && Float(p.x)-channelX(c)>=model.channelWidth(c)-30 && Float(p.x)-channelX(c)<model.channelWidth(c) {cursorChannel=c;onEffectColumns?(c,min(8,model.effectCount(c)+1));return}
      guard p.x >= CGFloat(gutterWidth), p.y >= CGFloat(headerHeight - 36) else { return }
      onMute?(c)
      return
    }
    cursorRow = r
    cursorChannel = c
    let x = Float(p.x)-channelX(c)
    column=fieldAt(x,c)
    parameterIndex=parameterAt(x,c,r,effectColumn)
    if column>=4 && column%2==0 && beginNudgeEdit() {return}
    if event.clickCount >= 2 {if column>=3 && model.nativeCommand(cursorRow,cursorChannel,effectColumn)?.kind != "tracker" && model.nativeCommand(cursorRow,cursorChannel,effectColumn) != nil {onNativeEffect?()} else if column<=2 {onPreciseNotes?()} else {onEffectPicker?()}}
    selectionStart = (r, c)
    selectionEnd = nil
    onCursor?()
    NSAccessibility.post(element: self, notification: .focusedUIElementChanged)
  }
  override func mouseDragged(with event: NSEvent) { selectionEnd = position(event) }
  private func selected(_ r: Int, _ c: Int) -> Bool {
    guard let a = selectionStart, let b = selectionEnd else { return false }
    return r >= min(a.0, b.0) && r <= max(a.0, b.0) && c >= min(a.1, b.1) && c <= max(a.1, b.1)
  }
  func selectRegion(from start: (Int, Int), to end: (Int, Int)) {
    selectionStart = (
      max(0, min(model.rows - 1, start.0)), max(0, min(model.channels - 1, start.1))
    )
    selectionEnd = (max(0, min(model.rows - 1, end.0)), max(0, min(model.channels - 1, end.1)))
  }
  override func scrollWheel(with event: NSEvent) {
    if nudgeEditor != nil {return}
    if abs(event.scrollingDeltaY) > 0.1 {
      firstRow = max(
        0,
        min(
          max(0, model.rows - max(1, Int((Float(bounds.height) - headerHeight) / rowHeight))),
          firstRow
            + Int(
              event.scrollingDeltaY > 0
                ? -max(1, event.scrollingDeltaY / 3) : max(1, -event.scrollingDeltaY / 3))))
    }
    if abs(event.scrollingDeltaX) > 1 {
      horizontalInset -= Float(event.scrollingDeltaX)*2
      normalizeHorizontalScroll()
    }
    isFollowing = false
  }
  func revealCursor() {
    let visible = max(1, Int((Float(bounds.height) - headerHeight) / rowHeight) - 1)
    if cursorRow < firstRow { firstRow = cursorRow }
    if cursorRow >= firstRow + visible { firstRow = cursorRow - visible }
    column=min(column,model.lastField(cursorChannel))
    if cursorChannel < firstChannel {firstChannel=cursorChannel;horizontalInset=0}
    let left=channelX(cursorChannel)+fieldOffset(column),right=left+fieldWidth(column)
    if left<gutterWidth {horizontalInset -= gutterWidth-left}
    else if right>Float(bounds.width) {horizontalInset += right-Float(bounds.width)+8}
    normalizeHorizontalScroll()
    onCursor?()
  }
  override func keyDown(with event: NSEvent) {
    if deferringEffectKeys { deferKey(event); return }
    let ch = event.charactersIgnoringModifiers?.lowercased() ?? ""
    if event.modifierFlags.intersection([.command,.control,.option,.shift]) == .option {
      switch event.keyCode {
      case 126: previousInputInstrument(nil);return
      case 125: nextInputInstrument(nil);return
      case 123: previousInputOctave(nil);return
      case 124: nextInputOctave(nil);return
      default:break
      }
    }
    // OpenMPT's octave keys: keypad divide and multiply. Keep the ordinary
    // slash available for effect entry and the laptop's musical keyboard.
    if event.modifierFlags.intersection([.command,.control,.option,.shift]).isEmpty {
      if event.keyCode==75 {previousInputOctave(nil);return}
      if event.keyCode==67 {nextInputOctave(nil);return}
      if (event.keyCode==36 || event.keyCode==76) && column==1 {useCursorInstrument(nil);return}
    }
    let typing = KeyboardSettings.isDataTyping(event)
    // Busy is short-lived: keep typed and navigation keys in order rather than
    // dropping them. Transport and modified shortcuts are never queued.
    if typing, event.keyCode != KeyboardSettings.transportKey, waitingForDocument { deferKey(event); return }
    // These edits are cursor-local even with a rectangular selection or an
    // unfinished two-character FX code. Do not let note mapping consume '.'.
    let editModifiers=event.modifierFlags.intersection([.command,.control,.option,.shift])
    if event.characters == ".", editModifiers.isDisjoint(with:[.command,.control,.option]) {
      clearCursorField(nil);return
    }
    if (event.keyCode==51 || event.keyCode==117), editModifiers == .shift {
      deleteChannelRow(nil);return
    }
    if canEdit(), typeEffectCode(event) { return }
    // Moving away can commit a pending one-letter tracker command. Preserve
    // that navigation key until the resulting transaction has completed too.
    if deferringEffectKeys { deferKey(event); return }
    if event.characters == "?" || (ch=="/" && event.modifierFlags.contains(.shift)),column>=2,!event.modifierFlags.contains(.command) {onEffectPicker?();return}
    if event.keyCode == KeyboardSettings.transportKey && !event.modifierFlags.contains(.command) {
      if !event.isARepeat { onTransport?() }
      return
    }
    if event.modifierFlags.contains(.command) {
      if ch == "z" {
        event.modifierFlags.contains(.shift) ? onRedo?() : onUndo?()
        return
      }
      if ch == "c" {
        copySelection()
        return
      }
      if ch == "x" { cut(nil);return }
      if ch == "v" {
        pasteSelection()
        return
      }
      if ch == "a" {
        guard model.rows > 0, model.channels > 0 else { return }
        selectionStart = (0, 0)
        selectionEnd = (model.rows - 1, model.channels - 1)
        return
      }
      super.keyDown(with: event)
      return
    }
    // Control/Option chords are shortcuts, never note or value entry. Cursor
    // and delete keys keep their existing meaning with any modifier.
    if !typing && ![126, 125, 123, 124, 48, 115, 119, 51, 117].contains(Int(event.keyCode)) {
      super.keyDown(with: event)
      return
    }
    guard model.rows > 0, model.channels > 0, canEdit() else {
      NSSound.beep()
      return
    }
    if event.keyCode == 51 || event.keyCode == 117, let a = selectionStart, let b = selectionEnd {
      onRowShift?(["operation":"clear","scope":"selection","pattern":model.pattern,"startRow":min(a.0,b.0),"rowCount":abs(a.0-b.0)+1,"startChannel":min(a.1,b.1),"channelCount":abs(a.1-b.1)+1,"fields":column>=3 ? ["effect"] : ["note","instrument","volume","effect"],"expectedRevision":commandRevision?() ?? model.revisionToken])
      return
    }
    if event.modifierFlags.contains(.shift) && [123, 124, 125, 126].contains(Int(event.keyCode)) {
      if selectionStart == nil { selectionStart = (cursorRow, cursorChannel) }
    } else {
      selectionStart = nil
      selectionEnd = nil
    }
    switch event.keyCode {
    case 126: cursorRow = max(0, cursorRow - 1)
    case 125: cursorRow = min(model.rows - 1, cursorRow + 1)
    case 123:
      if column>=4 && column%2==0 && selectedParameterIndex>0 {parameterIndex=selectedParameterIndex-1}
      else if column > 0 {
        column -= 1
        if column>=4 && column%2==0 {parameterIndex=max(0,parameterFields().count-1)}
      } else {
        cursorChannel = max(0, cursorChannel - 1)
        column = model.lastField(cursorChannel)
      }
    case 124:
      if column>=4 && column%2==0 && selectedParameterIndex+1<parameterFields().count {parameterIndex=selectedParameterIndex+1}
      else if column < model.lastField(cursorChannel) {
        column += 1
      } else {
        cursorChannel = min(model.channels - 1, cursorChannel + 1)
        column = 0
      }
    case 48:
      cursorChannel =
        (cursorChannel + (event.modifierFlags.contains(.shift) ? model.channels - 1 : 1))
        % max(1, model.channels)
    case 115: cursorRow = 0
    case 119: cursorRow = model.rows - 1
    case 51, 117:
      if column==0 && !model.notes(cursorRow,cursorChannel).isEmpty {onClearPreciseNotes?(cursorRow,cursorChannel);return}
      if column >= 3 {onClearNativeEffect?(cursorRow,cursorChannel,effectColumn);revealCursor();return}
      var cell = model.cell(cursorRow, cursorChannel)
      if column == 0 {
        onRowShift?(["operation":"clear","scope":"selection","pattern":model.pattern,"startRow":cursorRow,"rowCount":1,"startChannel":cursorChannel,"channelCount":1,"expectedRevision":commandRevision?() ?? model.revisionToken])
        return
      } else {
        cell[[0, 1, 3, 4, 5][column]] = 0
        if column == 2 { cell[2] = 0 }
      }
      commit(cell)
    default:
      if column<=2 && (event.keyCode==36 || (!model.notes(cursorRow,cursorChannel).isEmpty && (KeyboardSettings.note(for:ch) != nil || Int(ch,radix:16) != nil))) {onPreciseNotes?();return}
      if column >= 3 {
        let command=model.nativeCommand(cursorRow,cursorChannel,effectColumn)
        if event.keyCode==36 || event.keyCode==76 {if beginNudgeEdit() {return};if let command,command.kind != "tracker" {onNativeEffect?()} else {onEffectPicker?()};return}
        if column%2==1 {
          if let index=model.effectLetters.firstIndex(where:{$0.lowercased()==ch && $0 != "?" && $0 != "."}) {onTrackerEffect?(cursorRow,cursorChannel,effectColumn,index,command?.kind=="tracker" ? command!.parameter : 0)}
        } else if let command,command.kind != "tracker",ch.count==1 && (ch.first!.isLetter || ch.first!.isNumber || ch=="-" || ch=="+") {
          _=beginNudgeEdit(replacing:ch);return
        } else if let hex=Int(ch,radix:16) {
          if let command,command.kind != "tracker" {onNativeEffect?();return}
          let effect=command?.effect ?? 0,old=command?.parameter ?? 0
          let entry=model.commands.entry(command:effect,parameter:old)
          let next=entry?.mask==0xF0 ? entry!.value | hex : ((old<<4)|hex)&255
          onTrackerEffect?(cursorRow,cursorChannel,effectColumn,effect,next)
        }
        revealCursor();return
      }
      var cell = model.cell(cursorRow, cursorChannel)
      if column == 0 {
        if let n = KeyboardSettings.note(for: ch) {
          // Auto-repeat of a held note key is consumed; it must never reach
          // the note-off key below when a custom map assigns "1" to a note.
          if event.isARepeat { return }
          guard (0...255).contains(instrument) else {
            onMessage?(
              "Map this sample to an instrument before entering notes; pattern slots range from 0 to 255."
            )
            return
          }
          cell[0] = UInt8(max(model.noteMin, min(model.noteMax, octave * 12 + n + 1)))
          cell[1] = UInt8(instrument)
          let held=(note:Int(cell[0]),instrument:instrument,channel:cursorChannel)
          commit(cell)
          if !replayingReleasedKey {
            if let previous=heldKeys.updateValue(held,forKey:event.keyCode){onAudition?(previous.note,previous.instrument,previous.channel,false)}
            onAudition?(held.note, held.instrument, held.channel, true)
          }
        } else if ch == "1" {
          cell[0] = 255
          commit(cell)
        }
      } else if column == 3 {
        if let index = model.effectLetters.firstIndex(where: {
          $0.lowercased() == ch && $0 != "?" && $0 != "."
        }) {
          cell[4] = UInt8(index)
          onEdit?(cursorRow, cursorChannel, cell)
        }
      } else if let hex = UInt8(ch, radix: column == 2 ? 10 : 16) {
        let index = [0, 1, 3, 4, 5][column]
        if column==4,let command=model.commands.entry(command:Int(cell[4]),parameter:Int(cell[5])),command.mask==0xF0 {
          cell[index]=UInt8(command.value) | hex
        } else {cell[index] = column == 2 ? (cell[index] % 10) * 10 + hex : (cell[index] << 4) | hex}
        if column == 2 {
          cell[2] = 1
          cell[3] = min(64, cell[3])
        }
        if column == 3 { cell[4] = min(36, cell[4]) }
        onEdit?(cursorRow, cursorChannel, cell)
      }
    }
    if event.modifierFlags.contains(.shift) && selectionStart != nil {
      selectionEnd = (cursorRow, cursorChannel)
    }
    revealCursor()
  }
  override func keyUp(with event: NSEvent) {
    for index in deferredKeys.indices where deferredKeys[index].event.keyCode == event.keyCode { deferredKeys[index].released = true }
    if let note = heldKeys.removeValue(forKey: event.keyCode) {
      onAudition?(note.note, note.instrument, note.channel, false)
    } else {
      super.keyUp(with: event)
    }
  }
  override func resignFirstResponder() -> Bool {
    finishEffectPrefix()
    discardDeferredKeys()
    for note in heldKeys.values { onAudition?(note.note, note.instrument, note.channel, false) }
    heldKeys.removeAll()
    return super.resignFirstResponder()
  }
  private func commit(_ cell: [UInt8]) {
    onEdit?(cursorRow, cursorChannel, cell)
    cursorRow = min(model.rows - 1, cursorRow + step)
  }
  @objc func copy(_ sender: Any?) { copySelection() }
  @objc func cut(_ sender: Any?) { guard canEdit() else{return};copySelection(cutting:true) }
  @objc func paste(_ sender: Any?) { pasteSelection() }
  @objc func previousInputInstrument(_ sender:Any?) { stepInputInstrument(-1) }
  @objc func nextInputInstrument(_ sender:Any?) { stepInputInstrument(1) }
  @objc func previousInputOctave(_ sender:Any?) { octave=max(0,octave-1) }
  @objc func nextInputOctave(_ sender:Any?) { octave=min(8,octave+1) }
  func stepInputInstrument(_ delta:Int) {
    let slots=(model.instruments.isEmpty ? model.samples : model.instruments).compactMap{$0["index"] as? Int}.filter{(1...255).contains($0)}.sorted()
    guard !slots.isEmpty else {onMessage?("Load a sample or create an instrument first.");return}
    instrument=delta>0 ? (slots.first{$0>instrument} ?? slots.last!) : (slots.last{$0<instrument} ?? slots.first!)
  }
  @objc func useCursorInstrument(_ sender:Any?) {
    let number=Int(model.cell(cursorRow,cursorChannel)[1])
    let chosen=number>0 ? number : model.notes(cursorRow,cursorChannel).first(where:{$0.instrument>0})?.instrument ?? 0
    guard chosen>0 else{onMessage?("This cell has no instrument number.");return}
    instrument=chosen;onInputChanged?()
  }
  func transpose(_ delta: Int) {
    guard canEdit() else { return }
    let a = selectionStart ?? (cursorRow, cursorChannel)
    let b = selectionEnd ?? a
    onTransform? { model in
      var edits = Edits()
      for r in min(a.0, b.0)...max(a.0, b.0) {
        for c in min(a.1, b.1)...max(a.1, b.1) {
          var cell = model.cell(r, c)
          if cell[0] >= 1 && cell[0] <= 120 {
            cell[0] = UInt8(max(model.noteMin, min(model.noteMax, Int(cell[0]) + delta)))
            edits.append((r, c, cell))
          }
        }
      }
      return edits
    }
  }
  func shiftRows(_ insert: Bool) {
    guard canEdit(), cursorRow >= 0, cursorRow < model.rows, model.channels > 0 else { return }
    onRowShift?(["operation": insert ? "insertRows" : "deleteRows", "scope": "selection",
      "pattern": model.pattern, "startRow": cursorRow, "rowCount": model.rows - cursorRow,
      "startChannel": 0, "channelCount": model.channels, "amount": 1, "allowDataLoss": !insert,
      "expectedRevision": commandRevision?() ?? model.revisionToken])
  }
  @objc func clearCursorField(_ sender:Any?) {
    guard canEdit(),model.editable,cursorRow>=0,cursorRow<model.rows,cursorChannel>=0,cursorChannel<model.channels else{return}
    clearEffectPrefix()
    if column<3 {
      onRowShift?(["operation":"clear","scope":"selection","pattern":model.pattern,
        "startRow":cursorRow,"rowCount":1,"startChannel":cursorChannel,"channelCount":1,
        "fields":[["note","instrument","volume"][column]],"expectedRevision":commandRevision?() ?? model.revisionToken])
      return
    }
    guard let old=model.nativeCommand(cursorRow,cursorChannel,effectColumn),let onNudgeRequest else{return}
    var command:Any=NSNull(),message="Command cleared · Undo restores it"
    if column%2==0 {
      var reset:[String:Any]=["kind":old.kind]
      if old.kind=="tracker" {
        reset["effect"]=old.effect
        reset["parameter"]=old.parameter & (model.commands.entry(command:old.effect,parameter:old.parameter)?.mask ?? 0)
      }
      else {
        reset=old.editCommand
        let fields=parameterFields()
        if fields.indices.contains(selectedParameterIndex) {
          let field=fields[selectedParameterIndex]
          let zeroAllowed=field.minimum<=0 && field.maximum>=0 && field.choices.isEmpty && field.type != "boolean" && field.type != "bool"
          var value:Any=zeroAllowed ? (field.rowUnits || field.type=="integer" || field.type=="int" ? 0 as Any : 0.0 as Any) : field.defaultValue
          if field.storage=="binding" {
            let declared=model.effectBindings.compactMap{$0["id"] as? Int}
            if let proposed=value as? Int,declared.contains(proposed) {value=proposed}
            else {value=declared.first ?? old.binding}
          }
          if field.storage=="duration",let amount=value as? NSNumber {value=min(amount.intValue,(model.rows-cursorRow)*65536-old.position%65536)}
          if field.storage=="durationBeats",let amount=value as? NSNumber {
            value=min(amount.doubleValue,(Double(model.rows-cursorRow)-Double(old.position%65536)/65536)/Double(max(1,model.rowsPerBeat)))
          }
          message=field.name+" reset to "+field.text(value,model:model,timing:timingUnit)+" · Undo restores it"
          if field.storage.hasPrefix("parameters.") {var values=reset["parameters"] as? [String:Any] ?? [:];values[String(field.storage.dropFirst(11))]=value;reset["parameters"]=values}
          else {reset[field.storage]=value}
        } else {onMessage?("This command has no editable parameter at the cursor.");return}
      }
      command=reset
    }
    deferringEffectKeys=true
    onNudgeRequest(["pattern":model.pattern,"row":cursorRow,"channel":cursorChannel,"column":effectColumn,
      "command":command,"expectedRevision":commandRevision?() ?? model.revisionToken]) {[weak self] reply in
      if let error=reply["error"] as? [String:Any] {self?.onMessage?(error["message"] as? String ?? "Field could not be cleared")}
      else {self?.onMessage?(message)}
      self?.finishEffectKeys(success:reply["error"]==nil)
    }
  }
  @objc func deleteChannelRow(_ sender:Any?) {
    guard canEdit(),model.editable,cursorRow>=0,cursorRow<model.rows,cursorChannel>=0,cursorChannel<model.channels else{return}
    clearEffectPrefix()
    onRowShift?(["operation":"deleteRows","scope":"selection","pattern":model.pattern,
      "startRow":cursorRow,"rowCount":model.rows-cursorRow,"startChannel":cursorChannel,"channelCount":1,
      "amount":1,"allowDataLoss":true,"expectedRevision":commandRevision?() ?? model.revisionToken])
  }
  private func copySelection(cutting:Bool=false) {
    guard !copying, model.rows > 0, model.channels > 0 else { return }
    let a = selectionStart ?? (cursorRow, cursorChannel)
    let b = selectionEnd ?? a
    let snapshot = model
    let revision=commandRevision?() ?? model.revisionToken
    let changeCount = NSPasteboard.general.changeCount
    copying = true
    onMessage?("Preparing clipboard…")
    clipboardWorker.async {
      let firstRow=min(a.0,b.0),firstChannel=min(a.1,b.1),lastRow=max(a.0,b.0),lastChannel=max(a.1,b.1)
      var cells=[[Int]](),effects=[[String:Any]](),usedBindings=Set<Int>(),usedScratchGestures=Set<Int>()
      for row in firstRow...lastRow {for channel in firstChannel...lastChannel {
        cells.append(snapshot.cell(row,channel).map(Int.init))
        for fx in 0..<snapshot.effectCount(channel) {if let c=snapshot.nativeCommand(row,channel,fx),!(fx==0 && c.kind=="tracker") {
          var command=c.dictionary;command["channel"]=channel-firstChannel;command["position"]=c.position-firstRow*65536;effects.append(command);if c.binding>0 {usedBindings.insert(c.binding)};if c.native=="scratch",let id=(c.parameters["gesture"] as? NSNumber)?.intValue {usedScratchGestures.insert(id)}
        }}
      }}
      let notes=snapshot.preciseNotes.values.flatMap{$0}.filter{$0.row>=firstRow && $0.row<=lastRow && $0.channel>=firstChannel && $0.channel<=lastChannel}.map { event -> [String:Int] in
        var note=event.dictionary;note["channel"]=event.channel-firstChannel;note["position"]=event.position-firstRow*65536;return note
      }
      let payload:[String:Any]=["notes":notes,"rows":lastRow-firstRow+1,"channels":lastChannel-firstChannel+1,"cells":cells,"effects":effects,"bindings":snapshot.effectBindings.filter{usedBindings.contains($0["id"] as? Int ?? 0)},"scratchGestures":snapshot.scratchGestures.filter{usedScratchGestures.contains($0["id"] as? Int ?? 0)}.map{$0.filter{$0.key != "uses"}}]
      guard let data=try? JSONSerialization.data(withJSONObject:payload,options:[.sortedKeys]),data.count<=16*1024*1024,let json=String(data:data,encoding:.utf8) else {DispatchQueue.main.async {self.copying=false;self.onMessage?("Selection is too large to copy.")};return}
      let text="ScreamSeq Pattern 2\n"+json
      DispatchQueue.main.async {
        self.copying = false
        guard NSPasteboard.general.changeCount == changeCount else { return }
        NSPasteboard.general.clearContents()
        guard NSPasteboard.general.setString(text, forType: .string) else{self.onMessage?("Could not write the clipboard; pattern unchanged.");return}
        if cutting {
          guard self.canEdit(),(self.commandRevision?() ?? self.model.revisionToken)==revision,self.model.pattern==snapshot.pattern else{self.onMessage?("Copied, but cut cancelled because the song changed.");return}
          self.onRowShift?(["operation":"clear","scope":"selection","pattern":snapshot.pattern,"startRow":firstRow,"rowCount":lastRow-firstRow+1,"startChannel":firstChannel,"channelCount":lastChannel-firstChannel+1,"expectedRevision":revision])
        } else {self.onMessage?("Pattern selection copied")}
      }
    }
  }
  func pasteSelection(mode: String = "overwrite") {
    guard let s = NSPasteboard.general.string(forType: .string) else { return }
    pasteText(s, mode: mode)
  }
  func pasteText(_ s: String, mode: String = "overwrite") {
    guard !copying, canEdit(),
      (s.hasPrefix("Resonance Pattern 1\n") || s.hasPrefix("ScreamSeq Pattern 2\n"))
    else { return }
    guard s.utf8.count <= 16 * 1024 * 1024 else {
      onMessage?("The pattern clipboard exceeds the 16 MB limit.")
      return
    }
    let row = cursorRow
    let channel = cursorChannel
    if s.hasPrefix("ScreamSeq Pattern 2\n") {
      // Up to 16 MB of JSON: parse off the main thread, then apply only if the
      // pattern and revision captured with the cursor are still current.
      let pattern=model.pattern,revision=commandRevision?() ?? model.revisionToken
      copying=true
      clipboardWorker.async {
        var parsed:[String:Any]?
        if let data=s.dropFirst("ScreamSeq Pattern 2\n".count).data(using:.utf8),let request=(try? JSONSerialization.jsonObject(with:data)) as? [String:Any],Set(request.keys).isSubset(of:["rows","channels","cells","effects","bindings","notes","scratchGestures"]) {parsed=request}
        DispatchQueue.main.async {
          self.copying=false
          guard var request=parsed else {self.onMessage?("Invalid pattern clipboard.");return}
          guard self.canEdit(),self.model.pattern==pattern,(self.commandRevision?() ?? self.model.revisionToken)==revision else {
            self.onMessage?("Paste cancelled: the pattern changed while the clipboard was being read.")
            return
          }
          request["pattern"]=pattern;request["startRow"]=row;request["startChannel"]=channel;request["mode"]=mode;request["clip"]=true;request["expectedRevision"]=revision
          self.onPaste?(request)
        }
      }
      return
    }
    if let onPaste {
      let pattern = model.pattern
      let revision = commandRevision?()
      copying = true
      let pasteboard = s
      clipboardWorker.async {
        let lines = pasteboard.components(separatedBy: "\n").dropFirst()
        var cells = [[Int]]()
        var columns = 0
        var valid = !lines.isEmpty
        for line in lines {
          let entries = line.components(separatedBy: "\t")
          if columns == 0 { columns = entries.count }
          if columns != entries.count { valid = false; break }
          for text in entries {
            let values = text.components(separatedBy: ",").compactMap { UInt8($0, radix: 16) }
            if values.count != 6 { valid = false; break }
            cells.append(values.map(Int.init))
          }
          if !valid || cells.count > 262144 { valid = false; break }
        }
        let parsed = cells
        let width = columns
        let isValid = valid
        DispatchQueue.main.async {
          self.copying = false
          guard isValid, self.canEdit(), self.model.pattern == pattern else {
            self.onMessage?("Paste cancelled: clipboard is invalid, too large, or the pattern changed.")
            return
          }
          var request: [String: Any] = ["pattern": pattern, "startRow": row, "startChannel": channel,
            "rows": lines.count, "channels": width, "cells": parsed, "mode": mode, "clip": true]
          if let revision { request["expectedRevision"] = revision }
          onPaste(request)
        }
      }
      return
    }
    onTransform? { model in
      var edits = Edits()
      for (r, line) in s.components(separatedBy: "\n").dropFirst().prefix(max(0, model.rows - row))
        .enumerated()
      {
        for (c, text) in line.components(separatedBy: "\t").prefix(max(0, model.channels - channel))
          .enumerated()
        {
          let values = text.components(separatedBy: ",").compactMap { UInt8($0, radix: 16) }
          if values.count == 6 { edits.append((row + r, channel + c, values)) }
        }
      }
      return edits
    }
  }
  override func accessibilityChildren() -> [Any]? {
    let rows = max(0, min(model.rows - firstRow, Int((Float(bounds.height) - headerHeight) / rowHeight)))
    let columns=visibleChannelCount()
    var elements = [NSAccessibilityElement]()
    let ruler=PatternRulerAccessibility();ruler.owner=self;ruler.setAccessibilityParent(self);ruler.setAccessibilityRole(.button)
    ruler.setAccessibilityLabel("Position ruler: "+positionMode.title);ruler.setAccessibilityHelp("Click to cycle rows, beats, pattern time and song time")
    if let window{ruler.setAccessibilityFrame(window.convertToScreen(convert(NSRect(x:0,y:0,width:CGFloat(gutterWidth),height:CGFloat(headerHeight)),to:nil)))}
    elements.append(ruler)
    for row in firstRow..<firstRow + rows {
      for channel in firstChannel..<firstChannel + columns {
        let element = NSAccessibilityElement()
        element.setAccessibilityRole(.cell)
        element.setAccessibilityParent(self)
        let cell = model.cell(row, channel)
        let note = Int(cell[0])
        let label =
          note >= 1 && note <= 120
          ? notes[(note - 1) % 12] + String((note - 1) / 12) : note == 0 ? "empty" : "note off"
        element.setAccessibilityLabel(
          "Row \(row), channel \(channel+1), \(label), instrument \(cell[1]), volume \(cell[3]), FX \((0..<model.effectCount(channel)).map{fx in model.nativeCommand(row,channel,fx).map{c in c.kind=="tracker" ? "\(fx+1): \(model.commands.entry(command:c.effect,parameter:c.parameter)?.displayCode ?? "??") \(c.valueText)" : "\(fx+1): \(model.commands.schema(kind:c.kind,native:c.native)?.code ?? c.code) \((model.commands.schema(kind:c.kind,native:c.native)?.inlineFields ?? []).map{$0.name+" "+$0.text($0.value(c),model:model,timing:timingUnit,rateMode:c.parameters["rateMode"] as? String)}.joined(separator:", "))"} ?? "\(fx+1): empty"}.joined(separator:", "))"
        )
        let rect = NSRect(
          x: CGFloat(channelX(channel)),
          y: CGFloat(headerHeight) + CGFloat(row - firstRow) * CGFloat(rowHeight), width: CGFloat(model.channelWidth(channel)),
          height: CGFloat(rowHeight))
        if let window {
          element.setAccessibilityFrame(window.convertToScreen(convert(rect, to: nil)))
        }
        elements.append(element)
      }
    }
    var result:[Any]=elements
    if let nudgeEditor {result.insert(contentsOf:nudgeEditor.fields,at:0)}
    return result
  }
  override func accessibilityValue() -> Any? {
    "Row \(cursorRow), channel \(cursorChannel+1), column \(column+1)"+(column>=4 && column%2==0 && !parameterFields().isEmpty ? ", "+parameterFields()[selectedParameterIndex].help(model:model,timing:timingUnit,rateMode:model.nativeCommand(cursorRow,cursorChannel,effectColumn)?.parameters["rateMode"] as? String) : "")
  }
}
