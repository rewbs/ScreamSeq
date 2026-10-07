import AppKit
enum PatternTimingUnit:String,CaseIterable {
  case beats,rows
  var title:String {self == .beats ? "Beats" : "Rows"}
  var suffix:String {self == .beats ? "b" : "r"}
  func rowScale(_ model:PatternModel)->Double {self == .beats ? Double(max(1,model.rowsPerBeat)) : 1}
}

/// Presentation only. The shared catalogue supplies semantics and API storage keys.
struct PatternEffectField {
  let key:String,storage:String,name:String,type:String,unit:String,displayUnit:String
  let minimum:Double,maximum:Double,defaultValue:Any,choices:[String]
  let width:Float
  let inline:Bool
  init(_ raw:[String:Any]) {
    key=raw["key"] as? String ?? "value";storage=raw["storage"] as? String ?? key
    name=raw["name"] as? String ?? raw["label"] as? String ?? key.capitalized
    type=raw["type"] as? String ?? "number";unit=raw["unit"] as? String ?? "";displayUnit=raw["displayUnit"] as? String ?? ""
    minimum=(raw["minimum"] as? NSNumber)?.doubleValue ?? -Double.greatestFiniteMagnitude
    maximum=(raw["maximum"] as? NSNumber)?.doubleValue ?? Double.greatestFiniteMagnitude
    let initial=raw["default"] ?? 0
    if type=="integer",let number=initial as? NSNumber,number.doubleValue.isFinite,number.doubleValue>=Double(Int.min),number.doubleValue<Double(Int.max) {defaultValue=number.intValue}
    else {defaultValue=initial}
    choices=raw["choices"] as? [String] ?? []
    width=Float(max(4,min(14,(raw["width"] as? NSNumber)?.doubleValue ?? 7)))*9+8
    inline=raw["inline"] as? Bool ?? true
  }
  var rowUnits:Bool {unit=="row-units" || storage=="duration" || storage=="offset"}
  var timing:Bool {rowUnits || unit=="beats"}
  var percent:Bool {displayUnit=="percent" || unit=="normalized" || unit=="percent-normalized"}
  func value(_ command:NativePatternCommand)->Any {
    if storage=="offset" {return command.position%65536}
    if storage.hasPrefix("parameters.") {return command.parameters[String(storage.dropFirst(11))] ?? defaultValue}
    return command.dictionary[storage] ?? defaultValue
  }
  func number(_ raw:Any,model:PatternModel,timing:PatternTimingUnit,rateMode:String?=nil)->Double {
    let value=(raw as? NSNumber)?.doubleValue ?? 0
    if rowUnits {return value/65536/timing.rowScale(model)}
    if unit=="beats" {return timing == .beats ? value : value*Double(max(1,model.rowsPerBeat))}
    if unit=="cycles-per-beat-or-hz",rateMode != "hz",timing == .rows {return value/Double(max(1,model.rowsPerBeat))}
    return percent ? value*100 : value
  }
  func suffix(_ timing:PatternTimingUnit)->String {self.timing ? timing.suffix : percent ? "%" : unit=="semitones" ? "st" : ""}
  func text(_ raw:Any,model:PatternModel,timing:PatternTimingUnit,editing:Bool=false,rateMode:String?=nil)->String {
    if type=="boolean" || type=="bool" {return (raw as? Bool ?? false) ? "on" : "off"}
    if !choices.isEmpty || type=="choice" {return raw as? String ?? choices.first ?? ""}
    let value=number(raw,model:model,timing:timing,rateMode:rateMode)
    // Display rounding is cosmetic. The editor retains the original scalar when
    // the corresponding field was not changed, including after unit conversion.
    if editing {return String(format:"%.17g",value)}
    let suffix=unit=="cycles-per-beat-or-hz" ? (rateMode=="hz" ? "Hz" : timing == .beats ? "/b" : "/r") : suffix(timing),limit=max(4,Int((width-8)/9))
    for digits in stride(from:5,through:1,by:-1) {
      let text=String(format:"%.*g",digits,value)+suffix
      if text.count<=limit {return text}
    }
    return String(format:"%.0e",value)+suffix
  }
  func help(model:PatternModel,timing:PatternTimingUnit,rateMode:String?=nil)->String {
    let units=unit=="cycles-per-beat-or-hz" ? (rateMode=="hz" ? "Hz" : timing == .beats ? "cycles/beat" : "cycles/row") : self.timing ? timing.title.lowercased() : percent ? "%" : unit
    return name+(units.isEmpty ? "" : " (\(units))")+(!choices.isEmpty ? " · "+choices.joined(separator:", ") : "")
  }
}
struct PatternEffectSchema {
  let kind:String,native:String?,code:String,name:String,hint:String
  let fields:[PatternEffectField]
  init(_ raw:[String:Any]) {
    kind=raw["kind"] as? String ?? "";native=raw["native"] as? String
    code=raw["displayCode"] as? String ?? raw["label"] as? String ?? "??"
    name=raw["name"] as? String ?? kind;hint=raw["description"] as? String ?? ""
    fields=(raw["parameters"] as? [[String:Any]] ?? []).map(PatternEffectField.init)
  }
  var inlineFields:[PatternEffectField] {let shown=fields.filter(\.inline);return shown.isEmpty ? fields.filter{$0.storage=="offset"} : shown}
  static let existing:[PatternEffectSchema] = {
    func field(_ key:String,_ name:String,_ unit:String="",_ min:Double=0,_ max:Double=1,_ initial:Double=0,_ width:Int=7)->[String:Any] {
      ["key":key,"name":name,"storage":key,"unit":unit,"minimum":min,"maximum":max,"default":initial,"width":width,"type":key=="binding" || key=="pitchRange" ? "integer" : "number"]
    }
    let value=field("value","Value","normalized",0,1,0.5)
    let binding=field("binding","Binding","",1,255,1,4)
    let bend=field("value","Pitch","semitones",-96,96,0)
    let range=field("pitchRange","Wheel range","semitones",1,96,2,4)
    let duration=field("duration","Duration","row-units",1,Double(UInt32.max),65536)
    let nudgeDuration=field("durationBeats","Duration","beats",1.0/65536,65536,1)
    let strength=field("value","Strength","normalized",0,1,0.75)
    return [("parameter-set","PS","Set parameter",[binding,value]),("parameter-slide","PL","Slide parameter",[binding,value,duration]),
      ("pitch-set","BS","Set pitch",[bend,range]),("pitch-slide","BL","Slide pitch",[bend,duration,range]),
      ("nudge-forward","NF","Nudge forward",[strength,nudgeDuration]),("nudge-reverse","NR","Nudge reverse",[strength,nudgeDuration]),
      ("note-cut","NC","Note cut",[field("offset","Offset","row-units",0,65535,32768)])].map {kind,code,name,fields in
        PatternEffectSchema(["kind":kind,"displayCode":code,"name":name,"parameters":fields])
      }
  }()
}
struct PatternEffectColumnLayout {
  var slotWidths:[Float]=[44]
  var width:Float {30+slotWidths.reduce(0,+)}
  func slotOffset(_ index:Int)->Float {30+slotWidths.prefix(max(0,index)).reduce(0,+)}
}


