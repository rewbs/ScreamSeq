import AppKit

final class GraphNoteRouteControls:NSView {
  let enabled=NSButton(checkboxWithTitle:"Forward notes",target:nil,action:nil),channel=NSPopUpButton(),activity=Theme.label("Event activity unavailable",size:11,color:Theme.muted)
  var onChange:((Bool,Int)->Void)?
  override init(frame:NSRect){super.init(frame:frame);channel.addItems(withTitles:["Preserve note channel"]+(1...16).map{"MIDI channel \($0)"});channel.target=self;channel.action=#selector(change);enabled.target=self;enabled.action=#selector(change)
    let help=Theme.label("Plugin-instrument notes only. A new destination waits for the next note-on. Removing or disabling a cable releases the notes owned by that route.",size:11,color:Theme.muted);help.lineBreakMode = .byWordWrapping;help.maximumNumberOfLines=0;help.preferredMaxLayoutWidth=240
    let contents=stack(.vertical,[enabled,channel,activity,help],spacing:6);contents.stretchAcrossAxis();contents.fill(self)
    channel.setAccessibilityLabel("Note route MIDI channel");activity.maximumNumberOfLines=0;activity.lineBreakMode = .byWordWrapping;activity.preferredMaxLayoutWidth=240
  }
  required init?(coder:NSCoder){fatalError()}
  func configure(enabled value:Bool,channel:Int){enabled.state=value ? .on:.off;self.channel.selectItem(at:max(0,min(16,channel)))}
  @objc private func change(){onChange?(enabled.state == .on,channel.indexOfSelectedItem)}
}

