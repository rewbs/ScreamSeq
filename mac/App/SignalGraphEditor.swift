import AppKit

final class SignalGraphEditor: NSView, NSTextFieldDelegate {
  private var fieldDraft=false
  var hasDraft:Bool {get{fieldDraft || envelopeEditor.hasDraft} set{fieldDraft=newValue}}
  func controlTextDidChange(_ notification:Notification){hasDraft=true}
  let canvas=SignalCanvas(frame:NSRect(x:0,y:0,width:1000,height:600)), scroll=NSScrollView()
  let library=NSPopUpButton(),filter=NSPopUpButton(),nodeList=NSPopUpButton(),source=NSPopUpButton(),destination=NSPopUpButton(),connection=NSPopUpButton()
  let instrumentPicker=NSPopUpButton(),assignmentHeading=Theme.label("ORDINARY CHANNEL GRAPH",size:10,color:Theme.muted)
  let nodeKind=NSPopUpButton(),connectionKind=NSPopUpButton(),assignment=NSPopUpButton()
  let name=NSTextField(string:""),outputPort=NSTextField(string:"0"),inputPort=NSTextField(string:"0"),parameter=NSTextField(string:"0")
  let minimum=NSTextField(string:"0"),maximum=NSTextField(string:"1"),base=NSTextField(string:"0"),rate=NSTextField(string:"1"),phase=NSTextField(string:"0"),attack=NSTextField(string:"0.01"),release=NSTextField(string:"0.1"),controller=NSTextField(string:"1")
  let status=Theme.label("Shared audio graph",size:11,color:Theme.muted),detail=Theme.label("Select a node",size:12,weight:.semibold)
  var onRequest:((String,[String:Any],@escaping ([String:Any])->Void)->Void)?
  var onChoosePlugin:((@escaping ([String:Any])->Void)->Void)?,onBus:((String)->Void)?,onPlugin:((String)->Void)?
  var onPatternCommands:((String?)->Void)?
  private(set) var data=[String:Any](),revision="",loading=false
  var graphID:String?,selectedID:String?,filterID:String?
  var definitions:[[String:Any]]{data["library"] as? [[String:Any]] ?? []}
  var mixer:[String:Any]{data["mixer"] as? [String:Any] ?? [:]}
  var buses:[[String:Any]]{mixer["buses"] as? [[String:Any]] ?? []}
  var definition:[String:Any]?{definitions.first{$0["id"] as? String==graphID}}
  var nodes:[[String:Any]]{definition?["nodes"] as? [[String:Any]] ?? []}
  var selectedNode:[String:Any]?{nodes.first{$0["id"] as? String==selectedID}}
  let inspector=NSView()
  let connectionGain=NSTextField(string:"1")
  let connectionEnabled=NSButton(checkboxWithTitle:"Enabled",target:nil,action:nil)
  var portCatalogs=[String:[String:Any]](),exposedParameters=[String:UInt32]()
  lazy var enableRouting=ActionButton("Enable routing"){[weak self] in self?.mutate("mixer.enable",[:])}
  let envelopeEditor=GraphEnvelopeEditor(frame:.zero)
  let pluginControls=GraphPluginControls(frame:.zero)
  let libraryName=NSTextField(string:""),libraryNumber=NSTextField(string:"1"),assignAmount=NSTextField(string:"1"),assignWet=NSTextField(string:"1")
  var songNodeBus=[String:String](),songNodeGraph=[String:String](),songNodePlugin=[String:String](),songConnections=[[String:Any]]()
  private var busSection:NSStackView!,sourceSection:NSStackView!,librarySection:NSStackView!,audioPorts:NSStackView!,modulationSection:NSStackView!
  private var configuredScope:Bool?
  private var playbackActivity=[[String:Any]](),playbackRunning=false
  func showActivity(_ values:[[String:Any]],playing:Bool){
    playbackActivity=values;playbackRunning=playing
    var active=[String:[String:Any]]()
    for item in values{if item["role"] as? String=="instrument"{active["instrument-graph:\(item["instrument"] as? String ?? ""):\(item["target"] as? String ?? "")"]=item;continue};let role=(item["role"] as? String ?? "").capitalized;active["graph:\(item["target"] as? String ?? ""):\(role):\(item["graph"] as? String ?? "")"]=item}
    var changed=false
    for i in canvas.nodes.indices where canvas.nodes[i].id.hasPrefix("graph:") || canvas.nodes[i].id.hasPrefix("instrument-graph:"){
      let id=canvas.nodes[i].id,parts=id.split(separator:":"),role=id.hasPrefix("instrument-graph:") ? "Instrument" : parts.count>2 ? String(parts[2]) : "Copy",item=active[id]
      let order=item?["order"] as? Int ?? 0,tail=item?["tail"] as? Bool ?? false
      let state=playing ? (order>0 ? "\(role) · playing #\(order)" : tail ? "\(role) · tail" : "\(role) · bypassed") : "\(role) · independent copy"
      let badge=playing && (order>0 || tail) ? (tail ? "tail" : "playing") : nil
      if canvas.nodes[i].detail != state || canvas.nodes[i].activity != badge {canvas.nodes[i].detail=state;canvas.nodes[i].activity=badge;changed=true}
    }
    if changed{canvas.needsDisplay=true}
  }

