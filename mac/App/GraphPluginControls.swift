import AppKit

// The reusable recipe and a rack instance share the same searchable, direct
// parameter surface. Only the request adapter differs; identities stay stable.
final class GraphPluginControls: NSView, NSTextFieldDelegate {
  let parametersView=GraphRackControls(frame:.zero)
  let inputs=NSTextField(string:""),outputs=NSTextField(string:"")
  let message=Theme.label("Select an effect to inspect its settings",size:10,color:Theme.muted)
  var onRequest:((String,[String:Any],@escaping ([String:Any])->Void)->Void)?
  var onChanged:(()->Void)?,onParameter:((UInt32)->Void)?
  var onActivity:((String,String,UInt32)->Void)?
  var onCatalog:((String,String,[String:Any])->Void)?
  var currentRevision:(()->String)?
  private var graph="",node="",revision="",pending=false,gesture:String?
  private var savedInputs=[Int](),savedOutputs=[Int]()
  private var editor:String?,editorGraph="",editorNode=""
  override init(frame:NSRect){
    super.init(frame:frame)
    parametersView.recipeMode=true
    parametersView.currentRevision={[weak self] in self?.currentRevision?() ?? self?.revision ?? ""}
    parametersView.onOpen={[weak self] _ in self?.openEditor()}
    parametersView.onAutomate={[weak self] _,parameter in self?.onParameter?(parameter)}
    parametersView.onExpose=parametersView.onAutomate
    parametersView.onActivity={[weak self] _,parameter in guard let self else{return};self.onActivity?(self.graph,self.node,parameter)}
    parametersView.onGesture={[weak self] active in self?.gesture=active ? UUID().uuidString:nil}
    parametersView.onRequest={[weak self] method,params,reply in
      guard let self,let request=self.onRequest,!self.graph.isEmpty,!self.node.isEmpty else{reply(["error":["message":"No graph effect selected"]]);return}
      let g=self.graph,n=self.node
      guard params["plugin"] as? String==g+"/"+n else{reply(["error":["message":"Graph selection changed"]]);return}
      let write=method=="plugin.parameters.set"
      var mapped:[String:Any]=["graph":g,"node":n]
      if write {mapped["parameters"]=params["values"];mapped["expectedRevision"]=params["expectedRevision"];if let gesture=self.gesture{mapped["gesture"]=gesture}}
      request(write ? "graph.plugin.set":"graph.plugin.get",mapped){[weak self] response in
        guard let self else{return}
        var forwarded=response
        if var result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any] {
          if self.graph==g && self.node==n {
            self.revision=result["revision"] as? String ?? "";self.populateBuses(data)
          }
          if !write {result["data"]=data["parameters"] as? [[String:Any]] ?? [];forwarded["result"]=result}
        }
        reply(forwarded)
        if !write,self.graph==g,self.node==n,let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any],self.currentRevision == nil || self.currentRevision?()==self.revision {self.onCatalog?(g,n,data)}
        if write,response["result"] != nil,self.graph==g,self.node==n{self.onChanged?()}
      }
    }
    inputs.placeholderString="e.g. 1, 2";outputs.placeholderString="e.g. 1, 2"
    inputs.setAccessibilityLabel("Enabled auxiliary input buses");outputs.setAccessibilityLabel("Enabled auxiliary output buses")
    for field in [inputs,outputs] {field.delegate=self;field.target=self;field.action = #selector(commitField);field.toolTip="Commits on Return or leaving this field"}
    let more=ActionMenuButton { [weak self] in
      let menu=NSMenu(title:"Graph plugin");menu.autoenablesItems=false
      guard let self else{return menu}
      menu.addItem(ContextAction("Reload controls"){[weak self] in self?.load()})
      menu.addItem(ContextAction("Update template from custom interface",enabled:self.editor != nil){[weak self] in self?.commitEditor()})
      menu.addItem(ContextAction("Close custom interface",enabled:self.editor != nil){[weak self] in self?.closeEditor()})
      return menu
    }
    let ports=ToolSection("Audio buses",id:"graph-plugin-buses",views:[
      stack(.horizontal,[Theme.label("Aux in",size:11),inputs]),
      stack(.horizontal,[Theme.label("Aux out",size:11),outputs])])
    message.lineBreakMode = .byWordWrapping;message.maximumNumberOfLines=0;message.preferredMaxLayoutWidth=240
    let content=stack(.vertical,[parametersView,stack(.horizontal,[ports,more]),message],spacing:5)
    content.stretchAcrossAxis();content.fill(self)
  }
  required init?(coder:NSCoder){fatalError()}
  func context(graph:String?,node:String?,revision displayedRevision:String=""){
    let g=graph ?? "",n=node ?? ""
    if g != self.graph || n != self.node {self.graph=g;self.node=n;revision="";inputs.stringValue="";outputs.stringValue="";gesture=nil;message.stringValue="Parameter edits update all uses of this shared definition."}
    parametersView.context(g.isEmpty || n.isEmpty ? nil:["id":g+"/"+n],revision:displayedRevision)
  }
  private func request(_ method:String,_ extra:[String:Any]=[:],write:Bool=false,done:@escaping([String:Any])->Void){
    guard !pending,!graph.isEmpty,!node.isEmpty,let onRequest else{return};pending=true;let g=graph,n=node
    var params=extra;params["graph"]=g;params["node"]=n;if write{params["expectedRevision"]=currentRevision?() ?? revision}
    onRequest(method,params){[weak self] response in guard let self else{return};self.pending=false
      if method=="graph.plugin.editor.open",let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any],let token=data["editor"] as? String {self.editor=token;self.editorGraph=g;self.editorNode=n}
      guard self.graph==g,self.node==n else{self.load();return}
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any] else {self.message.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Plugin operation failed";return}
      self.revision=result["revision"] as? String ?? "";done(data)
    }
  }
  private func populateBuses(_ data:[String:Any]){
    let buses=data["buses"] as? [[String:Any]] ?? []
    for (field,direction) in [(inputs,"input"),(outputs,"output")]{field.stringValue=buses.filter{$0["direction"] as? String==direction && $0["active"] as? Bool==true && ($0["index"] as? Int ?? 0)>0}.compactMap{$0["index"] as? Int}.map(String.init).joined(separator:", ")}
    savedInputs=ports(inputs) ?? [];savedOutputs=ports(outputs) ?? []
  }
  func load(){parametersView.refresh()}
  @objc private func commitField(_ field:NSTextField){setPorts()}
  func controlTextDidEndEditing(_ notification:Notification){if let field=notification.object as? NSTextField{commitField(field)}}
  private func ports(_ field:NSTextField)->[Int]?{if field.stringValue.trimmingCharacters(in:.whitespaces).isEmpty{return []};let values=field.stringValue.split(separator:",").map{$0.trimmingCharacters(in:.whitespaces)};let numbers=values.compactMap(Int.init);return numbers.count==values.count ? numbers : nil}
  private func setPorts(){guard let i=ports(inputs),let o=ports(outputs)else{message.stringValue="Use comma-separated auxiliary bus numbers";return};guard i != savedInputs || o != savedOutputs else{return};request("graph.plugin.set",["inputs":i,"outputs":o],write:true){[weak self] data in self?.populateBuses(data);self?.load();self?.onChanged?()}}
  func openEditor(){request("graph.plugin.editor.open",write:true){[weak self] data in guard let self else{return};self.editor=data["editor"] as? String;self.editorGraph=self.graph;self.editorNode=self.node;self.message.stringValue=data["message"] as? String ?? "Apply plugin settings when finished"}}
  private func commitEditor(){guard let editor,graph==editorGraph,node==editorNode else{message.stringValue="Open this effect's custom interface first";return};request("graph.plugin.editor.commit",["editor":editor],write:true){[weak self] data in self?.populateBuses(data);self?.load();self?.onChanged?()}}
  private func closeEditor(){guard !pending,let editor,let onRequest else{return};pending=true;onRequest("graph.plugin.editor.close",["editor":editor,"expectedRevision":currentRevision?() ?? revision]){[weak self] response in guard let self else{return};self.pending=false;if response["result"] != nil{self.editor=nil;self.message.stringValue="Interface closed"}else{self.message.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Could not close interface"}}}
}

// A rack instance keeps its stable identity while slots move. This compact
// inspector shares the public parameter transaction with external agents.
final class GraphRackControls: NSView, NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate {
  let search=NSSearchField(),table=NSTableView(),message=Theme.label("",size:10,color:Theme.muted)
  let enabled=NSButton(checkboxWithTitle:"Bypass",target:nil,action:nil)
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  var onOpen:((String)->Void)?,onAutomate:((String,UInt32)->Void)?,onActivity:((String,UInt32)->Void)?
  var onGesture:((Bool)->Void)?,onChanged:(()->Void)?,onExpose:((String,UInt32)->Void)?
  var currentRevision:(()->String)?
  private(set) var identity:String?,values=[[String:Any]](),filtered=[[String:Any]]()
  private var generation=0,readGeneration=0,revision="",pending=false,queued=[UInt32:Double](),gestureActive=false
  var recipeMode=false {didSet{enabled.isHidden=recipeMode}}
  private var refreshAfterEdit=false
  override init(frame:NSRect) {
    super.init(frame:frame)
    search.placeholderString="Find a parameter…";search.delegate=self;search.setAccessibilityLabel("Find graph processor parameter")
    table.headerView=nil;table.rowHeight=64;table.backgroundColor=Theme.bg;table.selectionHighlightStyle = .none
    table.dataSource=self;table.delegate=self;table.columnAutoresizingStyle = .lastColumnOnlyAutoresizingStyle
    table.addTableColumn(NSTableColumn(identifier:.init("parameter")))
    table.setAccessibilityLabel("Selected graph processor parameters")
    let scroll=verticalScrollView();scroll.documentView=table;scroll.fixed(height:264)
    enabled.target=self;enabled.action=#selector(toggleBypass);enabled.toolTip="Host bypass · M on the graph · processing keeps running with latency preserved"
    enabled.identifier=NSUserInterfaceItemIdentifier(GraphCommand.bypass.id)
    message.lineBreakMode = .byWordWrapping;message.maximumNumberOfLines=0;message.preferredMaxLayoutWidth=240
    let content=stack(.vertical,[stack(.horizontal,[enabled,NSView(),ActionButton("Open interface"){[weak self] in guard let self,let id=self.identity else{return};self.onOpen?(id)}]),search,scroll,message],spacing:6)
    content.stretchAcrossAxis();content.fill(self)
  }
  required init?(coder:NSCoder){fatalError()}
  func context(_ plugin:[String:Any]?,revision displayedRevision:String="") {
    let id=plugin?["id"] as? String
    enabled.state=plugin?["bypass"] as? Bool==true ? .on:.off
    guard id != identity else{
      if id != nil,!displayedRevision.isEmpty,displayedRevision != revision {
        let fieldEditor=table.window?.firstResponder as? NSTextView
        let editingField=fieldEditor?.delegate as? NSView
        if pending || gestureActive || editingField?.isDescendant(of:table)==true {refreshAfterEdit=true}
        else{refresh()}
      }
      return
    }
    if gestureActive{onGesture?(false);gestureActive=false}
    identity=id;generation+=1;readGeneration+=1;pending=false;queued=[:];revision="";refreshAfterEdit=false;values=[];filter()
    guard let id else{return};load(id:id,generation:generation)
  }
  func refresh(){guard let id=identity else{return};load(id:id,generation:generation)}
  private func load(id:String,generation:Int,preserveMessage:Bool=false,attempt:Int=0) {
    readGeneration+=1;let read=readGeneration
    if !preserveMessage{message.stringValue="Loading parameters…"}
    onRequest?("plugin.parameters.get",["plugin":id]){[weak self] response in
      guard let self,self.identity==id,self.generation==generation,self.readGeneration==read,!self.pending else{return}
      if let error=response["error"] as? [String:Any],error["code"] as? Int == -32002,attempt<5 {
        DispatchQueue.main.asyncAfter(deadline:.now()+0.05*Double(attempt+1)){[weak self] in
          guard let self,self.identity==id,self.generation==generation,self.readGeneration==read,!self.pending else{return}
          self.load(id:id,generation:generation,preserveMessage:preserveMessage,attempt:attempt+1)
        };return
      }
      guard let result=response["result"] as? [String:Any],let values=result["data"] as? [[String:Any]] else{self.message.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Parameters unavailable";return}
      self.revision=result["revision"] as? String ?? "";self.values=values;self.filter();self.refreshAfterEdit=false
      if !preserveMessage{self.message.stringValue="Controls · use a parameter’s menu for automation and effective values."}
    }
  }
  func controlTextDidChange(_ notification:Notification){filter()}
  private func filter(){let q=search.stringValue.trimmingCharacters(in:.whitespacesAndNewlines);filtered=q.isEmpty ? values:values.filter{($0["name"] as? String ?? "").localizedCaseInsensitiveContains(q)};table.reloadData()}
  func numberOfRows(in tableView:NSTableView)->Int{filtered.count}
  func tableView(_ tableView:NSTableView,viewFor tableColumn:NSTableColumn?,row:Int)->NSView? {
    guard filtered.indices.contains(row),let id=identity,let parameter=(filtered[row]["id"] as? NSNumber)?.uint32Value else{return nil}
    let p=filtered[row],version=generation,title=p["name"] as? String ?? "Parameter"
    let choices=p["choices"] as? [String] ?? []
    let low=p["min"] as? Double ?? 0,high=p["max"] as? Double ?? 1,initial=p["value"] as? Double ?? 0
    let step=p["step"] as? Double ?? 0
    let choiceStep=choices.count>1 ? (high-low)/Double(choices.count-1):0
    let valid=low.isFinite && high.isFinite && high>low && (high-low).isFinite && initial.isFinite && initial>=low && initial<=high && step.isFinite && step>=0
    let writable=valid && p["writable"] as? Bool==true
    let logarithmic=p["displayScale"] as? String=="logarithmic" && low>0 && valid
    let fromSlider:(Double)->Double={logarithmic ? exp(log(low)+$0*(log(high)-log(low))):low+$0*(high-low)}
    let toSlider:(Double)->Double={logarithmic ? (log($0)-log(low))/(log(high)-log(low)):($0-low)/(high-low)}
    let format:(Double)->String={value in
      if choiceStep>0,let index=Int(exactly:((value-low)/choiceStep).rounded()),choices.indices.contains(index){return choices[index]}
      return String(format:"%.4g",value)
    }
    let reading=ParameterValueField();reading.fixed(width:88);reading.stringValue=format(initial);reading.isEditable=writable && choices.isEmpty
    reading.toolTip="\(low)…\(high) \(p["unitLabel"] as? String ?? "")";reading.setAccessibilityLabel(title+" value")
    let slider=ParameterSlider(value:valid ? toSlider(initial):0,min:0,max:1){_ in};slider.isEnabled=writable;slider.setAccessibilityLabel(title)
    slider.numberOfTickMarks=choices.count;slider.allowsTickMarkValuesOnly = !choices.isEmpty
    var current=initial
    let apply:(Double)->Void = {[weak self,weak reading,weak slider] requested in
      guard let self,self.identity==id,self.generation==version,writable,requested.isFinite,requested>=low,requested<=high else{return}
      var value=requested
      let increment=choiceStep>0 ? choiceStep:step
      if increment>0{let snapped=low+((requested-low)/increment).rounded()*increment;if snapped.isFinite{value=max(low,min(high,snapped))}}
      reading?.stringValue=format(value);slider?.doubleValue=toSlider(value)
      guard value != current else{return};current=value;self.set(parameter,value:value)
    }
    slider.changed={apply(fromSlider($0))}
    slider.gesture={[weak self] active in guard let self,self.identity==id,self.generation==version else{return};self.gestureActive=active;if active || !self.pending{self.onGesture?(active)};if !active && !self.pending && self.refreshAfterEdit{self.refresh()}}
    reading.editingText={String(current)}
    reading.commit={[weak self,weak reading] text in guard let v=Double(text),v.isFinite,v>=low,v<=high else{reading?.stringValue=format(current);return};apply(v);if let self,!self.pending,self.refreshAfterEdit{self.refresh()}}
    let more=ActionMenuButton("…"){[weak self] in
      let menu=NSMenu(title:title);menu.autoenablesItems=false
      menu.addItem(ContextAction(self?.recipeMode==true ? "Connect modulation…":"Automate this…",enabled:writable){self?.onAutomate?(id,parameter)})
      menu.addItem(ContextAction("Inspect effective value and sources…"){self?.onActivity?(id,parameter)})
      menu.addItem(ContextAction("Expose parameter port"){self?.onExpose?(id,parameter)})
      return menu
    }
    let label=Theme.label(title,size:11);label.lineBreakMode = .byTruncatingTail;label.toolTip=title
    let rowView=NSView();let content=stack(.vertical,[stack(.horizontal,[label,NSView(),more]),stack(.horizontal,[slider,reading])],spacing:2);content.stretchAcrossAxis();content.fill(rowView,inset:3)
    return rowView
  }
  func set(_ parameter:UInt32,value:Double) {
    guard let id=identity,let onRequest else{return}
    if pending{queued[parameter]=value;return}
    if let previous=values.first(where:{($0["id"] as? NSNumber)?.uint32Value==parameter})?["value"] as? NSNumber,previous.doubleValue==value {
      if let key=queued.keys.sorted().first,let amount=queued.removeValue(forKey:key){set(key,value:amount)}
      else if !gestureActive{onGesture?(false);if refreshAfterEdit{refresh()}}
      return
    }
    pending=true;readGeneration+=1;let version=generation
    onRequest("plugin.parameters.set",["plugin":id,"values":[["id":parameter,"value":value]],"expectedRevision":revision.isEmpty ? currentRevision?() ?? "":revision]){[weak self] response in
      guard let self,self.identity==id,self.generation==version else{return};self.pending=false
      guard let result=response["result"] as? [String:Any] else{self.queued=[:];self.onGesture?(false);self.gestureActive=false;self.message.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Parameter edit failed";self.load(id:id,generation:version,preserveMessage:true);return}
      self.revision=result["revision"] as? String ?? self.revision
      if let i=self.values.firstIndex(where:{($0["id"] as? NSNumber)?.uint32Value==parameter}){self.values[i]["value"]=value}
      if let key=self.queued.keys.sorted().first,let amount=self.queued.removeValue(forKey:key){self.set(key,value:amount)}
      else if !self.gestureActive{self.onGesture?(false);if self.refreshAfterEdit{self.refresh()}}
    }
  }
  @objc private func toggleBypass(){guard let id=identity,let onRequest else{return};let version=generation
    onRequest("plugin.bypass",["plugin":id,"bypass":enabled.state == .on,"expectedRevision":currentRevision?() ?? revision]){[weak self] response in
      guard let self,self.identity==id,self.generation==version else{return}
      if let result=response["result"] as? [String:Any]{self.revision=result["revision"] as? String ?? "";self.onChanged?()}
      else {self.enabled.state=self.enabled.state == .off ? .on:.off;self.message.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Bypass failed"}
    }
  }
}
