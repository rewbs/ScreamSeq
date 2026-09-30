import AppKit

final class SignalGraphEditor: NSView, NSSearchFieldDelegate {
  private var fieldDraft=false
  var hasDraft:Bool {get{fieldDraft || envelopeEditor.hasDraft || canvas.isEditing || busControls.editing} set{fieldDraft=newValue}}
  func controlTextDidChange(_ notification:Notification){if notification.object as AnyObject? === nodeSearch {changeNodeFilter()}else{hasDraft=true}}
  let canvas=SignalCanvas(frame:NSRect(x:0,y:0,width:1000,height:600)), scroll=NSScrollView()
  let library=NSPopUpButton(),filter=NSPopUpButton(),nodeList=NSPopUpButton(),source=NSPopUpButton(),destination=NSPopUpButton(),connection=NSPopUpButton()
  let nodeSearch=NSSearchField(),nodeCategory=NSPopUpButton(),filterCount=Theme.label("",size:11,color:Theme.muted)
  var definitionEdgeIndices=[Int]()
  let outputChoice=NSPopUpButton(),inputChoice=NSPopUpButton()
  var catalogLoading=false,catalogGeneration=0
  var catalogFailures=[String:String]()
  let instrumentPicker=NSPopUpButton(),assignmentHeading=Theme.label("ORDINARY CHANNEL GRAPH",size:10,color:Theme.muted)
  let nodeKind=NSPopUpButton(),connectionKind=NSPopUpButton(),assignment=NSPopUpButton()
  let name=NSTextField(string:""),outputPort=NSTextField(string:"0"),inputPort=NSTextField(string:"0"),parameter=NSTextField(string:"0")
  let minimum=NSTextField(string:"0"),maximum=NSTextField(string:"1"),base=NSTextField(string:"0"),rate=NSTextField(string:"1"),phase=NSTextField(string:"0"),attack=NSTextField(string:"0.01"),release=NSTextField(string:"0.1"),controller=NSTextField(string:"1")
  let status=Theme.label("Shared audio graph",size:11,color:Theme.muted),detail=Theme.label("Select a node",size:12,weight:.semibold)
  let routingStatus=Theme.label("",size:11,color:Theme.gold)
  var onRequest:((String,[String:Any],@escaping ([String:Any])->Void)->Void)?
  var onChoosePlugin:((@escaping ([String:Any])->Void)->Void)?,onBus:((String)->Void)?,onPlugin:((String)->Void)?
  var onPatternCommands:((String?)->Void)?
  private(set) var data=[String:Any](),revision="",loading=false
  var graphID:String?,selectedID:String?,filterID:String?
  var processingGroupID:String?,boundaryPorts=[GraphBoundaryPort:GraphRealPort]()
  var ungroupedNodes=[SignalCanvasNode](),ungroupedEdges=[SignalCanvasEdge]()
  var pendingModulationFocus:Int?
  var definitions:[[String:Any]]{data["library"] as? [[String:Any]] ?? []}
  var mixer:[String:Any]{data["mixer"] as? [String:Any] ?? [:]}
  var buses:[[String:Any]]{mixer["buses"] as? [[String:Any]] ?? []}
  var definition:[String:Any]?{definitions.first{$0["id"] as? String==graphID}}
  var nodes:[[String:Any]]{definition?["nodes"] as? [[String:Any]] ?? []}
  var selectedNode:[String:Any]?{nodes.first{$0["id"] as? String==selectedID}}
  let inspector=NSView(), inspectorScroll=verticalScrollView()
  let breadcrumbs=NSStackView(), scopeLabel=Theme.label("",size:10,color:Theme.muted)
  let addMenu=GraphAddMenu(),targetMenu=GraphAddMenu()
  var addCatalog=[[String:Any]](), addCatalogLoaded=false, addGeneration=0
  var graphViewStates=[String:GraphViewState](), graphOrigin:String?,graphTarget:String?
  var provisionalLayouts=[String:[String:NSPoint]](), projectionDocument=""
  var onShowPattern:((String)->Void)?
  var onReveal:(()->Void)?
  var deferredGraphCommand:(()->Void)?
  var manualConnection = false
  let connectionHeading=Theme.label("NEW CONNECTION",size:11,weight:.semibold)
  let connectionHint=Theme.label("Drag between matching ports, or choose endpoints below.",size:11,color:Theme.muted)
  var connectionSection:NSStackView!,connectionForm:NSStackView!,nodeSection:NSStackView!
  lazy var connectButton=ActionButton("Connect",prominent:true){[weak self] in self?.connectSelected()}
  lazy var updateConnectionButton=ActionButton("Update connection",prominent:true){[weak self] in self?.window?.makeFirstResponder(self?.canvas);self?.updateConnection()}
  lazy var removeConnectionButton=ActionButton("Remove"){[weak self] in self?.disconnect()}
  lazy var openConnectionOwnerButton=ActionButton("Edit chain…"){[weak self] in self?.openConnectionOwner()}
  lazy var useDetector=ActionButton("Use connected detector (Auto)"){[weak self] in self?.useConnectedDetector()}
  let gainLabel=Theme.label("Gain ×",size:11),portRow=NSStackView()