extension SignalGraphEditor {
  static let notePort:UInt32=UInt32.max-1
  var noteRouting:[String:Any]{data["noteRouting"] as? [String:Any] ?? [:]}
  var noteRoutes:[[String:Any]]{noteRouting["routes"] as? [[String:Any]] ?? []}
  var noteSourceInstruments:[[String:Any]] {(data["instruments"] as? [[String:Any]] ?? []).filter{instrument in instrument["plugin"] as? Bool==true || noteRoutes.contains{$0["sourceKind"] as? String=="instrument" && $0["source"] as? String==instrument["id"] as? String}}}
  func noteSource(_ key:String)->(kind:String,id:String)? {
    if key.hasPrefix("note-instrument:"),let value=noteSourceInstruments.first(where:{"note-instrument:"+($0["id"] as? String ?? "")==key}),let id=value["id"] as? String{return("instrument",id)}
    if let bus=buses.first(where:{$0["id"] as? String==key && $0["kind"] as? String=="track"}),let id=bus["id"] as? String{return("channel",id)}
    return nil
  }
  func noteTarget(_ key:String)->String? {guard let plugin=songNodePlugin[key],rackPlugins.contains(where:{$0["id"] as? String==plugin && $0["isInstrument"] as? Bool==true})else{return nil};return plugin}
  func appendNoteRouting(nodes:inout[SignalCanvasNode],edges:inout[SignalCanvasEdge]) {
    let saved=data["layout"] as? [[String:Any]] ?? [],suppressed=Set(noteRouting["suppressedAssignments"] as? [String] ?? [])
    var y=(nodes.map{$0.rect.maxY}.max() ?? 30)+60
    for instrument in noteSourceInstruments {
      guard let id=instrument["id"] as? String else{continue};let key="note-instrument:"+id,position=saved.first{$0["node"] as? String==key}
      var node=SignalCanvasNode(id:key,title:"I\(instrument["index"] ?? 0) · \(instrument["name"] as? String ?? "Instrument")",detail:"Plugin-instrument note source",kind:"events",x:position?["x"] as? Double ?? 30,y:position?["y"] as? Double ?? y)
      node.inputs=[];node.outputs=[SignalCanvasPort(number:Self.notePort,label:"Notes",signal:.events)];nodes.append(node);y+=110
    }
    for i in nodes.indices {
      if noteSource(nodes[i].id)?.kind=="channel"{nodes[i].outputs.append(.init(number:Self.notePort,label:"Notes",signal:.events))}
      if noteTarget(nodes[i].id) != nil{nodes[i].inputs.append(.init(number:Self.notePort,label:"Notes",signal:.events))}
    }
    func add(_ route:[String:Any],implicit:Bool) {
      guard let source=route["source"] as? String,let kind=route["sourceKind"] as? String,let plugin=route["plugin"] as? String else{return}
      let from=kind=="channel" ? source:"note-instrument:"+source,to="plugin:"+plugin
      guard nodes.contains(where:{$0.id==from}),nodes.contains(where:{$0.id==to})else{return}
      let channel=route["midiChannel"] as? Int ?? 0,enabled=route["enabled"] as? Bool ?? true
      edges.append(SignalCanvasEdge(source:from,target:to,label:(implicit ? "Assigned notes":"Notes")+(channel>0 ? " · MIDI \(channel)":""),output:Self.notePort,input:Self.notePort,enabled:enabled))
      edges[edges.count-1].connection=implicit ? "note-assignment:"+source:"note:"+(route["id"] as? String ?? "")
      var action=route;action["kind"]="note";action["implicit"]=implicit;songConnections.append(action)
    }
    for route in noteRoutes{add(route,implicit:false)}
    for plugin in rackPlugins where plugin["isInstrument"] as? Bool==true {
      guard let id=plugin["id"] as? String else{continue}
      for assignment in plugin["assignments"] as? [[String:Any]] ?? [] {
        guard let instrument=assignment["instrumentID"] as? String,!suppressed.contains(instrument)else{continue}
        add(["sourceKind":"instrument","source":instrument,"plugin":id,"midiChannel":assignment["channel"] ?? 1,"enabled":true],implicit:true)
      }
    }
  }
  func connectNotes(_ a:String,_ b:String,replacing:Int?=nil) {
    guard graphID==nil,let source=noteSource(a),let plugin=noteTarget(b)else{status.stringValue="Connect a channel or plugin-instrument Notes output to a plugin instrument’s Notes input";return}
    var p:[String:Any]=["sourceKind":source.kind,"source":source.id,"plugin":plugin]
    if let i=replacing,songConnections.indices.contains(i),songConnections[i]["kind"] as? String=="note" {
      let old=songConnections[i];p["midiChannel"]=old["midiChannel"] ?? 0;p["enabled"]=old["enabled"] ?? true
      if old["implicit"] as? Bool==true {
        guard source.kind=="instrument",source.id==old["source"] as? String else{status.stringValue="The assigned instrument owns this cable. Add a channel cable separately, or disconnect the assignment first.";return}
        p["suppressAssignment"]=true;mutate("graph.note.connect",p)
      }else if let id=old["id"] {p["id"]=id;mutate("graph.note.update",p)}
    }else{mutate("graph.note.connect",p)}
  }
  var selectedNoteRoute:[String:Any]? {guard graphID==nil,let i=canvas.selectedEdge,songConnections.indices.contains(i),songConnections[i]["kind"] as? String=="note" else{return nil};return songConnections[i]}
  func updateNoteRoute(enabled:Bool,channel:Int) {
    guard let route=selectedNoteRoute else{return}
    if route["implicit"] as? Bool==true {mutate("graph.note.connect",["sourceKind":"instrument","source":route["source"] ?? "","plugin":route["plugin"] ?? "","midiChannel":channel,"enabled":enabled,"suppressAssignment":true])}
    else if let id=route["id"]{mutate("graph.note.update",["id":id,"enabled":enabled,"midiChannel":channel])}
  }
  func disconnectNoteRoute(_ index:Int)->Bool {
    guard graphID==nil,songConnections.indices.contains(index),songConnections[index]["kind"] as? String=="note" else{return false}
    let route=songConnections[index]
    if route["implicit"] as? Bool==true{mutate("graph.note.disconnect",["instrument":route["source"] ?? ""])}
    else if let id=route["id"]{mutate("graph.note.disconnect",["id":id])}
    return true
  }
  func restoreNoteAssignment() {
    let suppressed=Set(noteRouting["suppressedAssignments"] as? [String] ?? [])
    if let source=(canvas.selected ?? selectedID).flatMap({noteSource($0)}),source.kind=="instrument",suppressed.contains(source.id){mutate("graph.note.restoreAssignment",["instrument":source.id]);return}
    chooseTarget(title:"Restore an instrument’s assigned plugin cable",entries:noteSourceInstruments.compactMap{instrument in guard let id=instrument["id"] as? String,suppressed.contains(id)else{return nil};return .init(id:id,title:"I\(instrument["index"] ?? 0) · \(instrument["name"] as? String ?? "Instrument")",detail:"Restore its implicit assignment; explicit routes remain",keywords:id)}){[weak self] id in self?.mutate("graph.note.restoreAssignment",["instrument":id])}
  }
}

