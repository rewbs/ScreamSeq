import AppKit

struct EnvelopePoint: Equatable {
  var position: Int
  var value: Double
  var curve: String
  var formula = ""
  var dictionary: [String: Any] {
    var result: [String:Any] = ["position":position,"value":value,"curve":curve]
    if !formula.isEmpty { result["formula"] = formula }; return result
  }
}

final class AutomationCanvas: NSView {
  var editPosition: Double? {didSet{if editPosition != oldValue{needsDisplay=true}}}
  var playbackPosition: Double? {didSet{if playbackPosition != oldValue{needsDisplay=true}}}
  var allowsEditing = true
  var points = [EnvelopePoint]() { didSet { previewValues = []; needsDisplay = true } }
  var rows = 64 { didSet { if rows != oldValue { fit() } } }
  var snap = 256, selected: Int?, curve = "linear"
  var onEdit: (() -> Void)?, onSelect: (() -> Void)?, onEditFinished: (() -> Void)?
  var visibleStart = 0.0, visibleEnd: Double? = nil
  var valueLow = 0.0, valueHigh = 1.0
  var previewValues = [(Double,Double)]() { didSet { needsDisplay = true } }
  var onViewport: (() -> Void)?
  var horizontalEnd: Double { min(Double(rows*256-1), visibleEnd ?? Double(rows*256-1)) }
  var horizontalSpan: Double { max(1,horizontalEnd-visibleStart) }
  func position(at x: CGFloat) -> Double { visibleStart + Double((x-plot.minX)/max(1,plot.width))*horizontalSpan }
  func normalizedValue(at y: CGFloat) -> Double { valueLow + Double((plot.maxY-y)/max(1,plot.height))*(valueHigh-valueLow) }
  func setViewport(start:Double, span:Double) {
    let width = min(Double(rows*256-1),max(1,span))
    visibleStart = min(max(0,start),max(0,Double(rows*256-1)-width)); visibleEnd = visibleStart+width
    needsDisplay=true;onViewport?()
  }
  func zoom(_ factor:Double, anchor:Double? = nil) {
    let point=anchor ?? selected.flatMap { points.indices.contains($0) ? Double(points[$0].position) : nil } ?? (visibleStart+horizontalSpan/2)
    let fraction=min(1,max(0,(point-visibleStart)/horizontalSpan))
    let width=min(Double(rows*256-1),max(1,horizontalSpan/factor))
    setViewport(start:point-fraction*width,span:width)
  }
  func fit() { visibleStart=0;visibleEnd=nil;valueLow=0;valueHigh=1;needsDisplay=true;onViewport?() }
  override func magnify(with event:NSEvent) { zoom(exp(Double(event.magnification)*2),anchor:position(at:convert(event.locationInWindow,from:nil).x)) }
  override func scrollWheel(with event:NSEvent) {
    let p=convert(event.locationInWindow,from:nil)
    if event.modifierFlags.contains(.option) {
      let factor=exp(Double(-event.scrollingDeltaY)*0.02)
      if event.modifierFlags.contains(.shift) {
        let anchor=min(valueHigh,max(valueLow,normalizedValue(at:p.y))), ratio=(anchor-valueLow)/(valueHigh-valueLow)
        let width=min(1,max(0.001,(valueHigh-valueLow)/factor))
        valueLow=min(1-width,max(0,anchor-ratio*width));valueHigh=valueLow+width;needsDisplay=true
      } else { zoom(factor,anchor:position(at:p.x)) }
    } else {
      let delta=abs(event.scrollingDeltaX)>abs(event.scrollingDeltaY) ? event.scrollingDeltaX : event.scrollingDeltaY
      setViewport(start:visibleStart+Double(delta/max(1,plot.width))*horizontalSpan,span:horizontalSpan)
    }
  }
  private var dragging = false
  var isDragging:Bool {dragging}
  override var isFlipped: Bool { true }
  override var acceptsFirstResponder: Bool { true }
  var plot: NSRect { bounds.insetBy(dx: 36, dy: 24) }
  override init(frame: NSRect) {
    super.init(frame: frame)
    setAccessibilityElement(true); setAccessibilityRole(.group)
    setAccessibilityLabel("Pattern automation envelope")
    setAccessibilityHelp("Click to add or select a point. Drag to move it. Delete removes the selected point. Changes save immediately. Blue marks the editing cursor; gold marks playback.")
  }
  required init?(coder: NSCoder) { fatalError() }
  func location(_ point: EnvelopePoint) -> NSPoint {
    NSPoint(x: plot.minX + CGFloat((Double(point.position)-visibleStart)/horizontalSpan) * plot.width,
            y: plot.maxY - (point.value-valueLow)/(valueHigh-valueLow) * plot.height)
  }
  func value(at position: Double) -> Double {
    if previewValues.count > 1, let first=previewValues.first, let last=previewValues.last, position>=first.0,position<=last.0 {
      let at=(position-first.0)/max(1e-12,last.0-first.0)*Double(previewValues.count-1)
      let i=min(previewValues.count-2,max(0,Int(at)))
      return previewValues[i].1+(previewValues[i+1].1-previewValues[i].1)*(at-Double(i))
    }
    guard let first = points.first else { return 0 }
    var low = 0, high = points.count
    while low < high {
      let middle = (low + high) / 2
      if Double(points[middle].position) <= position { low = middle + 1 } else { high = middle }
    }
    let right = low
    guard right < points.count else { return points.last!.value }
    guard right > 0 else { return first.value }
    let a = points[right - 1], b = points[right]
    if a.curve == "step" { return a.value }
    if a.curve == "step-next" { return position == Double(a.position) ? a.value : b.value }
    var x = min(1, max(0, (position - Double(a.position)) / Double(b.position - a.position)))
    switch a.curve {
    case "smooth": x = x * x * (3 - 2 * x)
    case "exponential": x = expm1(4 * x) / expm1(4)
    case "logarithmic": x = log1p(expm1(4) * x) / 4
    case "exponential-reverse": x = -expm1(-4 * x) / -expm1(-4)
    case "logarithmic-reverse": x = 1 - log1p(expm1(4) * (1 - x)) / 4
    default: break
    }
    return a.value + (b.value - a.value) * x
  }
  override func draw(_ dirtyRect: NSRect) {
    Theme.bg.setFill(); bounds.fill()
    guard plot.width > 0, plot.height > 0 else { return }
    Theme.border.setStroke()
    let grid = NSBezierPath()
    for step in 0...4 {
      let y = plot.minY + plot.height * CGFloat(step) / 4
      grid.move(to: NSPoint(x: plot.minX, y: y)); grid.line(to: NSPoint(x: plot.maxX, y: y))
      String(format:"%.1f",100*(valueHigh-(valueHigh-valueLow)*Double(step)/4)).draw(at: NSPoint(x: plot.minX - 29, y: y - 6),
        withAttributes: [.font: NSFont.monospacedSystemFont(ofSize: 10, weight: .regular), .foregroundColor: Theme.muted])
      let x = plot.minX + plot.width * CGFloat(step) / 4
      grid.move(to: NSPoint(x: x, y: plot.minY)); grid.line(to: NSPoint(x: x, y: plot.maxY))
      String(format:"%.2g",(visibleStart+horizontalSpan*Double(step)/4)/256).draw(at: NSPoint(x: x - 4, y: plot.maxY + 5),
        withAttributes: [.font: NSFont.monospacedSystemFont(ofSize: 10, weight: .regular), .foregroundColor: Theme.muted])
    }
    grid.stroke()
    NSGraphicsContext.saveGraphicsState(); NSBezierPath(rect:plot).addClip()
    defer { NSGraphicsContext.restoreGraphicsState() }
    if !points.isEmpty {
      let path = NSBezierPath(); path.lineWidth = 2
      for pixel in 0...max(1, Int(plot.width)) {
        let fraction = Double(pixel) / Double(max(1, Int(plot.width)))
        let point = NSPoint(x: plot.minX + fraction * plot.width,
          y: plot.maxY - (value(at: visibleStart + fraction * horizontalSpan)-valueLow)/(valueHigh-valueLow) * plot.height)
        if pixel == 0 { path.move(to: point) } else { path.line(to: point) }
      }
      Theme.accent.setStroke(); path.stroke()
      for (i, point) in points.enumerated() {
        let p = location(point)
        (i == selected ? Theme.gold : Theme.accent).setFill()
        NSBezierPath(ovalIn: NSRect(x: p.x - 4, y: p.y - 4, width: 8, height: 8)).fill()
      }
    }
    for (value,color,dashed) in [(editPosition,Theme.selectionMark,true),(playbackPosition,Theme.gold,false)] {
      guard let value,value.isFinite,value>=visibleStart,value<=horizontalEnd else{continue}
      let x=plot.minX+CGFloat((value-visibleStart)/horizontalSpan)*plot.width
      color.setStroke();let marker=NSBezierPath();marker.lineWidth=dashed ? 1:2
      if dashed {marker.setLineDash([4,3],count:2,phase:0)}
      marker.move(to:.init(x:x,y:plot.minY));marker.line(to:.init(x:x,y:plot.maxY));marker.stroke()
    }
  }
  func replaceSelected(position: Int, value: Double, curve: String) {
    guard allowsEditing, let selected, points.indices.contains(selected) else { return }
    var point = points[selected]
    if curve == "scripted" && point.formula.isEmpty { point.formula = "mix(start, end, t)" }
    point.position = min(rows * 256 - 1, max(0, position)); point.value = min(1, max(0, value)); point.curve = curve
    guard !points.enumerated().contains(where: { $0.offset != selected && $0.element.position == point.position }) else { return }
    points[selected] = point; points.sort { $0.position < $1.position }
    self.selected = points.firstIndex(where: { $0.position == point.position })
    onEdit?(); onSelect?()
  }
  func removeSelected() {
    guard allowsEditing, let selected, points.indices.contains(selected) else { return }
    points.remove(at: selected); self.selected = nil; onEdit?(); onSelect?()
  }
  override func mouseDown(with event: NSEvent) {
    guard allowsEditing,plot.width > 0, plot.height > 0 else { return }
    window?.makeFirstResponder(self)
    let p = convert(event.locationInWindow, from: nil)
    guard plot.insetBy(dx: -8, dy: -8).contains(p) else { return }
    selected = points.indices.min { hypot(location(points[$0]).x - p.x, location(points[$0]).y - p.y) < hypot(location(points[$1]).x - p.x, location(points[$1]).y - p.y) }
    if let selected, hypot(location(points[selected]).x - p.x, location(points[selected]).y - p.y) > 9 { self.selected = nil }
    if selected == nil {
      guard points.count < 4096 else { return }
      dragging = true
      let position = snapped(p.x)
      if let existing = points.firstIndex(where: { $0.position == position }) { selected = existing }
      else {
        points.append(EnvelopePoint(position: position, value: min(1, max(0, normalizedValue(at:p.y))), curve: curve, formula: curve == "scripted" ? "mix(start, end, t)" : ""))
        points.sort { $0.position < $1.position }; selected = points.firstIndex(where: { $0.position == position }); onEdit?()
      }
    }
    dragging = true; needsDisplay = true; onSelect?()
  }
  private func snapped(_ x: CGFloat) -> Int {
    let raw = position(at:x)
    return min(rows * 256 - 1, max(0, Int((raw / Double(snap)).rounded()) * snap))
  }
  override func mouseDragged(with event: NSEvent) {
    guard dragging, let selected else { return }
    let p = convert(event.locationInWindow, from: nil)
    replaceSelected(position: snapped(p.x), value: normalizedValue(at:p.y), curve: points[selected].curve)
  }
  override func mouseUp(with event: NSEvent) {let finished=dragging;dragging = false;if finished{onEditFinished?()}}
  override func keyDown(with event: NSEvent) {
    if event.keyCode == 51 || event.keyCode == 117 { removeSelected(); return }
    if event.keyCode == 48 {
      guard !points.isEmpty else{return};let reverse=event.modifierFlags.contains(.shift)
      selected=((selected ?? (reverse ? 0 : -1))+(reverse ? points.count-1 : 1))%points.count
      if let selected{let p=Double(points[selected].position);if p<visibleStart || p>horizontalEnd{setViewport(start:p-horizontalSpan/2,span:horizontalSpan)}}
      needsDisplay=true;onSelect?();return
    }
    if let selected,points.indices.contains(selected),[123,124,125,126].contains(event.keyCode){
      let point=points[selected],step=event.modifierFlags.contains(.shift) ? 1 : snap,amount=event.modifierFlags.contains(.shift) ? 0.001 : 0.01
      replaceSelected(position:point.position+(event.keyCode==123 ? -step : event.keyCode==124 ? step : 0),value:point.value+(event.keyCode==125 ? -amount : event.keyCode==126 ? amount : 0),curve:point.curve);return
    }
    super.keyDown(with:event)
  }
}

