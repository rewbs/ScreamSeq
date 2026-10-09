import AppKit
extension InterfaceTests {
  static func graphStageRoutingChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:700))
    func recipe(_ id:String,_ input:UInt32,_ output:UInt32)->[String:Any]{["id":id,"name":id,"number":1,"nodes":[["id":id+"i","kind":"input"],["id":id+"o","kind":"output"]],"audio":[["source":id+"i","target":id+"o","output":input,"input":output]]]}
    let route:[String:Any]=["source":["stage":"n1"],"target":["plugin":"B"],"output":2,"input":1,"gainDB":-7.0,"enabled":true]
    let ports:[[String:Any]]=[["index":0,"direction":"input","name":"Main","channels":2],["index":1,"direction":"input","name":"Detector","channels":2],["index":0,"direction":"output","name":"Main","channels":2]]
    let song:[String:Any]=["library":[recipe("ordinary",1,2),recipe("row",3,4),recipe("instrument",7,8)],"assignments":[["target":"n1","graph":"ordinary"]],"commands":[["target":"n1","graph":"row","kind":"row"]],"instrumentAssignments":[["target":"instrument1","graph":"instrument"]],"plugins":[["id":"A","name":"Source","slot":0,"audioBuses":ports],["id":"B","name":"Compressor","slot":1,"audioBuses":ports]],"mixer":["buses":[["id":"n1","name":"Track 1","kind":"track","output":"n3"],["id":"n2","name":"Track 2","kind":"track","output":"n3","inserts":["A"]],["id":"n3","name":"Master","kind":"master","inserts":["B"]]]],"stageConnections":[route]]
    editor.update(song)
    let stage=editor.canvas.nodes.first{$0.id=="stage:n1"}!
    try require(stage.title.contains("graph stage") && stage.detail.contains("aggregate") && stage.inputs.map(\.number)==[1,3] && stage.outputs.map(\.number)==[2,4],"Outer stage exposes the ordinary/row union and excludes instrument copies")
    try require(stage.outputs.allSatisfy{$0.label.contains("Combined")} && editor.canvas.nodes.filter{$0.id.hasPrefix("graph:")}.allSatisfy{$0.inputs.allSatisfy{$0.number==0} && $0.outputs.allSatisfy{$0.number==0}},"Auxiliary aggregate sockets are never presented as the selected recipe copy's exact ports")
    editor.selectedID=stage.id;editor.canvas.selected=stage.id;editor.inspect()
    try require(!editor.stageExplanation.isHidden && editor.name.isHidden && editor.busControls.isHidden,"Selecting an aggregate explains shared inputs and summed outputs without pretending to edit a separate copy or bus fader")
    let i=editor.songConnections.firstIndex{$0["kind"] as? String=="stage-connection"}!
    let e=editor.canvas.edges[i]
    try require(e.source==stage.id && e.target=="plugin:B" && e.output==2 && e.input==1 && e.amount == -7,"Typed stage cable draws on the exact combined output and processor detector socket")
    editor.selectConnection(i)
    try require(editor.connectionKind.titleOfSelectedItem=="Stage audio" && !editor.connectionEnabled.isHidden && editor.inputPort.isEnabled && editor.outputPort.isEnabled,"Stage cable controls immediately edit gain/enable and exact ports")
    var calls=[(String,[String:Any])]()
    editor.onRequest={m,p,reply in calls.append((m,p));reply(["error":["message":"captured"]])}
    editor.connectPorts("plugin:A","stage:n1",out:0,input:3,modulation:false)
    try require(calls.count==1 && calls[0].0=="graph.audio.connection.set" && (calls[0].1["source"] as? [String:String])?["plugin"]=="A" && (calls[0].1["target"] as? [String:String])?["stage"]=="n1","Fresh stage input gesture preserves typed bus/processor identities in one API transaction")
    calls=[];editor.rewire(i,source:"stage:n1",target:"plugin:B",out:4,input:0,modulation:false)
    let old=calls.first?.1["replace"] as? [String:Any]
    try require(calls.count==1 && calls[0].0=="graph.audio.connection.set" && (old?["source"] as? [String:String])?["stage"]=="n1" && old?["output"] as? Int==2 && calls[0].1["gainDB"] as? Double == -7,"Stage repatch retains exact old identity/gain and never moves rack ownership")
    calls=[];editor.connectStage("stage:n1","plugin:B",output:8,input:1)
    try require(calls.isEmpty && editor.status.stringValue.contains("unavailable"),"An instrument-copy port cannot be silently routed through an outer channel stage")
    let from=editor.portChoices.first{$0.key.node==stage.id && $0.key.output && $0.key.number==2}!,to=editor.portChoices.first{$0.key.node=="plugin:B" && !$0.key.output && $0.key.number==1}!
    try require(editor.portPairUnavailable(from,to)==nil,"Keyboard socket patching offers the same aggregate-to-rack route")
    calls=[];editor.cutConnections([i]);let cut=(calls.first?.1["connections"] as? [[String:Any]])?.first
    try require(calls.count==1 && calls[0].0=="graph.connections.remove" && cut?["kind"] as? String=="stage-connection" && (cut?["source"] as? [String:String])?["stage"]=="n1","Stage cable cut shares exact typed mixed-batch Undo")
    try require(editor.observedCablePort(i)==nil && editor.stageRuntimeEndpoint(["stage":"n1"])=="plugin:signal-bus-1","Missing exact adopted stage contribution remains unavailable rather than borrowing a selected-copy or bus meter")
    var instrumentSong=song
    instrumentSong["plugins"]=(song["plugins"] as! [[String:Any]])+[["id":"I","name":"Input synth","slot":2,"isInstrument":true,"audioBuses":ports],["id":"generator","name":"No input synth","slot":3,"isInstrument":true,"audioBuses":[["index":0,"direction":"output","channels":2]]]]
    let instruments=SignalGraphEditor(frame:.zero);instruments.update(instrumentSong)
    let synth=instruments.canvas.nodes.first{$0.id=="plugin:I"}!,generator=instruments.canvas.nodes.first{$0.id=="plugin:generator"}!
    try require(synth.inputs.contains{$0.number==0 && $0.signal == .audio} && synth.inputs.contains{$0.number==1 && ($0.signal == .audio || $0.signal == .sidechain)} && !generator.inputs.contains{$0.signal == .audio},"Scheduled plugin instruments expose actual physical inputs without inventing one for generator-only vendors")
    var inputCalls=[String]();instruments.onRequest={m,_,reply in inputCalls.append(m);reply(["error":["message":"captured"]])}
    instruments.connectPorts("stage:n1","plugin:I",out:2,input:0,modulation:false)
    try require(inputCalls==["graph.audio.connection.set"],"Qualified instrument input routing uses the same exact stage cable transaction")
    inputCalls=[];instruments.connectPorts("n2","plugin:I",out:0,input:0,modulation:false)
    try require(inputCalls==["mixer.sidechains.set"],"A physical instrument Main input sums a channel directly without offering an inapplicable effect-chain move")

    let follow=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:700))
    var followerSong=song
    followerSong["songSources"]=[["id":"n90","kind":"follower","name":"Stage follower","audioStage":"n1","output":2,"preFader":false]]
    follow.update(followerSong)
    follow.portCatalogs["plugin:B"]=["parameters":[["id":UInt32(7),"name":"Threshold","writable":true,"canSlide":true,"min":0.0,"max":1.0]]]
    follow.exposedParameters["plugin:B"]=7;follow.rebuild()
    let tap=follow.songFollowerTap("stage:n1",output:2)
    try require(tap?["audioStage"] as? String=="n1" && tap?["audioPlugin"] as? String=="" && tap?["audioBus"] as? String=="" && tap?["output"] as? UInt32==2,"A combined-output follower keeps a typed stage identity and never persists a synthetic plugin")
    let followerIndex=follow.songConnections.firstIndex{$0["kind"] as? String=="follower-input"}!
    let followerEdge=follow.canvas.edges[followerIndex]
    try require(followerEdge.source=="stage:n1" && followerEdge.target=="source:n90" && followerEdge.output==2 && follow.songConnections[followerIndex]["stage"] as? String=="n1","Saved stage followers draw on their exact combined auxiliary output")
    var followerCalls=[(String,[String:Any])]()
    follow.onRequest={method,params,reply in followerCalls.append((method,params));reply(["error":["message":"captured"]])}
    follow.offerAudioFollower("stage:n1","plugin:B",output:2,parameter:7,position:NSPoint(x:400,y:300))
    try require(followerCalls.isEmpty && follow.targetMenu.entries.first?.id=="follower","Audio-to-parameter stage drops explicitly offer a converter before editing")
    follow.targetMenu.choose()
    let added=followerCalls.first?.1["source"] as? [String:Any],target=followerCalls.first?.1["connect"] as? [String:Any]
    try require(followerCalls.count==1 && followerCalls[0].0=="graph.song.source.add" && added?["audioStage"] as? String=="n1" && added?["output"] as? UInt32==2 && added?["audioPlugin"] as? String=="" && target?["plugin"] as? String=="B" && target?["parameter"] as? UInt32==7,"The offered stage follower and zero-depth parameter target use one atomic source-add transaction")
    followerCalls=[];follow.cutConnections([followerIndex])
    let followerCut=(followerCalls.first?.1["connections"] as? [[String:Any]])?.first
    try require(followerCalls.count==1 && followerCalls[0].0=="graph.connections.remove" && followerCut?["stage"] as? String=="n1" && followerCut?["output"] as? Int==2,"Stage follower input cuts use the exact typed endpoint in the unified batch")
    followerCalls=[];follow.rewire(followerIndex,source:"plugin:A",target:"source:n90",out:0,input:0,modulation:false)
    let repatch=followerCalls.first?.1["source"] as? [String:Any]
    try require(followerCalls.count==1 && followerCalls[0].0=="graph.song.source.update" && repatch?["audioPlugin"] as? String=="A" && repatch?["audioStage"] as? String=="","Repatching a follower onto a rack output explicitly clears its prior stage identity")

    print("PASS typed outer graph-stage routing: declared union, aggregate labels, exact stable endpoints, one mutation, repatch/cut and no copy proxy")
  }
}