  let connectionGain=NSTextField(string:"1")
  let connectionEnabled=NSButton(checkboxWithTitle:"Enabled",target:nil,action:nil)
  var portCatalogs=[String:[String:Any]](),exposedParameters=[String:UInt32]()
  var signalReadings=GraphSignalReadings(),lastOverload:String?
  lazy var enableRouting=ActionButton("Enable routing"){[weak self] in self?.mutate("mixer.enable",[:])}
  let envelopeEditor=GraphEnvelopeEditor(frame:.zero)
  let pluginControls=GraphPluginControls(frame:.zero)
  let rackControls=GraphRackControls(frame:.zero)
  let busControls=GraphBusControls(frame:.zero)
  let signalScope=GraphSignalScope(frame:.zero)
  let listenControls=GraphListenControls(frame:.zero)
  let libraryName=NSTextField(string:""),libraryNumber=NSTextField(string:"1"),assignAmount=NSTextField(string:"1"),assignWet=NSTextField(string:"1")
  var songNodeBus=[String:String](),songNodeGraph=[String:String](),songNodePlugin=[String:String](),songConnections=[[String:Any]]()
  private var busSection:NSStackView!,sourceSection:NSStackView!,librarySection:NSStackView!,audioPorts:NSStackView!,modulationSection:NSStackView!
  private var configuredScope:Bool?
  private var playbackActivity=[[String:Any]](),playbackRunning=false
  private var renderedContext=[String]()
  private var inspectedObject=""
  private var viewContext:[String] {[graphID ?? "",filterID ?? "",selectedID ?? "",nodeSearch.stringValue,
    String(nodeCategory.indexOfSelectedItem),graphOrigin ?? "",graphTarget ?? "",processingGroupID ?? ""]}
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
    canvas.onSelect = {[weak self] id in self?.canvas.selectedEdge=nil;self?.manualConnection=false;self?.selectedID=id;self?.inspect();self?.configureConnectionInspector()}
    canvas.onSelectEdge = {[weak self] i in self?.selectConnection(i)}
    canvas.onAmount = {[weak self] index,value in self?.changeCableAmount(index,value:value)}
    canvas.onEditEdge = {[weak self] i in self?.selectConnection(i);self?.focusConnection()}
    canvas.onDelete = {[weak self] in guard let self else{return};if self.canvas.selectedEdge != nil{self.disconnect()}else if let id=self.canvas.selected{self.selectedID=id;self.removeNode()}}
    canvas.onZoom = {[weak self] factor in self?.zoom(factor)}
    canvas.onConnectPorts = {[weak self] a,b,out,input,mod in self?.connectPorts(a,b,out:out,input:input,modulation:mod)}
    canvas.onRewire = {[weak self] index,a,b,out,input,mod in self?.rewire(index,source:a,target:b,out:out,input:input,modulation:mod)}
    canvas.onCableHint = {[weak self] text in self?.status.stringValue=text}
    canvas.describeCable = {[weak self] a,b,out,input,mod in self?.cableDescription(a,b,out:out,input:input,modulation:mod) ?? "Release to connect"}
    canvas.onMoveNodes = {[weak self] positions in self?.moveNodes(positions)}
    canvas.onInsertNodes = {[weak self] ids,edge,positions in self?.insertNodes(ids,edge:edge,positions:positions)}
    canvas.onDetachNodes = {[weak self] ids,positions,remove in self?.detachNodes(ids,positions:positions,remove:remove)}
    canvas.onCutEdges = {[weak self] indices in self?.cutConnections(indices)}
    canvas.insertionHint = {[weak self] ids,edge in self?.insertionDescription(ids,edge:edge)}
    canvas.onConnect = {[weak self] a,b in self?.connect(a,b)}
    canvas.onOpen = {[weak self] id in self?.openNode(id)}
    canvas.onAdd = {[weak self] point in self?.showAdd(at:point)}
    canvas.onAddConnected = {[weak self] node,port,output,point in self?.showAdd(at:point,connecting:GraphAddConnection(node:node,port:port,output:output))}
    canvas.onParent = {[weak self] in self?.navigateParent()}
    canvas.onGroup = {[weak self] unpack in if unpack{self?.ungroupSelection()}else{self?.groupSelection()}}
    canvas.onFit = {[weak self] in self?.fit()}
    canvas.onFrame = {[weak self] in self?.frameSelection()}
    pluginControls.onRequest = {[weak self] method,p,reply in guard let request=self?.onRequest else{reply(["error":["message":"No song open"]]);return};request(method,p,reply)}
    rackControls.onRequest=pluginControls.onRequest
    busControls.onRequest=pluginControls.onRequest
    signalScope.onRequest=pluginControls.onRequest
    signalScope.currentRevision={[weak self] in self?.revision ?? ""}
    signalScope.nameForPort={[weak self] key in self?.signalReadings.ports.first{$0.key==key}?.name ?? key}
    listenControls.onRequest=pluginControls.onRequest
    listenControls.currentRevision={[weak self] in self?.revision ?? ""}
    listenControls.nameForPort=signalScope.nameForPort
    listenControls.onError={[weak self] message in self?.status.stringValue=message}
    canvas.onListen={[weak self] key in guard let self else{return};self.listenControls.select(key==self.listenControls.port ? nil:key)}
    canvas.onChooseListen={[weak self] in self?.listenSelected()}
    canvas.onBypass={[weak self] in self?.toggleSelectedBypass()}
    canvas.onStopListening={[weak self] in self?.listenControls.select(nil)}
    canvas.onScope={[weak self] port,spectrum in self?.signalScope.show(port:port,spectrum:spectrum)}
    canvas.observedEdgePort={[weak self] index in self?.observedCablePort(index)}
    busControls.onChanged={[weak self] in self?.load()}
    rackControls.currentRevision={[weak self] in self?.revision ?? ""}
    rackControls.onOpen={[weak self] id in self?.onPlugin?(id)}
    rackControls.onChanged={[weak self] in self?.load()}
    rackControls.onExpose={[weak self] id,parameter in guard let self else{return};self.exposedParameters["plugin:\(id)"]=parameter;self.rebuild()}
    pluginControls.currentRevision = {[weak self] in self?.revision ?? ""}
    pluginControls.onChanged = {[weak self] in self?.load()}
    pluginControls.onCatalog = {[weak self] graph,node,catalog in guard let self,self.graphID==graph else{return};self.portCatalogs[node]=catalog;self.rebuild()}
    pluginControls.onParameter = {[weak self] id in guard let self else{return};if let node=self.selectedID{self.exposedParameters[node]=id};self.parameter.stringValue=String(id);self.rebuild();self.connectionKind.selectItem(withTitle:"Modulation");self.connectionModeChanged();if let selected=self.selectedID,let i=self.destination.itemArray.firstIndex(where:{$0.representedObject as? String==selected}){self.destination.selectItem(at:i)}}
    envelopeEditor.isHidden=true
    envelopeEditor.onRequest = {[weak self] method,p,reply in self?.onRequest?(method,p,reply)}
    envelopeEditor.onChanged = {[weak self] in self?.load()}
    libraryNumber.fixed(width:50);assignAmount.fixed(width:65);assignWet.fixed(width:65)
    for field in [libraryName,libraryNumber,assignAmount,assignWet,name,outputPort,inputPort,parameter,minimum,maximum,base,rate,phase,attack,release,controller]{field.delegate=self;field.target=self;field.action=#selector(commitField(_:))}
    assignment.target=self;assignment.action=#selector(commitAssignment)
    libraryName.setAccessibilityLabel("Subgraph name");libraryNumber.setAccessibilityLabel("Subgraph number");assignAmount.setAccessibilityLabel("Ordinary graph Amount");assignWet.setAccessibilityLabel("Ordinary graph wet mix")
    librarySection=stack(.vertical,[Theme.label("LIBRARY DEFINITION",size:10,color:Theme.muted),stack(.horizontal,[libraryNumber,libraryName]),stack(.horizontal,[ActionButton("Delete unused"){[weak self] in if let id=self?.graphID{self?.mutate("graph.remove",["graph":id])}}])],spacing:5)
    busSection=stack(.vertical,[assignmentHeading,assignment,stack(.horizontal,[Theme.label("Amount / Wet",size:11),assignAmount,assignWet]),stack(.horizontal,[ActionButton("Mixer controls"){[weak self] in guard let self,let id=self.selectedID else{return};self.onBus?(self.songNodeBus[id] ?? id)}]),ActionButton("Pattern graph commands…"){[weak self] in guard let self else{return};self.onPatternCommands?(self.selectedID.flatMap{self.songNodeBus[$0]})}],spacing:5)
    sourceSection=stack(.vertical,[Theme.label("MODULATION SOURCE",size:10,color:Theme.muted),stack(.horizontal,[Theme.label("Rate / phase",size:11),rate,phase]),stack(.horizontal,[Theme.label("Attack / release",size:11),attack,release]),stack(.horizontal,[Theme.label("MIDI CC",size:11),controller])],spacing:5)
    connectionGain.fixed(width:60);connectionGain.setAccessibilityLabel("Connection gain");connectionGain.delegate=self;connectionGain.target=self;connectionGain.action=#selector(commitField(_:));connectionEnabled.state = .on
    connection.target=self;connection.action = #selector(connectionChosen)
    portRow.orientation = .vertical;portRow.spacing=5;portRow.stretchAcrossAxis()
    outputChoice.setAccessibilityLabel("Source audio output");inputChoice.setAccessibilityLabel("Destination audio input")
    for popup in [outputChoice,inputChoice]{popup.target=self;popup.action = #selector(portChoiceChanged(_:));popup.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
    for view in [outputChoice,inputChoice] {portRow.addArrangedSubview(view)}
    audioPorts=stack(.vertical,[portRow,stack(.horizontal,[gainLabel,connectionGain])],spacing:5)
    modulationSection=stack(.vertical,[stack(.horizontal,[Theme.label("Parameter ID",size:11),parameter]),stack(.horizontal,[Theme.label("Range",size:11),minimum,maximum]),stack(.horizontal,[Theme.label("Base",size:11),base,connectionEnabled])],spacing:5)
    connectionKind.target=self;connectionKind.action = #selector(connectionControlChanged(_:))
    for popup in [source,destination] {popup.target=self;popup.action = #selector(connectionControlChanged(_:))}
    connectionEnabled.target=self;connectionEnabled.action = #selector(connectionControlChanged(_:))
    connectionHint.lineBreakMode = .byWordWrapping;connectionHint.maximumNumberOfLines=0;connectionHint.preferredMaxLayoutWidth=246
    connectionHint.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    connectionForm=stack(.vertical,[connectionKind,stack(.horizontal,[Theme.label("From",size:11),source]),stack(.horizontal,[Theme.label("To",size:11),destination]),audioPorts,modulationSection],spacing:7)
    connectionForm.stretchAcrossAxis()
    connectionSection=stack(.vertical,[stack(.horizontal,[connectionHeading,NSView(),ActionButton("New…"){[weak self] in self?.newConnection()}]),stack(.horizontal,[connectButton,updateConnectionButton,removeConnectionButton]),connection,connectionHint,openConnectionOwnerButton,connectionForm],spacing:7)
    connectionSection.stretchAcrossAxis()
    nodeSection=stack(.vertical,[detail,pluginControls,rackControls,nodeList,name,
      stack(.horizontal,[ActionButton("Remove node"){[weak self] in self?.removeNode()},ActionButton("Open"){[weak self] in guard let self,let id=self.selectedID else{return};self.openNode(id)}]),
      useDetector,busControls,busSection,sourceSection],spacing:7)
    nodeSection.stretchAcrossAxis()
    let help=Theme.label("Sockets add cables; wire handles reroute. Shift/⌘-click or drag empty space to select nodes. Drop selected effects on a highlighted wire to insert. Hollow sockets enable automatically. Option-drag adds a Main input to an existing insert.",size:11,color:Theme.muted)
    help.lineBreakMode = .byWordWrapping;help.maximumNumberOfLines=0;help.preferredMaxLayoutWidth=246
    let controls=stack(.vertical,[connectionSection,librarySection,nodeSection,help],spacing:12)
    controls.stretchAcrossAxis();controls.fill(inspector,inset:8)
    inspectorScroll.documentView=inspector;inspectorScroll.fixed(width:272)
    inspector.translatesAutoresizingMaskIntoConstraints=false
    NSLayoutConstraint.activate([inspector.leadingAnchor.constraint(equalTo:inspectorScroll.contentView.leadingAnchor),inspector.topAnchor.constraint(equalTo:inspectorScroll.contentView.topAnchor),inspector.widthAnchor.constraint(equalTo:inspectorScroll.contentView.widthAnchor)])
    let drawing=stack(.vertical,[scroll,signalScope,envelopeEditor],spacing:2);drawing.stretchAcrossAxis()
    let body=stack(.horizontal,[drawing,inspectorScroll],spacing:2);body.stretchAcrossAxis()
    let more = ActionMenuButton { [weak self] in self?.actionMenu() ?? NSMenu() }
    breadcrumbs.orientation = .horizontal;breadcrumbs.spacing=3
    let toolbar=stack(.horizontal,[breadcrumbs,NSView(),filter,ActionButton("Add…", prominent:true){[weak self] in self?.showAdd()},more],spacing:5)
    let add = NSView(); add.isHidden = true
    instrumentPicker.setAccessibilityLabel("Sample instrument graph target")
    nodeSearch.placeholderString="Filter nodes…";nodeSearch.setAccessibilityLabel("Filter graph nodes");nodeSearch.delegate=self;nodeSearch.sendsSearchStringImmediately=true;nodeSearch.sendsWholeSearchString=false;nodeSearch.fixed(width:200)
    nodeCategory.addItems(withTitles:["All node types","Channels & buses","Plugins & subgraphs","Modulation sources"]);nodeCategory.setAccessibilityLabel("Graph node type filter");nodeCategory.target=self;nodeCategory.action = #selector(changeNodeFilter)
    let filters=stack(.horizontal,[nodeSearch,nodeCategory,ActionButton("Clear filters"){[weak self] in self?.clearNodeFilters()},filterCount,NSView()],spacing:5)
    routingStatus.isHidden=true;routingStatus.setAccessibilityLabel("Routing playback state")
    let content=stack(.vertical,[toolbar,scopeLabel,listenControls,add,filters,body,routingStatus,status],spacing:5);content.stretchAcrossAxis();content.fill(self,inset:6)
    for popup in [library,filter,source,destination,nodeList,assignment,connection]{popup.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
  }
  func toggleSelectedBypass() {
    withSongPlugin(title:"Toggle plugin bypass") {[weak self] identity in
      guard let self,let plugin=self.rackPlugins.first(where:{$0["id"] as? String==identity}) else{return}
      self.mutate("plugin.bypass",["plugin":identity,"bypass":!(plugin["bypass"] as? Bool ?? false)])
    }
  }
  func actionMenu() -> NSMenu {
    let menu = NSMenu(title: "Graph"); menu.autoenablesItems = false
    menu.addItem(GraphCommand.add.item("Add node…",key:"a",modifiers:.shift){[weak self] in self?.showAdd()})
    menu.addItem(GraphCommand.parent.item("Parent graph",key:"\u{f700}",modifiers:.option,reason:graphID == nil && processingGroupID==nil ? "Already at the song graph":nil){[weak self] in self?.navigateParent()})
    menu.addItem(GraphCommand.fit.item("Fit graph",key:"\u{f729}"){[weak self] in self?.fit()})
    menu.addItem(GraphCommand.traceSilence.item("Trace silence"){[weak self] in self?.traceSilence()})
    menu.addItem(GraphCommand.findOverload.item("Find next overload"){[weak self] in self?.findOverload()})
    menu.addItem(GraphCommand.clearOverloads.item("Clear overload indicators"){[weak self] in self?.clearOverloads()})
    menu.addItem(GraphCommand.scope.item("Scope selected signal",key:"q"){[weak self] in self?.openScope(spectrum:false)})
    menu.addItem(GraphCommand.spectrum.item("Spectrum of selected signal",key:"q",modifiers:.shift){[weak self] in self?.openScope(spectrum:true)})
    menu.addItem(GraphCommand.openPlugin.item("Open plugin interface",key:"\r"){[weak self] in self?.openSelectedPlugin()})
    menu.addItem(GraphCommand.bypass.item("Toggle plugin bypass",key:"m"){[weak self] in self?.toggleSelectedBypass()})
    menu.addItem(GraphCommand.listen.item("Listen here",key:"l"){[weak self] in self?.listenSelected()})
    menu.addItem(GraphCommand.stopListening.item("Stop listening"){[weak self] in self?.listenControls.select(nil)})
    menu.addItem(GraphCommand.frameSelection.item("Frame selection",key:"f"){[weak self] in self?.frameSelection()})
    menu.addItem(GraphCommand.showPattern.item("Show in pattern",reason:graphID == nil && selectedID.flatMap{songNodeBus[$0]} == nil ? "Select a channel or enter a group to locate its pattern":nil){[weak self] in guard let self else{return};self.onShowPattern?(self.selectedID.flatMap{self.songNodeBus[$0]} ?? self.graphTarget ?? "")})
    let definitionsMenu=NSMenu(title:"Reusable groups")
    for d in definitions { if let id=d["id"] as? String { definitionsMenu.addItem(ContextAction("\(d["number"] ?? 0) · \(d["name"] ?? "Group")",id:"graph.openGroup/\(revision.split(separator:":").first ?? "")/\(id)"){[weak self] in self?.navigate(graph:id)}) } }
    ContextActions.appendMenu(definitionsMenu,to:menu)
    menu.addItem(GraphCommand.newGroup.item("New subgraph…") { [weak self] in self?.mutate("graph.create", ["name":"New subgraph"]) })
    menu.addItem(GraphCommand.groupSelection.item("Group selection",key:"g",modifiers:.control,reason:canvas.selection.isEmpty ? "Select processors to package":nil){[weak self] in self?.groupSelection()})
    menu.addItem(GraphCommand.ungroup.item("Ungroup",key:"g",modifiers:[.control,.option],reason:selectedProcessingGroup==nil ? "Select a processing group to unpack":nil){[weak self] in self?.ungroupSelection()})
    menu.addItem(GraphCommand.exportGroup.item("Save to subgraph library…",reason:selectedProcessingGroup==nil ? "Select a processing group to save an independent copy":nil){[weak self] in self?.exportProcessingGroup()})
    let clone=GraphCommand.cloneGroup.item("Clone subgraph"){[weak self] in self?.withProcessingGroup(title:"Clone processing group",createThenAct:false){[weak self] id in self?.mutate("graph.clone",["graph":id])}}
    clone.toolTip="Choose the shared definition to make an independent copy";menu.addItem(clone)
    let sources=NSMenu(title:"Add modulation source");sources.autoenablesItems=false
    for (label,kind,command) in [("Pattern envelope","automation",GraphCommand.sourceAutomation),("LFO","lfo",.sourceLFO),("Audio follower","follower",.sourceFollower),("Random","random",.sourceRandom),("Note envelope","note-envelope",.sourceNote),("MIDI controller","midi",.sourceMIDI),("Amount","amount",.sourceAmount)] {
      let item=command.item(label){[weak self] in self?.withProcessingGroup(title:"Add \(label.lowercased()) to group"){[weak self] _ in self?.nodeKind.selectItem(withTitle:kind);self?.addSource()}}
      item.toolTip="Choose a processing group when none is open";sources.addItem(item)
    }
    ContextActions.appendMenu(sources,to:menu)
    menu.addItem(GraphCommand.patch.item("Patch by keyboard…") { [weak self] in self?.newConnection() })
    menu.addItem(GraphCommand.cut.item("Cut cables…"){[weak self] in guard let self else{return};self.canvas.cutTool=true;self.window?.makeFirstResponder(self.canvas);self.status.stringValue="Drag across cables to disconnect · Esc cancels"})
    menu.addItem(GraphCommand.detach.item("Detach and reconnect"){[weak self] in guard let self else{return};self.detachNodes(Array(self.canvas.selection),positions:[],remove:false)})
    menu.addItem(GraphCommand.deleteHeal.item("Delete and reconnect",key:"x",modifiers:.control){[weak self] in guard let self else{return};self.detachNodes(Array(self.canvas.selection),positions:[],remove:true)})
    menu.addItem(GraphCommand.arrange.item("Arrange nodes") { [weak self] in self?.arrange() })
    menu.addItem(GraphCommand.zoomIn.item("Zoom in") { [weak self] in self?.zoom(1.2) })
    menu.addItem(GraphCommand.zoomOut.item("Zoom out") { [weak self] in self?.zoom(1/1.2) })
    menu.addItem(GraphCommand.reload.item("Reload graph") { [weak self] in self?.portCatalogs=[:]; self?.catalogFailures=[:]; self?.load() })
    let instruments = NSMenu(title: "Instrument graphs"); instruments.autoenablesItems = false
    for instrument in sampleInstruments {
      guard let id=instrument["id"] as? String else { continue }
      instruments.addItem(ContextAction("I\(instrument["index"] ?? 0) · \(instrument["name"] ?? "Instrument")",id:"graph.instrument/\(revision.split(separator:":").first ?? "")/\(id)") { [weak self] in
        guard let self, let index=self.instrumentPicker.itemArray.firstIndex(where: { $0.representedObject as? String == id }) else { return }
        self.instrumentPicker.selectItem(at:index); self.inspectSampleInstrument()
      })
    }
    ContextActions.appendMenu(instruments, to:menu); return menu
  }
  @objc private func commitAssignment() { hasDraft=true; assign() }
  @objc private func commitField(_ field: NSTextField) {
    guard hasDraft, !loading else { return }
    if field === name { rename() }
    else if field === libraryName || field === libraryNumber { renameLibrary() }
    else if field === assignAmount || field === assignWet { assign() }
    else if [rate, phase, attack, release, controller].contains(where: { $0 === field }) { sourceSettings() }
    else if canvas.selectedEdge != nil { updateConnection() }
  }
  func controlTextDidEndEditing(_ notification: Notification) { if let field=notification.object as? NSTextField { commitField(field) } }
  required init?(coder:NSCoder){fatalError()}
  func openNode(_ id:String) {
    if processingGroups.contains(where:{$0["id"] as? String==id}){navigateProcessingGroup(id);return}
    if graphID == nil { openSongNode(id); return }
    canvas.selectedEdge=nil;selectedID=id;canvas.selected=id;inspect();configureConnectionInspector()
    if selectedNode?["kind"] as? String == "plugin" {
      if (selectedNode?["plugin"] as? [String:Any])?["format"] as? String == "Built-in" {
        pluginControls.load();pluginControls.scrollToVisible(pluginControls.bounds);window?.makeFirstResponder(pluginControls.parametersView.search)
      } else {pluginControls.openEditor()}
    }
    else { name.scrollToVisible(name.bounds); window?.makeFirstResponder(name) }
  }
  func load(completion:(()->Void)?=nil){
    guard !loading,onRequest != nil else{return}
    loading=true
    requestGraph("graph.get",["includeState":false,"includeImplicitMixer":true],document:projectionDocument){[weak self] response in
      guard let self else{return};self.loading=false
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any] else{
        self.deferredGraphCommand=nil;self.error(response);return
      }
      self.revision=result["revision"] as? String ?? ""
      let document=self.revision.split(separator:":").first.map(String.init) ?? ""
      var previous=self.data,next=data;previous.removeValue(forKey:"activity");next.removeValue(forKey:"activity")
      if self.projectionDocument==document,self.renderedContext==self.viewContext,
        NSDictionary(dictionary:previous).isEqual(to:next) {
        // A pattern edit advances the shared revision but need not reconstruct
        // every graph card, popup and breadcrumb. Inspector writes must still
        // use the current revision, and live activity arrives independently.
        self.data=data;self.inspect()
      }else{self.update(data)}
      completion?()
      let command=self.deferredGraphCommand;self.deferredGraphCommand=nil;command?();self.loadPortCatalogs()
    }
  }
  func update(_ value:[String:Any]){
    var selectedEdge=canvas.selectedEdge.flatMap{canvas.edges.indices.contains($0) ? canvas.edges[$0] : nil}
    let document=revision.split(separator:":").first.map(String.init) ?? ""
    if projectionDocument != document {
      if !projectionDocument.isEmpty {
        // IDs are only unique within a song. Reusing a selection or a channel
        // filter in another document can silently target an unrelated object.
        graphID=nil;filterID=nil;selectedID=nil;graphOrigin=nil;graphTarget=nil
        selectedEdge=nil;canvas.cancelGesture();canvas.selected=nil
        nodeSearch.stringValue="";nodeCategory.selectItem(at:0)
        source.removeAllItems();destination.removeAllItems();connection.removeAllItems()
        instrumentPicker.removeAllItems();manualConnection=false;pendingModulationFocus=nil;deferredGraphCommand=nil
        addGeneration+=1;addMenu.close();targetMenu.close()
        portCatalogs=[:];catalogFailures=[:];catalogGeneration+=1;exposedParameters=[:];catalogLoading=false
        envelopeEditor.resetDocument()
        playbackActivity=[];playbackRunning=false;showSignals([:]);lastOverload=nil
      }
      provisionalLayouts=[:];graphViewStates=[:];processingGroupID=nil;boundaryPorts=[:];projectionDocument=document
    }
    data=value;hasDraft=false;canvas.selectedEdge=nil
    if graphID != nil && definition==nil{graphID=nil}
    if processingGroupID != nil && !processingGroups.contains(where:{$0["id"] as? String==processingGroupID}){processingGroupID=nil}
    picker(library,[("Song graph","")]+definitions.map{("\($0["number"] as? Int ?? 0) · \($0["name"] as? String ?? "Subgraph")",$0["id"] as? String ?? "")},select:graphID)
    picker(filter,[("All channels","")]+buses.map{(busLabel($0),$0["id"] as? String ?? "")},select:filterID)
    picker(assignment,[("Dry","")]+definitions.map{($0["name"] as? String ?? "Subgraph",$0["id"] as? String ?? "")},select:nil)
    picker(instrumentPicker,sampleInstruments.map{("I\($0["index"] ?? 0) · \($0["name"] as? String ?? "Instrument")",$0["id"] as? String ?? "")},select:chosen(instrumentPicker))
    rebuild()
    // Restore the selected wire before changing inspector visibility. Hiding
    // its section even briefly tears down AppKit's active field editor during
    // an otherwise harmless graph refresh.
    if let requested=pendingModulationFocus {pendingModulationFocus=nil;let count=(definition?["audio"] as? [[String:Any]] ?? []).count;if let index=definitionEdgeIndices.firstIndex(of:count+requested){selectConnection(index);focusConnectionValue(maximum);return}}
    if let selectedEdge,let index=canvas.edges.firstIndex(where:{$0.source==selectedEdge.source && $0.target==selectedEdge.target && $0.modulation==selectedEdge.modulation && $0.output==selectedEdge.output && $0.input==selectedEdge.input}) {selectConnection(index)}
    else {inspect();configureConnectionInspector()}
  }
  func picker(_ picker:NSPopUpButton,_ values:[(String,String)],select:String?){picker.removeAllItems();for (title,id) in values{let item=NSMenuItem(title:title,action:nil,keyEquivalent:"");item.representedObject=id;picker.menu?.addItem(item)};if let index=values.firstIndex(where:{$0.1==(select ?? "")}){picker.selectItem(at:index)}}
  func chosen(_ picker:NSPopUpButton)->String?{guard let id=picker.selectedItem?.representedObject as? String,!id.isEmpty else{return nil};return id}
  @objc func changeLibrary(){navigate(graph:chosen(library))}
  @objc func changeFilter(){guard !hasDraft else{picker(filter,[("All channels","")]+buses.map{(busLabel($0),$0["id"] as? String ?? "")},select:filterID);status.stringValue="Finish the current edit before changing channel focus";return};filterID=chosen(filter);canvas.selectedEdge=nil;rebuild();inspect();configureConnectionInspector()}
  @objc func changeNode(){selectedID=chosen(nodeList);canvas.selected=selectedID;canvas.selectedEdge=nil;inspect();configureConnectionInspector()}
  func showBus(_ id:String,filter:Bool=false){graphID=nil;selectedID=id;if filter{filterID=id};update(data)}
  func rebuild(){
    let previousSource=chosen(source),previousTarget=chosen(destination),previousConnection=chosen(connection)
    enableRouting.isHidden = !buses.isEmpty
    canvas.emptyMessage = buses.isEmpty && graphID==nil ? "Open or create a song to connect channels, instruments and effects." : "Create a subgraph to start connecting sound."
    let scope=graphID != nil
    if configuredScope != scope {configuredScope=scope;connectionGain.doubleValue=scope ? 1 : 0;connectionKind.removeAllItems();connectionKind.addItems(withTitles:scope ? ["Audio","Modulation"] : ["Main output","Send","Graph sidechain","Graph auxiliary","Plugin sidechain","Plugin auxiliary","Mix into main"])}
    connectionModeChanged()

    var display=[SignalCanvasNode](),edges=[SignalCanvasEdge]()
    if let definition {
      for node in nodes{display.append(canvasNode(node,definition:definition))}
      for e in definition["audio"] as? [[String:Any]] ?? []{edges.append(SignalCanvasEdge(source:e["source"] as? String ?? "",target:e["target"] as? String ?? "",label:"\(e["output"] as? Int ?? 0) → \(e["input"] as? Int ?? 0) · ×\(e["gain"] ?? 1)",output:(e["output"] as? NSNumber)?.uint32Value ?? 0,input:(e["input"] as? NSNumber)?.uint32Value ?? 0,amount:e["gain"] as? Double ?? 1,amountRange:-16...16,amountUnit:"×"))}
      for e in definition["modulation"] as? [[String:Any]] ?? []{edges.append(SignalCanvasEdge(source:e["source"] as? String ?? "",target:e["target"] as? String ?? "",label:"Param \(e["parameter"] as? Int ?? 0)",modulation:true,input:(e["parameter"] as? NSNumber)?.uint32Value ?? 0,enabled:e["enabled"] as? Bool ?? true,amount:e["maximum"] as? Double ?? 1,amountRange:-1...1,amountUnit:"depth"))}
    }else{
      (display,edges)=buildSongOverview()
    }
    ungroupedNodes=display;ungroupedEdges=edges
    (display,edges)=projectProcessingGroups(display,edges:edges)
    if graphID==nil {songConnections=definitionEdgeIndices.map{songConnections[$0]}}
    signalReadings.aliases=boundaryPorts
    canvas.signalReadings=graphID==nil ? signalReadings:GraphSignalReadings()
    (display,edges)=filterNodes(display,edges:edges)
    canvas.update(display,edges:edges);canvas.selected=selectedID
    showActivity(playbackActivity,playing:playbackRunning)
    let list=display.map{($0.title,$0.id)};picker(nodeList,list,select:selectedID);picker(source,list,select:previousSource ?? selectedID);picker(destination,list,select:previousTarget)
    var connections=[(String,String)]();for (i,e) in edges.enumerated(){let a=display.first{$0.id==e.source}?.title ?? "?",b=display.first{$0.id==e.target}?.title ?? "?";connections.append(("\(a) → \(b) \(e.label)",String(i)))};picker(connection,connections,select:previousConnection)
    refreshPortChoices()
    status.stringValue=graphID==nil ? "Configured routes · live stack order appears on playing copies" : "Each channel gets its own copy · drag nodes and ports to edit"
    refreshBreadcrumbs()
    renderedContext=viewContext
  }
  func inspect(){
    let object="\(projectionDocument)/\(graphID ?? "song")/\(selectedID ?? "")/\(canvas.selectedEdge.map(String.init) ?? "")"
    let changedObject=object != inspectedObject;inspectedObject=object
    defer {if changedObject{layoutSubtreeIfNeeded();inspectorScroll.contentView.scroll(to:.zero);inspectorScroll.reflectScrolledClipView(inspectorScroll.contentView)}}
    nodeSection.isHidden=canvas.selectedEdge != nil || selectedID == nil
    useDetector.isHidden=selectedBuiltinDetector==nil
    envelopeEditor.context(graph:graphID,node:selectedNode,patterns:data["patterns"] as? [[String:Any]] ?? [],revision:revision)
    let busID=selectedID.flatMap{songNodeBus[$0]}
    let rackPlugin=graphID==nil ? selectedID.flatMap{songNodePlugin[$0]}.flatMap{id in rackPlugins.first{$0["id"] as? String==id}}:nil
    let selectedBus=graphID==nil && rackPlugin==nil && busID==selectedID ? buses.first{$0["id"] as? String==busID}:nil
    busControls.isHidden=selectedBus==nil;busControls.implicit=data["implicitMixer"] as? Bool==true;busControls.context(selectedBus,revision:revision)
    rackControls.isHidden=rackPlugin==nil;rackControls.context(rackPlugin,revision:revision)
    let copyGraph=selectedID.flatMap{songNodeGraph[$0]},ordinaryCopy=selectedID?.contains(":Ordinary:")==true
    name.isHidden=rackPlugin != nil || (graphID==nil && (copyGraph != nil || selectedInstrument != nil))
    librarySection.isHidden=graphID==nil || canvas.selectedEdge != nil || selectedID != nil
    busSection.isHidden=graphID != nil || rackPlugin != nil || (selectedBus==nil && selectedInstrument==nil && !ordinaryCopy)
    assignmentHeading.stringValue=selectedInstrument==nil ? "ORDINARY CHANNEL GRAPH" : "INSTRUMENT GRAPH · ALL CHANNELS"
    let kind=selectedNode?["kind"] as? String ?? ""
    sourceSection.isHidden=graphID==nil || !["lfo","follower","random","note-envelope","midi","amount"].contains(kind)
    pluginControls.isHidden=kind != "plugin";pluginControls.context(graph:graphID,node:kind=="plugin" ? selectedID : nil,revision:revision)
    libraryName.stringValue=definition?["name"] as? String ?? "";libraryNumber.integerValue=definition?["number"] as? Int ?? 1

    if let rackPlugin{detail.stringValue=rackPlugin["name"] as? String ?? "Processor"}
    else if graphID==nil,let instrument=selectedInstrument{
      let channel=busID.flatMap{id in buses.first{$0["id"] as? String==id}?["name"] as? String}
      detail.stringValue="I\(instrument["index"] ?? 0) · \(instrument["name"] as? String ?? "Instrument")"+(channel.map{" › \($0)"} ?? " · before channel");name.stringValue=instrument["name"] as? String ?? "Instrument"
      let entry=(data["instrumentAssignments"] as? [[String:Any]] ?? []).first{$0["target"] as? String==instrument["id"] as? String}
      assignAmount.doubleValue=entry?["amount"] as? Double ?? 1;assignWet.doubleValue=entry?["wet"] as? Double ?? 1
      for (i,item) in assignment.itemArray.enumerated() where item.representedObject as? String==(entry?["graph"] as? String ?? ""){assignment.selectItem(at:i)}
    }else if graphID==nil,let bus=buses.first(where:{$0["id"] as? String==busID}){detail.stringValue=bus["name"] as? String ?? "Bus";name.stringValue=detail.stringValue
      if copyGraph != nil,let display=canvas.nodes.first(where:{$0.id==selectedID}) {detail.stringValue += " › \(display.role ?? "Graph") · \(display.title)"}
      let entry=(data["assignments"] as? [[String:Any]] ?? []).first{$0["target"] as? String==busID}
      assignAmount.doubleValue=entry?["amount"] as? Double ?? 1;assignWet.doubleValue=entry?["wet"] as? Double ?? 1
      let assigned=entry?["graph"] as? String
      for (i,item) in assignment.itemArray.enumerated() where item.representedObject as? String==(assigned ?? ""){assignment.selectItem(at:i)}
    }else if let group=selectedProcessingGroup{detail.stringValue="Processing group · double-click to enter";name.stringValue=group["name"] as? String ?? "Group"}
    else if let node=selectedNode{detail.stringValue=node["name"] as? String ?? node["kind"] as? String ?? "Node";name.stringValue=node["name"] as? String ?? "";for (field,key) in [(rate,"rate"),(phase,"phase"),(attack,"attack"),(release,"release"),(controller,"controller")]{field.stringValue="\(node[key] ?? 0)"}}
    else if graphID==nil,let selectedID,let display=canvas.nodes.first(where:{$0.id==selectedID}){detail.stringValue=display.title;name.stringValue=display.title}
    else{detail.stringValue="Select a node";name.stringValue=""}
    if let index=nodeList.itemArray.firstIndex(where:{$0.representedObject as? String==selectedID}){nodeList.selectItem(at:index)}
  }
  func fit(){
    guard let first=canvas.nodes.first else{return}
    let content=canvas.nodes.dropFirst().reduce(first.rect){$0.union($1.rect)}.insetBy(dx:-20,dy:-20)
    frameCanvas(content,maximumScale:1)
  }
  func zoom(_ factor:Double){scroll.magnification=max(scroll.minMagnification,min(scroll.maxMagnification,scroll.magnification*factor))}
  private func error(_ response:[String:Any]){status.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Graph operation failed"}
  // -32002 is a definite rejection before dispatch, so retrying it cannot
  // duplicate a write. Keep the captured revision and target: conflicts and
  // uncertain transport outcomes must never be silently replayed or rebased.
  func requestGraph(_ method:String,_ params:[String:Any],document:String,attempt:Int=0,
                            completion:@escaping ([String:Any])->Void) {
    guard projectionDocument==document else{completion(["error":["code":-32001,"message":"The song changed; this graph edit was cancelled"]]);return}
    guard let onRequest else{completion(["error":["message":"The graph connection is unavailable"]]);return}
    onRequest(method,params){[weak self] response in
      guard let self else{return}
      if (response["error"] as? [String:Any])?["code"] as? Int == -32002,attempt<5 {
        DispatchQueue.main.asyncAfter(deadline:.now()+0.05*Double(attempt+1)){[weak self] in
          self?.requestGraph(method,params,document:document,attempt:attempt+1,completion:completion)
        }
      }else{completion(response)}
    }
  }
  func mutate(_ method:String,_ params:[String:Any],after:(([String:Any])->Void)?=nil){
    guard onRequest != nil else{return}
    var p=params
    if method=="graph.node.add",let processingGroupID{p["parent"]=processingGroupID}
    p["expectedRevision"]=revision
    startMutation(method,p,document:projectionDocument,context:viewContext,after:after)
  }
  private func startMutation(_ method:String,_ params:[String:Any],document:String,context:[String],attempt:Int=0,
                             after:(([String:Any])->Void)?) {
    guard projectionDocument==document else{status.stringValue="The song changed; this graph edit was cancelled";return}
    if loading {
      guard attempt<10 else{status.stringValue="The graph is still busy; retry this edit shortly";return}
      DispatchQueue.main.asyncAfter(deadline:.now()+0.05*Double(attempt+1)){[weak self] in
        self?.startMutation(method,params,document:document,context:context,attempt:attempt+1,after:after)
      }
      return
    }
    loading=true
    requestGraph(method,params,document:document){[weak self] response in
      guard let self else{return};self.loading=false
      guard let result=response["result"] as? [String:Any]else{
        self.deferredGraphCommand=nil;self.rebuild();self.error(response);return
      }
      let sameContext=self.projectionDocument==document && self.viewContext==context
      if sameContext {
        if ["graph.create","graph.clone"].contains(method){self.rememberGraphView();self.graphID=(result["data"] as? [String:Any])?["graph"] as? String}
        if method=="graph.node.add"{self.selectedID=(result["data"] as? [String:Any])?["node"] as? String;self.canvas.selected=self.selectedID}
        // Grouping hides the old selection. Select the returned boundary before
        // projection/filtering runs, so that cleanup of hidden child IDs is not
        // mistaken for a user navigating away from the rename completion.
        if ["graph.group.create","graph.song.group.create"].contains(method){self.selectedID=(result["data"] as? [String:Any])?["group"] as? String;self.canvas.selected=self.selectedID}
        self.hasDraft=false
      }
      let completionContext=self.viewContext
      self.load{[weak self] in
        guard let self,sameContext,self.projectionDocument==document,self.viewContext==completionContext else{return}
        after?(result)
      }
    }
  }
  func updateDefinition(_ change:(inout [String:Any])->Void){guard var d=definition else{return};change(&d);pruneProcessingGroups(&d);enableDefinitionPorts(&d);mutate("graph.update",["definition":d])}
  private func move(_ id:String,x:Double,y:Double){guard !loading else{rebuild();return};guard graphID != nil else{mutate("graph.layout.set",["positions":[["node":id,"x":x,"y":y]]]);return};updateDefinition{d in var n=d["nodes"] as? [[String:Any]] ?? [];if let i=n.firstIndex(where:{$0["id"] as? String==id}){n[i]["x"]=x;n[i]["y"]=y};d["nodes"]=n}}
  private func rename(){guard let id=selectedID else{return};if selectedProcessingGroup != nil{var p:[String:Any]=["group":id,"name":name.stringValue];if let graphID{p["graph"]=graphID};mutate(graphID==nil ? "graph.song.group.update":"graph.group.update",p);return};if graphID==nil{guard songNodeBus[id]==id else{status.stringValue="Open this processor to edit its definition";return};mutate("mixer.bus.set",["bus":id,"name":name.stringValue])}else{updateDefinition{d in var n=d["nodes"] as? [[String:Any]] ?? [];if let i=n.firstIndex(where:{$0["id"] as? String==id}){n[i]["name"]=name.stringValue};d["nodes"]=n}}}
  private func removeNode(){
    if let graphID,let selectedID {
      if selectedProcessingGroup != nil,canvas.selection.count<=1{mutate("graph.group.remove",["graph":graphID,"group":selectedID,"deleteContents":true]);return}
      if canvas.selection.count<=1{mutate("graph.node.remove",["graph":graphID,"node":selectedID]);return}
      let ids=expandedProcessingSelection(canvas.selection);guard nodes.filter({ids.contains($0["id"] as? String ?? "")}).allSatisfy({!["input","output"].contains($0["kind"] as? String ?? "")})else{status.stringValue="Input and Output are permanent; select only processors or modulation sources";return}
      updateDefinition{d in d["nodes"]=(d["nodes"] as? [[String:Any]] ?? []).filter{!ids.contains($0["id"] as? String ?? "")};for key in ["audio","modulation"]{d[key]=(d[key] as? [[String:Any]] ?? []).filter{!ids.contains($0["source"] as? String ?? "") && !ids.contains($0["target"] as? String ?? "")}}}
    }
  }
  private func assign(){if graphID==nil,let instrument=selectedInstrument{assignSampleInstrument(instrument);return};guard graphID==nil,let selectedID,let bus=songNodeBus[selectedID],let amount=Double(assignAmount.stringValue),let wet=Double(assignWet.stringValue) else{return};mutate("graph.assign",["target":bus,"graph":chosen(assignment) as Any? ?? NSNull(),"amount":amount,"wet":wet])}
  private func renameLibrary(){guard let number=Int(libraryNumber.stringValue)else{return};updateDefinition{$0["name"]=libraryName.stringValue;$0["number"]=number}}
  @objc func connectionControlChanged(_ sender:NSControl){hasDraft=true;if sender===connectionKind {connectionModeChanged()}else if sender===source || sender===destination {refreshPortChoices();if canvas.selectedEdge==nil{inferConnectionKind()}};if canvas.selectedEdge != nil {updateConnection()}}
  @objc func connectionModeChanged(){let kind=connectionKind.titleOfSelectedItem ?? "",modulation=kind=="Modulation";modulationSection?.isHidden = !modulation;audioPorts?.isHidden=modulation
    gainLabel.stringValue=graphID == nil ? "Gain dB" : "Gain ×"
    connectionGain.toolTip=graphID == nil ? "Send / sidechain gain in dB" : "Audio gain multiplier, −16…16"
    outputPort.isEnabled=graphID != nil || kind=="Graph auxiliary" || kind=="Plugin auxiliary"
    inputPort.isEnabled=graphID != nil || kind=="Graph sidechain" || kind=="Plugin sidechain"
    connectionGain.isEnabled=graphID != nil || kind=="Send" || kind.contains("sidechain") || kind=="Mix into main"
    portRow.isHidden=kind=="Send"
    refreshPortChoices()
  }

  private func addSource(){guard let graphID else{status.stringValue="Create or select a subgraph first";return};mutate("graph.node.add",["graph":graphID,"kind":nodeKind.titleOfSelectedItem ?? "lfo","x":300,"y":220+nodes.count*12])}
  var onAddSongEffect: ((String?) -> Void)?
  private func choosePlugin(){guard let graphID else{onAddSongEffect?(selectedID.flatMap { songNodeBus[$0] } ?? filterID);return};onChoosePlugin?{[weak self] descriptor in guard let self else{return};var recipe=[String:Any]();for key in ["format","name","path","classID","type","subtype","manufacturer"]{recipe[key]=descriptor[key]};var params:[String:Any]=["graph":graphID,"kind":"plugin","plugin":recipe,"name":descriptor["name"] ?? "Effect","x":300,"y":100+self.nodes.count*12]
      let edges=self.definition?["audio"] as? [[String:Any]] ?? [],output=self.nodes.first{$0["kind"] as? String=="output"}?["id"] as? String
      let after=self.selectedNode.flatMap{["input","plugin"].contains($0["kind"] as? String ?? "") ? $0["id"] as? String : nil} ?? edges.last{$0["target"] as? String==output && ($0["input"] as? Int ?? 0)==0}?["source"] as? String
      if let after,edges.filter({$0["source"] as? String==after && ($0["output"] as? Int ?? 0)==0}).count==1{params["insertAfter"]=after}
      self.mutate("graph.node.add",params)}}
  private func sourceSettings(){guard let selectedID else{return};var values=[String:Any]();for (field,key) in [(rate,"rate"),(phase,"phase"),(attack,"attack"),(release,"release")]{guard let value=Double(field.stringValue),value.isFinite else{status.stringValue="Enter valid source values";return};values[key]=value};guard let cc=Int(controller.stringValue)else{return};values["controller"]=cc;updateDefinition{d in var n=d["nodes"] as? [[String:Any]] ?? [];if let i=n.firstIndex(where:{$0["id"] as? String==selectedID}){n[i].merge(values){_,new in new}};d["nodes"]=n}}
  func connectSelected(){guard canvas.selectedEdge==nil else{status.stringValue="Choose New… to add another connection";return};guard let a=chosen(source),let b=chosen(destination)else{return};connect(a,b)}
  func connect(_ a:String,_ b:String){
    if graphID==nil{connectSong(a,b);return}
    if connectionKind.indexOfSelectedItem==0 {
      guard let input=UInt32(inputPort.stringValue),let output=UInt32(outputPort.stringValue),let gain=Double(connectionGain.stringValue),gain.isFinite else{status.stringValue="Enter valid port numbers and gain";return}
      let from=realPort(a,output,output:true,modulation:false),to=realPort(b,input,output:false,modulation:false)
      updateDefinition{d in var edges=d["audio"] as? [[String:Any]] ?? [];edges.append(["source":from.node,"target":to.node,"input":to.number,"output":from.number,"gain":gain]);d["audio"]=edges}
    } else {
      guard let parameter=UInt32(parameter.stringValue),let lo=Double(minimum.stringValue),let hi=Double(maximum.stringValue),let base=Double(base.stringValue)else{return}
      let from=realPort(a,UInt32(outputPort.stringValue) ?? 0,output:true,modulation:true),to=realPort(b,parameter,output:false,modulation:true)
      updateDefinition{d in var edges=d["modulation"] as? [[String:Any]] ?? [];edges.append(["source":from.node,"target":to.node,"parameter":to.number,"minimum":lo,"maximum":hi,"base":base,"enabled":connectionEnabled.state == .on]);d["modulation"]=edges}
    }
  }
  func disconnect(){guard let visibleIndex=canvas.selectedEdge ?? chosen(connection).flatMap(Int.init),canvas.edges.indices.contains(visibleIndex)else{return};guard graphID != nil else{disconnectSong(visibleIndex);return};let index=definitionEdgeIndices[visibleIndex];updateDefinition{d in var audio=d["audio"] as? [[String:Any]] ?? [],mod=d["modulation"] as? [[String:Any]] ?? [];if index<audio.count{audio.remove(at:index)}else if mod.indices.contains(index-audio.count){mod.remove(at:index-audio.count)};d["audio"]=audio;d["modulation"]=mod}}
}
