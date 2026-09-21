import AppKit

struct NativePatternCommand {
  let channel: Int, column: Int, position: Int, duration: Int, binding: Int
  let kind: String, value: Double, text: String
  var row: Int { position / 65536 }
  init(_ raw: [String: Any]) {
    channel=raw["channel"] as? Int ?? 0; column=raw["column"] as? Int ?? 0
    position=raw["position"] as? Int ?? 0; duration=raw["duration"] as? Int ?? 0; binding=raw["binding"] as? Int ?? 0
    kind=raw["kind"] as? String ?? ""; value=(raw["value"] as? NSNumber)?.doubleValue ?? 0
    text=kind.hasPrefix("pitch-") ? String(format:"%@ %+.2f",kind=="pitch-slide" ? "BL" : "BS",value) : String(format: "%@%02X %04X",kind=="parameter-slide" ? "PL" : "PS",binding,Int((max(0,min(1,value))*65535).rounded()))
  }
  var description: String {
    if kind.hasPrefix("pitch-") {return "\(kind=="pitch-slide" ? "Bend" : "Set pitch") to \(String(format:"%+.5f",value)) semitones · \(String(format:"%.5f",Double(duration)/65536)) rows"}
    return "\(kind == "parameter-slide" ? "Slide" : "Set") binding \(binding) to \(String(format:"%.4f",value * 100))% · offset \(String(format:"%.5f",Double(position % 65536)/65536)) rows" +
      (duration > 0 ? " · over \(String(format:"%.5f",Double(duration)/65536)) rows" : "")
  }
}