struct NativePatternCommand {
  private let raw:[String:Any]
  let native:String?,parameters:[String:Any]
  let channel: Int, column: Int, position: Int, duration: Int, binding: Int
  let durationBeats:Double
  let kind: String, value: Double, text: String
  let effect:Int, parameter:Int, pitchRange:Int
  let code:String,valueText:String
  var row: Int { position / 65536 }
  init(_ raw: [String: Any]) {
    self.raw=raw;native=raw["native"] as? String;parameters=raw["parameters"] as? [String:Any] ?? [:]
    kind=raw["kind"] as? String ?? ""
    channel=raw["channel"] as? Int ?? 0; column=raw["column"] as? Int ?? 0
    position=raw["position"] as? Int ?? 0; duration=kind.hasPrefix("nudge-") ? 0 : raw["duration"] as? Int ?? 0; binding=raw["binding"] as? Int ?? 0
    durationBeats=(raw["durationBeats"] as? NSNumber)?.doubleValue ?? (kind.hasPrefix("nudge-") ? 1 : 0)
    effect=raw["effect"] as? Int ?? 0;parameter=raw["parameter"] as? Int ?? 0;pitchRange=raw["pitchRange"] as? Int ?? 2
    value=(raw["value"] as? NSNumber)?.doubleValue ?? 0
    code=kind=="nudge-forward" ? "NF" : kind=="nudge-reverse" ? "NR" : kind=="note-cut" ? "NC" : kind=="pitch-slide" ? "BL" : kind=="pitch-set" ? "BS" : kind=="parameter-slide" ? "PL" : "PS"
    valueText=kind.hasPrefix("nudge-") ? String(format:"%.3g%% · %.3gb",value*100,durationBeats) : kind=="tracker" ? String(format:"%04X",parameter) : kind=="note-cut" ? String(format:"%04X",position%65536) : kind.hasPrefix("pitch-") ? String(format:"%+.3f",value) : String(format:"%02X:%04X",binding,Int((max(0,min(1,value))*65535).rounded()))
    text=kind.hasPrefix("nudge-") ? String(format:"%@ %.2f%%",code,value*100) : kind=="note-cut" ? String(format:"NC   %04X",position % 65536) : kind.hasPrefix("pitch-") ? String(format:"%@ %+.2f",kind=="pitch-slide" ? "BL" : "BS",value) : String(format: "%@%02X %04X",kind=="parameter-slide" ? "PL" : "PS",binding,Int((max(0,min(1,value))*65535).rounded()))
  }
  var dictionary:[String:Any] {
    var result=raw.merging(["channel":channel,"column":column,"position":position,"duration":duration,"binding":binding,"kind":kind,"value":value,"pitchRange":pitchRange,"effect":effect,"parameter":parameter]){_,new in new}
    if kind.hasPrefix("nudge-"){result.removeValue(forKey:"duration");result["durationBeats"]=durationBeats}
    return result
  }
  var editCommand:[String:Any] {
    var result:[String:Any]=["kind":kind,"offset":position%65536]
    if kind=="native" {result["native"]=native;result["parameters"]=parameters;result["duration"]=duration}
    else if kind=="tracker" {return ["kind":kind,"effect":effect,"parameter":parameter]}
    else {
      if kind != "note-cut" {result["value"]=value}
      if kind.hasSuffix("-slide") {result["duration"]=duration}
      if kind.hasPrefix("nudge-") {result["durationBeats"]=durationBeats}
      if kind.hasPrefix("parameter-") {result["binding"]=binding}
      if kind.hasPrefix("pitch-") {result["pitchRange"]=pitchRange}
    }
    return result
  }
  var description: String {
    if kind.hasPrefix("nudge-") {return "Nudge record \(kind=="nudge-forward" ? "forward" : "reverse") · \(String(format:"%.2f",value*100))% strength · \(String(format:"%.8g",durationBeats)) beats including recovery · samples only · Return edits strength and duration in the pattern"}
    if kind=="note-cut" {return "Cut sample / release plugin notes at \(String(format:"%.7f",Double(position % 65536)/65536)) rows · 1/65536-row timing"}
    if kind.hasPrefix("pitch-") {return "\(kind=="pitch-slide" ? "Bend" : "Set pitch") to \(String(format:"%+.5f",value)) semitones · \(String(format:"%.5f",Double(duration)/65536)) rows"}
    return "\(kind == "parameter-slide" ? "Slide" : "Set") binding \(binding) to \(String(format:"%.4f",value * 100))% · offset \(String(format:"%.5f",Double(position % 65536)/65536)) rows" +
      (duration > 0 ? " · over \(String(format:"%.5f",Double(duration)/65536)) rows" : "")
  }
}