  override init(frame:NSRect){
    super.init(frame:frame)
    library.setAccessibilityLabel("Graph library");filter.setAccessibilityLabel("Graph channel filter");nodeList.setAccessibilityLabel("Graph nodes");assignment.setAccessibilityLabel("Ordinary channel subgraph")
    for popup in [library,filter,nodeList]{popup.target=self;popup.action = popup===library ? #selector(changeLibrary) : popup===filter ? #selector(changeFilter) : #selector(changeNode)}
    nodeKind.addItems(withTitles:["automation","lfo","follower","random","note-envelope","midi","amount"]);connectionKind.addItems(withTitles:["Audio","Modulation"])
    for (view,label) in [(name,"Node or bus name"),(outputPort,"Source output port"),(inputPort,"Destination input port"),(parameter,"Stable plugin parameter ID"),(minimum,"Modulation minimum"),(maximum,"Modulation maximum"),(base,"Parameter base"),(rate,"Cycles per beat"),(phase,"Phase"),(attack,"Attack seconds"),(release,"Release seconds"),(controller,"MIDI controller")]{view.setAccessibilityLabel(label)}
    for field in [outputPort,inputPort,parameter,minimum,maximum,base,rate,phase,attack,release,controller]{field.fixed(width:60)}
    source.setAccessibilityLabel("Connection source");destination.setAccessibilityLabel("Connection destination");connection.setAccessibilityLabel("Existing graph connections")
    scroll.documentView=canvas;scroll.hasVerticalScroller=true;scroll.hasHorizontalScroller=true;scroll.allowsMagnification=true;scroll.minMagnification=0.3;scroll.maxMagnification=2;scroll.drawsBackground=false
    canvas.onSelect = {[weak self] id in self?.selectedID=id;self?.inspect()}
    canvas.onSelectEdge = {[weak self] i in self?.selectConnection(i)}
    canvas.onDelete = {[weak self] in guard let self else{return};if self.canvas.selectedEdge != nil{self.disconnect()}else{self.removeNode()}}
    canvas.onZoom = {[weak self] factor in self?.zoom(factor)}
    canvas.onConnectPorts = {[weak self] a,b,out,input,mod in guard let self else{return};if self.graphID != nil {self.connectionKind.selectItem(withTitle:mod ? "Modulation" : "Audio");self.outputPort.stringValue=String(out);self.inputPort.stringValue=String(input);if mod{self.parameter.stringValue=String(input)};self.connectionModeChanged()};self.connect(a,b)}
    canvas.onMove = {[weak self] id,x,y in self?.move(id,x:x,y:y)}
    canvas.onConnect = {[weak self] a,b in self?.connect(a,b)}
    canvas.onOpen = {[weak self] id in guard let self else{return};if self.graphID==nil{self.openSongNode(id)}else{self.selectedID=id;self.inspect();if self.selectedNode?["kind"] as? String=="plugin"{self.pluginControls.load()};self.name.scrollToVisible(self.name.bounds);self.window?.makeFirstResponder(self.name)}}
    pluginControls.onRequest = {[weak self] method,p,reply in self?.onRequest?(method,p,reply)}
    pluginControls.currentRevision = {[weak self] in self?.revision ?? ""}
    pluginControls.onChanged = {[weak self] in self?.load()}
    pluginControls.onCatalog = {[weak self] graph,node,catalog in guard let self,self.graphID==graph else{return};self.portCatalogs[node]=catalog;self.rebuild()}
    pluginControls.onParameter = {[weak self] id in guard let self else{return};if let node=self.selectedID{self.exposedParameters[node]=id};self.parameter.stringValue=String(id);self.rebuild();self.connectionKind.selectItem(withTitle:"Modulation");self.connectionModeChanged();if let selected=self.selectedID,let i=self.destination.itemArray.firstIndex(where:{$0.representedObject as? String==selected}){self.destination.selectItem(at:i)}}
    envelopeEditor.isHidden=true
    envelopeEditor.onRequest = {[weak self] method,p,reply in self?.onRequest?(method,p,reply)}
    envelopeEditor.onChanged = {[weak self] in self?.load()}
    libraryNumber.fixed(width:50);assignAmount.fixed(width:65);assignWet.fixed(width:65)
    for field in [libraryName,libraryNumber,assignAmount,assignWet,name,outputPort,inputPort,parameter,minimum,maximum,base,rate,phase,attack,release,controller]{field.delegate=self}
    libraryName.setAccessibilityLabel("Subgraph name");libraryNumber.setAccessibilityLabel("Subgraph number");assignAmount.setAccessibilityLabel("Ordinary graph Amount");assignWet.setAccessibilityLabel("Ordinary graph wet mix")
    librarySection=stack(.vertical,[Theme.label("LIBRARY DEFINITION",size:10,color:Theme.muted),stack(.horizontal,[libraryNumber,libraryName]),stack(.horizontal,[ActionButton("Save name / number"){[weak self] in self?.renameLibrary()},ActionButton("Delete unused"){[weak self] in if let id=self?.graphID{self?.mutate("graph.remove",["graph":id])}}])],spacing:5)
    busSection=stack(.vertical,[assignmentHeading,assignment,stack(.horizontal,[Theme.label("Amount / Wet",size:11),assignAmount,assignWet]),stack(.horizontal,[ActionButton("Assign"){[weak self] in self?.assign()},ActionButton("Mixer controls"){[weak self] in guard let self,let id=self.selectedID else{return};self.onBus?(self.songNodeBus[id] ?? id)}]),ActionButton("Pattern graph commands…"){[weak self] in guard let self else{return};self.onPatternCommands?(self.selectedID.flatMap{self.songNodeBus[$0]})}],spacing:5)
    sourceSection=stack(.vertical,[Theme.label("MODULATION SOURCE",size:10,color:Theme.muted),stack(.horizontal,[Theme.label("Rate / phase",size:11),rate,phase]),stack(.horizontal,[Theme.label("Attack / release",size:11),attack,release]),stack(.horizontal,[Theme.label("MIDI CC",size:11),controller,ActionButton("Apply source"){[weak self] in self?.sourceSettings()}])],spacing:5)
    connectionGain.fixed(width:60);connectionGain.setAccessibilityLabel("Connection gain");connectionGain.delegate=self;connectionEnabled.state = .on
    connection.target=self;connection.action = #selector(connectionChosen)
    audioPorts=stack(.horizontal,[Theme.label("Out / In / gain",size:11),outputPort,inputPort,connectionGain])
    modulationSection=stack(.vertical,[stack(.horizontal,[Theme.label("Parameter ID",size:11),parameter]),stack(.horizontal,[Theme.label("Range",size:11),minimum,maximum]),stack(.horizontal,[Theme.label("Base",size:11),base,connectionEnabled])],spacing:5)
    connectionKind.target=self;connectionKind.action = #selector(connectionModeChanged)
    let controls=stack(.vertical,[librarySection,detail,nodeList,name,
      stack(.horizontal,[ActionButton("Apply name"){[weak self] in self?.rename()},ActionButton("Remove node"){[weak self] in self?.removeNode()},ActionButton("Open"){[weak self] in guard let self,let id=self.selectedID else{return};if self.graphID==nil{self.openSongNode(id)}}]),
      busSection,sourceSection,pluginControls,
      Theme.label("CONNECTIONS",size:10,color:Theme.muted),connectionKind,source,destination,audioPorts,modulationSection,
      stack(.horizontal,[ActionButton("Connect"){[weak self] in self?.connectSelected()},ActionButton("Update wire"){[weak self] in self?.updateConnection()}]),connection,ActionButton("Remove connection"){[weak self] in self?.disconnect()},
      Theme.label("Green: audio · Gold: modulation. Drag ports to connect. Open a subgraph copy to edit its shared definition.",size:11,color:Theme.muted)
    ],spacing:7)
    controls.stretchAcrossAxis();controls.fill(inspector,inset:8)
    let inspectorScroll=verticalScrollView();inspectorScroll.documentView=inspector;inspectorScroll.fixed(width:272)
    inspector.translatesAutoresizingMaskIntoConstraints=false
    NSLayoutConstraint.activate([inspector.leadingAnchor.constraint(equalTo:inspectorScroll.contentView.leadingAnchor),inspector.topAnchor.constraint(equalTo:inspectorScroll.contentView.topAnchor),inspector.widthAnchor.constraint(equalTo:inspectorScroll.contentView.widthAnchor)])
    let drawing=stack(.vertical,[scroll,envelopeEditor],spacing:2);drawing.stretchAcrossAxis()
    let body=stack(.horizontal,[drawing,inspectorScroll],spacing:2);body.stretchAcrossAxis()
    let toolbar=stack(.horizontal,[enableRouting,library,ActionButton("New"){[weak self] in self?.mutate("graph.create",["name":"New subgraph"])},ActionButton("Clone"){[weak self] in if let id=self?.graphID{self?.mutate("graph.clone",["graph":id])}},filter,ActionButton("−"){[weak self] in self?.zoom(1/1.2)},ActionButton("+"){[weak self] in self?.zoom(1.2)},ActionButton("Fit"){[weak self] in self?.fit()},ActionButton("Arrange"){[weak self] in self?.arrange()},ActionButton("Reload"){[weak self] in self?.load()}],spacing:4)
    instrumentPicker.setAccessibilityLabel("Sample instrument graph target");instrumentPicker.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    let add=stack(.horizontal,[ActionButton("Add effect…"){[weak self] in self?.choosePlugin()},nodeKind,ActionButton("Add source"){[weak self] in self?.addSource()},instrumentPicker,ActionButton("Instrument graph"){[weak self] in self?.inspectSampleInstrument()},NSView(),Theme.label("Row → persistent → ordinary → output",size:10,color:Theme.muted)],spacing:5)
    let content=stack(.vertical,[toolbar,add,body,status],spacing:5);content.stretchAcrossAxis();content.fill(self,inset:6)
    for popup in [library,filter,source,destination,nodeList,assignment,connection]{popup.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
  }
  required init?(coder:NSCoder){fatalError()}
  func load(){guard !loading,let onRequest else{return};loading=true;onRequest("graph.get",["includeState":false]){[weak self] response in guard let self else{return};self.loading=false;if let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any]{self.revision=result["revision"] as? String ?? "";self.update(data)}else{self.error(response)}}}
  func update(_ value:[String:Any]){data=value;hasDraft=false
    if graphID != nil && definition==nil{graphID=nil}
    picker(library,[("Song graph","")]+definitions.map{("\($0["number"] as? Int ?? 0) · \($0["name"] as? String ?? "Subgraph")",$0["id"] as? String ?? "")},select:graphID)
    picker(filter,[("All channels","")]+buses.map{($0["name"] as? String ?? "Bus",$0["id"] as? String ?? "")},select:filterID)
    picker(assignment,[("Dry","")]+definitions.map{($0["name"] as? String ?? "Subgraph",$0["id"] as? String ?? "")},select:nil)
    picker(instrumentPicker,sampleInstruments.map{("I\($0["index"] ?? 0) · \($0["name"] as? String ?? "Instrument")",$0["id"] as? String ?? "")},select:chosen(instrumentPicker))
    rebuild();inspect()
  }
  func picker(_ picker:NSPopUpButton,_ values:[(String,String)],select:String?){picker.removeAllItems();for (title,id) in values{let item=NSMenuItem(title:title,action:nil,keyEquivalent:"");item.representedObject=id;picker.menu?.addItem(item)};if let index=values.firstIndex(where:{$0.1==(select ?? "")}){picker.selectItem(at:index)}}
  func chosen(_ picker:NSPopUpButton)->String?{guard let id=picker.selectedItem?.representedObject as? String,!id.isEmpty else{return nil};return id}
  @objc func changeLibrary(){hasDraft=false;graphID=chosen(library);selectedID=nil;rebuild();inspect();fit()}
  @objc func changeFilter(){filterID=chosen(filter);rebuild()}
  @objc func changeNode(){selectedID=chosen(nodeList);canvas.selected=selectedID;inspect()}
  func showBus(_ id:String,filter:Bool=false){graphID=nil;selectedID=id;if filter{filterID=id};update(data)}
  func rebuild(){
    let previousSource=chosen(source),previousTarget=chosen(destination),previousConnection=chosen(connection)
    enableRouting.isHidden = !buses.isEmpty
    canvas.emptyMessage = buses.isEmpty && graphID==nil ? "Enable routing to connect channels, instruments and effects." : "Create a subgraph to start connecting sound."
    let scope=graphID != nil
    if configuredScope != scope {configuredScope=scope;connectionKind.removeAllItems();connectionKind.addItems(withTitles:scope ? ["Audio","Modulation"] : ["Main output","Send","Graph sidechain","Graph auxiliary","Plugin sidechain","Plugin auxiliary"])}
    connectionModeChanged()

    var display=[SignalCanvasNode](),edges=[SignalCanvasEdge]()
    if let definition {
      for node in nodes{display.append(canvasNode(node,definition:definition))}
      for e in definition["audio"] as? [[String:Any]] ?? []{edges.append(SignalCanvasEdge(source:e["source"] as? String ?? "",target:e["target"] as? String ?? "",label:"\(e["output"] as? Int ?? 0) → \(e["input"] as? Int ?? 0) · ×\(e["gain"] ?? 1)",output:(e["output"] as? NSNumber)?.uint32Value ?? 0,input:(e["input"] as? NSNumber)?.uint32Value ?? 0))}
      for e in definition["modulation"] as? [[String:Any]] ?? []{edges.append(SignalCanvasEdge(source:e["source"] as? String ?? "",target:e["target"] as? String ?? "",label:"Param \(e["parameter"] as? Int ?? 0)",modulation:true,input:(e["parameter"] as? NSNumber)?.uint32Value ?? 0,enabled:e["enabled"] as? Bool ?? true))}
    }else{(display,edges)=buildSongOverview()}
    canvas.update(display,edges:edges);canvas.selected=selectedID
    showActivity(playbackActivity,playing:playbackRunning)
    let list=display.map{($0.title,$0.id)};picker(nodeList,list,select:selectedID);picker(source,list,select:previousSource ?? selectedID);picker(destination,list,select:previousTarget)
    var connections=[(String,String)]();for (i,e) in edges.enumerated(){let a=display.first{$0.id==e.source}?.title ?? "?",b=display.first{$0.id==e.target}?.title ?? "?";connections.append(("\(a) → \(b) \(e.label)",String(i)))};picker(connection,connections,select:previousConnection)
    status.stringValue=graphID==nil ? "Configured routes · live stack order appears on playing copies" : "Each channel gets its own copy · drag nodes and ports to edit"
  }
  func inspect(){
    envelopeEditor.context(graph:graphID,node:selectedNode,patterns:data["patterns"] as? [[String:Any]] ?? [],revision:revision)
    let busID=selectedID.flatMap{songNodeBus[$0]}
    librarySection.isHidden=graphID==nil;busSection.isHidden=graphID != nil || (busID==nil && selectedInstrument==nil)
    assignmentHeading.stringValue=selectedInstrument==nil ? "ORDINARY CHANNEL GRAPH" : "INSTRUMENT → CHANNEL GRAPH"
    let kind=selectedNode?["kind"] as? String ?? ""
    sourceSection.isHidden=graphID==nil || !["lfo","follower","random","note-envelope","midi","amount"].contains(kind)
    pluginControls.isHidden=kind != "plugin";pluginControls.context(graph:graphID,node:kind=="plugin" ? selectedID : nil)
    libraryName.stringValue=definition?["name"] as? String ?? "";libraryNumber.integerValue=definition?["number"] as? Int ?? 1

    if graphID==nil,let instrument=selectedInstrument{
      detail.stringValue="Instrument \(instrument["index"] ?? 0) · before channel";name.stringValue=instrument["name"] as? String ?? "Instrument"
      let entry=(data["instrumentAssignments"] as? [[String:Any]] ?? []).first{$0["target"] as? String==instrument["id"] as? String}
      assignAmount.doubleValue=entry?["amount"] as? Double ?? 1;assignWet.doubleValue=entry?["wet"] as? Double ?? 1
      for (i,item) in assignment.itemArray.enumerated() where item.representedObject as? String==(entry?["graph"] as? String ?? ""){assignment.selectItem(at:i)}
    }else if graphID==nil,let bus=buses.first(where:{$0["id"] as? String==busID}){detail.stringValue=bus["name"] as? String ?? "Bus";name.stringValue=detail.stringValue
      let entry=(data["assignments"] as? [[String:Any]] ?? []).first{$0["target"] as? String==busID}
      assignAmount.doubleValue=entry?["amount"] as? Double ?? 1;assignWet.doubleValue=entry?["wet"] as? Double ?? 1
      let assigned=entry?["graph"] as? String
      for (i,item) in assignment.itemArray.enumerated() where item.representedObject as? String==(assigned ?? ""){assignment.selectItem(at:i)}
    }else if let node=selectedNode{detail.stringValue=node["kind"] as? String ?? "Node";name.stringValue=node["name"] as? String ?? "";for (field,key) in [(rate,"rate"),(phase,"phase"),(attack,"attack"),(release,"release"),(controller,"controller")]{field.stringValue="\(node[key] ?? 0)"}}
    else if graphID==nil,let selectedID,let display=canvas.nodes.first(where:{$0.id==selectedID}){detail.stringValue=display.title;name.stringValue=display.title}
    else{detail.stringValue="Select a node";name.stringValue=""}
    if let index=nodeList.itemArray.firstIndex(where:{$0.representedObject as? String==selectedID}){nodeList.selectItem(at:index)}
  }
  func fit(){let content=canvas.nodes.reduce(NSRect.zero){$0.union($1.rect)};guard content.width>0 else{return};scroll.magnification=max(scroll.minMagnification,min(1,min((scroll.contentSize.width-30)/max(1,content.maxX+20),(scroll.contentSize.height-30)/max(1,content.maxY+20))));canvas.scroll(.zero)}
  func zoom(_ factor:Double){scroll.magnification=max(scroll.minMagnification,min(scroll.maxMagnification,scroll.magnification*factor))}
  private func error(_ response:[String:Any]){status.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Graph operation failed"}
  func mutate(_ method:String,_ params:[String:Any]){guard !loading,let onRequest else{return};loading=true;var p=params;p["expectedRevision"]=revision;onRequest(method,p){[weak self] response in guard let self else{return};self.loading=false;guard let result=response["result"] as? [String:Any]else{self.rebuild();self.error(response);return};if ["graph.create","graph.clone"].contains(method){self.graphID=(result["data"] as? [String:Any])?["graph"] as? String};self.hasDraft=false;self.load()}}
  func updateDefinition(_ change:(inout [String:Any])->Void){guard var d=definition else{return};change(&d);mutate("graph.update",["definition":d])}
  private func move(_ id:String,x:Double,y:Double){guard !loading else{rebuild();return};guard graphID != nil else{mutate("graph.layout.set",["positions":[["node":id,"x":x,"y":y]]]);return};updateDefinition{d in var n=d["nodes"] as? [[String:Any]] ?? [];if let i=n.firstIndex(where:{$0["id"] as? String==id}){n[i]["x"]=x;n[i]["y"]=y};d["nodes"]=n}}
  private func rename(){guard let id=selectedID else{return};if graphID==nil{guard songNodeBus[id]==id else{status.stringValue="Open this processor to edit its definition";return};mutate("mixer.bus.set",["bus":id,"name":name.stringValue])}else{updateDefinition{d in var n=d["nodes"] as? [[String:Any]] ?? [];if let i=n.firstIndex(where:{$0["id"] as? String==id}){n[i]["name"]=name.stringValue};d["nodes"]=n}}}
  private func removeNode(){if let graphID,let selectedID{mutate("graph.node.remove",["graph":graphID,"node":selectedID])}}
  private func assign(){if graphID==nil,let instrument=selectedInstrument{assignSampleInstrument(instrument);return};guard graphID==nil,let selectedID,let bus=songNodeBus[selectedID],let amount=Double(assignAmount.stringValue),let wet=Double(assignWet.stringValue) else{return};mutate("graph.assign",["target":bus,"graph":chosen(assignment) as Any? ?? NSNull(),"amount":amount,"wet":wet])}
  private func renameLibrary(){guard let number=Int(libraryNumber.stringValue)else{return};updateDefinition{$0["name"]=libraryName.stringValue;$0["number"]=number}}
  @objc func connectionModeChanged(){let kind=connectionKind.titleOfSelectedItem ?? "",modulation=kind=="Modulation";modulationSection?.isHidden = !modulation;audioPorts?.isHidden=modulation || kind=="Main output"
    connectionGain.toolTip=graphID == nil ? "Send / sidechain gain in dB" : "Audio gain multiplier, −16…16"
    outputPort.isEnabled=kind != "Send";inputPort.isEnabled=kind != "Send"
    if graphID==nil {if kind.contains("sidechain") && inputPort.stringValue=="0" {inputPort.stringValue="1"};if kind.contains("auxiliary") && outputPort.stringValue=="0" {outputPort.stringValue="1"}}
  }

  private func addSource(){guard let graphID else{status.stringValue="Create or select a subgraph first";return};mutate("graph.node.add",["graph":graphID,"kind":nodeKind.titleOfSelectedItem ?? "lfo","x":300,"y":220+nodes.count*12])}
  private func choosePlugin(){guard let graphID else{status.stringValue="Create or select a subgraph first";return};onChoosePlugin?{[weak self] descriptor in guard let self else{return};var recipe=[String:Any]();for key in ["format","name","path","classID","type","subtype","manufacturer"]{recipe[key]=descriptor[key]};var params:[String:Any]=["graph":graphID,"kind":"plugin","plugin":recipe,"name":descriptor["name"] ?? "Effect","x":300,"y":100+self.nodes.count*12]
      let edges=self.definition?["audio"] as? [[String:Any]] ?? [],output=self.nodes.first{$0["kind"] as? String=="output"}?["id"] as? String
      let after=self.selectedNode.flatMap{["input","plugin"].contains($0["kind"] as? String ?? "") ? $0["id"] as? String : nil} ?? edges.last{$0["target"] as? String==output && ($0["input"] as? Int ?? 0)==0}?["source"] as? String
      if let after,edges.filter({$0["source"] as? String==after && ($0["output"] as? Int ?? 0)==0}).count==1{params["insertAfter"]=after}
      self.mutate("graph.node.add",params)}}
  private func sourceSettings(){guard let selectedID else{return};var values=[String:Any]();for (field,key) in [(rate,"rate"),(phase,"phase"),(attack,"attack"),(release,"release")]{guard let value=Double(field.stringValue),value.isFinite else{status.stringValue="Enter valid source values";return};values[key]=value};guard let cc=Int(controller.stringValue)else{return};values["controller"]=cc;updateDefinition{d in var n=d["nodes"] as? [[String:Any]] ?? [];if let i=n.firstIndex(where:{$0["id"] as? String==selectedID}){n[i].merge(values){_,new in new}};d["nodes"]=n}}
  func connectSelected(){guard let a=chosen(source),let b=chosen(destination)else{return};connect(a,b)}
  func connect(_ a:String,_ b:String){
    if graphID==nil{connectSong(a,b);return}
    if connectionKind.indexOfSelectedItem==0{guard let input=Int(inputPort.stringValue),let output=Int(outputPort.stringValue),let gain=Double(connectionGain.stringValue),gain.isFinite else{status.stringValue="Enter valid port numbers and gain";return};updateDefinition{d in var edges=d["audio"] as? [[String:Any]] ?? [];edges.append(["source":a,"target":b,"input":input,"output":output,"gain":gain]);d["audio"]=edges}}
    else{guard let parameter=UInt32(parameter.stringValue),let lo=Double(minimum.stringValue),let hi=Double(maximum.stringValue),let base=Double(base.stringValue)else{return};updateDefinition{d in var edges=d["modulation"] as? [[String:Any]] ?? [];edges.append(["source":a,"target":b,"parameter":parameter,"minimum":lo,"maximum":hi,"base":base,"enabled":connectionEnabled.state == .on]);d["modulation"]=edges}}
  }
  func disconnect(){guard let value=chosen(connection),let index=Int(value)else{return};guard graphID != nil else{disconnectSong(index);return};updateDefinition{d in var audio=d["audio"] as? [[String:Any]] ?? [],mod=d["modulation"] as? [[String:Any]] ?? [];if index<audio.count{audio.remove(at:index)}else if mod.indices.contains(index-audio.count){mod.remove(at:index-audio.count)};d["audio"]=audio;d["modulation"]=mod}}
}
