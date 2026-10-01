import AppKit
extension InterfaceTests {
  static var timingData: [String:Any] { ["mode":"modern","tempo":127.125,"speed":7,"rowsPerBeat":4,"rowsPerMeasure":12,"groove":[1.5,0.5,1.25,0.75],"sequence":0,"patternOverrides":[2]] }
  static func songTimingFixture() -> SongTimingEditor {
    let view=SongTimingEditor(frame:.zero)
    view.onRequest={_,_,reply in reply(["result":["revision":"song:1","data":timingData]])};view.load();view.onRequest=nil
    return view
  }
  static func songTimingChecks() throws {
    let view=SongTimingEditor(frame:.zero)
    var calls=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    view.onRequest={method,params,reply in calls.append((method,params));replies.append(reply)}
    view.apply(dryRun:false);try require(calls.isEmpty,"Unloaded timing cannot submit an edit")
    view.load();view.load();view.apply(dryRun:false)
    try require(calls.count==1 && view.pending && !view.tempo.isEnabled,"Pending timing reads disable controls and duplicate requests")
    replies.removeFirst()(["result":["revision":"song:1","data":timingData]])
    try require(view.tempo.stringValue=="127.125" && view.groove.stringValue=="1.5, 0.5, 1.25, 0.75" && view.scope.stringValue.contains("1 pattern"),"Read preserves fractional tempo and reports overriding patterns")
    view.swing.stringValue="62.5";view.setSwing();view.apply(dryRun:true)
    try require(calls.last?.1["groove"] as? [Double]==[1.25,0.75,1.25,0.75] && calls.last?.1["expectedRevision"] as? String=="song:1" && calls.last?.1["tempo"] as? Double==127.125,"Swing and fractional tempo use revision-pinned shared API")
    view.apply(dryRun:false);try require(calls.count==2 && !view.tempo.isEnabled,"Pending previews cannot be committed or retargeted")
    replies.removeFirst()(["result":["revision":"song:1","data":["after":timingData,"wouldChange":true]]])
    view.apply(dryRun:false);replies.removeFirst()(["error":["message":"Song changed; reload"]])
    try require(view.revision=="song:1" && view.status.stringValue=="Song changed; reload" && view.groove.stringValue=="1.25, 0.75, 1.25, 0.75","Stale timing preserves draft and does not silently rebase")
    let count=calls.count
    for invalid in ["1,,1,1","NaN,1,1,1","0,1,1,1","1,1"] {view.groove.stringValue=invalid;view.apply(dryRun:false)}
    view.groove.stringValue="";view.tempo.stringValue="nan";view.apply(dryRun:false)
    view.tempo.stringValue="125";view.beat.stringValue="3";view.swing.stringValue="62.5";view.setSwing()
    try require(calls.count==count && view.groove.stringValue.isEmpty,"Malformed and odd-beat swing inputs never submit or silently truncate")
    view.beat.stringValue="4";view.setSwing();view.apply(dryRun:false)
    replies.removeFirst()(["result":["revision":"song:2","data":["after":timingData,"wouldChange":true]]])
    try require(view.revision=="song:2" && view.tempo.stringValue=="127.125" && view.status.stringValue.contains("Undo"),"Commit adopts canonical quantized settings and revision")
    let model=PatternModel(["tempo":120.0123,"displayRowsPerBeat":3,"displayRowsPerMeasure":12])
    try require(model.tempoText=="120.0123" && model.rowsPerBeat==3 && model.rowsPerMeasure==12,"Native grid model preserves fractional tempo and musical highlights")
    try require(PatternModel(["tempo":120]).tempoText=="120","Integer tempo display keeps significant trailing zeros")
    try require(PatternModel(["displayRowsPerBeat":0,"displayRowsPerMeasure":0]).rowsPerBeat==4,"Legacy unspecified beat length uses the core's four-row fallback")
  }
}