final class PatternPerformanceEditor: NSView {
  var onContext: (() -> (PatternModel,Int,Int,Int))?
  var onInlineEdit: ((Int,Int,Int,Int,String)->Void)?
  var onRequest: ((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  let location=Theme.label("",size:13), status=Theme.label("",size:12,color:Theme.muted)
  let columns=NSPopUpButton(), effectColumn=NSPopUpButton(), kind=NSPopUpButton(), binding=NSPopUpButton(), offsetUnits=NSPopUpButton()
  let plugin=NSPopUpButton(), parameter=NSPopUpButton()
  let durationUnits=NSPopUpButton(),durationLabel=Theme.label("Duration (rows)",size:12,color:Theme.muted)
  let value=NSTextField(string:"50"), offset=NSTextField(string:"0"), duration=NSTextField(string:"1")
  let bindingNumber=NSTextField(string:"1"), bindingName=NSTextField(string:"")
  let pitchRange=NSTextField(string:"2"),valueLabel=Theme.label("Target value (%)",size:12,color:Theme.muted)
  private(set) var revision:String?, pending=false, capturedPattern=0, capturedRow=0, capturedChannel=0
  private(set) var commands=[[String:Any]](), bindings=[[String:Any]](), plugins=[[String:Any]](), parameters=[[String:Any]]()
  private var rows=64, generation=0, rowsPerBeat=4
  private var lastOffsetUnit=0
  private var lastDurationUnit=0,durationUsesBeats=false
  private var retainedNudgeDuration:(value:Double,text:String)?
  private(set) var requiresInlineEditor=false
  // Popup selections are resolved by stable ID. addItem(withTitle:) removes an
  // earlier item with an equal title, so an index is not a reliable key.
  private var selectedPlugin:[String:Any]? {guard let id=plugin.selectedItem?.representedObject as? String else{return nil};return plugins.first{$0["instanceID"] as? String==id}}
  private var selectedParameter:[String:Any]? {guard let id=parameter.selectedItem?.representedObject as? Int else{return nil};return parameters.first{$0["id"] as? Int==id}}
  private func item(_ title:String,id:Any?,tag:Int=0)->NSMenuItem {let item=NSMenuItem(title:title,action:nil,keyEquivalent:"");item.representedObject=id;item.tag=tag;return item}
  private func choosePlugin(_ id:String?) {plugin.selectItem(at:id.map{plugin.indexOfItem(withRepresentedObject:$0)} ?? -1)}
  var requestedKind: String?
  var requestedTarget: (plugin: String, parameter: Int)?
  let targetSummary=Theme.label("",size:12,color:Theme.accent)
  var applyButton:ActionButton!, checkButton:ActionButton!, reloadButton:ActionButton!, inlineButton:ActionButton!
  override init(frame:NSRect) {
    super.init(frame:frame)
    for count in 1...8 { columns.addItem(withTitle:"\(count) FX columns"); columns.lastItem?.tag=count }
    for column in 0..<8 { effectColumn.addItem(withTitle:"FX \(column + 1)");effectColumn.lastItem?.tag=column }
    kind.addItems(withTitles:["PS · Set plugin parameter","PL · Slide plugin parameter","BS · Set pitch bend","BL · Slide pitch bend","NC · Precise note cut / plugin note-off","NF · Nudge record forward","NR · Nudge record reverse","Clear command"])
    offsetUnits.addItems(withTitles:["Rows","Beats"]);offsetUnits.target=self;offsetUnits.action=#selector(changedOffsetUnit)
    offsetUnits.setAccessibilityLabel("Offset units");offset.setAccessibilityLabel("Row or beat offset")
    durationUnits.addItems(withTitles:["Beats","Rows"]);durationUnits.target=self;durationUnits.action=#selector(changedDurationUnit)
    durationUnits.setAccessibilityLabel("Nudge duration units")
    kind.target=self;kind.action=#selector(changedKind)
    columns.target=self;columns.action=#selector(changedColumns);effectColumn.target=self;effectColumn.action=#selector(selectCell)
    binding.target=self;binding.action=#selector(selectBinding);plugin.target=self;plugin.action=#selector(selectPlugin)
    parameter.target=self;parameter.action=#selector(selectParameter)
    bindingNumber.isEditable=false; bindingNumber.toolTip="Assigned automatically. References the persistent plugin and parameter IDs, not their position in the list."
    reloadButton=ActionButton("Use current cursor"){[weak self] in self?.capture()}
    applyButton=ActionButton("Apply", prominent: true){[weak self] in self?.apply(dryRun:false)}
    checkButton=ActionButton("Preview"){[weak self] in self?.apply(dryRun:true)}
    inlineButton=ActionButton("Edit parameters in pattern"){[weak self] in self?.editInline()};inlineButton.isHidden=true
    let explanation=Theme.label("PS sets a value immediately; PL glides from the current value to the target over Duration. BS/BL do the same for pitch. NC cuts the sample or sends plugin note-offs at Offset; plugin release tails remain. NF/NR scratch samples forward/backward: below 50% an opposing push slows, above 50% it reverses. Nudge duration is in beats and includes recovery; Rows is an optional display. Other timing uses 1/65536 row. Match the plugin wheel range to its instrument.",size:12,color:Theme.muted)
    let history=Theme.label("Columns and numbered bindings belong to the song. A binding follows its plugin through rack reordering. Apply stops playback and makes one Undo step.",size:12,color:Theme.muted)
    for label in [explanation,history,status,targetSummary] {label.maximumNumberOfLines=5;label.lineBreakMode = .byWordWrapping;label.preferredMaxLayoutWidth=600;label.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
    func field(_ title:String,_ control:NSView)->NSView {
      control.setAccessibilityLabel(title)
      let label=title=="Target value (%)" ? valueLabel : title=="Duration (rows)" ? durationLabel : Theme.label(title,size:12,color:Theme.muted);label.widthAnchor.constraint(equalToConstant:135).isActive=true
      let row=stack(.horizontal,[label,control],spacing:12);row.heightAnchor.constraint(equalToConstant:26).isActive=true;control.setContentHuggingPriority(.defaultLow,for:.horizontal);return row
    }
    let body=stack(.vertical,[stack(.horizontal,[Theme.label("Pattern effects",size:22,weight:.semibold),NSView(),reloadButton!]),location,
      field("FX columns",columns),field("Edit FX column",effectColumn),field("Command",kind),explanation,
      field("Saved target",binding),field("Plugin",plugin),field("Parameter",parameter),targetSummary,
      field("Target value (%)",value),field("Offset in this row",stack(.horizontal,[offset,offsetUnits],spacing:8)),field("Duration (rows)",stack(.horizontal,[duration,durationUnits],spacing:8)),field("Plugin wheel range",pitchRange),
      ToolSection("Target name & binding reference",id:"pattern.target",views:[field("Binding number",bindingNumber),field("Name",bindingName)]),
      history,inlineButton!,stack(.horizontal,[NSView(),checkButton!,applyButton!]),status,NSView()],spacing:10)
    body.stretchAcrossAxis()
    let document=NSView(),scroll=verticalScrollView()
    body.fill(document,inset:24);scroll.documentView=document;scroll.fill(self)
    document.translatesAutoresizingMaskIntoConstraints=false
    let fillHeight=document.heightAnchor.constraint(equalTo:scroll.contentView.heightAnchor);fillHeight.priority = .defaultLow
    NSLayoutConstraint.activate([document.leadingAnchor.constraint(equalTo:scroll.contentView.leadingAnchor),
      document.topAnchor.constraint(equalTo:scroll.contentView.topAnchor),document.widthAnchor.constraint(equalTo:scroll.contentView.widthAnchor),
      document.heightAnchor.constraint(greaterThanOrEqualToConstant:740),fillHeight])
    controls()
  }
  required init?(coder:NSCoder){fatalError()}
  func capture() {
    guard !pending,let onContext,let onRequest else{return}
    let (model,row,channel,column)=onContext()
    generation += 1;let current=generation;pending=true;revision=nil;controls()
    capturedPattern=model.pattern;capturedRow=row;capturedChannel=channel;rows=model.rows;rowsPerBeat=max(1,model.rowsPerBeat);plugins=model.nativePlugins
    offsetUnits.selectItem(at:0);lastOffsetUnit=0
    durationUnits.selectItem(at:0);lastDurationUnit=0;retainedNudgeDuration=nil
    let previousPlugin=plugin.selectedItem?.representedObject as? String
    plugin.removeAllItems()
    for (slot,entry) in plugins.enumerated() {plugin.menu?.addItem(item("\(slot+1) · \(entry["name"] as? String ?? "Plugin")",id:entry["instanceID"]))}
    if let previousPlugin,plugin.indexOfItem(withRepresentedObject:previousPlugin)>=0 {choosePlugin(previousPlugin)}
    location.stringValue="Pattern \(model.pattern) · Row \(row) · Channel \(channel+1)"
    onRequest("pattern.effects.get",["pattern":model.pattern]){[weak self] reply in
      guard let self,self.generation==current else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],data["pattern"] as? Int==self.capturedPattern,
        let revision=result["revision"] as? String else {self.failure(reply);return}
      self.revision=revision;self.commands=data["commands"] as? [[String:Any]] ?? [];self.bindings=data["bindings"] as? [[String:Any]] ?? []
      let count=(data["columns"] as? [[String:Any]] ?? []).first{$0["channel"] as? Int==channel}?["count"] as? Int ?? 1
      self.columns.selectItem(withTag:max(count,max(0,min(7,(column-3)/2))+1));self.effectColumn.selectItem(withTag:max(0,min(7,(column-3)/2)))
      self.binding.removeAllItems();self.binding.menu?.addItem(self.item("Choose a plugin & parameter…",id:nil))
      for entry in self.bindings {self.binding.menu?.addItem(self.item("\(entry["id"] as? Int ?? 0) · \(entry["name"] as? String ?? "")\(entry["resolved"] as? Bool==true ? "" : " (unresolved)")",id:nil,tag:entry["id"] as? Int ?? 0))}
      self.selectCell();self.status.stringValue="Edit this cell, or choose Use current cursor to move the editor.";self.controls()
    }
  }
  @objc func changedOffsetUnit(){
    guard window?.makeFirstResponder(nil) != false else{return}
    guard lastOffsetUnit != offsetUnits.indexOfSelectedItem else{return}
    if let amount=Double(offset.stringValue),amount.isFinite {offset.stringValue=String(format:"%.12g",amount * (lastOffsetUnit==0 ? 1.0/Double(rowsPerBeat) : Double(rowsPerBeat)))}
    lastOffsetUnit=offsetUnits.indexOfSelectedItem
  }
  private func showNudgeDuration(_ beats:Double) {
    duration.stringValue=String(format:"%.17g",beats*(durationUnits.indexOfSelectedItem==1 ? Double(rowsPerBeat) : 1))
    retainedNudgeDuration=(beats,duration.stringValue)
  }
  @objc func changedDurationUnit(){
    guard window?.makeFirstResponder(nil) != false else{return}
    guard lastDurationUnit != durationUnits.indexOfSelectedItem else{return}
    guard let entered=Double(duration.stringValue),entered.isFinite else {
      durationUnits.selectItem(at:lastDurationUnit);status.stringValue="Enter a finite duration before changing units.";return
    }
    let beats=retainedNudgeDuration.flatMap{$0.text==duration.stringValue ? $0.value : nil} ?? entered/(lastDurationUnit==1 ? Double(rowsPerBeat) : 1)
    lastDurationUnit=durationUnits.indexOfSelectedItem;showNudgeDuration(beats);controls()
  }
  private func defaultNudgeDuration()->Double {
    let offset=(Double(self.offset.stringValue) ?? 0)*(offsetUnits.indexOfSelectedItem==1 ? Double(rowsPerBeat) : 1)
    return min(1,max(0,(Double(rows-capturedRow)-offset)/Double(rowsPerBeat)))
  }
  @objc func changedColumns(){controls()}
  @objc func changedKind(){
    requiresInlineEditor=false
    let nudge=[5,6].contains(kind.indexOfSelectedItem)
    if nudge && !durationUsesBeats {
      offsetUnits.selectItem(at:1);changedOffsetUnit()
    }
    if nudge && (!durationUsesBeats || (Double(duration.stringValue) ?? 0)<=0) {
      durationUnits.selectItem(at:0);lastDurationUnit=0;showNudgeDuration(defaultNudgeDuration())
    } else if [1,3].contains(kind.indexOfSelectedItem),Double(duration.stringValue)==0 {
      duration.stringValue=String(min(1,max(1.0/65536,Double(rows-capturedRow)-(Double(offset.stringValue) ?? 0)*(offsetUnits.indexOfSelectedItem==1 ? Double(rowsPerBeat) : 1))))
    }
    durationUsesBeats=nudge
    controls()
  }
  @objc func selectCell() {
    let col=effectColumn.selectedTag()
    let existing=commands.first{($0["channel"] as? Int)==capturedChannel && ($0["position"] as? Int ?? 0)/65536==capturedRow && ($0["column"] as? Int)==col}
    let commandKind=requestedKind ?? existing?["kind"] as? String ?? "parameter-set"
    requestedKind=nil
    let supported=["parameter-set","parameter-slide","pitch-set","pitch-slide","note-cut","nudge-forward","nudge-reverse"].firstIndex(of:commandKind)
    kind.selectItem(at:supported ?? -1)
    requiresInlineEditor=supported==nil
    if requiresInlineEditor {
      targetSummary.stringValue="This native effect uses named parameters. Edit its aligned fields in the pattern, or explicitly choose a replacement command above."
      controls();return
    }
    let pitch=commandKind.hasPrefix("pitch-")
    pitchRange.stringValue=String(existing?["pitchRange"] as? Int ?? 2)
    let storedValue=commandKind.hasPrefix("nudge-") && !(existing?["kind"] as? String ?? "").hasPrefix("nudge-") ? nil : (existing?["value"] as? NSNumber)?.doubleValue
    value.stringValue=String(format:"%.17g",(storedValue ?? (pitch ? 0 : commandKind.hasPrefix("nudge-") ? 0.75 : 0.5))*(pitch ? 1 : 100))
    durationUsesBeats=commandKind.hasPrefix("nudge-")
    if durationUsesBeats {offsetUnits.selectItem(at:1);lastOffsetUnit=1}
    offset.stringValue=String(format:durationUsesBeats ? "%.17g" : "%.10g",Double((existing?["position"] as? Int ?? (commandKind=="note-cut" ? 32768 : 0))%65536)/65536 / (offsetUnits.indexOfSelectedItem==1 ? Double(rowsPerBeat) : 1))
    if durationUsesBeats {
      durationUnits.selectItem(at:0);lastDurationUnit=0
      showNudgeDuration((existing?["durationBeats"] as? NSNumber)?.doubleValue ?? defaultNudgeDuration())
    } else {retainedNudgeDuration=nil;duration.stringValue=String(format:"%.10g",Double(existing?["duration"] as? Int ?? 65536)/65536)}
    changedKind()
    if let number=existing?["binding"] as? Int {binding.selectItem(withTag:number)} else {binding.selectItem(at:0)}
    if commandKind.hasPrefix("nudge-") || pitch {controls()} else if commandKind=="note-cut" {targetSummary.stringValue="NC stops this tracker channel at the chosen offset, including notes started on earlier rows.";controls()} else {selectBinding()}
  }
  @objc func selectBinding() {
    guard !pending else{return}
    let existing=bindings.first{$0["id"] as? Int==binding.selectedTag()}
    bindingNumber.stringValue=String(existing?["id"] as? Int ?? ((1...255).first{n in !bindings.contains{$0["id"] as? Int==n}} ?? 255))
    bindingName.stringValue=existing?["name"] as? String ?? ""
    if let target=requestedTarget {
      requestedTarget=nil; bindingName.stringValue=""; choosePlugin(target.plugin)
      loadParameters(selected:target.parameter); return
    }
    if let id=existing?["plugin"] as? String {choosePlugin(id)}
    loadParameters(selected:existing?["parameter"] as? Int)
  }
  @objc func selectPlugin(){binding.selectItem(at:0);bindingName.stringValue="";loadParameters(selected:nil)}
  @objc func selectParameter(){
    guard let p=selectedPlugin,let q=selectedParameter else{targetSummary.stringValue="Choose an available instrument or effect plugin.";return}
    targetSummary.stringValue="\(p["name"] as? String ?? "Plugin") → \(q["name"] as? String ?? "Parameter") · \(q["canSlide"] as? Bool==true ? "set or slide" : "set only")"
  }
  func loadParameters(selected:Int?) {
    guard !pending,let chosen=selectedPlugin?["instanceID"] as? String,let slot=plugins.firstIndex(where:{$0["instanceID"] as? String==chosen}),let onRequest else{parameters=[];parameter.removeAllItems();controls();return}
    let current=generation;pending=true;controls()
    onRequest("plugin.parameters.get",["slot":slot]){[weak self] reply in
      guard let self,self.generation==current else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],result["revision"] as? String==self.revision,let data=result["data"] as? [[String:Any]] else {
        self.parameters=[];self.parameter.removeAllItems();self.failure(reply);return
      }
      self.parameters=data.filter{$0["writable"] as? Bool != false};self.parameter.removeAllItems()
      for entry in self.parameters {let id=entry["id"] as? Int ?? 0;self.parameter.menu?.addItem(self.item("\(entry["name"] as? String ?? "Parameter")\(entry["canSlide"] as? Bool==true ? "" : " · set only")",id:id,tag:id))}
      if let selected {self.parameter.selectItem(at:self.parameter.indexOfItem(withRepresentedObject:selected))}
      self.selectParameter();self.controls()
    }
  }
  func editInline() {
    guard !pending,requiresInlineEditor,let revision else{return}
    onInlineEdit?(capturedPattern,capturedRow,capturedChannel,3+2*effectColumn.selectedTag(),revision)
  }
  func apply(dryRun:Bool) {
    guard !pending,!requiresInlineEditor,kind.indexOfSelectedItem>=0,let revision,let onRequest else{return}
    let col=effectColumn.selectedTag(),clearing=kind.indexOfSelectedItem==7
    let nudge=[5,6].contains(kind.indexOfSelectedItem)
    let pitch=kind.indexOfSelectedItem==2 || kind.indexOfSelectedItem==3,slide=kind.indexOfSelectedItem==1 || kind.indexOfSelectedItem==3 || nudge
    var replacement=commands.filter{!($0["channel"] as? Int==capturedChannel && ($0["position"] as? Int ?? 0)/65536==capturedRow && $0["column"] as? Int==col)}
      .map{raw in raw.filter{$0.key != "track"}}
    var request:[String:Any]=["pattern":capturedPattern,"expectedRevision":revision,"dryRun":dryRun,
      "columns":[["channel":capturedChannel,"count":columns.selectedTag()]]]
    if !clearing {
      let cut=kind.indexOfSelectedItem==4
      guard let target=cut ? 0 : Double(value.stringValue),target.isFinite,(pitch ? -96...96 : 0...100).contains(target),
        let enteredOffset=Double(offset.stringValue),enteredOffset.isFinite,enteredOffset>=0,enteredOffset<(offsetUnits.indexOfSelectedItem==1 ? 1.0/Double(rowsPerBeat) : 1),
        let length=Double(duration.stringValue),length.isFinite,length>=0,(nudge || length<=Double(rows)),col<columns.selectedTag() else {
        status.stringValue="Choose an available FX column. Parameter values use 0–100%; pitch uses −96 to +96 semitones. Offset must stay inside this row (1 row = \(String(format:"%.6g",1.0/Double(rowsPerBeat))) beats).";return
      }
      let start=enteredOffset * (offsetUnits.indexOfSelectedItem==1 ? Double(rowsPerBeat) : 1)
      let position=capturedRow*65536+min(65535,Int((start*65536).rounded())),ticks=slide && !nudge ? Int((length*65536).rounded()) : 0
      guard (nudge || !slide || ticks>0),position+ticks<=rows*65536 else {status.stringValue="Slides need a positive duration and must end within this pattern.";return}
      var command:[String:Any]=["channel":capturedChannel,"position":position,"duration":ticks,"column":col,
        "kind":nudge ? (kind.indexOfSelectedItem==5 ? "nudge-forward" : "nudge-reverse") : cut ? "note-cut" : pitch ? (slide ? "pitch-slide" : "pitch-set") : (slide ? "parameter-slide" : "parameter-set"),"value":pitch ? target : target/100]
      if nudge {
        let beats=retainedNudgeDuration.flatMap{$0.text==duration.stringValue ? $0.value : nil} ?? length/(durationUnits.indexOfSelectedItem==1 ? Double(rowsPerBeat) : 1)
        guard beats>=1.0/65536,beats<=65536,beats<=Double(rows*65536-position)/65536/Double(rowsPerBeat) else {
          status.stringValue="Nudge duration must be positive and end within this pattern.";return
        }
        command.removeValue(forKey:"duration");command["durationBeats"]=beats;command["binding"]=0
      } else if cut {command["binding"]=0;command["value"]=0} else if pitch {
        guard let range=Int(pitchRange.stringValue),(1...96).contains(range) else{status.stringValue="Plugin wheel range must be 1–96 semitones, matching the instrument's setting.";return}
        command["binding"]=0;command["pitchRange"]=range
      } else {
        guard let pluginData=selectedPlugin,let parameterData=selectedParameter,let pluginID=pluginData["instanceID"] as? String else {
          status.stringValue="Choose a binding number, plugin and parameter.";return
        }
        guard !slide || parameterData["canSlide"] as? Bool==true else{status.stringValue="This parameter supports set commands only.";return}
        let parameterID=parameterData["id"] as? Int ?? 0
        // Choosing a new target must never silently retarget other cells. Reuse
        // the same stable target, or allocate a new song-wide binding.
        let saved=bindings.first{$0["plugin"] as? String==pluginID && $0["parameter"] as? Int==parameterID}
        guard let number=saved?["id"] as? Int ?? (1...255).first(where:{n in !bindings.contains{$0["id"] as? Int==n}}) else{status.stringValue="All 255 target bindings are in use.";return}
        let name=bindingName.stringValue.isEmpty ? "\(pluginData["name"] as? String ?? "Plugin") · \(parameterData["name"] as? String ?? "Parameter")" : bindingName.stringValue
        command["binding"]=number
        request["bindings"]=[["id":number,"plugin":pluginID,"parameter":parameterID,"name":name]]
      }
      replacement.append(command)
    }
    request["commands"]=replacement;pending=true;controls()
    onRequest("pattern.effects.set",request){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let next=result["revision"] as? String else{self.failure(reply);return}
      self.revision=next
      if !dryRun {
        self.commands=replacement
        if let changed=(request["bindings"] as? [[String:Any]])?.first,let number=changed["id"] as? Int {
          self.bindings.removeAll{$0["id"] as? Int==number};self.bindings.append(changed);self.bindingNumber.stringValue=String(number)
          if self.binding.indexOfItem(withTag:number)<0 {self.binding.menu?.addItem(self.item("\(number) · \(changed["name"] as? String ?? "")",id:nil,tag:number))}
        }
      }
      self.status.stringValue=dryRun ? "Preview is valid. Apply will save this command and its binding." : "Pattern effect saved. Undo restores the previous command and binding."
      self.controls()
    }
  }
  private func failure(_ reply:[String:Any]) {status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "The song changed or this reply is unavailable. Use current cursor to reload.";controls()}
  private func controls() {
    for control in [columns,effectColumn,kind,binding,plugin,parameter] {control.isEnabled = !pending}
    let nudge=[5,6].contains(kind.indexOfSelectedItem)
    let pitch=kind.indexOfSelectedItem==2 || kind.indexOfSelectedItem==3,clearing=kind.indexOfSelectedItem==7,cut=kind.indexOfSelectedItem==4
    for control in [value,offset,duration,bindingNumber,bindingName,pitchRange] {control.isEnabled = !pending}
    for control in [binding,plugin,parameter] {control.isEnabled = !pending && !pitch && !clearing && !cut && !nudge}
    bindingNumber.isEnabled = !pending && !pitch && !clearing && !cut && !nudge;bindingName.isEnabled=bindingNumber.isEnabled
    pitchRange.isEnabled = !pending && pitch
    duration.isEnabled = !pending && (kind.indexOfSelectedItem==1 || kind.indexOfSelectedItem==3 || nudge)
    durationUnits.isHidden = !nudge;durationUnits.isEnabled = !pending && nudge
    durationLabel.stringValue=nudge ? "Duration (\(durationUnits.indexOfSelectedItem==1 ? "rows" : "beats"))" : "Duration (rows)"
    duration.setAccessibilityLabel(durationLabel.stringValue)
    duration.superview?.setAccessibilityLabel(durationLabel.stringValue)
    value.isEnabled = !pending && !clearing && !cut;offset.isEnabled = !pending && !clearing;offsetUnits.isEnabled=offset.isEnabled
    if nudge {targetSummary.stringValue="Samples only · 50% reaches a stop against the normal direction; stronger pushes reverse. Returns to normal speed. A new nudge smoothly replaces the previous push."}
    else if pitch {targetSummary.stringValue="Sample pitch and plugin MIDI pitch wheel · match the plugin wheel range."}
    valueLabel.stringValue=nudge ? "Nudge strength (%)" : pitch ? "Pitch (semitones)" : "Target value (%)"
    value.setAccessibilityLabel(valueLabel.stringValue)
    inlineButton?.isHidden = !requiresInlineEditor;inlineButton?.isEnabled = !pending && onInlineEdit != nil
    if requiresInlineEditor {
      for control in [binding,plugin,parameter,offsetUnits] {control.isEnabled=false}
      for control in [value,offset,duration,bindingNumber,bindingName,pitchRange] {control.isEnabled=false}
    }
    reloadButton?.isEnabled = !pending;applyButton?.isEnabled = !pending && revision != nil && !requiresInlineEditor;checkButton?.isEnabled=applyButton?.isEnabled ?? false
  }
}
