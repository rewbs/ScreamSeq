import AppKit

// Transient diagnostics. Song edits still go through revision-guarded APIs.
final class ParameterActivityEditor:NSView,NSTableViewDataSource,NSTableViewDelegate,NSSearchFieldDelegate {
  let processor=NSPopUpButton(),parameter=NSPopUpButton(),search=NSSearchField()
  let viewMode=NSSegmentedControl(labels:["Pattern pass","Recent time"],trackingMode:.selectOne,target:nil,action:nil)
  let detailMode=NSSegmentedControl(labels:["Sources","Recent changes","Recorded points"],trackingMode:.selectOne,target:nil,action:nil)
  let pass=NSPopUpButton(),trace=ParameterTraceView(frame:.zero),table=NSTableView()
  let reading=Theme.label("Choose a parameter",size:16,weight:.semibold),status=Theme.label("Graph copies appear after starting playback.",size:11,color:Theme.muted)
  let rule=Theme.label("The trace shows host-delivered values; plugin-internal modulation is not exposed.",size:11,color:Theme.muted)
  let pointTime=NSTextField(string:"0"),pointValue=NSTextField(string:"0")
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  var onCapture:(()->Void)?
  var onOpen:(([String:Any])->Void)?,onContext:(()->PatternModel)?
  var onRecordedNames:((String,Int)->(plugin:String?,parameter:String?))?
  private(set) var targets=[[String:Any]](),parameters=[[String:Any]](),filtered=[[String:Any]](),sources=[[String:Any]](),recorded=[[String:Any]]()
  private(set) var samples=[ParameterTraceSample](),audit=[ParameterTraceSample](),contributions=[String:Double]()
  private(set) var targetKey="",parameterID:Int?,pending=false,token="",cursor:UInt64=0,revision=""
  private var generation=0,engine:UInt64=0,lastPoll = -Double.infinity,lastCatalog = -Double.infinity,lastSourceRevision=""
  private var preferredTarget:String?,preferredParameter:Int?,preferredGraph:String?,preferredNode:String?,preferredCopy:String?
  private var frozen=false,freezeButton:ActionButton!,recordBar:NSStackView!,loadMore:ActionButton!
  private var recordedOffset=0,recordedTotal=0
  private var passes=[(label:String,samples:[ParameterTraceSample])](),displayPass=0
  private var selectedSource:[String:Any]?
  private var recordedInspection:[String:Any]?,preferredRecording:(plugin:String,parameter:Int)?
  var target:[String:Any]? {recordedInspection ?? targets.first{$0["key"] as? String==targetKey}}
  override init(frame:NSRect){super.init(frame:frame)
    processor.target=self;processor.action = #selector(selectProcessor);processor.setAccessibilityLabel("Parameter activity processor copy")
    parameter.target=self;parameter.action = #selector(selectParameter);parameter.setAccessibilityLabel("Parameter activity parameter")
    search.placeholderString="Find parameter";search.delegate=self;search.setAccessibilityLabel("Search activity parameters");search.fixed(width:180)
    viewMode.selectedSegment=0;viewMode.target=self;viewMode.action = #selector(changeView)
    detailMode.selectedSegment=0;detailMode.target=self;detailMode.action = #selector(changeDetail)
    pass.target=self;pass.action = #selector(selectPass);pass.setAccessibilityLabel("Captured pattern pass")
    freezeButton=ActionButton("Freeze view"){[weak self] in guard let self else{return};self.frozen.toggle();self.freezeButton.title=self.frozen ? "Resume view":"Freeze view";if !self.frozen{self.updateDisplay()}}
    let clear=ActionButton("Clear capture"){[weak self] in self?.watch(clear:true)}
    table.dataSource=self;table.delegate=self;table.backgroundColor=Theme.bg;table.rowHeight=25;table.usesAlternatingRowBackgroundColors=true
    table.setAccessibilityLabel("Parameter sources and recent changes");table.target=self;table.doubleAction = #selector(activateTableCell)
    for (id,title,width) in [("source","Source / recorded time (seconds)",400.0),("value","Value / contribution",160.0)] {let c=NSTableColumn(identifier:.init(id));c.title=title;c.width=width;table.addTableColumn(c)}
    let scroll=verticalScrollView();scroll.documentView=table;scroll.heightAnchor.constraint(greaterThanOrEqualToConstant:140).isActive=true
    trace.heightAnchor.constraint(greaterThanOrEqualToConstant:180).isActive=true
    trace.onSelect = {[weak self] sample in guard let self else{return};self.frozen=true;self.freezeButton.title="Resume view";self.selectedSource=self.link(sample);self.reading.stringValue=String(format:"%.7g",sample.value)+" · "+parameterOriginName(sample.kind)+String(format:" · %.3fs",sample.seconds);self.status.stringValue="View frozen at the selected trace point. Open source jumps to its editor."}
    pointTime.fixed(width:85);pointValue.fixed(width:90);pointTime.setAccessibilityLabel("Recorded point seconds");pointValue.setAccessibilityLabel("Recorded point value")
    loadMore=ActionButton("Next page"){[weak self] in guard let self else{return};self.recordedOffset=self.recordedOffset+512<self.recordedTotal ? self.recordedOffset+512:0;self.loadRecorded()}
    recordBar=stack(.horizontal,[Theme.label("Seconds",size:11),pointTime,Theme.label("Value",size:11),pointValue,ActionButton("Add / update"){[weak self] in self?.addPoint()},ActionButton("Delete point"){[weak self] in self?.deletePoint()},loadMore!],spacing:6)
    recordBar.isHidden=true
    let content=stack(.vertical,[
      stack(.horizontal,[ActionButton("Last touched"){[weak self] in self?.lastTouched()},NSView(),ActionButton("Refresh copies"){[weak self] in self?.recordedInspection=nil;self?.reloadTargets()}]),
      processor,stack(.horizontal,[search,parameter]),reading,
      stack(.horizontal,[freezeButton!,clear,NSView()]),stack(.horizontal,[viewMode,pass]),
      stack(.horizontal,[ActionButton("Play / capture pattern"){[weak self] in guard let self,!self.pending else{return};self.onCapture?()},NSView(),ActionButton("−"){[weak self] in self?.trace.zoom(0.5)},ActionButton("+"){[weak self] in self?.trace.zoom(2)},ActionButton("Fit"){[weak self] in self?.trace.fit()}]),trace,
      detailMode,stack(.horizontal,[ActionButton("Edit mapping…"){[weak self] in self?.openSelectedMapping()},ActionButton("Open source…"){[weak self] in self?.openSelected()},NSView()]),scroll,recordBar!,rule,status
    ],spacing:8);content.stretchAcrossAxis();content.fill(self,inset:12)
    for p in [processor,parameter,pass]{p.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
    for label in [reading,status,rule] {
      label.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
      label.cell?.wraps=true;label.cell?.usesSingleLineMode=false;label.lineBreakMode = .byWordWrapping
      label.maximumNumberOfLines=label===reading ? 2:3
    }
  }
  required init?(coder:NSCoder){fatalError()}
  func request(_ method:String,_ params:[String:Any]=[:],done:@escaping([String:Any])->Void) {
    guard !pending,let onRequest else{return};pending=true;let expected=generation
    onRequest(method,params){[weak self] response in guard let self else{return};self.pending=false;guard self.generation==expected else{return}
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any] else{if method=="automation.recorded.edit" || (response["error"] as? [String:Any])?["code"] as? Int != -32002 {self.status.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Parameter activity unavailable"};return}
      self.revision=result["revision"] as? String ?? self.revision;done(data)
    }
  }
  private func resetInspection(){
    generation+=1;recordedInspection=nil;preferredRecording=nil;targetKey="";parameterID=nil
    parameters=[];filtered=[];sources=[];recorded=[];samples=[];audit=[];contributions=[:];token="";selectedSource=nil
    trace.samples=[];processor.removeAllItems();parameter.removeAllItems();table.reloadData()
    frozen=false;freezeButton.title="Freeze view";reading.stringValue="Loading parameter…"
  }
  func inspect(plugin:String,parameter:Int?=nil){resetInspection();if parameter != nil{search.stringValue=""};preferredTarget="rack/"+plugin;preferredParameter=parameter;preferredGraph=nil;preferredNode=nil;preferredCopy=nil;if !pending{reloadTargets()}}
  func inspect(graph:String,node:String,parameter:Int,copy:String?=nil){resetInspection();search.stringValue="";preferredTarget=nil;preferredGraph=graph;preferredNode=node;preferredCopy=copy;preferredParameter=parameter
    // Recorded song points belong to rack plugins, not recipe instances.
    if detailMode.selectedSegment==2{detailMode.selectedSegment=0;recordBar.isHidden=true;for column in table.tableColumns{column.isEditable=false}}
    if !pending{reloadTargets()}
  }
  func inspectRecorded(plugin:String,parameter:Int){
    generation+=1;preferredRecording=(plugin,parameter);recordedInspection=nil
    if !pending{openPreferredRecording()}
  }
  private func openPreferredRecording(){
    guard let wanted=preferredRecording,!pending else{return};preferredRecording=nil
    let named=onRecordedNames?(wanted.plugin,wanted.parameter)
    let pluginName=named?.plugin ?? targets.first{$0["plugin"] as? String==wanted.plugin}?["name"] as? String ?? onContext?().nativePlugins.first{$0["instanceID"] as? String==wanted.plugin}?["name"] as? String ?? wanted.plugin
    let parameterName=named?.parameter ?? (target?["plugin"] as? String==wanted.plugin ? parameters.first{$0["id"] as? Int==wanted.parameter}?["name"] as? String:nil) ?? "Parameter \(wanted.parameter)"
    preferredTarget=nil;preferredGraph=nil;preferredNode=nil;preferredCopy=nil;preferredParameter=nil
    processor.isEnabled=false;parameter.isEnabled=false;search.isEnabled=false
    recordedInspection=["plugin":wanted.plugin,"key":"recorded/"+wanted.plugin];parameterID=wanted.parameter;targetKey=""
    parameters=[];filtered=[];sources=[];samples=[];audit=[];recorded=[];contributions=[:];token="";trace.samples=[]
    processor.removeAllItems();processor.addItem(withTitle:pluginName+" · recorded song data");processor.toolTip=wanted.plugin
    parameter.removeAllItems();parameter.addItem(withTitle:parameterName);parameter.toolTip="Parameter ID \(wanted.parameter)";search.stringValue=""
    reading.stringValue="Recorded song-time points";rule.stringValue="Existing recorded base automation. A live processor is only needed for capture and parameter validation when editing."
    detailMode.selectedSegment=2;changeDetail()
  }
  func reloadTargets(){
    if recordedInspection != nil{return}
    processor.isEnabled=true;parameter.isEnabled=true;search.isEnabled=true
    processor.toolTip=nil;parameter.toolTip=nil
    request("parameter.activity.targets"){[weak self] data in guard let self else{return};self.lastCatalog=ProcessInfo.processInfo.systemUptime
      let next=(data["engine"] as? NSNumber)?.uint64Value ?? 0,changed=next != self.engine;self.engine=next
      self.targets=data["targets"] as? [[String:Any]] ?? [];self.processor.removeAllItems()
      let names=Dictionary(grouping:self.targets,by:{$0["name"] as? String ?? "Plugin"})
      for (index,t) in self.targets.enumerated() {
        let name=t["name"] as? String ?? "Plugin",graph=(t["graph"] as? String ?? "n0") != "n0"
        let roles=["row copy","persistent copy","ordinary copy","instrument copy"]
        let repeated=(names[name]?.count ?? 0)>1
        let role=graph ? roles[min(3,max(0,t["role"] as? Int ?? 2))]:"rack"+(repeated ? " \(index+1)":"")
        let instrument=t["instrument"] as? String ?? "n0",channel=t["channel"] as? Int ?? 65535
        let copyName=instrument != "n0" && !instrument.isEmpty ? instrument+" · "+(channel==65535 ? "Inspector":"Channel \(channel+1)"):(t["target"] as? String ?? "copy \(index+1)")
        let identity=graph && repeated ? " · "+copyName:""
        // NSPopUpButton.addItem(withTitle:) removes an existing item with the
        // same title, breaking the correspondence with stable target IDs.
        let item=NSMenuItem(title:"\(name) · \(role)\(identity)\(t["bypass"] as? Bool == true ? " · bypassed":"")",action:nil,keyEquivalent:"")
        item.representedObject=t["key"];item.toolTip=t["key"] as? String;self.processor.menu?.addItem(item)
      }
      let explicit=self.preferredTarget != nil || self.preferredGraph != nil
      let preferred=self.preferredTarget ?? self.targetKey
      let graphMatch=self.targets.firstIndex{self.preferredGraph != nil && $0["graph"] as? String==self.preferredGraph && $0["node"] as? String==self.preferredNode && (self.preferredCopy==nil || Self.copyKey($0)==self.preferredCopy)}
      let exact=self.targets.firstIndex{let key=$0["key"] as? String ?? "";return key==preferred || (self.preferredTarget != nil && key.hasSuffix("/"+preferred))}
      let match=(self.preferredGraph != nil ? graphMatch:exact) ?? (!explicit && self.targetKey.isEmpty ? self.targets.indices.first:nil)
      guard let index=match else {self.status.stringValue=explicit || !self.targetKey.isEmpty ? "Selected processor is unavailable. Choose another copy; it will never be retargeted silently.":"No processors prepared. Add a plugin or play the song to see graph copies.";self.processor.select(nil);self.reading.stringValue="Selected processor unavailable";return}
      self.processor.selectItem(at:index);let key=self.targets[index]["key"] as? String ?? ""
      if changed || key != self.targetKey || explicit || self.preferredParameter != nil || self.parameters.isEmpty {self.targetKey=key;self.preferredTarget=nil;self.preferredGraph=nil;self.preferredNode=nil;self.preferredCopy=nil;self.loadParameters()}
    }
  }
  static func copyKey(_ target:[String:Any])->String? {
    let roles=["row","persistent","ordinary","instrument"]
    guard let graph=target["graph"] as? String,graph != "n0",let role=target["role"] as? Int,roles.indices.contains(role) else{return nil}
    func identity(_ key:String)->String {let value=target[key] as? String ?? "";return value=="n0" ? "":value}
    let channel=target["channel"] as? Int
    return [graph,identity("target"),roles[role],identity("instrument"),channel==nil || channel==65535 ? "inspector":String(channel!)].joined(separator:"/")
  }
  @objc func selectProcessor(){guard recordedInspection==nil else{return};recordedInspection=nil;preferredRecording=nil;guard let key=processor.selectedItem?.representedObject as? String,targets.contains(where:{$0["key"] as? String==key})else{return};generation+=1;targetKey=key;preferredTarget=nil;preferredGraph=nil;preferredNode=nil;preferredCopy=nil;parameterID=nil;preferredParameter=nil;parameters=[];samples=[];audit=[];token="";trace.samples=[];if !pending{loadParameters()}}
  func loadParameters(){request("parameter.activity.parameters",["target":targetKey]){[weak self] data in guard let self else{return};self.parameters=data["parameters"] as? [[String:Any]] ?? [];self.filterParameters();self.watch()}}
  func filterParameters(){let wanted=preferredParameter ?? parameterID;filtered=parameters.filter{search.stringValue.isEmpty || ($0["name"] as? String ?? "").localizedCaseInsensitiveContains(search.stringValue)};parameter.removeAllItems();parameter.addItems(withTitles:filtered.map{"\($0["name"] as? String ?? "Parameter") · \($0["id"] ?? 0)"});if let wanted,let i=filtered.firstIndex(where:{$0["id"] as? Int==wanted}){parameter.selectItem(at:i)};if let wanted,search.stringValue.isEmpty,!filtered.contains(where:{$0["id"] as? Int==wanted}) {parameter.select(nil);status.stringValue="The requested parameter is unavailable in this processor."};preferredParameter=nil;parameterID=filtered.indices.contains(parameter.indexOfSelectedItem) ? filtered[parameter.indexOfSelectedItem]["id"] as? Int:nil}
  func controlTextDidChange(_ obj:Notification){if obj.object as? NSSearchField === search {generation+=1;token="";filterParameters();if !pending{watch()}}}
  @objc func selectParameter(){generation+=1;token="";parameterID=filtered.indices.contains(parameter.indexOfSelectedItem) ? filtered[parameter.indexOfSelectedItem]["id"] as? Int:nil;if !pending{watch()}}
  func watch(clear:Bool=false){if recordedInspection != nil{status.stringValue="Select Refresh copies to capture a live processor.";return};guard let id=parameterID,!targetKey.isEmpty else{return};request("parameter.activity.watch",["target":targetKey,"parameter":id,"clear":clear]){[weak self] data in guard let self else{return};let token=data["token"] as? String ?? "";if token != self.token {self.token=token;self.cursor=0;self.samples=[];self.audit=[];self.contributions=[:];self.passes=[];self.selectedSource=nil;self.trace.samples=[]};self.lastSourceRevision="";self.loadSources()}}
  func loadSources(){guard let id=parameterID else{return};request("parameter.activity.sources",["target":targetKey,"parameter":id]){[weak self] data in guard let self else{return};let sources=data["sources"] as? [[String:Any]] ?? [];let changed = !(self.sources as NSArray).isEqual(sources as NSArray);self.sources=sources;var rule=data["rule"] as? String ?? "";self.lastSourceRevision=self.revision;let omitted=data["omitted"] as? Int ?? 0;if omitted>0{rule+=" · \(omitted) further command sources omitted; trace links remain available."};if self.rule.stringValue != rule {self.rule.stringValue=rule};if changed && self.detailMode.selectedSegment==0 {self.table.reloadData()};if self.detailMode.selectedSegment==2{self.loadRecorded()}}}
  func poll(){if preferredRecording != nil{if !pending{openPreferredRecording()};return};if recordedInspection != nil{return};let now=ProcessInfo.processInfo.systemUptime;guard !pending,table.editedRow<0,now-lastPoll>=0.1 else{return};lastPoll=now
    if preferredTarget != nil || preferredGraph != nil || parameters.isEmpty || now-lastCatalog>2 {reloadTargets();return}
    guard parameterID != nil else{return}
    if token.isEmpty {watch();return}
    request("parameter.activity.get",["after":cursor,"limit":8192]){[weak self] data in guard let self else{return}
      guard (data["target"] as? [String:Any])?["key"] as? String==self.targetKey,data["parameter"] as? Int==self.parameterID else {self.status.stringValue="Another client changed the capture target. Choose a parameter or Clear capture to resume here.";return}
      guard data["token"] as? String==self.token else {self.token="";self.lastCatalog = -Double.infinity;return}
      let incoming=(data["points"] as? [[String:Any]] ?? []).compactMap(ParameterTraceSample.init)
      self.cursor=(data["cursor"] as? NSNumber)?.uint64Value ?? self.cursor
      for point in incoming {if point.kind=="graph-source"{self.contributions[point.source["id"] as? String ?? ""]=point.value;continue};self.samples.append(point)
        if let last=self.audit.last,NSDictionary(dictionary:last.source).isEqual(to:point.source),last.pattern==point.pattern,point.seconds-last.seconds<0.25 {self.audit[self.audit.count-1]=point}else{self.audit.append(point)}
      }
      if self.samples.count>32768{self.samples.removeFirst(self.samples.count-32768)};if self.audit.count>128{self.audit.removeFirst(self.audit.count-128)}
      if !self.frozen && (!incoming.isEmpty || self.samples.isEmpty){self.updateDisplay()}
      let dropped=(data["dropped"] as? NSNumber)?.uint64Value ?? 0,oldest=(data["oldest"] as? NSNumber)?.uint64Value ?? 0
      self.status.stringValue=(data["active"] as? Bool == true ? "Capturing":"Stopped · capture retained")+" · about 1 ms detail with min/max · \(self.samples.count) trace points"+(dropped>0 ? " · \(dropped) queue drops":"")+(oldest>1 ? " · oldest history expires":"")
      if self.lastSourceRevision != self.revision {self.loadSources()}
    }
  }
  func updateDisplay(){
    if let info=parameters.first(where:{$0["id"] as? Int==parameterID}){trace.low=(info["min"] as? NSNumber)?.doubleValue ?? 0;trace.high=(info["max"] as? NSNumber)?.doubleValue ?? 1;trace.unit=info["unitLabel"] as? String ?? ""}
    if let last=samples.last {reading.stringValue=String(format:"%.7g %@",last.value,trace.unit)+" · "+parameterOriginName(last.kind)+(last.audible ? "":" · inaudible copy")}else{reading.stringValue=parameterID == nil ? "Choose a parameter":"Ready to capture · play to see delivered values"}
    passes=[];for s in samples {if let previous=passes.last?.samples.last,previous.pattern==s.pattern,previous.order==s.order,s.position>=previous.position {passes[passes.count-1].samples.append(s)}else{passes.append(("Pattern \(s.pattern) · order \(s.order) · \(String(format:"%.2fs",s.seconds))",[s]))}}
    if passes.count>16{passes.removeFirst(passes.count-16)}
    let titles=["Latest pass"]+passes.dropLast().reversed().map(\.label)
    if pass.itemTitles != titles {pass.removeAllItems();pass.addItems(withTitles:titles)};if displayPass>=pass.numberOfItems{displayPass=0};pass.selectItem(at:displayPass)
    if samples.last?.kind=="graph",let sum=unclampedModulationValue,sum<0 || sum>1 {reading.stringValue+=String(format:" · clamped from %.3f",sum)}
    updateTrace();if detailMode.selectedSegment==1 || (detailMode.selectedSegment==0 && !contributions.isEmpty){table.reloadData()}
  }
  func updateTrace(){trace.patternMode=viewMode.selectedSegment==0;pass.isHidden = !trace.patternMode
    if trace.patternMode {let index=max(0,passes.count-1-displayPass);trace.samples=passes.indices.contains(index) ? passes[index].samples:[];let p=trace.samples.last?.pattern;trace.span=Double(onContext?().patterns.first{$0["index"] as? Int==p}?["rows"] as? Int ?? 64)}else{trace.samples=samples;if !frozen,trace.viewEnd==nil{trace.viewStart=max(0,(samples.last?.seconds ?? 30)-30);trace.span=max(30,samples.last?.seconds ?? 30)}}
  }
  @objc func changeView(){trace.viewStart=0;trace.viewEnd=nil;updateTrace();trace.fit()}
  @objc func selectPass(){displayPass=pass.indexOfSelectedItem;if displayPass>0{frozen=true;freezeButton.title="Resume view"};updateTrace()}
  @objc func changeDetail(){if let selected=recordedInspection,detailMode.selectedSegment != 2,let plugin=selected["plugin"] as? String,let id=parameterID{recordedInspection=nil;inspect(plugin:plugin,parameter:id)};selectedSource=nil;recordBar.isHidden=detailMode.selectedSegment != 2;table.reloadData();for c in table.tableColumns{c.isEditable=detailMode.selectedSegment==2};if detailMode.selectedSegment==2{recordedOffset=0;loadRecorded()}}
  func numberOfRows(in tableView:NSTableView)->Int {detailMode.selectedSegment==0 ? sources.count:detailMode.selectedSegment==1 ? audit.count:recorded.count}
  func tableView(_ tableView:NSTableView,objectValueFor column:NSTableColumn?,row:Int)->Any? {
    let value=column?.identifier.rawValue=="value"
    if detailMode.selectedSegment==0 {guard sources.indices.contains(row)else{return nil};let s=sources[row];if value {if s["enabled"] as? Bool == false{return "Inactive"};if let n=contributions[s["id"] as? String ?? ""],s["kind"] as? String=="graph-source"{return String(format:"%+.5f normalized",n)};return "Enabled"};return s["title"]}
    if detailMode.selectedSegment==1 {guard audit.indices.contains(row)else{return nil};let s=audit[audit.count-1-row];return value ? String(format:"%.7g",s.value):String(format:"%.3fs · ",s.seconds)+parameterOriginName(s.kind)+" · P\(s.pattern) R\(String(format:"%.3f",s.position/256))"}
    guard recorded.indices.contains(row)else{return nil};return value ? "\(recorded[row]["value"] ?? 0)":String(format:"%.7f",(recorded[row]["frame"] as? Double ?? 0)/48000)
  }
  func tableView(_ tableView:NSTableView,shouldEdit column:NSTableColumn?,row:Int)->Bool {detailMode.selectedSegment==2 && !pending}
  @objc func activateTableCell(){
    guard detailMode.selectedSegment==2 else{openSelected();return}
    editRecordedCell(row:table.clickedRow,column:table.clickedColumn)
  }
  func editRecordedCell(row:Int,column:Int){
    guard !pending,recorded.indices.contains(row),table.tableColumns.indices.contains(column) else{return}
    table.editColumn(column,row:row,with:nil,select:true)
  }
  func tableView(_ tableView:NSTableView,setObjectValue object:Any?,for column:NSTableColumn?,row:Int){guard detailMode.selectedSegment==2,recorded.indices.contains(row),let n=Double("\(object ?? "")"),n.isFinite,n>=0 || column?.identifier.rawValue=="value" else{return};var params:[String:Any]=["frame":recorded[row]["frame"] ?? 0,"value":recorded[row]["value"] ?? 0];if column?.identifier.rawValue=="value"{params["value"]=n}else{params["newFrame"]=(n*48000).rounded()};editRecorded(params)}
  func tableViewSelectionDidChange(_ notification:Notification){let row=table.selectedRow;selectedSource=nil;if detailMode.selectedSegment==0,sources.indices.contains(row){selectedSource=sources[row];selectedSource?["parameter"]=parameterID;selectedSource?["plugin"]=target?["plugin"]}else if detailMode.selectedSegment==1,audit.indices.contains(row){selectedSource=link(audit[audit.count-1-row])}else if detailMode.selectedSegment==2,recorded.indices.contains(row){pointTime.stringValue=String(format:"%.7f",(recorded[row]["frame"] as? Double ?? 0)/48000);pointValue.stringValue="\(recorded[row]["value"] ?? 0)"}}
  var unclampedModulationValue:Double? {
    let active=sources.filter{$0["kind"] as? String=="graph-source" && $0["enabled"] as? Bool != false}
    guard !active.isEmpty else{return nil}
    var sum=0.0
    for source in active {guard let id=source["id"] as? String,let value=contributions[id] else{return nil};sum+=value}
    return sum
  }
  func link(_ sample:ParameterTraceSample)->[String:Any]{var source=sample.source
    if sample.kind=="graph-source",let contribution=sources.first(where:{$0["kind"] as? String=="graph-source" && $0["id"] as? String==source["id"] as? String}) {source=contribution}
    else if sample.kind=="graph" || sample.kind=="graph-source" {
      source["graph"]=target?["graph"];source["node"]=target?["node"];source["kind"]="graph"
      if let plugin=target?["plugin"] as? String,!plugin.isEmpty {source["scope"]="song";source["graph"]=NSNull();source["node"]=nil}
    }
    if sample.kind=="envelope",let lane=sources.first(where:{$0["kind"] as? String=="envelope" && $0["id"] as? String==source["id"] as? String}){source=lane}
    if sample.kind=="recorded" {source["kind"]="recorded"};source["plugin"]=target?["plugin"];source["parameter"]=parameterID;return source}
  func showPositions(editPattern:Int,row:Int,playPattern:Int?,position:Double?) {
    let pattern=trace.samples.last?.pattern
    let edit=pattern==editPattern ? Double(row):nil,play=pattern==playPattern ? position.map{$0/256}:nil
    if trace.editRow != edit || trace.playRow != play {trace.editRow=edit;trace.playRow=play}
  }
  @objc func openSelected(){guard let source=selectedSource else{status.stringValue="Select a source, recent change, or point on the trace.";return};if source["kind"] as? String=="recorded" {detailMode.selectedSegment=2;changeDetail()}else{onOpen?(source)}}
  func openSelectedMapping(){
    guard var source=selectedSource,source["connection"] != nil || (source["scope"] as? String=="song" && source["node"] as? String != nil) else{status.stringValue="Select a modulation contribution to edit its range.";return}
    source["editConnection"]=true;onOpen?(source)
  }
  func loadRecorded(){guard let plugin=target?["plugin"] as? String,!plugin.isEmpty,let id=parameterID else{recorded=[];table.reloadData();status.stringValue="Recorded automation belongs to rack plugins. Graph copies use graph sources.";return};request("automation.recorded.get",["plugin":plugin,"parameter":id,"offset":recordedOffset,"limit":512]){[weak self] data in guard let self else{return};self.recorded=data["points"] as? [[String:Any]] ?? [];self.recordedTotal=data["total"] as? Int ?? 0;self.loadMore.title=self.recordedTotal==0 ? "No recorded points":"\(self.recordedOffset+1)…\(self.recordedOffset+self.recorded.count) / \(self.recordedTotal) · Next";self.loadMore.isEnabled=self.recordedTotal>512;self.table.reloadData();self.status.stringValue="Edit time or value directly during playback. Recorded points use song time. Changes take effect immediately and support Undo."}}
  func editRecorded(_ changes:[String:Any]){guard let plugin=target?["plugin"] as? String,!plugin.isEmpty,let id=parameterID else{return};var p=changes;p["plugin"]=plugin;p["parameter"]=id;p["expectedRevision"]=revision;sendRecordedEdit(p,generation:generation)}
  private func sendRecordedEdit(_ params:[String:Any],generation expected:Int,attempt:Int=0){
    guard generation==expected else{return}
    if pending {guard attempt<40 else{status.stringValue="The app is busy. Your point fields are retained; retry the edit.";return};DispatchQueue.main.asyncAfter(deadline:.now()+0.05){[weak self] in self?.sendRecordedEdit(params,generation:expected,attempt:attempt+1)};return}
    request("automation.recorded.edit",params){[weak self] _ in self?.loadRecorded();self?.lastSourceRevision=""}
  }
  func addPoint(){guard let time=Double(pointTime.stringValue),time.isFinite,time>=0,let value=Double(pointValue.stringValue),value.isFinite else{status.stringValue="Enter a finite time in seconds and parameter value.";return};editRecorded(["frame":(time*48000).rounded(),"value":value])}
  func deletePoint(){guard recorded.indices.contains(table.selectedRow)else{return};editRecorded(["frame":recorded[table.selectedRow]["frame"] ?? 0,"remove":true])}
  func lastTouched(){request("automation.target.get"){[weak self] data in guard let self,let t=data["target"] as? [String:Any],let plugin=t["plugin"] as? String,let id=t["parameter"] as? Int else{return};self.search.stringValue="";self.inspect(plugin:plugin,parameter:id)}}
}
