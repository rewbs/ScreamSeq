import AppKit

extension InterfaceTests {
  static func graphNoteRoutingChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1200,height:700))
    var song:[String:Any]=["instruments":[["id":"n10","index":1,"name":"Bass","plugin":true]],
      "plugins":[["id":"synth-a","name":"Same synth","isInstrument":true,"assignments":[["instrument":1,"instrumentID":"n10","channel":3]]],["id":"synth-b","name":"Same synth","isInstrument":true]],
      "mixer":["buses":[["id":"n1","name":"Track 1","kind":"track","output":"n2"],["id":"n2","name":"Master","kind":"master","output":""]]],
      "noteRouting":["routes":[],"suppressedAssignments":[]]]
    var writes=[(String,[String:Any])](),revision="note-song:1"
    editor.onRequest={method,p,reply in
      if method=="graph.get"{reply(["result":["revision":revision,"data":song]])}
      else if method=="graph.note.activity"{reply(["result":["revision":revision,"data":["available":false]]])}
      else if method.hasPrefix("graph.note.") || method=="graph.connections.remove" || method=="plugin.duplicate" || method=="instrument.plugin.set"{writes.append((method,p));reply(["result":["revision":revision,"data":[:]]])}
      else{reply(["result":["revision":revision,"data":[]]])}
    }
    editor.load()
    let event=SignalGraphEditor.notePort
    try require(editor.canvas.nodes.first{$0.id=="n1"}?.outputs.contains{$0.signalType == .events && $0.number==event}==true,"Channel Notes sockets are distinct from physical audio output zero")
    try require(editor.canvas.nodes.first{$0.id=="plugin:synth-a"}?.inputs.contains{$0.signalType == .events && $0.number==event}==true,"Only plugin-instrument destinations expose Notes inputs")
    let assigned=editor.canvas.edges.firstIndex{$0.connection=="note-assignment:n10"}!
    try require(editor.canvas.edges[assigned].source=="note-instrument:n10" && editor.canvas.edges[assigned].label.contains("MIDI 3"),"Implicit assignment is an explicit visible note cable with its instrument and MIDI mapping")
    try require(editor.canvas.edgeSignal(editor.canvas.edges[assigned]) == .events,"Note cables retain their event signal type")
    editor.selectConnection(assigned)
    try require(!editor.noteControls.isHidden && editor.connectionForm.isHidden && editor.noteControls.channel.indexOfSelectedItem==3,"Selected note cables expose immediate event settings without stale audio gain controls")
    editor.updateNoteRoute(enabled:false,channel:4)
    try require(writes.last?.0=="graph.note.connect" && writes.last?.1["source"] as? String=="n10" && writes.last?.1["suppressAssignment"] as? Bool==true && writes.last?.1["enabled"] as? Bool==false && writes.last?.1["midiChannel"] as? Int==4,"Editing an implicit cable creates its exact explicit replacement and suppresses assignment in one API transaction")
    editor.connectPorts("n1","plugin:synth-b",out:event,input:event,modulation:false)
    try require(writes.last?.0=="graph.note.connect" && writes.last?.1["sourceKind"] as? String=="channel" && writes.last?.1["source"] as? String=="n1" && writes.last?.1["plugin"] as? String=="synth-b","Socket connection dispatches stable channel/instrument identities rather than mixer routing")
    let count=writes.count
    editor.connectPorts("n1","plugin:synth-b",out:0,input:event,modulation:false)
    try require(writes.count==count && editor.status.stringValue.contains("Notes connect only"),"Audio cannot accidentally connect to an event socket")
    editor.disconnectSong(assigned)
    try require(writes.last?.0=="graph.note.disconnect" && writes.last?.1["instrument"] as? String=="n10","Deleting an implicit cable suppresses its instrument assignment instead of leaving a cable that reappears")
    song["noteRouting"]=["routes":[["id":"n20","sourceKind":"channel","source":"n1","plugin":"synth-b","midiChannel":1,"enabled":true],["id":"n21","sourceKind":"channel","source":"n1","plugin":"synth-b","midiChannel":2,"enabled":true]],"suppressedAssignments":["n10"]]
    revision="note-song:2";editor.load()
    let first=editor.canvas.edges.firstIndex{$0.connection=="note:n20"}!,second=editor.canvas.edges.firstIndex{$0.connection=="note:n21"}!
    try require(editor.cableLocation(editor.canvas.edges[first]) != editor.cableLocation(editor.canvas.edges[second]),"Distinct MIDI mapping routes sharing sockets keep separate stable navigation/gesture identities")
    let firstKey=editor.visualEdgeIdentity(first)!,secondKey=editor.visualEdgeIdentity(second)!
    try require(!editor.visualCableMatches(firstKey,secondKey),"Reroute geometry cannot leak across same-socket note cables")
    let audio=editor.canvas.edges.firstIndex{$0.source=="n1" && $0.target=="n2" && $0.output==0}!
    editor.cutConnections([second,audio])
    let references=writes.last?.1["connections"] as? [[String:Any]] ?? []
    try require(writes.last?.0=="graph.connections.remove" && references.count==2 && references.contains{$0["route"] as? String=="n21"},"Mixed audio/event cable cut dispatches one atomic batch with exact route identity")
    editor.selectedID="plugin:synth-b";editor.canvas.selectNodes(["plugin:synth-b"]);editor.duplicateGraphSelection()
    try require(writes.last?.0=="plugin.duplicate" && writes.last?.1["plugin"] as? String=="synth-b" && writes.last?.1["position"] as? [String:Any] != nil,"Song processor Duplicate uses the stable unassigned-clone API")
    editor.selectConnection(second);editor.updateNoteRoute(enabled:true,channel:16)
    try require(writes.last?.0=="graph.note.update" && writes.last?.1["id"] as? String=="n21","Editing a selected same-socket mapping addresses its route ID")
    editor.selectedID="note-instrument:n10";editor.canvas.selected=editor.selectedID;editor.restoreNoteAssignment()
    try require(writes.last?.0=="graph.note.restoreAssignment" && writes.last?.1["instrument"] as? String=="n10","Restore assignment is reachable directly on the exact instrument source")
    editor.connectFromSocket(.init(node:"n1",number:event,output:true,modulation:false))
    try require(editor.targetMenu.entries.contains{$0.title.contains("Same synth") && $0.unavailable==nil} && editor.targetMenu.entries.contains{$0.unavailable?.contains("Notes connect only")==true},"Keyboard note patching exposes valid instrument targets and explains incompatible sockets")
    let oldChoice=editor.targetMenu.onChoose,entry=editor.targetMenu.entries.first{$0.unavailable==nil}!,before=writes.count
    revision="note-song:3";editor.load();oldChoice?(entry)
    try require(writes.count==before,"A stale keyboard note chooser cannot mutate a changed route graph")
    editor.targetMenu.close()
    let snapshot:[String:Any]=["available":true,"active":true,"fresh":true,"pending":false,"routes":[["route":"n21","implicit":false,"sourceKind":"channel","source":"n1","plugin":"synth-b","current":true,"member":true,"heldNotes":2,"noteOns":5,"noteOffs":3,"events":8]]]
    let route:[String:Any]=["id":"n21","implicit":false,"sourceKind":"channel","source":"n1","plugin":"synth-b"]
    try require(GraphNoteActivitySnapshot(snapshot,route:route).text.contains("Notes held: 2"),"Event inspector displays its exact adopted route counters without audio proxy meters")
    let implicitActivity:[String:Any]=["available":true,"active":true,"fresh":true,"routes":[["implicit":true,"sourceKind":"instrument","source":"n10","plugin":"synth-a","midiChannel":3,"current":true,"member":true,"heldNotes":1],["implicit":true,"sourceKind":"instrument","source":"n10","plugin":"synth-a","midiChannel":4,"current":true,"member":true,"heldNotes":7]]]
    try require(GraphNoteActivitySnapshot(implicitActivity,route:["implicit":true,"sourceKind":"instrument","source":"n10","plugin":"synth-a","midiChannel":4]).text.contains("Notes held: 7"),"Implicit activity identifies the exact MIDI assignment rather than aggregating shared endpoints")
    var stale=snapshot;stale["fresh"]=false
    try require(GraphNoteActivitySnapshot(stale,route:route).text.contains("consistent adopted"),"Torn generation snapshots never display stale event activity as current")
    stale=snapshot;stale["routes"]=[["route":"n21","implicit":false,"sourceKind":"channel","source":"n1","plugin":"synth-b","current":false,"member":false,"heldNotes":99]]
    try require(GraphNoteActivitySnapshot(stale,route:route).text.contains("retired activity"),"Retired route counters do not impersonate the current mapping")
    song["instruments"]=[["id":"n10","index":1,"name":"Bass","plugin":false]]
    song["noteRouting"]=["routes":[],"suppressedAssignments":[],"triggerSources":[["instrument":"n10","midiChannel":4]]]
    revision="note-song:4";editor.load()
    try require(editor.canvas.nodes.first{$0.id=="note-instrument:n10"}?.detail=="Plugin trigger · no default destination","An orphan trigger remains visible without a rack assignment or outgoing route")
    editor.selectedID="note-instrument:n10";editor.canvas.selected=editor.selectedID;editor.useNoteSampleMapping()
    try require(writes.last?.0=="instrument.plugin.set" && writes.last?.1["instrument"] as? Int==1 && writes.last?.1["plugin"] as? String=="","Use sample mapping explicitly clears the orphan trigger through the instrument API")
    song["noteRouting"]=["routes":[["id":"n30","sourceKind":"instrument","source":"n10","plugin":"synth-b","midiChannel":4,"enabled":true]],"triggerSources":[],"suppressedAssignments":[]]
    revision="note-song:5";editor.load()
    try require(editor.canvas.nodes.first{$0.id=="note-instrument:n10"}?.detail=="Inactive note routes · sample mode" && editor.canvas.edges.first{$0.connection=="note:n30"}?.enabled==false,"Explicit cables remain visible and truthfully inactive after restoring sample mode")
    let pair=SignalGraphEditor.audioPort(4,output:true,catalog:[["index":4,"direction":"output","name":"Surround","channels":2,"physicalBus":0,"firstChannel":4,"physicalChannels":6,"supported":true]])
    try require(pair.number==4 && pair.label=="Surround · channels 5–6 of 6" && pair.unavailable==nil,"A physical multichannel bus exposes readable channel slices without changing stable logical routing IDs")
    print("PASS note routing UI: typed channel/instrument ports, implicit suppression, exact MIDI mapping identity, immediate controls and stale keyboard guards")
  }
}
