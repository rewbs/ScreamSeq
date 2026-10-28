import AppKit

// Absolute automation uses the shared 48 kHz song clock. Keep native scalar
// values in the draft: normalising the canvas must not round untouched points.
final class SongAutomationEditor: NSView, NSSearchFieldDelegate {
  static let maxFrame = 48_000 * 604_800
  let plugin=NSPopUpButton(),parameter=NSPopUpButton(),search=NSSearchField()
  let canvas=AutomationCanvas(frame:.zero),units=NSPopUpButton(),snap=NSPopUpButton()
  let time=NSTextField(string:"0"),value=NSTextField(string:"0")
  let pointIndex=NSTextField(string:"0")
  let viewStart=NSTextField(string:"0"),viewEnd=NSTextField(string:"8")
  let status=Theme.label("Choose a plugin parameter",size:12,color:Theme.muted)
  let heading=Theme.label("Song parameter automation",size:18,weight:.semibold)
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  var onContext:(()->PatternModel)?,onBusy:(()->Bool)?,onOpen:((String,Int,Bool)->Void)?
  private(set) var model=PatternModel([:]),revision="",pluginID="",parameterID:Int?,slot=0
  private(set) var catalog=[[String:Any]](),filtered=[[String:Any]](),points=[[String:Any]]()
  private(set) var pending=false,hasDraft=false,fieldDraft=false,failed=false
  private var generation=0,setting=false,unitIndex=0
  private var work:DispatchWorkItem?,waiters=[(Bool)->Void](),dragWasDirty=false
  private var selectionFrame:Int? {canvas.selected.flatMap{points.indices.contains($0) ? points[$0]["frame"] as? Int:nil}}
  private var target:[String:Any]? {catalog.first{$0["id"] as? Int==parameterID}}
  private var minimum:Double {(target?["min"] as? NSNumber)?.doubleValue ?? 0}
  private var maximum:Double {(target?["max"] as? NSNumber)?.doubleValue ?? 1}
  var needsCompletion:Bool {pending || hasDraft || fieldDraft || canvas.isDragging}
  override init(frame:NSRect) {
    super.init(frame:frame)
    for (control,label,action) in [(plugin,"Song automation plugin",#selector(changePlugin)),(parameter,"Song automation parameter",#selector(changeParameter)),(units,"Song automation time units",#selector(changeUnits)),(snap,"Song automation snap",#selector(changeSnap))] {
      control.target=self;control.action=action;control.setAccessibilityLabel(label)
    }
    search.placeholderString="Find parameter by name, unit or ID";search.delegate=self
    search.setAccessibilityLabel("Search song automation parameters")
    units.addItems(withTitles:["Seconds","48 kHz frames"]);snap.addItems(withTitles:["10 ms","100 ms","1 second","1 frame"])
    for (field,label) in [(time,"Song automation point time"),(value,"Song automation point value"),(pointIndex,"Song automation point index, zero based"),(viewStart,"Song automation visible start seconds"),(viewEnd,"Song automation visible end seconds")] {field.fixed(width:115);field.setAccessibilityLabel(label)}
    time.delegate=self;value.delegate=self;time.target=self;value.target=self;time.action = #selector(setPoint);value.action = #selector(setPoint)
    canvas.axisUnits=48000;canvas.positionLimit=Self.maxFrame;canvas.snap=480;canvas.pointLimit=100000;canvas.curve="step";canvas.doubleClickAdds=true
    canvas.setAccessibilityLabel("Song automation timeline")
    canvas.setAccessibilityHelp("Double-click to add a point. Drag to move it. Arrows change time or value; Shift uses one frame or 0.1 percent. Tab selects points. Escape cancels a drag. Values hold until the next point.")
    canvas.heightAnchor.constraint(greaterThanOrEqualToConstant:220).isActive=true
    canvas.beforeInteraction = {[weak self] in guard let self,!self.pending,!self.fieldDraft,!self.failed else{return false};self.dragWasDirty=self.hasDraft;return true}
    canvas.onEdit = {[weak self] in self?.readCanvas()}
    canvas.onSelect = {[weak self] in self?.showPoint()}
    canvas.onEditFinished = {[weak self] in self?.schedule()}
    canvas.onCancelEdit = {[weak self] in guard let self else{return};self.readCanvas();self.hasDraft=self.dragWasDirty;if !self.hasDraft{self.work?.cancel();self.work=nil}}
    canvas.onViewport = {[weak self] in self?.showRange()}
    let content=stack(.vertical,[heading,
      stack(.horizontal,[plugin,ActionButton("Last touched"){[weak self] in self?.lastTouched()},ActionButton("Reload"){[weak self] in self?.reload()}]),
      stack(.horizontal,[search,parameter]),
      Theme.label("Absolute song time · step values hold until the next point · edits save with Undo",size:11,color:Theme.muted),canvas,
      stack(.horizontal,[ActionButton("‹"){[weak self] in self?.pan(-0.25)},ActionButton("−"){[weak self] in self?.canvas.zoom(0.5)},ActionButton("Fit"){[weak self] in self?.fit()},ActionButton("+"){[weak self] in self?.canvas.zoom(2)},ActionButton("›"){[weak self] in self?.pan(0.25)},Theme.label("Seconds",size:11),viewStart,viewEnd,ActionButton("Set view"){[weak self] in self?.setView()},snap]),
      stack(.horizontal,[Theme.label("Point",size:11),pointIndex,ActionButton("Select"){[weak self] in self?.selectIndex()},ActionButton("Previous"){[weak self] in self?.selectRelative(-1)},ActionButton("Next"){[weak self] in self?.selectRelative(1)}]),
      stack(.horizontal,[units,time,Theme.label("Value",size:11),value,ActionButton("Set point"){[weak self] in self?.setPoint()}]),
      stack(.horizontal,[ActionButton("New point"){[weak self] in self?.newPoint()},ActionButton("Delete point"){[weak self] in self?.deletePoint()},ActionButton("Clear lane"){[weak self] in self?.clearLane()},ActionButton("Clear all recordings"){[weak self] in self?.clearAll()},NSView(),ActionButton("Retry changes"){[weak self] in self?.retry()}]),
      stack(.horizontal,[ActionButton("Undo"){[weak self] in self?.history(false)},ActionButton("Redo"){[weak self] in self?.history(true)},ActionButton("Show in rack"){[weak self] in self?.open(false)},ActionButton("Pattern curves…"){[weak self] in self?.open(true)}]),status],spacing:8)
    content.stretchAcrossAxis();content.fill(self,inset:12)
    status.cell?.wraps=true;status.maximumNumberOfLines=3
    canvas.allowsEditing=false
  }
  required init?(coder:NSCoder){fatalError()}
  private func fail(_ message:String){failed=true;status.stringValue=message;complete(false)}
  private func complete(_ accepted:Bool){let callbacks=waiters;waiters=[];for callback in callbacks{callback(accepted)}}
  private func normalize(_ scalar:Double)->Double {min(1,max(0,(scalar-minimum)/max(1e-12,maximum-minimum)))}
  private func renderPoints(){canvas.points=points.map{EnvelopePoint(position:$0["frame"] as? Int ?? 0,value:normalize(($0["value"] as? NSNumber)?.doubleValue ?? 0),curve:"step")}}
  private func readCanvas(){
    let existing=Dictionary(uniqueKeysWithValues:points.map{($0["frame"] as! Int,($0["value"] as! NSNumber).doubleValue)})
    points=canvas.points.map{p in let old=existing[p.position];let scalar=old.flatMap{normalize($0)==p.value ? $0:nil} ?? (minimum+p.value*(maximum-minimum));return ["frame":p.position,"value":scalar]}
    changed()
  }
  private func changed(){generation+=1;hasDraft=true;failed=false;status.stringValue="Saving song automation…";if !canvas.isDragging{schedule()}}
  private func schedule(){
    guard (hasDraft || fieldDraft),!failed,!canvas.isDragging,work==nil else{return}
    let task=DispatchWorkItem{[weak self] in guard let self else{return};self.work=nil
      if self.pending || self.onBusy?()==true{self.schedule();return}
      if self.fieldDraft,!self.acceptFields(){self.complete(false);return}
      self.commit()
    };work=task;DispatchQueue.main.asyncAfter(deadline:.now()+0.3,execute:task)
  }
  func finishAutomaticEdit(_ done:@escaping(Bool)->Void){
    guard !(failed && needsCompletion),!canvas.isDragging else{done(false);return}
    if fieldDraft,!acceptFields(){done(false);return}
    if !hasDraft && !pending{done(true);return}
    waiters.append(done);work?.cancel();work=nil
    if pending{return};if onBusy?()==true{schedule()}else{commit()}
  }
  private func afterEdits(_ action:@escaping()->Void){finishAutomaticEdit{accepted in if accepted{action()}}}
  func inspect(plugin wanted:String,parameter desired:Int?=nil){afterEdits{[weak self] in self?.load(plugin:wanted,parameter:desired)}}
  func reload(){guard !pending,!canvas.isDragging else{return}
    guard revision.isEmpty || onContext?().revisionToken.split(separator:":").first==revision.split(separator:":").first else{status.stringValue="This draft belongs to another document. Open a parameter from the current rack.";return}
    work?.cancel();work=nil;load(plugin:pluginID,parameter:parameterID)}
  private func load(plugin wanted:String,parameter desired:Int?) {
    guard !pending,let context=onContext?(),let request=onRequest else{return}
    let chosen=context.nativePlugins.firstIndex{$0["instanceID"] as? String==wanted} ?? (wanted.isEmpty ? context.nativePlugins.indices.first:nil)
    guard let chosen,let id=context.nativePlugins[chosen]["instanceID"] as? String else{status.stringValue="Captured plugin is unavailable. Choose a plugin from the rack.";return}
    let expected=context.revisionToken,token=generation,oldFrame=selectionFrame
    let same=pluginID==id && desired==parameterID && revision.split(separator:":").first==expected.split(separator:":").first
    pending=true;canvas.allowsEditing=false
    var all=[[String:Any]]()
    func readPage(){request("automation.get",["offset":all.count,"limit":4096]){[weak self] response in
      guard let self else{return}
      guard let result=response["result"] as? [String:Any],result["revision"] as? String==expected,
        let data=result["data"] as? [String:Any],let page=data["points"] as? [[String:Any]],let total=data["total"] as? Int,
        data["sampleRate"] as? Int==48000,total<=100000,all.count+page.count<=total,
        all.count+page.count==total || !page.isEmpty,self.generation==token,self.onContext?().revisionToken==expected else{
          self.pending=false;self.canvas.allowsEditing=self.parameterID != nil;self.fail((response["error"] as? [String:Any])?["message"] as? String ?? "Song changed while loading; the captured editor was retained.");return}
      all+=page;if all.count<total{readPage();return}
      request("plugin.parameters.get",["slot":chosen]){[weak self] response in
        guard let self else{return};self.pending=false
        guard let result=response["result"] as? [String:Any],result["revision"] as? String==expected,let catalog=result["data"] as? [[String:Any]],
          self.generation==token,self.onContext?().revisionToken==expected,desired==nil || catalog.contains(where:{$0["id"] as? Int==desired}) else{
            self.canvas.allowsEditing=self.parameterID != nil;self.fail((response["error"] as? [String:Any])?["message"] as? String ?? "Captured parameter is unavailable; the editor was retained.");return}
        self.model=context;self.revision=expected;self.slot=chosen;self.pluginID=id;self.catalog=catalog;self.parameterID=desired ?? catalog.first?["id"] as? Int
        self.points=all.filter{$0["slot"] as? Int==chosen && $0["id"] as? Int==self.parameterID}.map{["frame":$0["frame"]!,"value":$0["value"]!]}.sorted{($0["frame"] as! Int)<($1["frame"] as! Int)}
        self.hasDraft=false;self.fieldDraft=false;self.failed=false;self.generation+=1;self.renderPoints()
        self.canvas.selected=same ? self.points.firstIndex{$0["frame"] as? Int==oldFrame}:(self.points.isEmpty ? nil:0)
        self.canvas.allowsEditing=self.target?["writable"] as? Bool != false && self.parameterID != nil
        self.plugin.removeAllItems();for p in context.nativePlugins{let item=NSMenuItem(title:p["name"] as? String ?? "Plugin",action:nil,keyEquivalent:"");item.representedObject=p["instanceID"];self.plugin.menu?.addItem(item)};self.plugin.selectItem(at:chosen)
        self.filter();self.showPoint();if !same{self.fit()};self.status.stringValue="\(self.points.count) points · saved song time · Undo restores each edit."
      }
    }};readPage()
  }
  func refreshAfterHistory(){guard !needsCompletion,!failed,!revision.isEmpty,onContext?().revisionToken != revision,onBusy?() != true,
    onContext?().revisionToken.split(separator:":").first==revision.split(separator:":").first else{return};load(plugin:pluginID,parameter:parameterID)}
  func filter(){let term=search.stringValue;filtered=catalog.filter{term.isEmpty || "\($0["name"] ?? "") \($0["unitLabel"] ?? "") \($0["id"] ?? "")".localizedCaseInsensitiveContains(term)}
    if let selected=catalog.first(where:{$0["id"] as? Int==parameterID}),!filtered.contains(where:{$0["id"] as? Int==parameterID}){filtered.insert(selected,at:0)}
    parameter.removeAllItems();for p in filtered{let name=p["name"] as? String ?? "Parameter",outside = !term.isEmpty && !"\(name) \(p["unitLabel"] ?? "") \(p["id"] ?? "")".localizedCaseInsensitiveContains(term)
      let item=NSMenuItem(title:"\(name) · \(p["id"] ?? 0)"+(outside ? " · selected; outside search":""),action:nil,keyEquivalent:"");item.representedObject=p["id"];parameter.menu?.addItem(item)}
    parameter.selectItem(at:filtered.firstIndex{$0["id"] as? Int==parameterID} ?? -1)
  }
  func controlTextDidChange(_ notification:Notification){if notification.object as? NSSearchField === search{filter();return};guard !setting else{return};fieldDraft=true;failed=false;generation+=1;if canvas.selected != nil{schedule()}}
  func control(_ control:NSControl,textView:NSTextView,doCommandBy selector:Selector)->Bool {if selector==#selector(NSResponder.cancelOperation(_:)){fieldDraft=false;failed=false;generation+=1;showPoint();return true};return false}
  @objc private func changePlugin(){guard let wanted=plugin.selectedItem?.representedObject as? String else{return};plugin.selectItem(at:slot);afterEdits{[weak self] in self?.load(plugin:wanted,parameter:nil)}}
  @objc private func changeParameter(){guard let wanted=parameter.selectedItem?.representedObject as? Int else{return};filter();afterEdits{[weak self] in guard let self else{return};self.load(plugin:self.pluginID,parameter:wanted)}}
  @objc private func changeUnits(){let wanted=units.indexOfSelectedItem;units.selectItem(at:unitIndex);afterEdits{[weak self] in guard let self else{return};self.unitIndex=wanted;self.units.selectItem(at:wanted);self.showPoint()}}
  @objc private func changeSnap(){canvas.snap=[480,4800,48000,1][max(0,snap.indexOfSelectedItem)]}
  func showPoint(){guard !fieldDraft else{return};setting=true;defer{setting=false}
    pointIndex.stringValue=String(canvas.selected ?? -1)
    if let i=canvas.selected,points.indices.contains(i){time.stringValue=unitIndex==0 ? String(format:"%.10g",Double(points[i]["frame"] as! Int)/48000):String(points[i]["frame"] as! Int);value.stringValue=String(format:"%.9g",(points[i]["value"] as! NSNumber).doubleValue)}else{time.stringValue="0";value.stringValue=String(format:"%.9g",(target?["value"] as? NSNumber)?.doubleValue ?? minimum)}
    canvas.setAccessibilityValue("\(points.count) song points; selected time \(time.stringValue), value \(value.stringValue)")
  }
  @discardableResult private func acceptFields()->Bool {
    guard parameterID != nil,target?["writable"] as? Bool != false,let t=Double(time.stringValue),t.isFinite,t>=0,let v=Double(value.stringValue),v.isFinite,v>=minimum,v<=maximum else{fail("Enter a finite time and a value within the parameter's native range.");return false}
    let raw=t*(unitIndex==0 ? 48000:1)
    guard raw<=Double(Self.maxFrame),(unitIndex==0 || raw.rounded()==raw) else{fail("Use seconds up to 604800, or whole 48 kHz frames.");return false}
    let frame=Int(raw.rounded()),selected=canvas.selected
    guard !points.enumerated().contains(where:{$0.offset != selected && $0.element["frame"] as? Int==frame}) else{fail("Another point occupies this frame; your fields are retained.");return false}
    var next=points;if let selected,next.indices.contains(selected){next.remove(at:selected)}
    guard next.count<100000 else{fail("Automation is limited to 100000 points.");return false}
    next.append(["frame":frame,"value":v]);next.sort{($0["frame"] as! Int)<($1["frame"] as! Int)}
    fieldDraft=false;if !(next as NSArray).isEqual(points as NSArray){points=next;renderPoints();canvas.selected=points.firstIndex{$0["frame"] as? Int==frame};changed()};return true
  }
  @objc func setPoint(){guard !pending else{return};if acceptFields(){schedule()}}
  func newPoint(){afterEdits{[weak self] in self?.canvas.selected=nil;self?.showPoint();if let self{self.window?.makeFirstResponder(self.time)}}}
  func deletePoint(){let frame=selectionFrame;afterEdits{[weak self] in guard let self,let frame,let index=self.points.firstIndex(where:{$0["frame"] as? Int==frame}) else{return};self.points.remove(at:index);self.renderPoints();self.canvas.selected=nil;self.changed();self.showPoint()}}
  func clearLane(){afterEdits{[weak self] in guard let self,!self.points.isEmpty else{return};self.points=[];self.renderPoints();self.canvas.selected=nil;self.changed();self.showPoint()}}
  func selectRelative(_ amount:Int){afterEdits{[weak self] in guard let self,!self.points.isEmpty else{return};self.canvas.selected=min(self.points.count-1,max(0,(self.canvas.selected ?? -1)+amount));self.showPoint();if let frame=self.selectionFrame,Double(frame)<self.canvas.visibleStart || Double(frame)>self.canvas.horizontalEnd{self.canvas.setViewport(start:Double(frame)-self.canvas.horizontalSpan/2,span:self.canvas.horizontalSpan)};self.canvas.needsDisplay=true}}
  func selectIndex(){guard let index=Int(pointIndex.stringValue) else{status.stringValue="Enter an existing point index.";return};afterEdits{[weak self] in guard let self,self.points.indices.contains(index) else{return};self.canvas.selected=index;self.showPoint();if let frame=self.selectionFrame{self.canvas.setViewport(start:Double(frame)-self.canvas.horizontalSpan/2,span:self.canvas.horizontalSpan)};self.canvas.needsDisplay=true}}
  func retry(){failed=false;if fieldDraft,!acceptFields(){return};schedule()}
  private func commit(){
    guard hasDraft,!pending,let id=parameterID,let request=onRequest else{if !pending{complete(!hasDraft && !fieldDraft)};return}
    work?.cancel();work=nil
    guard onContext?().revisionToken==revision,onContext?().nativePlugins.indices.contains(slot)==true,onContext?().nativePlugins[slot]["instanceID"] as? String==pluginID else{fail("Song changed; your automation draft is retained. Reload before editing.");return}
    let token=generation;pending=true
    request("automation.replaceLane",["slot":slot,"id":id,"points":points,"expectedRevision":revision]){[weak self] response in
      guard let self else{return};self.pending=false
      guard let result=response["result"] as? [String:Any],let next=result["revision"] as? String else{self.fail((response["error"] as? [String:Any])?["message"] as? String ?? "Save failed; draft retained. Retry explicitly.");return}
      self.revision=next;self.failed=false
      if self.generation==token{self.hasDraft=false;self.status.stringValue="Song automation saved · Undo restores the previous lane.";self.complete(true)}else{self.schedule()}
    }
  }
  private func guardedOperation(_ method:String){afterEdits{[weak self] in guard let self,let request=self.onRequest,self.onContext?().revisionToken==self.revision else{return};self.pending=true
    request(method,["expectedRevision":self.revision]){[weak self] response in guard let self else{return};self.pending=false
      guard response["result"] != nil else{self.fail((response["error"] as? [String:Any])?["message"] as? String ?? "Operation failed");return};self.load(plugin:self.pluginID,parameter:self.parameterID)
    }
  }}
  func history(_ redo:Bool){guardedOperation(redo ? "history.redo":"history.undo")}
  func clearAll(){guardedOperation("automation.recorded.clear")}
  private func open(_ pattern:Bool){afterEdits{[weak self] in guard let self,let id=self.parameterID else{return};self.onOpen?(self.pluginID,id,pattern)}}
  private func lastTouched(){afterEdits{[weak self] in guard let self,!self.pending,let request=self.onRequest else{return};self.pending=true
    request("automation.target.get",[:]){[weak self] response in guard let self else{return};self.pending=false
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any],let target=data["target"] as? [String:Any],let plugin=target["plugin"] as? String,let id=target["parameter"] as? Int else{self.status.stringValue="No available last-touched parameter.";return};self.load(plugin:plugin,parameter:id)
    }
  }}
  func fit(){let first=Double(points.first?["frame"] as? Int ?? 0),last=Double(points.last?["frame"] as? Int ?? 384000),width=max(points.count>1 ? 1:48000,(last-first)*1.1);canvas.valueLow=0;canvas.valueHigh=1;canvas.setViewport(start:max(0,first-(width-(last-first))/2),span:width)}
  private func pan(_ amount:Double){canvas.setViewport(start:canvas.visibleStart+canvas.horizontalSpan*amount,span:canvas.horizontalSpan)}
  private func showRange(){viewStart.stringValue=String(format:"%.10g",canvas.visibleStart/48000);viewEnd.stringValue=String(format:"%.10g",canvas.horizontalEnd/48000)}
  func setView(){guard let start=Double(viewStart.stringValue),let end=Double(viewEnd.stringValue),start.isFinite,end.isFinite,start>=0,end>start,end<=604800 else{status.stringValue="Choose an ordered time range from 0 to 604800 seconds.";return};canvas.setViewport(start:start*48000,span:(end-start)*48000)}
}

final class SongAutomationWindow:NSWindowController,NSWindowDelegate {
  let editor=SongAutomationEditor(frame:.zero)
  init(){let window=NSWindow(contentRect:NSRect(x:0,y:0,width:1000,height:650),styleMask:[.titled,.closable,.resizable,.miniaturizable],backing:.buffered,defer:false)
    super.init(window:window);window.title="Song parameter automation";window.isReleasedWhenClosed=false;window.contentView=editor;window.minSize=NSSize(width:960,height:580);window.delegate=self;window.center()}
  required init?(coder:NSCoder){fatalError()}
  func windowShouldClose(_ sender:NSWindow)->Bool {guard editor.needsCompletion else{return true};editor.finishAutomaticEdit{[weak self] accepted in if accepted{self?.window?.orderOut(nil)}};return false}
}
