import AppKit
extension InterfaceTests {
  static func automationToolsChecks() throws {
    try liveAutomationEditorChecks()
    let editor = PatternAutomationEditor(frame: NSRect(x:0,y:0,width:960,height:620))
    editor.model = PatternModel(["nativePlugins":[["name":"Gain", "instanceID":"gain"]]])
    editor.revision = "source"; editor.laneID = "n1"; editor.parameterID = 7
    editor.canvas.points = [EnvelopePoint(position:0,value:0.2,curve:"step"), EnvelopePoint(position:1023,value:0.8,curve:"linear")]
    var calls = [(String,[String:Any])](), replies = [([String:Any])->Void]()
    editor.onRequest = { method, params, reply in calls.append((method,params));replies.append(reply) }
    func answer(_ data: [String:Any], revision: String = "source") { replies.removeFirst()(["result":["revision":revision,"data":data]]) }
    let preview: [String:Any] = ["wouldChange":true,"clippedValues":0,"after":[["position":0,"value":0.8,"curve":"step-next"],["position":1023,"value":0.2,"curve":"linear"]]]
    editor.toolEnd.stringValue = "4"; editor.previewTool(); editor.previewTool()
    try require(calls.count==1 && calls[0].0=="automation.pattern.transform" && calls[0].1["end"] as? Int == 1024 && calls[0].1["dryRun"] as? Bool == true, "Tools preview uses exact half-open musical units and cannot duplicate pending requests")
    editor.toolEnd.stringValue = "5";answer(preview)
    try require(!editor.hasDraft && editor.canvas.points[0].value==0.2 && editor.status.stringValue.contains("discarded"), "Changed tool controls discard delayed previews")
    editor.previewTool();editor.markDraft();editor.canvas.points[0].value=0.3;answer(preview)
    try require(editor.canvas.points[0].value==0.3, "A new local gesture survives an older preview")
    editor.hasDraft=false;editor.previewTool();answer(preview)
    try require(editor.hasDraft && editor.canvas.points[0].curve=="step-next", "Native preview becomes an editable draft, not a silent commit")
    let count=calls.count;editor.copyRange();try require(calls.count==count,"Copy cannot read past an unsaved draft")
    editor.hasDraft=false;editor.copyRange();answer(["span":1024,"unitsPerRow":256,"points":editor.canvas.points.map(\.dictionary)])
    try require(editor.envelopeClipboard?["span"] as? Int == 1024,"Envelope clipboard keeps exact musical span")
    editor.toolOperation.selectItem(at:7);editor.changeTool();editor.toolStart.stringValue="8";editor.toolEnd.stringValue="16";editor.toolValues[0].stringValue="2";editor.previewTool()
    try require(calls.last?.1["end"]==nil && calls.last?.1["start"] as? Int==2048 && (calls.last?.1["options"] as? [String:Any])?["repeats"] as? Double==2,"Repeat paste uses clip span and starts at the chosen row")
    answer(["wouldChange":false]);try require(!editor.hasDraft,"No-op preview preserves clean editor state")
    editor.toolStart.stringValue="0.1";editor.previewTool();try require(!editor.loading,"Inexact musical unit input sends no request")
    editor.markDraft();editor.apply();editor.canvas.points[0].value=0.4;editor.markDraft();answer(["lane":"n1"],revision:"saved")
    try require(editor.canvas.points[0].value==0.4 && editor.hasDraft && editor.revision=="saved", "A second gesture survives Apply and uses the successful revision")
    for shape in ["step-next","exponential-reverse","logarithmic-reverse"] {
      editor.canvas.points=[EnvelopePoint(position:0,value:0,curve:shape),EnvelopePoint(position:256,value:1,curve:"linear")]
      let mid=editor.canvas.value(at:128)
      try require(mid.isFinite && mid>=0 && mid<=1,"Mirrored shapes draw finite normalized values")
    }

    let zoomed=AutomationCanvas(frame:NSRect(x:0,y:0,width:800,height:240))
    zoomed.rows=64;zoomed.points=[EnvelopePoint(position:16*256,value:0.6,curve:"linear")];zoomed.selected=0
    let originalSpan=zoomed.horizontalSpan;zoomed.zoom(4)
    try require(abs(zoomed.horizontalSpan-originalSpan/4)<0.001,"Zoom changes visible musical span")
    let location=zoomed.location(zoomed.points[0])
    try require(abs(zoomed.position(at:location.x)-4096)<0.001,"Zoomed hit testing and drawing use identical coordinates")
    zoomed.setViewport(start:1e9,span:1000);try require(zoomed.horizontalEnd<=Double(zoomed.rows*256-1),"Pan clamps to pattern")
    zoomed.valueLow=0.4;zoomed.valueHigh=0.8
    try require(abs(zoomed.normalizedValue(at:zoomed.location(zoomed.points[0]).y)-0.6)<0.001,"Value zoom keeps point editing accurate")
    zoomed.fit();try require(zoomed.visibleStart==0 && zoomed.horizontalSpan==originalSpan && zoomed.valueLow==0 && zoomed.valueHigh==1,"Fit resets both axes")
    editor.onRequest=nil;editor.canvas.points=[EnvelopePoint(position:0,value:0.2,curve:"linear"),EnvelopePoint(position:512,value:0.8,curve:"linear")];editor.canvas.selected=0
    editor.curve.selectItem(at:8);editor.changeCurve()
    try require(editor.canvas.points[0].curve=="scripted" && !editor.formulaBox.isHidden && editor.canvas.points[0].formula=="mix(start, end, t)","Curve menu edits selected outgoing segment and reveals formula")
    editor.formula.stringValue="start + (end-start)*t^3";editor.controlTextDidChange(Notification(name:NSControl.textDidChangeNotification,object:editor.formula))
    try require(editor.canvas.points[0].dictionary["formula"] as? String=="start + (end-start)*t^3","Formula edits remain in the node draft/API payload")
    editor.curve.selectItem(at:1);editor.changeCurve();editor.curve.selectItem(at:8);editor.changeCurve()
    try require(editor.formula.stringValue=="start + (end-start)*t^3","Changing curve types preserves the custom expression")
    let pattern=PatternView();pattern.model=PatternModel(["rows":64,"channels":8])
    try require(pattern.playbackSelection==nil,"A cursor alone is not a playback selection")
    pattern.selectRegion(from:(9,2),to:(4,0));try require(pattern.playbackSelection==(4..<10),"Playback selection has sorted, exclusive row bounds")
  }
}

