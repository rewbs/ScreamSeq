import AppKit

final class SongTimingEditor: NSView {
  let mode = NSPopUpButton(), tempo = NSTextField(string: "125"), speed = NSTextField(string: "6")
  let beat = NSTextField(string: "4"), bar = NSTextField(string: "16"), groove = NSTextField(string: "")
  let swing = NSTextField(string: "62.5")
  let status = Theme.label("", size: 12, color: Theme.muted)
  let scope = Theme.label("", size: 12, color: Theme.muted)
  private(set) var revision: String?, pending = false
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  private var buttons = [NSButton]()
  override init(frame: NSRect) {
    super.init(frame: frame)
    mode.addItems(withTitles: ["Classic tracker timing", "Alternative tracker timing", "Musical timing (BPM + rows per beat)"])
    mode.setAccessibilityLabel("Timing mode")
    for (field, label) in [(tempo,"Tempo in BPM"),(speed,"Ticks per row"),(beat,"Rows per beat"),(bar,"Rows per bar"),(swing,"First row swing percentage")] {
      field.fixed(width: 96); field.setAccessibilityLabel(label)
    }
    groove.placeholderString = "Straight timing (no groove)"; groove.setAccessibilityLabel("Groove row durations")
    groove.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    status.maximumNumberOfLines = 3; status.lineBreakMode = .byWordWrapping
    status.heightAnchor.constraint(greaterThanOrEqualToConstant: 52).isActive = true
    scope.maximumNumberOfLines = 3; scope.lineBreakMode = .byWordWrapping
    let explanation = Theme.label("Groove uses one duration per row of a beat, separated by commas. Values are normalized to keep the beat length. Pattern timing overrides take precedence. Applying stops playback and can be undone.", size: 12, color: Theme.muted)
    explanation.maximumNumberOfLines = 5; explanation.lineBreakMode = .byWordWrapping
    for label in [status,scope,explanation] { label.preferredMaxLayoutWidth = 576; label.setContentCompressionResistancePriority(.defaultLow, for: .horizontal) }
    let preset = ActionButton("Set swing") { [weak self] in self?.setSwing() }
    let straight = ActionButton("Straight") { [weak self] in self?.groove.stringValue = "" }
    let preview = ActionButton("Preview") { [weak self] in self?.apply(dryRun: true) }
    let apply = ActionButton("Apply") { [weak self] in self?.apply(dryRun: false) }
    let reload = ActionButton("Reload") { [weak self] in self?.load() }
    buttons = [preset,straight,preview,apply,reload]
    func row(_ name: String, _ control: NSView) -> NSView { stack(.horizontal,[Theme.label(name,size:12),control,NSView()],spacing:12) }
    let content = stack(.vertical,[Theme.label("Tempo and groove",size:22,weight:.semibold),scope,mode,
      stack(.horizontal,[row("BPM",tempo),row("Ticks / row",speed)],spacing:24),
      stack(.horizontal,[row("Rows / beat",beat),row("Rows / bar",bar)],spacing:24),
      Theme.label("GROOVE DURATIONS",size:10,color:Theme.muted),groove,
      stack(.horizontal,[Theme.label("First row %",size:12),swing,preset,straight,NSView()],spacing:12),
      explanation,stack(.horizontal,[preview,apply,reload,NSView()],spacing:12),status],spacing:14)
    content.stretchAcrossAxis();content.translatesAutoresizingMaskIntoConstraints=false;addSubview(content)
    NSLayoutConstraint.activate([content.leadingAnchor.constraint(equalTo:leadingAnchor,constant:24),
      content.trailingAnchor.constraint(equalTo:trailingAnchor,constant:-24),content.topAnchor.constraint(equalTo:topAnchor,constant:24),
      content.bottomAnchor.constraint(lessThanOrEqualTo:bottomAnchor,constant:-20)])
    controls()
  }
  required init?(coder: NSCoder) { fatalError() }
  func load() {
    guard !pending,let onRequest else{return};pending=true;controls();status.stringValue="Reading timing…"
    onRequest("document.timing.get",[:]) { [weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],let revision=result["revision"] as? String else {self.failure(reply);return}
      self.revision=revision;self.show(data);self.status.stringValue="Ready. Preview checks the settings without changing playback.";self.controls()
    }
  }
  private func show(_ data:[String:Any]) {
    mode.selectItem(at:["classic","alternative","modern"].firstIndex(of:data["mode"] as? String ?? "") ?? 0)
    for (field,key) in [(tempo,"tempo"),(speed,"speed"),(beat,"rowsPerBeat"),(bar,"rowsPerMeasure")] {field.stringValue=(data[key] as? NSNumber)?.stringValue ?? ""}
    groove.stringValue=(data["groove"] as? [NSNumber] ?? []).map{String(format:"%.8g",$0.doubleValue)}.joined(separator:", ")
    let count=(data["patternOverrides"] as? [Int] ?? []).count
    scope.stringValue="Sequence \((data["sequence"] as? Int ?? 0)+1): tempo and ticks per row. Other settings apply to the song.\(count>0 ? " \(count) pattern(s) have timing overrides." : "")"
  }
  func setSwing() {
    guard !pending else{return}
    guard let percent=Double(swing.stringValue),percent.isFinite,(12.5...87.5).contains(percent),let rows=Int(beat.stringValue),(2...32).contains(rows),rows%2==0 else {
      status.stringValue="Swing needs an even rows-per-beat count and a first-row share from 12.5% to 87.5%.";return
    }
    mode.selectItem(at:2)
    groove.stringValue=(0..<rows).map{String(format:"%.8g",$0%2==0 ? percent/50 : 2-percent/50)}.joined(separator:", ")
    status.stringValue="Swing is ready to preview or apply."
  }
  func apply(dryRun:Bool) {
    guard !pending,let revision,let onRequest else{return}
    guard let bpm=Double(tempo.stringValue),bpm.isFinite,(32...512).contains(bpm),
      let ticks=Int(speed.stringValue),(1...31).contains(ticks),let rows=Int(beat.stringValue),(1...32).contains(rows),
      let measure=Int(bar.stringValue),(rows...128).contains(measure) else {status.stringValue="Use BPM 32–512, ticks 1–31, rows per beat 1–32, and a bar at least one beat long (up to 128 rows).";return}
    let text=groove.stringValue.trimmingCharacters(in:.whitespacesAndNewlines)
    let pieces=text.isEmpty ? [] : text.split(separator:",",omittingEmptySubsequences:false).map{String($0).trimmingCharacters(in:.whitespaces)}
    let weights=pieces.compactMap(Double.init)
    guard weights.count==pieces.count,weights.allSatisfy({$0.isFinite && (0.25...4).contains($0)}),
      weights.isEmpty || (weights.count==rows && mode.indexOfSelectedItem==2) else {status.stringValue="Use musical timing and one duration from 0.25 to 4 per row of a beat, or leave groove empty.";return}
    pending=true;controls();status.stringValue=dryRun ? "Checking timing…" : "Applying timing…"
    onRequest("document.timing.set",["expectedRevision":revision,"mode":["classic","alternative","modern"][mode.indexOfSelectedItem],
      "tempo":bpm,"speed":ticks,"rowsPerBeat":rows,"rowsPerMeasure":measure,"groove":weights,"dryRun":dryRun]) { [weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],let after=data["after"] as? [String:Any],let revision=result["revision"] as? String else {self.failure(reply);return}
      self.revision=revision
      if !dryRun {self.show(after)}
      self.status.stringValue=(data["wouldChange"] as? Bool == true) ? (dryRun ? "Settings are valid. Apply to save them; playback will stop." : "Timing saved. Undo restores the previous settings.") : "These settings are already saved."
      self.controls()
    }
  }
  private func failure(_ reply:[String:Any]) {status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Timing request failed.";controls()}
  private func controls() {
    for field in [tempo,speed,beat,bar,groove,swing] {field.isEnabled = !pending && revision != nil}
    mode.isEnabled = !pending && revision != nil
    for button in buttons {button.isEnabled = !pending && (revision != nil || button.title=="Reload")}
  }
}