final class PatternPerformanceEditor: NSView {
  var onContext: (() -> (PatternModel,Int,Int,Int))?
  var onRequest: ((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  let location=Theme.label("",size:13), status=Theme.label("",size:12,color:Theme.muted)
  let columns=NSPopUpButton(), effectColumn=NSPopUpButton(), kind=NSPopUpButton(), binding=NSPopUpButton()
  let plugin=NSPopUpButton(), parameter=NSPopUpButton()
  let value=NSTextField(string:"50"), offset=NSTextField(string:"0"), duration=NSTextField(string:"1")
  let bindingNumber=NSTextField(string:"1"), bindingName=NSTextField(string:"")
  let pitchRange=NSTextField(string:"2"),valueLabel=Theme.label("Target value (%)",size:12,color:Theme.muted)
  private(set) var revision:String?, pending=false, capturedPattern=0, capturedRow=0, capturedChannel=0
  private(set) var commands=[[String:Any]](), bindings=[[String:Any]](), plugins=[[String:Any]](), parameters=[[String:Any]]()
  private var rows=64, generation=0
  var requestedKind: String?
  var requestedTarget: (plugin: String, parameter: Int)?
  let targetSummary=Theme.label("",size:12,color:Theme.accent)
  var applyButton:ActionButton!, checkButton:ActionButton!, reloadButton:ActionButton!
  override init(frame:NSRect) {
    super.init(frame:frame)
    for count in 0...8 { columns.addItem(withTitle:"\(count) extra"); columns.lastItem?.tag=count }
    for column in 0..<8 { effectColumn.addItem(withTitle:"FX \(column + 1)");effectColumn.lastItem?.tag=column }
    kind.addItems(withTitles:["PS · Set plugin parameter","PL · Slide plugin parameter","BS · Set pitch bend","BL · Slide pitch bend","Clear command"])
    kind.target=self;kind.action=#selector(changedKind)
    columns.target=self;columns.action=#selector(changedColumns);effectColumn.target=self;effectColumn.action=#selector(selectCell)
    binding.target=self;binding.action=#selector(selectBinding);plugin.target=self;plugin.action=#selector(selectPlugin)
    parameter.target=self;parameter.action=#selector(selectParameter)
    bindingNumber.isEditable=false; bindingNumber.toolTip="Assigned automatically. References the persistent plugin and parameter IDs, not their position in the list."
    reloadButton=ActionButton("Use current cursor"){[weak self] in self?.capture()}
    applyButton=ActionButton("Apply"){[weak self] in self?.apply(dryRun:false)}
    checkButton=ActionButton("Preview"){[weak self] in self?.apply(dryRun:true)}
    let explanation=Theme.label("PS sets a value immediately; PL glides from the current value to the target over Duration. BS/BL do the same for pitch. Values retain double precision; timing uses 1/65536 row. Match the plugin wheel range to its instrument.",size:12,color:Theme.muted)
    let history=Theme.label("Columns and numbered bindings belong to the song. A binding follows its plugin through rack reordering. Apply stops playback and makes one Undo step.",size:12,color:Theme.muted)
    for label in [explanation,history,status] {label.maximumNumberOfLines=3;label.lineBreakMode = .byWordWrapping;label.preferredMaxLayoutWidth=600;label.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
    func field(_ title:String,_ control:NSView)->NSView {
      control.setAccessibilityLabel(title)
      let label=title=="Target value (%)" ? valueLabel : Theme.label(title,size:12,color:Theme.muted);label.widthAnchor.constraint(equalToConstant:135).isActive=true
      let row=stack(.horizontal,[label,control],spacing:12);row.heightAnchor.constraint(equalToConstant:26).isActive=true;control.setContentHuggingPriority(.defaultLow,for:.horizontal);return row
    }
    let body=stack(.vertical,[stack(.horizontal,[Theme.label("Pattern effects",size:22,weight:.semibold),NSView(),reloadButton!]),location,
      field("Effect subcolumns",columns),field("Edit subcolumn",effectColumn),field("Command",kind),explanation,
      field("Saved target",binding),field("Plugin",plugin),field("Parameter",parameter),targetSummary,
      field("Target value (%)",value),field("Row offset",offset),field("Duration (rows)",duration),field("Plugin wheel range",pitchRange),
      ToolSection("Target name & binding reference",id:"pattern.target",views:[field("Binding number",bindingNumber),field("Name",bindingName)]),
      history,stack(.horizontal,[NSView(),checkButton!,applyButton!]),status,NSView()],spacing:10)
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
    capturedPattern=model.pattern;capturedRow=row;capturedChannel=channel;rows=model.rows;plugins=model.nativePlugins
    plugin.removeAllItems()
    for (slot,item) in plugins.enumerated() {plugin.addItem(withTitle:"\(slot+1) · \(item["name"] as? String ?? "Plugin")");plugin.lastItem?.representedObject=item["instanceID"]}
    location.stringValue="Pattern \(model.pattern) · Row \(row) · Channel \(channel+1)"
    onRequest("pattern.performance.get",["pattern":model.pattern]){[weak self] reply in
      guard let self,self.generation==current else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],data["pattern"] as? Int==self.capturedPattern,
        let revision=result["revision"] as? String else {self.failure(reply);return}
      self.revision=revision;self.commands=data["commands"] as? [[String:Any]] ?? [];self.bindings=data["bindings"] as? [[String:Any]] ?? []
      let count=(data["columns"] as? [[String:Any]] ?? []).first{$0["channel"] as? Int==channel}?["count"] as? Int ?? 0
      self.columns.selectItem(withTag:max(count,max(0,min(7,column-5))+1));self.effectColumn.selectItem(withTag:max(0,min(7,column-5)))
      self.binding.removeAllItems();self.binding.addItem(withTitle:"Choose a plugin & parameter…")
      for item in self.bindings {self.binding.addItem(withTitle:"\(item["id"] as? Int ?? 0) · \(item["name"] as? String ?? "")\(item["resolved"] as? Bool==true ? "" : " (unresolved)")");self.binding.lastItem?.tag=item["id"] as? Int ?? 0}
      self.selectCell();self.status.stringValue="Edit this cell, or choose Use current cursor to move the editor.";self.controls()
    }
  }
  @objc func changedColumns(){controls()}
  @objc func changedKind(){controls()}
  @objc func selectCell() {
    let col=effectColumn.selectedTag()
    let existing=commands.first{($0["channel"] as? Int)==capturedChannel && ($0["position"] as? Int ?? 0)/65536==capturedRow && ($0["column"] as? Int)==col}
    let commandKind=requestedKind ?? existing?["kind"] as? String ?? "parameter-set"
    requestedKind=nil
    kind.selectItem(at:["parameter-set","parameter-slide","pitch-set","pitch-slide"].firstIndex(of:commandKind) ?? 0)
    let pitch=commandKind.hasPrefix("pitch-")
    pitchRange.stringValue=String(existing?["pitchRange"] as? Int ?? 2)
    value.stringValue=String(format:"%.17g",((existing?["value"] as? NSNumber)?.doubleValue ?? (pitch ? 0 : 0.5))*(pitch ? 1 : 100))
    offset.stringValue=String(format:"%.10g",Double((existing?["position"] as? Int ?? 0)%65536)/65536)
    duration.stringValue=String(format:"%.10g",Double(existing?["duration"] as? Int ?? 65536)/65536)
    if let number=existing?["binding"] as? Int {binding.selectItem(withTag:number)} else {binding.selectItem(at:0)}
    selectBinding()
  }
  @objc func selectBinding() {
    guard !pending else{return}
    let existing=bindings.first{$0["id"] as? Int==binding.selectedTag()}
    bindingNumber.stringValue=String(existing?["id"] as? Int ?? ((1...255).first{n in !bindings.contains{$0["id"] as? Int==n}} ?? 255))
    bindingName.stringValue=existing?["name"] as? String ?? ""
    if let target=requestedTarget {
      requestedTarget=nil; bindingName.stringValue=""; plugin.selectItem(at:plugins.firstIndex(where:{$0["instanceID"] as? String==target.plugin}) ?? -1)
      loadParameters(selected:target.parameter); return
    }
    if let id=existing?["plugin"] as? String {plugin.selectItem(at:plugins.firstIndex(where:{$0["instanceID"] as? String==id}) ?? -1)}
    loadParameters(selected:existing?["parameter"] as? Int)
  }
  @objc func selectPlugin(){binding.selectItem(at:0);bindingName.stringValue="";loadParameters(selected:nil)}
  @objc func selectParameter(){
    guard plugins.indices.contains(plugin.indexOfSelectedItem),parameters.indices.contains(parameter.indexOfSelectedItem) else{targetSummary.stringValue="Choose an available instrument or effect plugin.";return}
    let p=plugins[plugin.indexOfSelectedItem],q=parameters[parameter.indexOfSelectedItem]
    targetSummary.stringValue="\(p["name"] as? String ?? "Plugin") → \(q["name"] as? String ?? "Parameter") · \(q["canSlide"] as? Bool==true ? "set or slide" : "set only")"
  }
  func loadParameters(selected:Int?) {
    guard !pending,plugins.indices.contains(plugin.indexOfSelectedItem),let onRequest else{parameters=[];parameter.removeAllItems();controls();return}
    let slot=plugin.indexOfSelectedItem,current=generation;pending=true;controls()
    onRequest("plugin.parameters.get",["slot":slot]){[weak self] reply in
      guard let self,self.generation==current else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],result["revision"] as? String==self.revision,let data=result["data"] as? [[String:Any]] else {
        self.parameters=[];self.parameter.removeAllItems();self.failure(reply);return
      }
      self.parameters=data.filter{$0["writable"] as? Bool != false};self.parameter.removeAllItems()
      for item in self.parameters {self.parameter.addItem(withTitle:"\(item["name"] as? String ?? "Parameter")\(item["canSlide"] as? Bool==true ? "" : " · set only")");self.parameter.lastItem?.tag=item["id"] as? Int ?? 0}
      if let selected {if !self.parameter.selectItem(withTag:selected){self.parameter.selectItem(at:-1)}}
      self.selectParameter();self.controls()
    }
  }
  func apply(dryRun:Bool) {
    guard !pending,let revision,let onRequest else{return}
    let col=effectColumn.selectedTag(),clearing=kind.indexOfSelectedItem==4
    let pitch=kind.indexOfSelectedItem==2 || kind.indexOfSelectedItem==3,slide=kind.indexOfSelectedItem==1 || kind.indexOfSelectedItem==3
    var replacement=commands.filter{!($0["channel"] as? Int==capturedChannel && ($0["position"] as? Int ?? 0)/65536==capturedRow && $0["column"] as? Int==col)}
      .map{raw in raw.filter{$0.key != "track"}}
    var request:[String:Any]=["pattern":capturedPattern,"expectedRevision":revision,"dryRun":dryRun,
      "columns":[["channel":capturedChannel,"count":columns.selectedTag()]]]
    if !clearing {
      guard let target=Double(value.stringValue),target.isFinite,(pitch ? -96...96 : 0...100).contains(target),
        let start=Double(offset.stringValue),start.isFinite,start>=0,start<1,
        let length=Double(duration.stringValue),length.isFinite,length>=0,length<=Double(rows),col<columns.selectedTag() else {
        status.stringValue="Choose an available subcolumn. Parameter values use 0–100%; pitch uses −96 to +96 semitones. Offset is 0 to less than 1 row.";return
      }
      let position=capturedRow*65536+min(65535,Int((start*65536).rounded())),ticks=slide ? Int((length*65536).rounded()) : 0
      guard (!slide || ticks>0),position+ticks<=rows*65536 else {status.stringValue="Slides need a positive duration and must end within this pattern.";return}
      var command:[String:Any]=["channel":capturedChannel,"position":position,"duration":ticks,"column":col,
        "kind":pitch ? (slide ? "pitch-slide" : "pitch-set") : (slide ? "parameter-slide" : "parameter-set"),"value":pitch ? target : target/100]
      if pitch {
        guard let range=Int(pitchRange.stringValue),(1...96).contains(range) else{status.stringValue="Plugin wheel range must be 1–96 semitones, matching the instrument's setting.";return}
        command["binding"]=0;command["pitchRange"]=range
      } else {
        guard plugins.indices.contains(plugin.indexOfSelectedItem),
          parameters.indices.contains(parameter.indexOfSelectedItem),let pluginID=plugins[plugin.indexOfSelectedItem]["instanceID"] as? String else {
          status.stringValue="Choose a binding number, plugin and parameter.";return
        }
        let parameterData=parameters[parameter.indexOfSelectedItem]
        guard !slide || parameterData["canSlide"] as? Bool==true else{status.stringValue="This parameter supports set commands only.";return}
        let parameterID=parameterData["id"] as? Int ?? 0
        // Choosing a new target must never silently retarget other cells. Reuse
        // the same stable target, or allocate a new song-wide binding.
        let saved=bindings.first{$0["plugin"] as? String==pluginID && $0["parameter"] as? Int==parameterID}
        guard let number=saved?["id"] as? Int ?? (1...255).first(where:{n in !bindings.contains{$0["id"] as? Int==n}}) else{status.stringValue="All 255 target bindings are in use.";return}
        let name=bindingName.stringValue.isEmpty ? "\(plugins[plugin.indexOfSelectedItem]["name"] as? String ?? "Plugin") · \(parameterData["name"] as? String ?? "Parameter")" : bindingName.stringValue
        command["binding"]=number
        request["bindings"]=[["id":number,"plugin":pluginID,"parameter":parameterID,"name":name]]
      }
      replacement.append(command)
    }
    request["commands"]=replacement;pending=true;controls()
    onRequest("pattern.performance.set",request){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let next=result["revision"] as? String else{self.failure(reply);return}
      self.revision=next
      if !dryRun {
        self.commands=replacement
        if let changed=(request["bindings"] as? [[String:Any]])?.first,let number=changed["id"] as? Int {
          self.bindings.removeAll{$0["id"] as? Int==number};self.bindings.append(changed);self.bindingNumber.stringValue=String(number)
          if self.binding.indexOfItem(withTag:number)<0 {self.binding.addItem(withTitle:"\(number) · \(changed["name"] as? String ?? "")");self.binding.lastItem?.tag=number}
        }
      }
      self.status.stringValue=dryRun ? "Preview is valid. Apply will save this command and its binding." : "Pattern effect saved. Undo restores the previous command and binding."
      self.controls()
    }
  }
  private func failure(_ reply:[String:Any]) {status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "The song changed or this reply is unavailable. Use current cursor to reload.";controls()}
  private func controls() {
    for control in [columns,effectColumn,kind,binding,plugin,parameter] {control.isEnabled = !pending}
    let pitch=kind.indexOfSelectedItem==2 || kind.indexOfSelectedItem==3,clearing=kind.indexOfSelectedItem==4
    for control in [value,offset,duration,bindingNumber,bindingName,pitchRange] {control.isEnabled = !pending}
    for control in [binding,plugin,parameter] {control.isEnabled = !pending && !pitch && !clearing}
    bindingNumber.isEnabled = !pending && !pitch && !clearing;bindingName.isEnabled=bindingNumber.isEnabled
    pitchRange.isEnabled = !pending && pitch
    duration.isEnabled = !pending && (kind.indexOfSelectedItem==1 || kind.indexOfSelectedItem==3)
    value.isEnabled = !pending && !clearing;offset.isEnabled=value.isEnabled
    valueLabel.stringValue=pitch ? "Pitch (semitones)" : "Target value (%)"
    value.setAccessibilityLabel(valueLabel.stringValue)
    reloadButton?.isEnabled = !pending;applyButton?.isEnabled = !pending && revision != nil;checkButton?.isEnabled=applyButton?.isEnabled ?? false
  }
}