final class PatternAutomationEditor: NSView, NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate {
  let canvas = AutomationCanvas(frame: .zero), plugin = NSPopUpButton(), search = NSSearchField(), table = NSTableView()
  var onActivity:((String,Int)->Void)?
  let heading = Theme.label("Pattern automation", size: 20, weight: .semibold)
  let status = Theme.label("", size: 12, color: Theme.muted)
  let curve = NSPopUpButton(), snap = NSPopUpButton()
  let formula = NSTextField(string:"mix(start, end, t)")
  let formulaHelp = Theme.label("t / beatOffset: 0–1 · start, end: 0–1 · beat: pattern beats · beats: segment beats",size:10,color:Theme.muted)
  var formulaBox: NSStackView!
  var formulaWorkbench: FormulaWorkbench?
  var bankWindow:EnvelopeBankWindow?
  var clearRecorded: (() -> Void)?
  weak var advancedTools: NSView?
  var formulaPreviewWork: DispatchWorkItem?, formulaPreviewGeneration = 0
  let viewportLabel = Theme.label("Whole pattern",size:10,color:Theme.muted)
  let pointRow = NSTextField(string: "0"), pointValue = NSTextField(string: "50")
  let enabled = NSButton(checkboxWithTitle: "Enabled", target: nil, action: nil)
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onContext: (() -> PatternModel)?
  var model = PatternModel([:]), revision = "", lanes = [[String: Any]](), values = [[String: Any]](), filtered = [[String: Any]]()
  var preferredParameter: Int?, preferredPointPosition: Int?
  var selectedPluginID:String?, pendingPluginID:String?, autoSaveWork:DispatchWorkItem?
  var requestGeneration=0
  var onPluginSelection: ((String)->Void)?
  func showPositions(editPattern:Int,row:Int,playPattern:Int?,position:Double?) {
    canvas.editPosition=editPattern==model.pattern ? Double(row*256):nil
    canvas.playbackPosition=playPattern==model.pattern ? position:nil
  }
  var pluginIndex = 0, parameterID: Int?, laneID: String?, loading = false, hasDraft = false
  let toolOperation = NSPopUpButton(), toolStart = NSTextField(string: "0"), toolEnd = NSTextField(string: "64")
  let toolValues = (0..<4).map { _ in NSTextField(string: "0") }
  let toolLabels = (0..<4).map { _ in Theme.label("", size: 12) }
  var toolValueGroups = [NSView](), envelopeClipboard: [String: Any]?, draftGeneration = 0
  let curves = ["step", "linear", "smooth", "exponential", "logarithmic", "step-next", "exponential-reverse", "logarithmic-reverse", "scripted"]
  override init(frame: NSRect) {
    super.init(frame: frame)
    plugin.target = self; plugin.action = #selector(selectPlugin)
    search.placeholderString = "Find a parameter"; search.delegate = self
    table.delegate = self; table.dataSource = self; table.rowHeight = 28; table.headerView = nil
    table.addTableColumn(NSTableColumn(identifier: .init("parameter")))
    table.setAccessibilityLabel("Automation parameters")
    let list = verticalScrollView(); list.documentView = table
    let left = stack(.vertical, [search, list]); left.fixed(width: 220); left.stretchAcrossAxis()
    curve.addItems(withTitles: ["Step", "Linear", "Smooth", "Exponential", "Logarithmic", "Step at start", "Exponential reversed", "Logarithmic reversed", "Scripted…"]); curve.selectItem(at: 1)
    curve.target = self; curve.action = #selector(changeCurve)
    curve.setAccessibilityLabel("Selected point outgoing curve")
    curve.toolTip = "Shape of the segment leaving the selected point, until the next point or pattern end."
    snap.addItems(withTitles: ["1 row", "½ row", "¼ row", "1/256 row"])
    snap.target = self; snap.action = #selector(changeSnap)
    pointRow.fixed(width: 65); pointValue.fixed(width: 65)
    pointRow.setAccessibilityLabel("Automation point row"); pointValue.setAccessibilityLabel("Automation point percent")
    for field in [pointRow,pointValue] {field.delegate=self;field.target=self;field.action=#selector(setPoint)}
    enabled.state = .on; enabled.target = self; enabled.action = #selector(markDraft)
    canvas.onEdit = { [weak self] in self?.markDraft() }
    canvas.onSelect = { [weak self] in self?.showPoint() }
    canvas.onViewport = { [weak self] in
      guard let self else { return }
      self.viewportLabel.stringValue=String(format:"Rows %.2f–%.2f",self.canvas.visibleStart/256,self.canvas.horizontalEnd/256)
      self.previewFormula()
    }
    canvas.toolTip = "Pinch or Option-scroll to zoom time; scroll to pan. Shift-Option-scroll zooms values. Fit resets both axes."
    formula.setAccessibilityLabel("Selected point curve formula");formula.delegate=self
    formula.font=NSFont.monospacedSystemFont(ofSize:12,weight:.regular)
    let formulaRow=stack(.horizontal,[formula,ActionButton("Expand…"){[weak self] in self?.expandFormula()},ActionButton("Reference"){[weak self] in FormulaWorkbench.showReference(self?.onRequest)}],spacing:4)
    formula.setContentHuggingPriority(.defaultLow,for:.horizontal)
    formulaBox=stack(.vertical,[formulaRow,formulaHelp,Theme.label("Autocomplete while typing or ⌃Space in the expanded editor. Valid changes save immediately.",size:10,color:Theme.muted)],spacing:3)
    formulaBox.stretchAcrossAxis();formulaBox.isHidden=true
    let right = stack(.vertical, [
      stack(.horizontal, [labeled("SELECTED POINT → NEXT", curve), labeled("SNAP", snap), NSView(), enabled]),
      formulaBox!,
      stack(.horizontal,[ActionButton("−"){[weak self] in self?.canvas.zoom(0.5)},ActionButton("+"){[weak self] in self?.canvas.zoom(2)},ActionButton("Fit"){[weak self] in self?.canvas.fit()},viewportLabel,NSView(),Theme.label("│ Edit  │ Play",size:10,color:Theme.muted)],spacing:6),
      canvas,
      stack(.horizontal, [Theme.label("Row", size: 12), pointRow, Theme.label("Value %", size: 12), pointValue,
        ActionButton("Add at cursor") { [weak self] in self?.addPointAtCursor() },
        ActionButton("Delete point") { [weak self] in self?.canvas.removeSelected() }, NSView()]),
    ]); right.stretchAcrossAxis()
    let editors = stack(.horizontal, [left, right], spacing: 16)
    left.heightAnchor.constraint(equalTo: editors.heightAnchor).isActive = true
    right.heightAnchor.constraint(equalTo: editors.heightAnchor).isActive = true
    canvas.heightAnchor.constraint(greaterThanOrEqualToConstant: 150).isActive = true
    let targetRow = stack(.horizontal, [plugin, ActionButton("Envelope bank…") { [weak self] in self?.showBank() }, ActionMenuButton { [weak self] in self?.automationMenu() ?? NSMenu() }])
    plugin.setContentHuggingPriority(.defaultLow, for: .horizontal)
    let tools = makeTools(); tools.isHidden = true; advancedTools = tools
    let content = stack(.vertical, [heading, targetRow, editors, tools,
      stack(.horizontal, [
        ActionButton("Ramp up") { [weak self] in self?.ramp(false) },
        ActionButton("Ramp down") { [weak self] in self?.ramp(true) }, NSView(),

      ]), status,
      Theme.label("Changes save immediately and update playback at the next audio buffer. Blue: editing cursor · Gold: playback. Envelopes repeat with the pattern.", size: 11, color: Theme.muted)
    ], spacing: 12)
    content.fill(self, inset: 20); content.stretchAcrossAxis()
  }
  required init?(coder: NSCoder) { fatalError() }
  func showBank(){
    if let bankWindow,bankWindow.window?.isVisible==true{bankWindow.window?.makeKeyAndOrderFront(nil);return}
    guard !loading,let parameterID,model.nativePlugins.indices.contains(pluginIndex),let plugin=model.nativePlugins[pluginIndex]["instanceID"] as? String,let onRequest else{return}
    let target:[String:Any]=["kind":"parameter","pattern":model.pattern,"plugin":plugin,"parameter":parameterID]
    let shape:[String:Any] = ["span":canvas.rows*256,"rowsPerBeat":model.rowsPerBeat,"points":canvas.points.map(\.dictionary)]
    let points=canvas.points,pattern=model.pattern;bankWindow?.close();bankWindow=EnvelopeBankWindow(title:"Pattern \(model.pattern)",target:target,shape:canvas.points.isEmpty ? nil:shape,revision:revision,request:onRequest,canReplace:{[weak self] in (self?.hasDraft==false || self?.canvas.points==points) && self?.model.pattern==pattern && self?.parameterID==parameterID && self?.pluginIndex==self?.model.nativePlugins.firstIndex(where:{$0["instanceID"] as? String==plugin})},applied:{[weak self] in self?.hasDraft=false;self?.load()})
  }
  func configureDocked() {
    guard let body=subviews.first as? NSStackView,body.arrangedSubviews.count>=6 else{return}
    heading.isHidden=true;body.arrangedSubviews.last?.isHidden=true;body.spacing=6
    let tools=body.arrangedSubviews[3];tools.isHidden=true

  }
  func addRecordingControls(_ record: NSButton, clear: @escaping () -> Void) {
    guard let body = subviews.first as? NSStackView, let row = body.arrangedSubviews[1] as? NSStackView else { return }
    record.title = "Record gestures"
    record.toolTip = "Capture manual plugin parameter changes while playing. Inspect captured points in Parameter activity → Recorded points."
    row.insertArrangedSubview(record, at: min(2, row.arrangedSubviews.count))
    clearRecorded = clear
  }
  func automationMenu() -> NSMenu {
    let menu = NSMenu(title: "Automation"); menu.autoenablesItems = false
    menu.addItem(ContextAction("Use last touched parameter") { [weak self] in self?.useLastTouched() })
    menu.addItem(ContextAction("Parameter activity…", enabled: parameterID != nil) { [weak self] in
      guard let self, let plugin = self.selectedPluginID, let parameter = self.parameterID else { return }; self.onActivity?(plugin, parameter)
    })
    menu.addItem(ContextAction("Range and transform tools…") { [weak self] in self?.advancedTools?.isHidden.toggle() })
    menu.addItem(ContextAction("Remove lane", enabled: laneID != nil) { [weak self] in self?.removeLane() })
    menu.addItem(ContextAction("Clear all recorded automation", enabled: clearRecorded != nil) { [weak self] in self?.clearRecorded?() })
    menu.addItem(ContextAction("Reload") { [weak self] in self?.load() })
    return menu
  }

  func load() {
    guard !loading, let context = onContext?() else { return }
    requestGeneration += 1;autoSaveWork?.cancel();autoSaveWork=nil
    draftGeneration += 1
    let samePattern = context.pattern == model.pattern
    preferredParameter = parameterID
    preferredPointPosition = canvas.selected.flatMap { canvas.points.indices.contains($0) ? canvas.points[$0].position : nil }
    if !samePattern { canvas.fit(); preferredPointPosition=nil }
    formulaPreviewGeneration += 1; formulaPreviewWork?.cancel()
    model = context; hasDraft = false; parameterID = nil
    if let selectedPluginID,let slot=model.nativePlugins.firstIndex(where:{$0["instanceID"] as? String==selectedPluginID}) {pluginIndex=slot}
    heading.stringValue = "Pattern \(model.pattern) · automation"
    plugin.removeAllItems()
    for (i, item) in model.nativePlugins.enumerated() { plugin.addItem(withTitle: "\(i + 1). \(item["name"] ?? "Plugin")") }
    pluginIndex = min(pluginIndex, max(0, model.nativePlugins.count - 1)); plugin.selectItem(at: pluginIndex)
    request("automation.pattern.get", ["pattern": model.pattern]) { data in
      self.lanes = data["lanes"] as? [[String: Any]] ?? []; self.canvas.rows = data["rows"] as? Int ?? 64
      self.toolStart.stringValue = "0"; self.toolEnd.stringValue = String(self.canvas.rows)
      self.loadParameters()
    }
  }
  func request(_ method: String, _ params: [String: Any], done: @escaping ([String: Any]) -> Void) {
    guard !loading, let onRequest else { return }
    loading = true
    let generation=requestGeneration
    onRequest(method, params) { [weak self] reply in
      guard let self,generation==self.requestGeneration else { return }; self.loading = false
      guard let result = reply["result"] as? [String: Any] else {
        let error=reply["error"] as? [String:Any]
        self.status.stringValue = error?["message"] as? String ?? "Operation failed"
        if error?["code"] as? Int == -32002 {
          self.loading=true
          DispatchQueue.main.asyncAfter(deadline:.now()+0.035){[weak self] in
            guard let self,self.requestGeneration==generation else{return}
            self.loading=false;self.request(method,params,done:done)
          }
        }
        return
      }
      let token = result["revision"] as? String ?? self.revision
      if (method == "plugin.parameters.get" || method == "automation.pattern.copy") && token != self.revision {
        self.status.stringValue = "Song changed while loading. Reload before editing."; return
      }
      self.revision = token
      if let data = result["data"] as? [String: Any] { done(data) }
      else { done(["values": result["data"] as? [[String: Any]] ?? []]) }
      self.finishPendingSelection()
    }
  }
  func loadParameters() {
    canvas.allowsEditing=false
    table.deselectAll(nil);values=[];filtered=[];table.reloadData()
    canvas.points = []; canvas.selected = nil; laneID = nil; parameterID = nil
    guard model.nativePlugins.indices.contains(pluginIndex) else {
      values = []; filter(); status.stringValue = "Add an AU or VST3 plugin to create an envelope."; return
    }
    selectedPluginID=model.nativePlugins[pluginIndex]["instanceID"] as? String
    request("plugin.parameters.get", ["slot": pluginIndex]) { data in
      self.values = data["values"] as? [[String: Any]] ?? []; self.filter()
      if !self.filtered.isEmpty { let row = self.filtered.firstIndex { $0["id"] as? Int == self.preferredParameter } ?? 0; self.table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false);self.tableViewSelectionDidChange(Notification(name:NSTableView.selectionDidChangeNotification)) }
    }
  }
  @objc func selectPlugin() {
    let desired=plugin.indexOfSelectedItem
    guard model.nativePlugins.indices.contains(desired) else{return}
    if hasDraft || loading {pendingPluginID=model.nativePlugins[desired]["instanceID"] as? String;plugin.selectItem(at:pluginIndex);apply();return}
    switchPlugin(desired)
  }
  func switchPlugin(_ index:Int) {
    requestGeneration += 1;draftGeneration += 1;autoSaveWork?.cancel();autoSaveWork=nil
    preferredParameter=nil;preferredPointPosition=nil;search.stringValue="";pluginIndex=index;plugin.selectItem(at:index)
    if let id=model.nativePlugins[index]["instanceID"] as? String {selectedPluginID=id;onPluginSelection?(id)}
    loadParameters()
  }
  func finishPendingSelection() {
    guard !loading,!hasDraft,let pending=pendingPluginID else{return}
    pendingPluginID=nil
    guard let index=model.nativePlugins.firstIndex(where:{$0["instanceID"] as? String==pending}) else{return}
    switchPlugin(index)
  }
  func controlTextDidChange(_ notification: Notification) {
    if notification.object as? NSTextField === formula {
      guard let selected=canvas.selected,canvas.points.indices.contains(selected) else{return}
      canvas.points[selected].formula=formula.stringValue;markDraft();FormulaCatalog.suggest(formula.currentEditor() as? NSTextView)
    } else if notification.object as? NSTextField === pointRow || notification.object as? NSTextField === pointValue {setPoint()}
    else { filter() }
  }
  func filter() {
    guard !hasDraft else {status.stringValue="Wait for pending changes to save before filtering.";return}
    let selected=parameterID
    filtered = search.stringValue.isEmpty ? values : values.filter { ($0["name"] as? String ?? "").localizedCaseInsensitiveContains(search.stringValue) }
    table.deselectAll(nil);table.reloadData()
    if let selected,let row=filtered.firstIndex(where:{$0["id"] as? Int==selected}) {table.selectRowIndexes(IndexSet(integer:row),byExtendingSelection:false)}
    else if selected != nil {parameterID=nil;laneID=nil;canvas.points=[];canvas.selected=nil;canvas.allowsEditing=false;showPoint()}
  }
  func numberOfRows(in tableView: NSTableView) -> Int { filtered.count }
  func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
    let view = tableView.makeView(withIdentifier: .init("name"), owner: self) as? NSTextField ?? Theme.label("", size: 12)
    view.identifier = .init("name"); view.stringValue = filtered[row]["name"] as? String ?? "Parameter"; return view
  }
  func tableView(_ tableView: NSTableView, shouldSelectRow row: Int) -> Bool {
    if hasDraft || loading { status.stringValue = "Changes are saving; select the parameter again in a moment."; return false }; return true
  }
  func tableViewSelectionDidChange(_ notification: Notification) {
    guard !loading, !hasDraft, model.nativePlugins.indices.contains(pluginIndex), filtered.indices.contains(table.selectedRow) else { return }
    draftGeneration += 1
    parameterID = filtered[table.selectedRow]["id"] as? Int
    canvas.allowsEditing=true
    let instance = model.nativePlugins[pluginIndex]["instanceID"] as? String
    let lane = lanes.first { $0["plugin"] as? String == instance && $0["parameter"] as? Int == parameterID }
    laneID = lane?["id"] as? String; enabled.state = lane?["enabled"] as? Bool == false ? .off : .on
    canvas.points = (lane?["points"] as? [[String: Any]] ?? []).map {
      EnvelopePoint(position: $0["position"] as? Int ?? 0, value: $0["value"] as? Double ?? 0, curve: $0["curve"] as? String ?? "linear", formula: $0["formula"] as? String ?? "")
    }
    canvas.selected = canvas.points.firstIndex { $0.position == preferredPointPosition };preferredPointPosition=nil;showPoint();previewFormula()
    status.stringValue = lane == nil ? "Click the graph or generate a ramp to create an envelope." : "\(canvas.points.count) points · changes save immediately."
  }
  @objc func markDraft() {guard parameterID != nil else{return};draftGeneration += 1;hasDraft=true;status.stringValue="Saving changes…";previewFormula();saveSoon()}
  func saveSoon() {
    guard autoSaveWork == nil else { return }
    let generation=requestGeneration
    let work=DispatchWorkItem{[weak self] in
      guard let self,self.requestGeneration==generation else{return}
      self.autoSaveWork=nil;self.apply()
    }
    autoSaveWork=work;DispatchQueue.main.asyncAfter(deadline:.now()+0.035,execute:work)
  }
  @objc func changeSnap() { canvas.snap = [256, 128, 64, 1][max(0, snap.indexOfSelectedItem)] }
  @objc func changeCurve() {
    canvas.curve = curves[max(0, curve.indexOfSelectedItem)]
    if let selected = canvas.selected { let point = canvas.points[selected]; canvas.replaceSelected(position: point.position, value: point.value, curve: canvas.curve) }
  }
  func showPoint() {
    let hasPoint = canvas.selected.map { canvas.points.indices.contains($0) } ?? false
    pointRow.isEnabled=hasPoint; pointValue.isEnabled=hasPoint
    pointRow.toolTip=hasPoint ? "Commits on Return or leaving this field" : "Click the curve or use Add at cursor first"
    pointValue.toolTip=pointRow.toolTip
    guard let selected = canvas.selected, canvas.points.indices.contains(selected) else { formulaBox.isHidden=true; return }
    let point = canvas.points[selected]
    canvas.curve = point.curve
    formulaBox.isHidden = point.curve != "scripted"
    formula.stringValue = point.formula
    if point.curve=="scripted"{FormulaCatalog.load(onRequest)}
    pointRow.stringValue = String(format: "%.8g", Double(point.position) / 256)
    pointValue.stringValue = String(format: "%.6g", point.value * 100)
    curve.selectItem(at: curves.firstIndex(of: point.curve) ?? 1)
    canvas.setAccessibilityValue("Row \(pointRow.stringValue), \(pointValue.stringValue) percent")
  }
  func previewFormula() {
    formulaPreviewGeneration += 1; let generation=formulaPreviewGeneration
    formulaPreviewWork?.cancel()
    guard canvas.points.contains(where:{$0.curve=="scripted"}),let onRequest else { return }
    let points=canvas.points.map(\.dictionary), start=canvas.visibleStart, end=canvas.horizontalEnd
    let work=DispatchWorkItem { [weak self] in
      guard let self, generation==self.formulaPreviewGeneration else{return}
      onRequest("automation.formula.preview",["points":points,"rows":self.canvas.rows,"rowsPerBeat":self.model.rowsPerBeat,"start":start,"end":end,"samples":2048]) { [weak self] response in
        guard let self,generation==self.formulaPreviewGeneration else{return}
        if let data=(response["result"] as? [String:Any])?["data"] as? [String:Any], let values=data["values"] as? [[Double]] {
          self.canvas.previewValues=values.filter{$0.count==2}.map{($0[0],$0[1])}
          // A successful preview must not hide a failed save or revision conflict.
          if !self.hasDraft {self.status.stringValue="Scripted envelope · select a point to edit its formula."}
        } else if let error=response["error"] as? [String:Any] {
          if error["code"] as? Int == -32002 { self.previewFormula() }
          else { self.status.stringValue=error["message"] as? String ?? "Invalid formula" }
        }
      }
    }
    formulaPreviewWork=work;DispatchQueue.main.asyncAfter(deadline:.now()+0.15,execute:work)
  }
  func control(_ control:NSControl,textView:NSTextView,completions words:[String],forPartialWordRange range:NSRange,indexOfSelectedItem index:UnsafeMutablePointer<Int>)->[String]{
    guard control === formula else{return words};index.pointee = -1;return FormulaCatalog.completions(textView.string,range:range)
  }
  func expandFormula(){
    if let formulaWorkbench,formulaWorkbench.window?.isVisible==true{formulaWorkbench.window?.makeKeyAndOrderFront(nil);return}
    guard let selected=canvas.selected,canvas.points.indices.contains(selected),canvas.points[selected].curve=="scripted" else{return}
    let token=draftGeneration
    formulaWorkbench?.close()
    formulaWorkbench=FormulaWorkbench(source:canvas.points[selected].formula,title:"Pattern \(model.pattern) · formula",points:canvas.points.map(\.dictionary),selected:selected,rows:canvas.rows,rowsPerBeat:model.rowsPerBeat,request:onRequest){[weak self] text in
      guard let self,self.draftGeneration==token,self.canvas.selected==selected,self.canvas.points.indices.contains(selected) else{return false}
      self.canvas.points[selected].formula=text;self.formula.stringValue=text;self.markDraft();return true
    }
  }
  @objc func setPoint() {
    guard let row = Double(pointRow.stringValue), let value = Double(pointValue.stringValue),
      row.isFinite, value.isFinite, row >= 0, row < Double(canvas.rows), value >= 0, value <= 100 else {
      status.stringValue = "Enter a row inside the pattern and a value from 0 to 100%."; return
    }
    canvas.replaceSelected(position: Int((row * 256).rounded()), value: value / 100, curve: curves[max(0, curve.indexOfSelectedItem)])
  }
  func addPointAtCursor() {
    guard parameterID != nil, !loading else { return }
    let position = max(0,min(canvas.rows * 256 - 1,Int(canvas.editPosition ?? 0)))
    if let existing=canvas.points.firstIndex(where: { $0.position==position }) { canvas.selected=existing; showPoint(); return }
    canvas.points.append(EnvelopePoint(position:position,value:0.5,curve:canvas.curve,formula:canvas.curve == "scripted" ? "mix(start, end, t)" : ""))
    canvas.points.sort { $0.position < $1.position }; canvas.selected=canvas.points.firstIndex { $0.position==position }
    markDraft(); showPoint()
  }
  func ramp(_ down: Bool) {
    guard parameterID != nil else { return }
    canvas.points = [EnvelopePoint(position: 0, value: down ? 1 : 0, curve: canvas.curve, formula: canvas.curve == "scripted" ? "mix(start, end, t)" : ""),
                     EnvelopePoint(position: canvas.rows * 256 - 1, value: down ? 0 : 1, curve: canvas.curve, formula: canvas.curve == "scripted" ? "mix(start, end, t)" : "")]
    canvas.selected = 0; markDraft(); showPoint()
  }
  func apply() {
    guard hasDraft,!loading,let parameterID,model.nativePlugins.indices.contains(pluginIndex),
      let instance=model.nativePlugins[pluginIndex]["instanceID"] as? String else{return}
    let generation=draftGeneration,pattern=model.pattern,points=canvas.points,isEnabled=enabled.state == .on
    let removing=points.isEmpty
    if removing && laneID==nil {hasDraft=false;finishPendingSelection();return}
    let params:[String:Any]=removing ? ["expectedRevision":revision,"lane":laneID!] :
      ["expectedRevision":revision,"pattern":pattern,"plugin":instance,"parameter":parameterID,"enabled":isEnabled,"points":points.map(\.dictionary)]
    request(removing ? "automation.pattern.remove":"automation.pattern.set",params) {data in
      self.lanes.removeAll{$0["plugin"] as? String==instance && $0["parameter"] as? Int==parameterID}
      self.laneID=removing ? nil:data["lane"] as? String
      if let id=self.laneID {self.lanes.append(["id":id,"plugin":instance,"parameter":parameterID,"enabled":isEnabled,"points":points.map(\.dictionary)])}
      if self.draftGeneration==generation {self.hasDraft=false;self.status.stringValue="Saved · playback uses the current envelope."}
      else {self.saveSoon()}
    }
  }
  func removeLane() {
    guard let laneID else { return }
    request("automation.pattern.remove", ["expectedRevision": revision, "lane": laneID]) { _ in self.load() }
  }
}
