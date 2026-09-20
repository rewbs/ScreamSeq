import AppKit
import CoreText
import MetalKit

private struct Vertex {
  var position: SIMD2<Float>
  var uv: SIMD2<Float>
  var color: SIMD4<Float>
}
final class PatternView: MTKView, MTKViewDelegate, CAMetalDisplayLinkDelegate {
  var model = PatternModel([:]) {
    didSet {
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
      column = min(column,4 + model.extraColumns(cursorChannel))
      horizontalInset = min(horizontalInset,max(0,model.channelWidth(firstChannel)-1))
    }
  }
  var cursorRow = 0, cursorChannel = 0, column = 0, octave = 4, instrument = 1, step = 1
  var firstRow = 0, firstChannel = 0, playRow = -1, playPattern = -1
  var isFollowing = true { didSet { if oldValue != isFollowing { onFollowChanged?(isFollowing) } } }
  var onFollowChanged: ((Bool) -> Void)?
  var navigation: EditorNavigation {
    EditorNavigation(pattern: model.pattern, row: cursorRow, channel: cursorChannel, column: column, following: isFollowing)
  }
  var contextToken: String {
    let region = automationSelection
    return navigation.token + ":" + ["startRow","endRow","startChannel","endChannel"].map { String(region[$0] ?? 0) }.joined(separator: ":")
  }
  func navigate(_ state: EditorNavigation, clearSelection: Bool) {
    if clearSelection { selectionStart = nil; selectionEnd = nil }
    cursorRow = state.row; cursorChannel = state.channel; column = state.column
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
  var onClearNativeEffect: ((Int,Int,Int) -> Void)?
  var onTransport: (() -> Void)?
  var onCursor: (() -> Void)?
  var onUndo: (() -> Void)?, onRedo: (() -> Void)?
  var onAudition: ((Int, Bool) -> Void)?
  private var heldKeys = [UInt16: Int]()
  var onMute: ((Int) -> Void)?
  var muted = Set<Int>()
  var headerHeight: Float { model.noteTracks.isEmpty ? 36 : 58 }
  private var metalDisplayLink: CAMetalDisplayLink?
  var renderingPaused = false {
    didSet { metalDisplayLink?.isPaused = renderingPaused }
  }
  private let renderWorker = DispatchQueue(label: "org.resonance.metal", qos: .userInteractive)
  private var commandQueue: MTLCommandQueue!
  private var pipeline: MTLRenderPipelineState!
  private var atlas: MTLTexture!
  private var buffers: [MTLBuffer] = []
  private var availableBuffers = [0, 1, 2]
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
    maxFrameMS = 0
    maxGPUMS = 0
  }
  func resetPresentationClock() { lastPresentTime = 0 }
  var maxFrameMS: Double = 0
  var maxGPUMS: Double = 0
  var rowHeight: Float = KeyboardSettings.rowHeight
  let channelWidth: Float = 162
  private var horizontalInset: Float = 0
  private var clipLeft: Float = 0
  func channelX(_ channel: Int) -> Float { 52 + model.channelOffset(channel) - model.channelOffset(firstChannel) - horizontalInset }
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
  func fieldOffset(_ column:Int) -> Float { column < 5 ? [7,44,72,105,124][max(0,column)] : 162 + Float(column-5)*96 + 5 }
  func fieldWidth(_ column:Int) -> Float { column < 5 ? [32,24,28,20,28][max(0,column)] : 86 }
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
    for _ in 0..<3 {
      buffers.append(device.makeBuffer(length: 16 * 1024 * 1024, options: .storageModeShared)!)
    }
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
  deinit { metalDisplayLink?.invalidate() }
  override func viewDidMoveToWindow() {
    super.viewDidMoveToWindow()
    if window == nil { metalDisplayLink?.isPaused = true; return }
    wantsLayer = true
    if metalDisplayLink == nil, let metalLayer = layer as? CAMetalLayer {
      let link = CAMetalDisplayLink(metalLayer: metalLayer)
      link.delegate = self
      link.preferredFrameRateRange = CAFrameRateRange(minimum: 60, maximum: 60, preferred: 60)
      link.preferredFrameLatency = 2
      link.add(to: .main, forMode: .common)
      metalDisplayLink = link
    }
    metalDisplayLink?.isPaused = renderingPaused
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
    guard w>0,x+w>clipLeft,x<Float(bounds.width) else{return}
    let left=max(x,clipLeft),right=min(x+w,Float(bounds.width))
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
  func metalDisplayLink(_ link: CAMetalDisplayLink, needsUpdate update: CAMetalDisplayLink.Update) {
    renderFrame(drawable: update.drawable)
  }
  private func prepareGeometry() {
    vertices.removeAll(keepingCapacity: true)
    let width = Float(bounds.width)
    let height = Float(bounds.height)
    quad(0, 0, width, headerHeight, SIMD4(0.09, 0.11, 0.14, 1))
    text("ROW", 9, headerHeight - 28, SIMD4(0.42, 0.48, 0.55, 1))
    let visibleChannels = visibleChannelCount()
    clipLeft=52
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
      text(
        Self.decimal[ch + 1] + "  " + (ch < model.tracks.count && !(model.tracks[ch]["name"] as? String ?? "").isEmpty ? String((model.tracks[ch]["name"] as? String ?? "").prefix(12)).uppercased() : (model.noteTrackByChannel[ch] == nil ? "CHANNEL" : "NOTE")), x + 9, headerHeight - 28,
        muted.contains(ch) ? SIMD4(0.38, 0.41, 0.45, 1) : SIMD4(0.66, 0.73, 0.79, 1))
      quad(x, 0, 1, height, SIMD4(0.16, 0.19, 0.23, 1))
      for effect in 0..<model.extraColumns(ch) {
        let fx=x+162+Float(effect)*96
        text("FX \(effect+1)",fx+7,headerHeight-28,SIMD4(0.59,0.63,0.78,1))
        quad(fx,headerHeight,1,height-headerHeight,SIMD4(0.13,0.16,0.21,1))
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
        r < Self.rowNumbers.count ? Self.rowNumbers[r] : String(r), 9, y + 1,
        r % model.rowsPerBeat == 0 ? SIMD4(0.46, 0.55, 0.62, 1) : SIMD4(0.28, 0.35, 0.41, 1))
      clipLeft=52
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
        let effect =
          cell.effect == 0
          ? "."
          : (Int(cell.effect) < model.effectLetters.count
            ? model.effectLetters[Int(cell.effect)] : "?")
        let effectColor = model.commands.entry(command: Int(cell.effect), parameter: Int(cell.parameter))?.rgba ?? SIMD4<Float>(0.77, 0.57, 0.86, 1)
        text(effect, x + 105, y + 1, cell.effect == 0 ? faint : effectColor)
        text(
          cell.effect == 0 && cell.parameter == 0 ? ".." : Self.hexadecimal[Int(cell.parameter)],
          x + 124, y + 1,
          cell.effect == 0 ? faint : effectColor)
        for effect in 0..<model.extraColumns(ch) {
          let command=model.nativeCommand(r,ch,effect)
          text(command?.text ?? "... ....",x+fieldOffset(effect+5),y+1,command == nil ? faint : SIMD4(0.69,0.66,0.98,1))
        }
      }
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
    let count = min(vertices.count, buffers[0].length / MemoryLayout<Vertex>.stride)
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
  private func renderFrame(drawable: CAMetalDrawable) {
    let begin = CFAbsoluteTimeGetCurrent()
    guard window?.occlusionState.contains(.visible) == true else { return }
    guard let bufferIndex = availableBuffers.popLast() else { return }
    let preparationStart = CFAbsoluteTimeGetCurrent()
    prepareGeometry()
    let width = Float(bounds.width), height = Float(bounds.height)
    let buffer = buffers[bufferIndex]
    let count = min(vertices.count, buffer.length / MemoryLayout<Vertex>.stride)
    vertices.withUnsafeBytes { bytes in
      buffer.contents().copyMemory(
        from: bytes.baseAddress!, byteCount: count * MemoryLayout<Vertex>.stride)
    }
    let size = SIMD2(width, height)
    let releaseBuffer = { [weak self] in
      DispatchQueue.main.async { self?.availableBuffers.append(bufferIndex) }
    }
    let generation = metricsGeneration
    let pipeline = self.pipeline!, atlas = self.atlas!, queue = self.commandQueue!
    let clear = clearColor
    // The display link supplies and schedules the drawable. Explicit timed
    // presentation is invalid for these drawables; present() preserves its pacing.
    // Encode immutable geometry off the event thread.
    // At most three jobs exist. A buffer returns to the main-thread pool only
    // after its own command completes, including out-of-order failures.
    renderWorker.async { [weak self] in
      autoreleasepool {
        let waitMS = 0.0 // Display link hands us an already acquired drawable.
        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = drawable.texture
        pass.colorAttachments[0].loadAction = .clear
        pass.colorAttachments[0].storeAction = .store
        pass.colorAttachments[0].clearColor = clear
        guard let command = queue.makeCommandBuffer(),
          let encoder = command.makeRenderCommandEncoder(descriptor: pass) else {
          releaseBuffer(); return
        }
        var frameSize = size
        encoder.setRenderPipelineState(pipeline)
        encoder.setVertexBuffer(buffer, offset: 0, index: 0)
        encoder.setVertexBytes(&frameSize, length: MemoryLayout<SIMD2<Float>>.stride, index: 1)
        encoder.setFragmentTexture(atlas, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: count)
        encoder.endEncoding()
        command.present(drawable)
        command.addCompletedHandler { [weak self] command in
          let ms = (command.gpuEndTime - command.gpuStartTime) * 1000
          DispatchQueue.main.async {
            guard let self, generation == self.metricsGeneration else { return }
            self.maxGPUMS = max(self.maxGPUMS, ms)
            if self.frameCount > 120 && self.gpuTimes.count < 216000 { self.gpuTimes.append(ms) }
          }
          releaseBuffer()
        }
        drawable.addPresentedHandler { [weak self] draw in
          let timestamp = draw.presentedTime
          DispatchQueue.main.async {
            guard let self, generation == self.metricsGeneration else { return }
            self.presentationCallbacks += 1
            if timestamp <= 0 { self.unpresentedDrawables += 1; return }
            if self.presentationTimestamps.count < 216000 { self.presentationTimestamps.append(timestamp) }
            if timestamp <= self.lastPresentTime { self.outOfOrderPresentations += 1; return }
            if self.lastPresentTime > 0 && self.frameCount > 120 {
              let interval = (timestamp - self.lastPresentTime) * 1000
              if interval > 0 && self.presentIntervals.count < 216000 { self.presentIntervals.append(interval) }
            }
            self.lastPresentTime = timestamp
            self.frameCount += 1
          }
        }
        command.commit()
        DispatchQueue.main.async {
          guard let self, generation == self.metricsGeneration else { return }
          self.submittedFrames += 1
          if self.frameCount > 120 && self.drawableWaitTimes.count < 216000 { self.drawableWaitTimes.append(waitMS) }
        }
      }
    }
    let end = CFAbsoluteTimeGetCurrent()
    let cpuMS = (end - preparationStart) * 1000
    maxFrameMS = max(maxFrameMS, cpuMS)
    if frameCount > 120 && cpuTimes.count < 216000 {
      cpuTimes.append(cpuMS)
      mainThreadTimes.append((end - begin) * 1000)
    }
  }

  func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}
  private func position(_ event: NSEvent) -> (Int, Int) {
    let p = convert(event.locationInWindow, from: nil)
    var channel=firstChannel
    while channel < model.channels-1 && Float(p.x) >= channelX(channel+1) { channel += 1 }
    return (min(model.rows-1,max(0,firstRow+Int((p.y-CGFloat(headerHeight))/CGFloat(rowHeight)))),channel)
  }
  override func mouseDown(with event: NSEvent) {
    window?.makeFirstResponder(self)
    let p = convert(event.locationInWindow, from: nil)
    let (r, c) = position(event)
    if p.y < CGFloat(headerHeight) {
      guard p.x >= 52, p.y >= CGFloat(headerHeight - 36) else { return }
      onMute?(c)
      return
    }
    cursorRow = r
    cursorChannel = c
    let x = Float(p.x)-channelX(c)
    column = x >= 162 ? min(4+model.extraColumns(c),5+Int((x-162)/96)) : x < 43 ? 0 : x < 70 ? 1 : x < 104 ? 2 : x < 123 ? 3 : 4
    if event.clickCount >= 2 {if column>=5 {onNativeEffect?()} else if column<=2 {onPreciseNotes?()}}
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
    column=min(column,4+model.extraColumns(cursorChannel))
    if cursorChannel < firstChannel {firstChannel=cursorChannel;horizontalInset=0}
    let left=channelX(cursorChannel)+fieldOffset(column),right=left+fieldWidth(column)
    if left<52 {horizontalInset -= 52-left}
    else if right>Float(bounds.width) {horizontalInset += right-Float(bounds.width)+8}
    normalizeHorizontalScroll()
    onCursor?()
  }
  override func keyDown(with event: NSEvent) {
    let ch = event.charactersIgnoringModifiers?.lowercased() ?? ""
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
      if ch == "v" {
        pasteSelection()
        return
      }
      if ch == "a" {
        selectionStart = (0, 0)
        selectionEnd = (model.rows - 1, model.channels - 1)
        return
      }
      super.keyDown(with: event)
      return
    }
    guard model.rows > 0, model.channels > 0, canEdit() else {
      NSSound.beep()
      return
    }
    if event.keyCode == 51 || event.keyCode == 117, let a = selectionStart, let b = selectionEnd {
      if column>=5 {onMessage?("Select a single extra effect cell to clear it.");return}
      onTransform? { _ in
        var edits = Edits()
        for row in min(a.0, b.0)...max(a.0, b.0) {
          for channel in min(a.1, b.1)...max(a.1, b.1) {
            edits.append((row, channel, [0, 0, 0, 0, 0, 0]))
          }
        }
        return edits
      }
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
      if column > 0 {
        column -= 1
      } else {
        cursorChannel = max(0, cursorChannel - 1)
        column = 4 + model.extraColumns(cursorChannel)
      }
    case 124:
      if column < 4 + model.extraColumns(cursorChannel) {
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
      if column >= 5 {onClearNativeEffect?(cursorRow,cursorChannel,column-5);revealCursor();return}
      var cell = model.cell(cursorRow, cursorChannel)
      if column == 0 {
        cell = [0, 0, 0, 0, 0, 0]
      } else {
        cell[[0, 1, 3, 4, 5][column]] = 0
        if column == 2 { cell[2] = 0 }
      }
      commit(cell)
    default:
      if column<=2 && (event.keyCode==36 || (!model.notes(cursorRow,cursorChannel).isEmpty && (KeyboardSettings.note(for:ch) != nil || Int(ch,radix:16) != nil))) {onPreciseNotes?();return}
      if column >= 5 {if event.keyCode==36 || ["p","l","t","b"].contains(ch) {onNativeEffect?()};return}
      var cell = model.cell(cursorRow, cursorChannel)
      if column == 0 {
        if let n = KeyboardSettings.note(for: ch), !event.isARepeat {
          guard (0...255).contains(instrument) else {
            onMessage?(
              "Map this sample to an instrument before entering notes; pattern slots range from 0 to 255."
            )
            return
          }
          cell[0] = UInt8(max(model.noteMin, min(model.noteMax, octave * 12 + n + 1)))
          cell[1] = UInt8(instrument)
          heldKeys[event.keyCode] = Int(cell[0])
          commit(cell)
          onAudition?(Int(cell[0]), true)
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
        cell[index] = column == 2 ? (cell[index] % 10) * 10 + hex : (cell[index] << 4) | hex
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
    if let note = heldKeys.removeValue(forKey: event.keyCode) {
      onAudition?(note, false)
    } else {
      super.keyUp(with: event)
    }
  }
  override func resignFirstResponder() -> Bool {
    for note in heldKeys.values { onAudition?(note, false) }
    heldKeys.removeAll()
    return super.resignFirstResponder()
  }
  private func commit(_ cell: [UInt8]) {
    onEdit?(cursorRow, cursorChannel, cell)
    cursorRow = min(model.rows - 1, cursorRow + step)
  }
  @objc func copy(_ sender: Any?) { copySelection() }
  @objc func paste(_ sender: Any?) { pasteSelection() }
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
  private func copySelection() {
    guard !copying, model.rows > 0, model.channels > 0 else { return }
    let a = selectionStart ?? (cursorRow, cursorChannel)
    let b = selectionEnd ?? a
    let snapshot = model
    let changeCount = NSPasteboard.general.changeCount
    copying = true
    onMessage?("Preparing clipboard…")
    clipboardWorker.async {
      var lines = [String]()
      for r in min(a.0, b.0)...max(a.0, b.0) {
        var cells = [String]()
        for c in min(a.1, b.1)...max(a.1, b.1) {
          cells.append(snapshot.cell(r, c).map { Self.hexadecimal[Int($0)] }.joined(separator: ","))
        }
        lines.append(cells.joined(separator: "\t"))
      }
      let text = "Resonance Pattern 1\n" + lines.joined(separator: "\n")
      DispatchQueue.main.async {
        self.copying = false
        guard NSPasteboard.general.changeCount == changeCount else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
        self.onMessage?("Pattern selection copied")
      }
    }
  }
  func pasteSelection(mode: String = "overwrite") {
    guard !copying, canEdit(), let s = NSPasteboard.general.string(forType: .string),
      s.hasPrefix("Resonance Pattern 1\n")
    else { return }
    guard s.utf8.count <= 16 * 1024 * 1024 else {
      onMessage?("The pattern clipboard exceeds the 16 MB limit.")
      return
    }
    let row = cursorRow
    let channel = cursorChannel
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
      for (r, line) in s.components(separatedBy: "\n").dropFirst().prefix(model.rows - row)
        .enumerated()
      {
        for (c, text) in line.components(separatedBy: "\t").prefix(model.channels - channel)
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
          "Row \(row), channel \(channel+1), \(label), instrument \(cell[1]), volume \(cell[3]), effect \(cell[4]) parameter \(cell[5])"
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
    return elements
  }
  override func accessibilityValue() -> Any? {
    "Row \(cursorRow), channel \(cursorChannel+1), column \(column+1)"
  }
}
