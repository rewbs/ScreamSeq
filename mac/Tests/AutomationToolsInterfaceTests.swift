import AppKit
extension InterfaceTests {
  static func automationToolsChecks() throws {
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