struct GraphNoteActivitySnapshot {
  let text:String
  init(_ data:[String:Any],route:[String:Any]) {
    guard data["available"] as? Bool==true else{text="Event activity unavailable · no adopted note router";return}
    guard data["active"] as? Bool==true else{text="Stopped · event counters are retained snapshots";return}
    guard data["fresh"] as? Bool==true else{text="Waiting for a consistent adopted note-route snapshot";return}
    let implicit=route["implicit"] as? Bool==true
    let records=(data["routes"] as? [[String:Any]] ?? []).filter{record in
      guard record["implicit"] as? Bool==implicit,record["sourceKind"] as? String==route["sourceKind"] as? String,record["source"] as? String==route["source"] as? String,record["plugin"] as? String==route["plugin"] as? String,(record["midiChannel"] as? Int ?? 0)==(route["midiChannel"] as? Int ?? 0) else{return false}
      return implicit || record["route"] as? String==route["id"] as? String
    }
    let adopted=records.filter{$0["member"] as? Bool==true && $0["current"] as? Bool==true}
    guard adopted.count==1,let record=adopted.first else{text=data["pending"] as? Bool==true ? "Waiting for the audio engine to adopt this note cable":"This note cable is not in the adopted route · retired activity is not current";return}
    let held=record["heldNotes"] as? Int ?? 0,pedals=record["heldPedals"] as? Int ?? 0
    text="Notes held: \(held) · pedals: \(pedals)\nNote on: \(record["noteOns"] ?? 0) · off: \(record["noteOffs"] ?? 0) · events: \(record["events"] ?? 0)\nRoute releases: \(record["routingReleases"] ?? 0) · failures: \(record["failures"] ?? 0)\nLast event: frame \(record["lastFrame"] ?? 0) · adopted route"
  }
}
extension SignalGraphEditor {
  func refreshNoteActivity(force:Bool=false) {
    guard let route=selectedNoteRoute,let index=canvas.selectedEdge,canvas.edges.indices.contains(index)else{return}
    let now=ProcessInfo.processInfo.systemUptime
    guard !noteActivityPending,(force || now-noteActivityLastRead>=0.25)else{return}
    noteActivityPending=true;noteActivityLastRead=now
    let identity=canvas.edges[index].connection,document=projectionDocument
    requestGraph("graph.note.activity",[:],document:document){[weak self] reply in
      guard let self else{return};self.noteActivityPending=false
      guard self.projectionDocument==document,self.graphID==nil,let index=self.canvas.selectedEdge,self.canvas.edges.indices.contains(index),self.canvas.edges[index].connection==identity else{return}
      let value=(reply["result"] as? [String:Any])?["data"] as? [String:Any] ?? [:]
      let text=GraphNoteActivitySnapshot(value,route:route).text
      if self.noteControls.activity.stringValue != text{self.noteControls.activity.stringValue=text}
    }
  }
}
