import AppKit

final class InstrumentEnvelopeClipboard { var clip: [String:Any]? }
final class InstrumentEnvelopeToolsEditor: NSView {
  let instrument:String,kind:String,clipboard:InstrumentEnvelopeClipboard
  let operation=NSPopUpButton(),start=NSTextField(string:"0"),end=NSTextField(string:"49")
  let values=(0..<5).map{_ in NSTextField(string:"")}
  let labels=(0..<5).map{_ in Theme.label("",size:12)}
  let canvas=EnvelopeView(frame:.zero),status=Theme.label("",size:12,color:Theme.muted)
  let title=Theme.label("",size:13),markers=Theme.label("",size:12,color:Theme.muted)
  private var valueRows=[NSView](),buttons=[NSButton]()
  private(set) var revision:String?,pending=false,preview:[String:Any]?,baseline:[String:Any]=[:]
  private var previewSignature:NSDictionary?
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  var applyButton:ActionButton!
  let operations=["flip-time","flip-values","shift","scale","ramp","sine","humanize","paste","insert"]
  init(instrument:String,kind:String,clipboard:InstrumentEnvelopeClipboard=InstrumentEnvelopeClipboard()) {
    self.instrument=instrument;self.kind=kind;self.clipboard=clipboard;super.init(frame:.zero)
    operation.addItems(withTitles:["Flip time","Flip values","Shift","Scale","Ramp","Sine","Humanize","Paste","Insert paste"])
    operation.target=self;operation.action=#selector(changeOperation);operation.fixed(width:150)
    start.fixed(width:72);end.fixed(width:72);start.setAccessibilityLabel("First envelope tick");end.setAccessibilityLabel("End envelope tick, exclusive")
    canvas.fixed(height:170);canvas.canEdit={false};canvas.setAccessibilityLabel("Instrument envelope tool preview")
    for i in values.indices {
      values[i].fixed(width:84);labels[i].fixed(width:120)
      labels[i].isEditable=false;labels[i].isSelectable=false;labels[i].isBordered=false;labels[i].isBezeled=false;labels[i].drawsBackground=false
      valueRows.append(stack(.horizontal,[labels[i],values[i]],spacing:8))
    }
    for text in [title,markers,status] {text.maximumNumberOfLines=3;text.lineBreakMode = .byWordWrapping;text.preferredMaxLayoutWidth=624;text.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
    status.heightAnchor.constraint(greaterThanOrEqualToConstant:54).isActive=true
    let copy=ActionButton("Copy range"){[weak self] in self?.copyRange()}
    let check=ActionButton("Preview"){[weak self] in self?.previewTool()}
    applyButton=ActionButton("Apply"){[weak self] in self?.apply()}
    let reload=ActionButton("Reload"){[weak self] in self?.load()};buttons=[copy,check,applyButton,reload]
    let explanation=Theme.label("Values are 0–64 and time uses whole ticks. Preview shows the saved result. Apply stops playback and saves one Undo step.",size:12,color:Theme.muted)
    explanation.maximumNumberOfLines=3;explanation.lineBreakMode = .byWordWrapping;explanation.preferredMaxLayoutWidth=624
    explanation.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    let settings=stack(.vertical,[stack(.horizontal,Array(valueRows[0...1])+[NSView()],spacing:24),
      stack(.horizontal,Array(valueRows[2...3])+[NSView()],spacing:24),stack(.horizontal,[valueRows[4],NSView()])],spacing:8)
    settings.stretchAcrossAxis()
    let content=stack(.vertical,[Theme.label("Instrument envelope tools",size:22,weight:.semibold),title,canvas,markers,
      stack(.horizontal,[operation,Theme.label("Ticks",size:12),start,Theme.label("to",size:12),end,NSView()],spacing:12),settings,
      explanation,stack(.horizontal,[copy,check,applyButton!,reload,NSView()],spacing:12),status],spacing:14)
    content.stretchAcrossAxis();content.translatesAutoresizingMaskIntoConstraints=false;addSubview(content)
    NSLayoutConstraint.activate([content.leadingAnchor.constraint(equalTo:leadingAnchor,constant:24),content.trailingAnchor.constraint(equalTo:trailingAnchor,constant:-24),
      content.topAnchor.constraint(equalTo:topAnchor,constant:24),content.bottomAnchor.constraint(lessThanOrEqualTo:bottomAnchor,constant:-20)])
    changeOperation();controls()
  }
  required init?(coder:NSCoder){fatalError()}
  @objc func changeOperation() {
    let configs:[[(String,String)]]=[[],[],[("Shift (ticks)","1")],[("Multiply","1"),("Add","0")],[("From","0"),("To","64")],
      [("Center","32"),("Amplitude","32"),("Cycles","1"),("Phase (°)","0"),("Spacing (ticks)","4")],
      [("Value jitter","3"),("Time jitter (ticks)","0"),("Seed","0")],[("Repeats","1")],[("Repeats","1")]]
    let config=configs[max(0,operation.indexOfSelectedItem)]
    for i in values.indices {valueRows[i].isHidden=i>=config.count;if i<config.count {labels[i].stringValue=config[i].0;values[i].stringValue=config[i].1;values[i].setAccessibilityLabel(config[i].0)}}
    if !baseline.isEmpty {show(baseline)};preview=nil;previewSignature=nil;controls()
  }
  private var signature:NSDictionary { ["operation":operation.indexOfSelectedItem,"start":start.stringValue,"end":end.stringValue,"values":values.map(\.stringValue)] }
  private func accepts(_ data:[String:Any])->Bool {data["instrument"] as? String==instrument && data["envelope"] as? String==kind}
  func load() {
    guard !pending,let onRequest else{return};pending=true;controls()
    onRequest("instrument.envelope.get",["instrument":instrument,"envelope":kind]){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],self.accepts(data),let revision=result["revision"] as? String else{self.failure(reply);return}
      self.revision=revision;self.baseline=data;self.preview=nil;self.previewSignature=nil;self.show(data)
      self.start.stringValue="0";self.end.stringValue=String((data["points"] as? [[Int]])?.last.map{$0[0]+1} ?? 49)
      self.status.stringValue=data["editable"] as? Bool==true ? "Choose a tool, then Preview. Reload discards the preview." : "This envelope is not editable in this module format.";self.controls()
    }
  }
  private func show(_ data:[String:Any]) {
    title.stringValue="\(data["index"] as? Int ?? 0). \(data["name"] as? String ?? "Instrument") · \(kind.capitalized) · \(data["maxPoints"] as? Int ?? 0) points maximum"
    canvas.points=data["points"] as? [[Int]] ?? [];canvas.selectedNode=nil
    func marker(_ name:String,_ enabled:String,_ first:String,_ last:String)->String {data[enabled] as? Bool==true ? "\(name): nodes \(data[first] as? Int ?? 0)–\(data[last] as? Int ?? 0)" : "\(name): off"}
    let release=data["releaseNode"] as? Int ?? 255
    markers.stringValue=[marker("Loop","loop","loopStart","loopEnd"),marker("Sustain","sustain","sustainPoint","sustainEnd"),release==255 ? "Release: none" : "Release: node \(release)"].joined(separator:" · ")
  }
  private func range()->(Int,Int)? {
    guard let first=Int(start.stringValue),let last=Int(end.stringValue),(0...65535).contains(first),(1...65536).contains(last),first<last else{status.stringValue="Use a nonempty whole-tick range within 0–65536.";return nil};return(first,last)
  }
  func copyRange() {
    guard !pending,let revision,let onRequest else{return}
    guard preview==nil else{status.stringValue="Apply or Reload before copying saved points.";return}
    guard let (first,last)=range() else{return};pending=true;controls()
    onRequest("instrument.envelope.copy",["instrument":instrument,"envelope":kind,"start":first,"end":last]){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any] else{self.failure(reply);return}
      guard result["revision"] as? String==revision else{self.status.stringValue="The song changed. Reload before copying.";self.controls();return}
      self.clipboard.clip=data;self.status.stringValue="Copied \((data["points"] as? [Any])?.count ?? 0) points. This clipboard also works in other instrument envelopes.";self.controls()
    }
  }
  func previewTool() {
    guard !pending,let revision,let onRequest,baseline["editable"] as? Bool==true,let (first,last)=range() else{return}
    let op=operations[max(0,operation.indexOfSelectedItem)];var numbers=[Double]()
    for i in values.indices where !valueRows[i].isHidden {guard let n=Double(values[i].stringValue),n.isFinite else{status.stringValue="Enter finite numbers in the tool settings.";return};numbers.append(n)}
    var options:[String:Any]=[:]
    switch op {
    case "shift":options=["amount":numbers[0]]
    case "scale":options=["amount":numbers[0],"offset":numbers[1]]
    case "ramp":options=["from":numbers[0],"to":numbers[1]]
    case "sine":options=["center":numbers[0],"amplitude":numbers[1],"cycles":numbers[2],"phase":numbers[3],"spacing":numbers[4]]
    case "humanize":options=["amount":numbers[0],"jitter":numbers[1],"seed":numbers[2]]
    case "paste","insert":guard let clip=clipboard.clip else{status.stringValue="Copy a saved instrument envelope range first.";return};options=["clip":clip,"repeats":numbers[0]]
    default:break
    }
    var params:[String:Any]=["instrument":instrument,"envelope":kind,"expectedRevision":revision,"operation":op,"start":first,"options":options,"dryRun":true]
    if op != "paste" && op != "insert" {params["end"]=last}
    let signature=self.signature;pending=true;controls()
    onRequest("instrument.envelope.transform",params){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],let after=data["after"] as? [String:Any],self.accepts(after) else{self.failure(reply);return}
      guard result["revision"] as? String==revision,self.signature.isEqual(signature) else{self.status.stringValue="Settings changed; preview discarded. Preview again.";self.controls();return}
      self.show(after);self.preview=(data["wouldChange"] as? Bool==true) ? params : nil;self.previewSignature=signature
      self.status.stringValue=self.preview==nil ? "This tool leaves the envelope unchanged." : "Preview: \((after["points"] as? [Any])?.count ?? 0) points; \(data["clippedValues"] as? Int ?? 0) clipped, \(data["roundedValues"] as? Int ?? 0) rounded, \(data["reanchoredMarkers"] as? Int ?? 0) markers reattached. Apply saves one Undo step."
      self.controls()
    }
  }
  func apply() {
    guard !pending,var params=preview,let onRequest else{return}
    guard let previewSignature,signature.isEqual(previewSignature) else{status.stringValue="Settings changed. Preview again before applying.";return}
    params["dryRun"]=false;pending=true;controls()
    onRequest("instrument.envelope.transform",params){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],let after=data["after"] as? [String:Any],self.accepts(after),let revision=result["revision"] as? String else{self.failure(reply);return}
      self.revision=revision;self.baseline=after;self.preview=nil;self.previewSignature=nil;self.show(after);self.status.stringValue="Envelope saved. Undo restores its points and markers together.";self.controls()
    }
  }
  private func failure(_ reply:[String:Any]){status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Could not read this instrument envelope.";controls()}
  private func controls(){let editable = !pending && revision != nil && baseline["editable"] as? Bool==true
    operation.isEnabled=editable;for field in [start,end]+values{field.isEnabled=editable}
    for button in buttons {button.isEnabled = !pending && (button.title=="Reload" || editable)}
    applyButton?.isEnabled=editable && preview != nil
  }
}
