import AppKit

// Transient diagnostics. Song edits still go through revision-guarded APIs.
final class ParameterActivityEditor:NSView,NSTableViewDataSource,NSTableViewDelegate,NSSearchFieldDelegate {
  let processor=NSPopUpButton(),parameter=NSPopUpButton(),search=NSSearchField()
  let viewMode=NSSegmentedControl(labels:["Pattern pass","Recent time"],trackingMode:.selectOne,target:nil,action:nil)
  let detailMode=NSSegmentedControl(labels:["Sources","Recent changes","Recorded points"],trackingMode:.selectOne,target:nil,action:nil)
  let pass=NSPopUpButton(),trace=ParameterTraceView(frame:.zero),table=DirectActionTable()
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
  private var frozen=false,freezeGeneration=0,freezeButton:ActionButton!,recordBar:NSStackView!,loadMore:ActionButton!
  private var recordedOffset=0,recordedTotal=0
  private var refreshingRecorded=false
  private var passes=[(label:String,samples:[ParameterTraceSample])](),displayPass=0
  private var selectedSource:[String:Any]?
  private var recordedInspection:[String:Any]?,preferredRecording:(plugin:String,parameter:Int)?
  var onBusy:(()->Bool)?
  private(set) var recordedRevision="",pointDraft=false,pointFailed=false,pointWriting=false
  private var pointOwner:(plugin:String,parameter:Int,revision:String,frame:Int?)?
  private var pointGeneration=0,pointWork:DispatchWorkItem?,pointWaiters=[(Bool)->Void]()
  private var lastRecordedRead = -Double.infinity,displayedDetail=0
  private var recordedFrameMoves=[Double:Double]()
  var needsCompletion:Bool {pointDraft || pointWriting || !pointWaiters.isEmpty}
  var target:[String:Any]? {recordedInspection ?? targets.first{$0["key"] as? String==targetKey}}
  func resumeCapture(){frozen=false;freezeGeneration+=1;freezeButton.title="Freeze view";updateDisplay()}
  override init(frame:NSRect){super.init(frame:frame)
    processor.target=self;processor.action = #selector(selectProcessor);processor.setAccessibilityLabel("Parameter activity processor copy")
    parameter.target=self;parameter.action = #selector(selectParameter);parameter.setAccessibilityLabel("Parameter activity parameter")
    search.placeholderString="Find parameter";search.delegate=self;search.setAccessibilityLabel("Search activity parameters");search.fixed(width:180)
    viewMode.selectedSegment=0;viewMode.target=self;viewMode.action = #selector(changeView)
    detailMode.selectedSegment=0;detailMode.target=self;detailMode.action = #selector(changeDetail)
    pass.target=self;pass.action = #selector(selectPass);pass.setAccessibilityLabel("Captured pattern pass")
    freezeButton=ActionButton("Freeze view"){[weak self] in guard let self else{return};self.frozen.toggle();self.freezeGeneration+=1;self.freezeButton.title=self.frozen ? "Resume view":"Freeze view";if !self.frozen{self.updateDisplay()}}
    let clear=ActionButton("Clear capture"){[weak self] in self?.watch(clear:true)}
    table.dataSource=self;table.delegate=self;table.backgroundColor=Theme.bg;table.rowHeight=25;table.usesAlternatingRowBackgroundColors=true
    table.setAccessibilityLabel("Parameter sources and recent changes");table.target=self;table.doubleAction = #selector(activateTableCell)
    for (id,title,width) in [("source","Source / recorded time (seconds)",400.0),("value","Value / contribution",160.0)] {let c=NSTableColumn(identifier:.init(id));c.title=title;c.width=width;table.addTableColumn(c)}
    let scroll=verticalScrollView();scroll.documentView=table;scroll.heightAnchor.constraint(greaterThanOrEqualToConstant:140).isActive=true
    trace.heightAnchor.constraint(greaterThanOrEqualToConstant:180).isActive=true
    trace.onSelect = {[weak self] sample in guard let self else{return};self.frozen=true;self.freezeGeneration+=1;self.freezeButton.title="Resume view";self.selectedSource=self.link(sample);self.reading.stringValue=String(format:"%.7g",sample.value)+" · "+parameterOriginName(sample.kind)+String(format:" · %.3fs",sample.seconds);self.status.stringValue="View frozen at the selected trace point. Open source jumps to its editor."}
    trace.onOpen = {[weak self] in self?.openSelected()}
    table.activate = {[weak self] in self?.activateTableCell()}
    table.remove = {[weak self] in self?.deletePoint()}
    pointTime.fixed(width:85);pointValue.fixed(width:90);pointTime.setAccessibilityLabel("Recorded point seconds");pointValue.setAccessibilityLabel("Recorded point value")
    pointTime.delegate=self;pointValue.delegate=self;pointTime.target=self;pointValue.target=self;pointTime.action = #selector(commitPointFields);pointValue.action = #selector(commitPointFields)
    loadMore=ActionButton("Next page"){[weak self] in self?.nextRecordedPage()}
    recordBar=stack(.horizontal,[Theme.label("Seconds",size:11),pointTime,Theme.label("Value",size:11),pointValue,ActionButton("Add / update"){[weak self] in self?.addPoint()},ActionButton("Delete point"){[weak self] in self?.deletePoint()},loadMore!],spacing:6)
    recordBar.isHidden=true
    let content=stack(.vertical,[
      stack(.horizontal,[ActionButton("Last touched"){[weak self] in self?.lastTouched()},NSView(),ActionButton("Refresh copies"){[weak self] in self?.refreshCopies()}]),
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
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any] else{if method=="automation.recorded.edit" || (response["error"] as? [String:Any])?["code"] as? Int != -32002 {self.status.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Parameter activity unavailable"};if method=="automation.recorded.get"{self.finishPointWaiters(false)};return}
      self.revision=result["revision"] as? String ?? self.revision;done(data)
    }
  }
  private func resetInspection(){
    generation+=1;recordedInspection=nil;preferredRecording=nil;targetKey="";parameterID=nil
    parameters=[];filtered=[];sources=[];recorded=[];samples=[];audit=[];contributions=[:];token="";selectedSource=nil
    trace.samples=[];processor.removeAllItems();parameter.removeAllItems();table.reloadData()
    frozen=false;freezeButton.title="Freeze view";reading.stringValue="Loading parameter…"
  }
  func inspect(plugin:String,parameter:Int?=nil){if finishBefore({[weak self] in self?.inspect(plugin:plugin,parameter:parameter)}){return};resetInspection();if parameter != nil{search.stringValue=""};preferredTarget="rack/"+plugin;preferredParameter=parameter;preferredGraph=nil;preferredNode=nil;preferredCopy=nil;if !pending{reloadTargets()}}
  func inspect(graph:String,node:String,parameter:Int,copy:String?=nil){if finishBefore({[weak self] in self?.inspect(graph:graph,node:node,parameter:parameter,copy:copy)}){return};resetInspection();search.stringValue="";preferredTarget=nil;preferredGraph=graph;preferredNode=node;preferredCopy=copy;preferredParameter=parameter
    // Recorded song points belong to rack plugins, not recipe instances.
    if detailMode.selectedSegment==2{detailMode.selectedSegment=0;recordBar.isHidden=true;for column in table.tableColumns{column.isEditable=false}}
    if !pending{reloadTargets()}
  }
  func inspectRecorded(plugin:String,parameter:Int){
    if finishBefore({[weak self] in self?.inspectRecorded(plugin:plugin,parameter:parameter)}){return}
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
  @objc func selectProcessor(){if needsCompletion {let wanted=processor.selectedItem?.representedObject as? String;processor.selectItem(at:targets.firstIndex{$0["key"] as? String==targetKey} ?? -1);finishAutomaticEdit{[weak self] accepted in guard let self,accepted,let wanted,let i=self.targets.firstIndex(where:{$0["key"] as? String==wanted}) else{return};self.processor.selectItem(at:i);self.selectProcessor()};return};guard recordedInspection==nil else{return};recordedInspection=nil;preferredRecording=nil;guard let key=processor.selectedItem?.representedObject as? String,targets.contains(where:{$0["key"] as? String==key})else{return};generation+=1;targetKey=key;preferredTarget=nil;preferredGraph=nil;preferredNode=nil;preferredCopy=nil;parameterID=nil;preferredParameter=nil;parameters=[];samples=[];audit=[];token="";trace.samples=[];if !pending{loadParameters()}}
  func loadParameters(){request("parameter.activity.parameters",["target":targetKey]){[weak self] data in guard let self else{return};self.parameters=data["parameters"] as? [[String:Any]] ?? [];self.filterParameters();self.watch()}}
  func filterParameters(){
    let wanted=preferredParameter ?? parameterID,term=search.stringValue
    func matches(_ p:[String:Any])->Bool {term.isEmpty || "\(p["name"] ?? "") \(p["unitLabel"] ?? "") \(p["id"] ?? "")".localizedCaseInsensitiveContains(term)}
    filtered=parameters.filter(matches)
    if let observed=parameters.first(where:{$0["id"] as? Int==wanted}),!filtered.contains(where:{$0["id"] as? Int==wanted}){filtered.insert(observed,at:0)}
    parameter.removeAllItems()
    for p in filtered {let item=NSMenuItem(title:"\(p["name"] as? String ?? "Parameter") · \(p["id"] ?? 0)"+(matches(p) ? "":" · observed; outside search"),action:nil,keyEquivalent:"");parameter.menu?.addItem(item)}
    if let wanted {parameter.selectItem(at:filtered.firstIndex{$0["id"] as? Int==wanted} ?? -1)}
    if let wanted,!parameters.contains(where:{$0["id"] as? Int==wanted}){status.stringValue="The requested parameter is unavailable in this processor."}
    preferredParameter=nil;parameterID=filtered.indices.contains(parameter.indexOfSelectedItem) ? filtered[parameter.indexOfSelectedItem]["id"] as? Int:nil
  }
  func controlTextDidChange(_ obj:Notification){if obj.object as? NSSearchField === search {filterParameters();return}
    guard detailMode.selectedSegment==2,obj.object as? NSTextField === pointTime || obj.object as? NSTextField === pointValue else{return}
    if !pointDraft {guard let plugin=target?["plugin"] as? String,let parameter=parameterID else{return};let frame=recorded.indices.contains(table.selectedRow) ? recorded[table.selectedRow]["frame"] as? Int:nil;pointOwner=(plugin,parameter,recordedRevision,frame)}
    pointDraft=true;pointFailed=false;pointGeneration+=1;if pointOwner?.frame != nil{queuePointFields()}
  }
  func control(_ control:NSControl,textView:NSTextView,doCommandBy selector:Selector)->Bool {if selector==#selector(NSResponder.cancelOperation(_:)),pointDraft {pointWork?.cancel();pointWork=nil;pointDraft=false;pointFailed=false;pointOwner=nil;showRecordedSelection();return true};return false}
  @objc func selectParameter(){if needsCompletion {let wanted=filtered.indices.contains(parameter.indexOfSelectedItem) ? filtered[parameter.indexOfSelectedItem]["id"] as? Int:nil;filterParameters();finishAutomaticEdit{[weak self] accepted in guard let self,accepted,let wanted,let i=self.filtered.firstIndex(where:{$0["id"] as? Int==wanted}) else{return};self.parameter.selectItem(at:i);self.selectParameter()};return};generation+=1;token="";parameterID=filtered.indices.contains(parameter.indexOfSelectedItem) ? filtered[parameter.indexOfSelectedItem]["id"] as? Int:nil;if !pending{watch()}}
  func watch(clear:Bool=false){if recordedInspection != nil{status.stringValue="Select Refresh copies to capture a live processor.";return};guard let id=parameterID,!targetKey.isEmpty else{return};request("parameter.activity.watch",["target":targetKey,"parameter":id,"clear":clear]){[weak self] data in guard let self else{return};let token=data["token"] as? String ?? "";if token != self.token {self.token=token;self.cursor=0;self.samples=[];self.audit=[];self.contributions=[:];self.passes=[];self.selectedSource=nil;self.trace.samples=[]};self.lastSourceRevision="";self.loadSources()}}
  func loadSources(){guard let id=parameterID else{return};let freezeToken=freezeGeneration;request("parameter.activity.sources",["target":targetKey,"parameter":id]){[weak self] data in guard let self,self.freezeGeneration==freezeToken else{return};let sources=data["sources"] as? [[String:Any]] ?? [];let changed = !(self.sources as NSArray).isEqual(sources as NSArray);self.sources=sources;var rule=data["rule"] as? String ?? "";self.lastSourceRevision=self.revision;let omitted=data["omitted"] as? Int ?? 0;if omitted>0{rule+=" · \(omitted) further command sources omitted; trace links remain available."};if self.rule.stringValue != rule {self.rule.stringValue=rule};if changed && self.detailMode.selectedSegment==0 {self.reloadTableKeepingSelection()};if self.detailMode.selectedSegment==2{self.loadRecorded()}}}
  func poll(){if preferredRecording != nil{if !pending{openPreferredRecording()};return};if detailMode.selectedSegment==2,!needsCompletion,!pending,let current=onContext?().revisionToken,current != recordedRevision,ProcessInfo.processInfo.systemUptime-lastRecordedRead>0.5{loadRecorded();return};if recordedInspection != nil{return};let now=ProcessInfo.processInfo.systemUptime;guard !frozen,!pending,table.editedRow<0,now-lastPoll>=0.1 else{return};lastPoll=now
    if preferredTarget != nil || preferredGraph != nil || parameters.isEmpty || now-lastCatalog>2 {reloadTargets();return}
    guard parameterID != nil else{return}
    if token.isEmpty {watch();return}
    let freezeToken=freezeGeneration
    request("parameter.activity.get",["after":cursor,"limit":8192]){[weak self] data in guard let self else{return}
      guard !self.frozen,self.freezeGeneration==freezeToken else{return}
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
    guard !frozen else{return}
    if let info=parameters.first(where:{$0["id"] as? Int==parameterID}){trace.low=(info["min"] as? NSNumber)?.doubleValue ?? 0;trace.high=(info["max"] as? NSNumber)?.doubleValue ?? 1;trace.unit=info["unitLabel"] as? String ?? ""}
    if let last=samples.last {reading.stringValue=String(format:"%.7g %@",last.value,trace.unit)+" · "+parameterOriginName(last.kind)+(last.audible ? "":" · inaudible copy")}else{reading.stringValue=parameterID == nil ? "Choose a parameter":"Ready to capture · play to see delivered values"}
    passes=[];for s in samples {if let previous=passes.last?.samples.last,previous.pattern==s.pattern,previous.order==s.order,s.position>=previous.position {passes[passes.count-1].samples.append(s)}else{passes.append(("Pattern \(s.pattern) · order \(s.order) · \(String(format:"%.2fs",s.seconds))",[s]))}}
    if passes.count>16{passes.removeFirst(passes.count-16)}
    let titles=["Latest pass"]+passes.dropLast().reversed().map(\.label)
    if pass.itemTitles != titles {pass.removeAllItems();pass.addItems(withTitles:titles)};if displayPass>=pass.numberOfItems{displayPass=0};pass.selectItem(at:displayPass)
    if samples.last?.kind=="graph",let sum=unclampedModulationValue,sum<0 || sum>1 {reading.stringValue+=String(format:" · clamped from %.3f",sum)}
    updateTrace();if detailMode.selectedSegment==1 || (detailMode.selectedSegment==0 && !contributions.isEmpty){reloadTableKeepingSelection()}
  }
  func reloadTableKeepingSelection(){
    let link=selectedSource,origin=table.enclosingScrollView?.contentView.bounds.origin
    let sequence=(link?["traceSequence"] as? NSNumber)?.uint64Value
    refreshingRecorded=true;table.reloadData()
    if detailMode.selectedSegment==1,let sequence,let index=audit.firstIndex(where:{$0.sequence==sequence}){table.selectRowIndexes(IndexSet(integer:audit.count-1-index),byExtendingSelection:false)}
    else if detailMode.selectedSegment==0,let link,let index=sources.firstIndex(where:{source in ["kind","id","graph","node","connection","pattern","position","binding","scope"].allSatisfy{key in String(describing:source[key])==String(describing:link[key])}}){table.selectRowIndexes(IndexSet(integer:index),byExtendingSelection:false)}
    else{table.deselectAll(nil)}
    refreshingRecorded=false;if let origin{table.enclosingScrollView?.contentView.scroll(to:origin)}
  }
  func updateTrace(){trace.patternMode=viewMode.selectedSegment==0;pass.isHidden = !trace.patternMode
    if trace.patternMode {let index=max(0,passes.count-1-displayPass);trace.samples=passes.indices.contains(index) ? passes[index].samples:[];let p=trace.samples.last?.pattern;trace.span=Double(onContext?().patterns.first{$0["index"] as? Int==p}?["rows"] as? Int ?? 64)}else{trace.samples=samples;if !frozen,trace.viewEnd==nil{trace.viewStart=max(0,(samples.last?.seconds ?? 30)-30);trace.span=max(30,samples.last?.seconds ?? 30)}}
  }
  @objc func changeView(){trace.viewStart=0;trace.viewEnd=nil;updateTrace();trace.fit()}
  @objc func selectPass(){displayPass=pass.indexOfSelectedItem;if displayPass>0{frozen=true;freezeButton.title="Resume view"};updateTrace()}
  @objc func changeDetail(){if needsCompletion{let desired=detailMode.selectedSegment;detailMode.selectedSegment=displayedDetail;finishAutomaticEdit{[weak self] accepted in guard let self,accepted else{return};self.detailMode.selectedSegment=desired;self.changeDetail()};return};displayedDetail=detailMode.selectedSegment;if let selected=recordedInspection,detailMode.selectedSegment != 2,let plugin=selected["plugin"] as? String,let id=parameterID{recordedInspection=nil;inspect(plugin:plugin,parameter:id)};selectedSource=nil;recordBar.isHidden=detailMode.selectedSegment != 2;table.reloadData();for c in table.tableColumns{c.isEditable=detailMode.selectedSegment==2};if detailMode.selectedSegment==2{recordedOffset=0;loadRecorded()}}
  func numberOfRows(in tableView:NSTableView)->Int {detailMode.selectedSegment==0 ? sources.count:detailMode.selectedSegment==1 ? audit.count:recorded.count}
  func tableView(_ tableView:NSTableView,objectValueFor column:NSTableColumn?,row:Int)->Any? {
    let value=column?.identifier.rawValue=="value"
    if detailMode.selectedSegment==0 {guard sources.indices.contains(row)else{return nil};let s=sources[row];if value {if s["enabled"] as? Bool == false{return "Inactive"};if let n=contributions[s["id"] as? String ?? ""],s["kind"] as? String=="graph-source"{return String(format:"%+.5f normalized",n)};return "Enabled"};return s["title"]}
    if detailMode.selectedSegment==1 {guard audit.indices.contains(row)else{return nil};let s=audit[audit.count-1-row];return value ? String(format:"%.7g",s.value):String(format:"%.3fs · ",s.seconds)+parameterOriginName(s.kind)+" · P\(s.pattern) R\(String(format:"%.3f",s.position/256))"}
    guard recorded.indices.contains(row)else{return nil};return value ? "\(recorded[row]["value"] ?? 0)":String(format:"%.7f",(recorded[row]["frame"] as? Double ?? 0)/48000)
  }
  func tableView(_ tableView:NSTableView,shouldSelectRow row:Int)->Bool {
    guard detailMode.selectedSegment==2,needsCompletion else{return true}
    let frame=recorded.indices.contains(row) ? recorded[row]["frame"] as? Int:nil
    finishAutomaticEdit{[weak self] accepted in guard let self,accepted,let frame,let row=self.recorded.firstIndex(where:{$0["frame"] as? Int==frame}) else{return};self.table.selectRowIndexes(IndexSet(integer:row),byExtendingSelection:false);self.showRecordedSelection()};return false
  }
  func tableView(_ tableView:NSTableView,shouldEdit column:NSTableColumn?,row:Int)->Bool {detailMode.selectedSegment==2 && !pending}
  @objc func activateTableCell(){
    guard detailMode.selectedSegment==2 else{openSelected();return}
    editRecordedCell(row:table.clickedRow>=0 ? table.clickedRow:table.selectedRow,column:table.clickedColumn>=0 ? table.clickedColumn:0)
  }
  func editRecordedCell(row:Int,column:Int){
    guard !pending,recorded.indices.contains(row),table.tableColumns.indices.contains(column) else{return}
    table.editColumn(column,row:row,with:nil,select:true)
  }
  func tableView(_ tableView:NSTableView,setObjectValue object:Any?,for column:NSTableColumn?,row:Int){guard detailMode.selectedSegment==2,recorded.indices.contains(row),let n=Double("\(object ?? "")"),n.isFinite,n>=0 || column?.identifier.rawValue=="value" else{return};var params:[String:Any]=["frame":recorded[row]["frame"] ?? 0,"value":recorded[row]["value"] ?? 0];if column?.identifier.rawValue=="value"{params["value"]=n}else{params["newFrame"]=(n*48000).rounded()};editRecorded(params)}
  func tableViewSelectionDidChange(_ notification:Notification){guard !refreshingRecorded else{return};let row=table.selectedRow;selectedSource=nil;if detailMode.selectedSegment==0,sources.indices.contains(row){selectedSource=sources[row];selectedSource?["parameter"]=parameterID;selectedSource?["plugin"]=target?["plugin"]}else if detailMode.selectedSegment==1,audit.indices.contains(row){selectedSource=link(audit[audit.count-1-row])}else if detailMode.selectedSegment==2{showRecordedSelection()}}
  private func showRecordedSelection(){
    guard !pointDraft,detailMode.selectedSegment==2,recorded.indices.contains(table.selectedRow) else{return}
    let point=recorded[table.selectedRow]
    pointTime.stringValue=String(format:"%.7f",((point["frame"] as? NSNumber)?.doubleValue ?? 0)/48000)
    pointValue.stringValue="\(point["value"] ?? 0)"
  }
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
    if sample.kind=="recorded" {source["kind"]="recorded"};source["traceSequence"]=sample.sequence;source["plugin"]=target?["plugin"];source["parameter"]=parameterID;return source}
  func showPositions(editPattern:Int,row:Int,playPattern:Int?,position:Double?) {
    let pattern=trace.samples.last?.pattern
    let edit=pattern==editPattern ? Double(row):nil,play=pattern==playPattern ? position.map{$0/256}:nil
    if trace.editRow != edit || trace.playRow != play {trace.editRow=edit;trace.playRow=play}
  }
  @objc func openSelected(){
    guard let source=selectedSource else{status.stringValue="Select a source, recent change, or point on the trace.";return}
    finishAutomaticEdit{[weak self] accepted in if accepted{self?.onOpen?(source)}}
  }
  func openSelectedMapping(){
    guard var source=selectedSource,source["connection"] != nil || (source["scope"] as? String=="song" && source["node"] as? String != nil) else{status.stringValue="Select a modulation contribution to edit its range.";return}
    source["editConnection"]=true
    finishAutomaticEdit{[weak self] accepted in if accepted{self?.onOpen?(source)}}
  }
  func nextRecordedPage(){
    if finishBefore({[weak self] in self?.nextRecordedPage()}){return}
    guard !pending else{return}
    recordedOffset=recordedOffset+512<recordedTotal ? recordedOffset+512:0;loadRecorded()
  }
  func refreshCopies(){
    if finishBefore({[weak self] in self?.refreshCopies()}){return}
    guard !pending else{return}
    recordedInspection=nil;reloadTargets()
  }
  func loadRecorded(movedPoint:(from:Double,to:Double)?=nil){
    guard !pointDraft,!pointWriting else{return}
    lastRecordedRead=ProcessInfo.processInfo.systemUptime
    guard let plugin=target?["plugin"] as? String,!plugin.isEmpty,let id=parameterID else{recorded=[];table.reloadData();status.stringValue="Recorded automation belongs to rack plugins. Graph copies use graph sources.";return}
    request("automation.recorded.get",["plugin":plugin,"parameter":id,"offset":recordedOffset,"limit":512]){[weak self] data in
      guard let self else{return}
      // Row indexes may change when a time edit reorders points. Keep the point
      // selected now, including a selection made while this read was pending.
      var frame=self.detailMode.selectedSegment==2 && self.recorded.indices.contains(self.table.selectedRow) ? (self.recorded[self.table.selectedRow]["frame"] as? NSNumber)?.doubleValue:nil
      if let move=movedPoint,frame==move.from{frame=move.to}
      var seen=Set<Double>()
      while let current=frame,seen.insert(current).inserted,let moved=self.recordedFrameMoves[current]{frame=moved}
      guard !self.pointDraft,!self.pointWriting else{return}
      self.recordedRevision=self.revision
      self.recordedFrameMoves.removeAll()
      let origin=self.table.enclosingScrollView?.contentView.bounds.origin
      self.refreshingRecorded=true
      self.recorded=data["points"] as? [[String:Any]] ?? [];self.recordedTotal=data["total"] as? Int ?? 0
      self.loadMore.title=self.recordedTotal==0 ? "No recorded points":"\(self.recordedOffset+1)…\(self.recordedOffset+self.recorded.count) / \(self.recordedTotal) · Next";self.loadMore.isEnabled=self.recordedTotal>512
      self.table.reloadData()
      if self.detailMode.selectedSegment==2 {
        if let frame,let row=self.recorded.firstIndex(where:{($0["frame"] as? NSNumber)?.doubleValue==frame}){self.table.selectRowIndexes(IndexSet(integer:row),byExtendingSelection:false)}else{self.table.deselectAll(nil)}
      }
      self.refreshingRecorded=false
      if let origin{self.table.enclosingScrollView?.contentView.scroll(to:origin)}
      // reloadData does not send a selection notification when the same row
      // remains selected. Refresh its form too, without overwriting typed text.
      if self.pointTime.currentEditor()==nil && self.pointValue.currentEditor()==nil{self.showRecordedSelection()}
      self.status.stringValue="Edit time or value directly during playback. Recorded points use song time. Changes take effect immediately and support Undo."
      self.finishPointWaiters(true)
    }
  }
  private func finishBefore(_ action:@escaping()->Void)->Bool {guard needsCompletion else{return false};finishAutomaticEdit{accepted in if accepted{action()}};return true}
  private func finishPointWaiters(_ accepted:Bool){let callbacks=pointWaiters;pointWaiters=[];for callback in callbacks{callback(accepted)}}
  func finishAutomaticEdit(_ done:@escaping(Bool)->Void){
    guard !pointFailed else{done(false);return}
    if !needsCompletion{done(true);return}
    pointWaiters.append(done);pointWork?.cancel();pointWork=nil
    if !pointWriting{commitPointFields()}
  }
  private func queuePointFields(){
    pointWork?.cancel();let item=DispatchWorkItem{[weak self] in self?.pointWork=nil;self?.commitPointFields()};pointWork=item
    DispatchQueue.main.asyncAfter(deadline:.now()+0.3,execute:item)
  }
  @objc func commitPointFields(){
    guard pointDraft,!pointFailed,!pointWriting else{return}
    guard let owner=pointOwner,let seconds=Double(pointTime.stringValue),seconds.isFinite,seconds>=0,seconds<=604800,let value=Double(pointValue.stringValue),value.isFinite else{pointFailed=true;status.stringValue="Enter a finite time from 0 to 604800 seconds and a parameter value. Your fields are retained.";finishPointWaiters(false);return}
    let frame=Int((seconds*48000).rounded())
    var params:[String:Any]=["plugin":owner.plugin,"parameter":owner.parameter,"frame":owner.frame ?? frame,"value":value,"expectedRevision":owner.revision]
    if owner.frame != nil,owner.frame != frame{params["newFrame"]=frame}
    sendRecordedEdit(params,pointToken:pointGeneration)
  }
  func editRecorded(_ changes:[String:Any]){
    guard !needsCompletion,let plugin=target?["plugin"] as? String,!plugin.isEmpty,let id=parameterID else{return}
    var p=changes;p["plugin"]=plugin;p["parameter"]=id;p["expectedRevision"]=recordedRevision
    if changes["remove"] as? Bool != true {
      pointTime.stringValue=String(format:"%.10g",((changes["newFrame"] ?? changes["frame"]) as? NSNumber)?.doubleValue ?? 0)
      pointTime.stringValue=String(format:"%.10g",(Double(pointTime.stringValue) ?? 0)/48000)
      pointValue.stringValue="\(changes["value"] ?? 0)";pointDraft=true;pointFailed=false;pointGeneration+=1
      pointOwner=(plugin,id,recordedRevision,changes["frame"] as? Int);sendRecordedEdit(p,pointToken:pointGeneration)
    }else{sendRecordedEdit(p,pointToken:nil)}
  }
  private func sendRecordedEdit(_ params:[String:Any],pointToken:Int?){
    guard !pointWriting,let onRequest else{return}
    if pending || onBusy?()==true {
      let expected=generation
      let task=DispatchWorkItem{[weak self] in guard let self,self.generation==expected else{return};self.pointWork=nil
        if pointToken != nil{self.commitPointFields()}else{self.sendRecordedEdit(params,pointToken:nil)}
      };pointWork?.cancel();pointWork=task;DispatchQueue.main.asyncAfter(deadline:.now()+0.05,execute:task);return
    }
    let expected=generation;pending=true;pointWriting=true
    onRequest("automation.recorded.edit",params){[weak self] response in
      guard let self else{return};self.pending=false;self.pointWriting=false
      guard self.generation==expected else{self.finishPointWaiters(false);return}
      guard let result=response["result"] as? [String:Any],let next=result["revision"] as? String else{
        self.pointFailed=true;self.status.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Point edit failed; your fields are retained. Retry explicitly.";self.finishPointWaiters(false);return
      }
      self.revision=next;self.recordedRevision=next
      let from=(params["frame"] as? NSNumber)?.doubleValue ?? 0,to=(params["newFrame"] as? NSNumber)?.doubleValue ?? from
      if from != to{self.recordedFrameMoves[from]=to}
      if let pointToken,self.pointGeneration != pointToken {
        self.pointOwner?.revision=next;self.pointOwner?.frame=Int(to);self.queuePointFields();return
      }
      self.pointDraft=false;self.pointFailed=false;self.pointOwner=nil
      self.loadRecorded(movedPoint:(from,to));self.lastSourceRevision=""
    }
  }
  func addPoint(){
    if pointDraft{pointFailed=false;commitPointFields();return}
    guard let time=Double(pointTime.stringValue),time.isFinite,time>=0,time<=604800,let value=Double(pointValue.stringValue),value.isFinite else{status.stringValue="Enter a finite time in seconds and parameter value.";return};editRecorded(["frame":Int((time*48000).rounded()),"value":value])
  }
  func deletePoint(){
    if finishBefore({[weak self] in self?.deletePoint()}){return}
    guard recorded.indices.contains(table.selectedRow)else{return};editRecorded(["frame":recorded[table.selectedRow]["frame"] ?? 0,"remove":true])
  }
  func lastTouched(){request("automation.target.get"){[weak self] data in guard let self,let t=data["target"] as? [String:Any],let plugin=t["plugin"] as? String,let id=t["parameter"] as? Int else{return};self.search.stringValue="";self.inspect(plugin:plugin,parameter:id)}}
}