extension InterfaceTests {
  static func liveAutomationEditorChecks() throws {
    let editor=PatternAutomationEditor(frame:NSRect(x:0,y:0,width:1000,height:620))
    let model=PatternModel(["pattern":2,"rows":64,"nativePlugins":[["name":"First","instanceID":"stable-A"],["name":"Second","instanceID":"stable-B"]]])
    editor.onContext={model}
    var calls=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    editor.onRequest={method,params,reply in calls.append((method,params));replies.append(reply)}
    func answer(_ data:Any,_ revision:String="r0"){replies.removeFirst()(["result":["revision":revision,"data":data]])}
    func waitForSave(){let deadline=Date(timeIntervalSinceNow:0.25);while Date()<deadline {_ = RunLoop.main.run(mode:.default,before:deadline)}}
    editor.load()
    replies.removeFirst()(["error":["code":-32002,"message":"Busy"]]);waitForSave()
    try require(calls.count==2 && editor.loading,"Busy automation loads retry without making stale parameters editable")
    answer(["rows":64,"lanes":[]])
    answer([["id":7,"name":"Gain","value":0.5]])
    try require(editor.parameterID==7 && editor.canvas.allowsEditing,"Initial parameter is selected even at table row zero")
    editor.search.stringValue="Gain";editor.filter()
    editor.plugin.selectItem(at:1);editor.selectPlugin()
    try require(editor.parameterID==nil && editor.filtered.isEmpty && !editor.canvas.allowsEditing && calls.last?.1["slot"] as? Int==1,"Switch clears stale parameters and disables editing while the new catalogue loads")
    answer([["id":93,"name":"Cutoff","value":0.2]])
    try require(editor.parameterID==93 && editor.filtered.count==1 && editor.selectedPluginID=="stable-B","Changing plugin refreshes row-zero selection by stable parameter identity and clears the previous search")
    editor.canvas.points=[EnvelopePoint(position:0,value:0.2,curve:"linear")];editor.markDraft();waitForSave()
    try require(calls.last?.0=="automation.pattern.set" && calls.last?.1["plugin"] as? String=="stable-B" && calls.last?.1["parameter"] as? Int==93,"Graph edits save automatically to the selected stable plugin/parameter")
    editor.canvas.points[0].value=0.7;editor.markDraft()
    editor.plugin.selectItem(at:0);editor.selectPlugin()
    try require(editor.pluginIndex==1 && editor.pendingPluginID=="stable-A","Plugin changes wait for the current envelope save")
    answer(["lane":"n20"],"r1");waitForSave()
    try require(calls.last?.1["expectedRevision"] as? String=="r1" && calls.last?.1["plugin"] as? String=="stable-B" &&
      (calls.last?.1["points"] as? [[String:Any]])?.first?["value"] as? Double==0.7,"A newer drag is resubmitted with the saved revision and original target")
    answer(["lane":"n20"],"r2")
    try require(editor.pluginIndex==0 && calls.last?.0=="plugin.parameters.get","Deferred plugin switch proceeds only after the latest edit saves")
    answer([["id":7,"name":"Gain"]],"r2")
    editor.showPositions(editPattern:2,row:3,playPattern:2,position:825.5)
    try require(editor.canvas.editPosition==768 && editor.canvas.playbackPosition==825.5,"Edit and playback cursors retain independent fractional positions")
    editor.canvas.zoom(4);editor.showPositions(editPattern:9,row:3,playPattern:nil,position:nil)
    try require(editor.canvas.editPosition==nil && editor.canvas.playbackPosition==nil,"Unrelated patterns and stopped playback do not show misleading cursors")
    editor.search.stringValue="missing";editor.filter()
    try require(editor.parameterID==nil && !editor.canvas.allowsEditing,"Filtering out a parameter clears its editing target")
    editor.autoSaveWork?.cancel();editor.formulaPreviewWork?.cancel();editor.onRequest=nil
  }
}
