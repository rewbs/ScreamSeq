import AppKit

extension InterfaceTests {
  static func pluginLocationChecks() throws {
    var document="song-a",revision="r1",requests=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    let target=["plugin":"stable-plugin"],hash=String(repeating:"a",count:64)
    let editor=PluginLocationEditor(target:target)
    editor.onContext={(document,revision)}
    editor.onRequest={method,params,reply in requests.append((method,params));replies.append(reply)}
    func read(_ saved:String="C:/Old/Plugin.vst3")->[String:Any] {
      ["result":["revision":revision,"data":["plugin":"stable-plugin","descriptor":["path":saved],
        "candidates":[["descriptor":["path":"/first.vst3"],"moduleSHA256":hash],["descriptor":["path":"/chosen.vst3"],"moduleSHA256":hash]]]]]
    }
    func success(_ path:String,dry:Bool=false)->[String:Any] {
      ["result":["revision":revision,"data":["plugin":"stable-plugin","path":path,"moduleSHA256":hash,"dryRun":dry,"wouldChange":true,"reconnected":!dry]]]
    }
    editor.load();editor.path.stringValue="Typed during read";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.path))
    replies.removeFirst()(read())
    try require(editor.path.stringValue=="Typed during read" && editor.candidates.indexOfSelectedItem==0,"A read retains newer input without choosing the first verified module")
    editor.reconnect();try require(requests.count==1,"An unselected typed path never reconnects")
    editor.candidates.selectItem(at:2);editor.selectCandidate();editor.scan()
    try require(requests.last?.0=="plugin.path.scan" && requests.last?.1["path"] as? String=="/chosen.vst3","Scan captures the exact input path and stable target")
    editor.candidates.selectItem(at:1);editor.selectCandidate();replies.removeFirst()(read())
    try require(editor.path.stringValue=="/first.vst3" && editor.candidates.indexOfSelectedItem==1,"A late scan cannot replace a newer candidate selection")
    editor.reconnect(dryRun:true);replies.removeFirst()(success("/first.vst3",dry:true))
    try require(editor.saved.stringValue.contains("C:/Old") && requests.last?.1["dryRun"] as? Bool==true,"Check leaves the displayed saved path unchanged")
    editor.reconnect();editor.path.stringValue="Newer draft";replies.removeFirst()(success("/first.vst3"))
    try require(editor.path.stringValue=="Newer draft" && editor.saved.stringValue.contains("/first.vst3"),"A completed reconnect acknowledges its captured path without erasing newer text")
    editor.candidates.selectItem(at:2);editor.selectCandidate();revision="r2";let count=requests.count
    editor.reconnect();try require(requests.count==count,"A stale revision cannot silently reconnect a newer song")
    editor.load();replies.removeFirst()(read());editor.reconnect()
    replies.removeFirst()(["error":["code":-32602,"message":"Changed module","data":["writeOutcome":"notCommitted"]]])
    try require(!editor.uncertain && editor.path.stringValue=="/chosen.vst3","A definite preflight refusal retains the path and permits explicit correction")
    editor.reconnect();replies.removeFirst()(["error":["code":-32002,"message":"Outcome unavailable"]])
    try require(editor.uncertain,"An admitted unknown write requires explicit review")
    let uncertainCount=requests.count;editor.reconnect();editor.scan()
    try require(requests.count==uncertainCount,"Unknown outcomes are never retried or rescanned automatically")
    editor.load();replies.removeFirst()(read("/chosen.vst3"));revision="r3";editor.acceptObservedState()
    try require(editor.uncertain,"Review cannot acknowledge a state superseded by another edit")
    editor.load();replies.removeFirst()(read("/chosen.vst3"));editor.acceptObservedState()
    try require(!editor.uncertain && editor.path.stringValue=="/chosen.vst3","Explicit current-state acknowledgement retains the draft")
    editor.reconnect();document="song-b";replies.removeFirst()(success("/chosen.vst3"))
    let oldDocumentCount=requests.count;editor.load();editor.reconnect()
    try require(editor.uncertain && requests.count==oldDocumentCount && editor.capturedDocument=="song-a","Late completions and reads never retarget a different document")

    let graph=PluginLocationWindow(target:["graph":"n12","node":"n15"])
    graph.editor.onContext={(document,revision)};graph.editor.onRequest={method,params,reply in requests.append((method,params));replies.append(reply)}
    graph.editor.load();try require(requests.last?.0=="graph.plugin.path.get" && requests.last?.1["node"] as? String=="n15","Graph locations capture stable recipe identity")
    replies.removeFirst()(read())
    try require(graph.editor.revision.isEmpty,"A read for another target cannot authorize an edit")
    graph.editor.load()
    let longPath="/"+String(repeating:"long-folder/",count:100)+"Module.vst3"
    replies.removeFirst()(["result":["revision":revision,"data":["graph":"n12","node":"n15","descriptor":["path":longPath],
      "candidates":[["descriptor":["path":longPath],"moduleSHA256":hash]]]]])
    graph.editor.layoutSubtreeIfNeeded()
    try require(graph.editor.fittingSize.width<=700,"Long saved and candidate paths must not force the inspector wider than its initial window")
    graph.editor.path.stringValue="Retained while hidden";graph.close()
    try require(graph.editor.path.stringValue=="Retained while hidden","Closing the retained window keeps unfinished input")
  }
}
