import AppKit
extension InterfaceTests {
  static func sampleLoopChecks() throws {
    let editor=SampleEditor(frame:NSRect(x:0,y:0,width:729,height:1400))
    let info:[String:Any]=["index":1,"name":"Loops","channels":2,"rate":44100,"volume":64,"pan":128,"pingpong":false,"sustainPingpong":false,"reverseLoop":false,"sustainReverse":false,"frames":2048,"loop":true,"loopStart":64,"loopEnd":1024,"sustainLoop":false,"sustainStart":0,"sustainEnd":0]
    let inventory:[[String:Any]]=[["index":1,"id":"loop-one","name":"Loops"],["index":2,"id":"loop-two","name":"Other"]]
    editor.update(info,samples:inventory,revision:"source")
    var currentRevision = "source"
    editor.onSampleRevision = { currentRevision }; editor.waveformDocumentID = "loop-song"
    var calls=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    editor.onRequest={method,params,reply in calls.append((method,params));replies.append(reply)}
    func answer(_ revision:String) throws {try require(!replies.isEmpty,"Expected loop save before reply \(revision): \(editor.loopsStatus.stringValue)");replies.removeFirst()(["result":["revision":revision,"data":["loopsChanged":true]]])}
    func reloadAnswer(_ revision: String, _ data: [String: Any]) throws {
      try require(!replies.isEmpty, "Expected metadata request before loop readback")
      replies.removeFirst()(["result": ["revision": revision, "documentId": "loop-song", "data": data]])
      try require(calls.last?.0 == "sample.waveform.get" && !replies.isEmpty, "Loop reload stages matching waveform after metadata")
      var wave = calls.last!.1; wave["peaks"] = Array(repeating: 0.5, count: (wave["bins"] as! Int) * 2)
      replies.removeFirst()(["result": ["revision": revision, "documentId": "loop-song", "data": wave]])
    }
    func waitForSave(){let deadline=Date(timeIntervalSinceNow:2);while editor.loopSaveWork != nil && Date()<deadline {_ = RunLoop.main.run(mode:.default,before:Date(timeIntervalSinceNow:0.01))}}
    editor.waveform.moveLoopMarker(.start,to:100,finished:false)
    waitForSave()
    try require(calls.count==1 && calls[0].0=="sample.loops.set" && calls[0].1["dryRun"] as? Bool==false &&
      (calls[0].1["normal"] as? [String:Any])?["start"] as? Int==100 && calls[0].1["expectedRevision"] as? String=="source",
      "Dragging a loop handle commits during the gesture with the source revision: \(calls) \(editor.loopsStatus.stringValue)")
    editor.waveform.moveLoopMarker(.start,to:200,finished:true)
    try require(calls.count==1,"In-flight loop saves cannot duplicate")
    try answer("first");waitForSave()
    try require(calls.count==2 && calls[1].1["expectedRevision"] as? String=="first" &&
      (calls[1].1["normal"] as? [String:Any])?["start"] as? Int==200,"A later drag survives the first save and uses its new revision")
    replies.removeFirst()(["error":["code":-32002,"message":"Busy","data":["writeOutcome":"notCommitted"]]]);waitForSave()
    try require(calls.count==3 && editor.loopsBusy,"A transient busy response retries the loop edit")
    try answer("second")
    editor.waveform.moveLoopMarker(.start,to:3000,finished:true)
    try require(editor.loopStart.integerValue==1023,"Loop start clamps before the exclusive end")
    try answer("third")
    editor.sustaining.state = .on;editor.loopModeChanged(editor.sustaining)
    try require(editor.sustainEnd.integerValue==2048 && calls.last?.1["dryRun"] as? Bool==false,"Enabling an empty sustain loop immediately saves the sample span")
    try answer("fourth")
    editor.waveform.moveLoopMarker(.sustainEnd,to:900,finished:true);try answer("fifth")
    try require(editor.sustainEnd.integerValue==900 && editor.waveform.sustainEnd==900,"Sustain handles edit the independent sustain loop")
    editor.loopReverse.state = .on;editor.loopModeChanged(editor.loopReverse)
    try require(editor.pingpong.state == .off && (calls.last?.1["normal"] as? [String:Any])?["reverse"] as? Bool==true,"Reverse mode saves immediately")
    try answer("sixth")
    editor.pingpong.state = .on;editor.loopModeChanged(editor.pingpong)
    try require(editor.loopReverse.state == .off,"Ping-pong excludes reverse")
    try answer("seventh")
    editor.loopStart.stringValue="-1";let count=calls.count;editor.loopFieldChanged()
    try require(calls.count==count,"Invalid loop text cannot modify the song")
    editor.loopStart.stringValue="72";editor.loopFieldChanged()
    editor.update(info,samples:inventory,revision:"external")
    replies.removeFirst()(["error":["message":"Song changed"]])
    try require(editor.loopDraftRevision=="seventh" && editor.loopStart.integerValue==72 && editor.loopsStatus.stringValue.contains("Reload loops"),"External conflicts keep the local intent and require explicit reload")
    currentRevision = "external"; editor.waveformDocumentID = "loop-song"; editor.reloadLoops()
    try require(editor.loopStart.integerValue==72 && editor.loopsBusy, "Reload retains conflicting edits until its read succeeds")
    var readback = info; readback["index"] = 1
    for key in ["pingpong", "sustainPingpong", "reverseLoop", "sustainReverse"] { readback[key] = false }
    try reloadAnswer("external", readback)
    try require(editor.loopDraftRevision=="external" && editor.loopStart.integerValue==64,"Successful readback retires conflicting edits")
    editor.waveform.moveLoopMarker(.end,to:1200,finished:true)
    editor.index=2
    try require(editor.loopDraftRevision==nil,"Changing sample retires the previous loop draft")
    editor.update(info,samples:inventory,revision:"new-sample")
    editor.waveform.moveLoopMarker(.start,to:150,finished:true)
    try answer("retired");waitForSave()
    try require(editor.loopsBusy && calls.last?.1["sample"] as? Int==2 && calls.last?.1["expectedRevision"] as? String=="new-sample",
      "Retiring a previous sample's reply resumes a new sample's pending gesture without adopting the old revision")
    try answer("new-saved")
    editor.waveform.moveLoopMarker(.start,to:160,finished:true)
    let uncertainCount=calls.count
    replies.removeFirst()(["error":["code":-32002,"message":"Unknown outcome","data":["writeOutcome":"unknown"]]])
    pumpMainRunLoop(0.2)
    try require(calls.count==uncertainCount && !editor.loopsBusy && editor.loopSaveWork==nil && editor.loopStart.integerValue==160 && editor.loopDraftRevision=="new-saved" && editor.loopsStatus.stringValue.contains("Unknown outcome"),
      "An uncertain sample loop write keeps its original draft/revision and cancels automatic replay")
    editor.setLoops(dryRun:false)
    try require(calls.count == uncertainCount, "Unknown outcomes cannot be resent before explicit readback")
    currentRevision = "new-saved"; editor.waveformDocumentID = "loop-song"; editor.reloadLoops(); readback["index"] = 2; readback["loopStart"] = 150
    try reloadAnswer("new-saved", readback)
    editor.loopStart.stringValue = "160"; editor.setLoops(dryRun: false)
    let firstRetryCount=calls.count
    for _ in 0..<6 {
      try require(!replies.isEmpty,"Expected one bounded loop request")
      replies.removeFirst()(["error":["code":-32002,"message":"Still busy","data":["writeOutcome":"notCommitted"]]])
      waitForSave()
    }
    pumpMainRunLoop(0.2)
    try require(calls.count==firstRetryCount+5 && replies.isEmpty && !editor.loopsBusy && editor.loopSaveWork==nil && editor.loopStart.integerValue==160 && editor.loopsStatus.stringValue.contains("Still busy"),
      "Loop retry exhaustion is bounded and keeps the editable draft without another automatic write")
    editor.reloadLoops()
    replies.removeFirst()(["error": ["message": "Read failed"]])
    try require(editor.loopStart.integerValue == 160 && editor.loopNeedsReload && editor.loopDraftRevision == "new-saved", "Failed loop reload keeps fields and their captured revision")
    editor.reloadLoops(); replies.removeFirst()(["result": ["revision": "incomplete", "data": ["index": 2, "frames": 2048]]])
    try require(editor.loopStart.integerValue == 160 && editor.loopDraftRevision == "new-saved" && editor.loopNeedsReload, "Incomplete readback cannot silently replace loop fields with defaults")
    currentRevision = "reviewed"; editor.reloadLoops(); try reloadAnswer("reviewed", readback)
    editor.loopStart.stringValue = "170"; editor.setLoops(dryRun: false)
    let captured = calls.last!.1 as NSDictionary
    replies.removeFirst()(["error": ["code": -32002, "message": "Busy", "data": ["writeOutcome": "notCommitted"]]])
    editor.loopStart.stringValue = "180"; editor.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: editor.loopStart))
    waitForSave()
    try require(captured.isEqual(to: calls.last!.1), "A definite busy retry sends the original loop payload, not newer text")
    try answer("accepted")
    try require(editor.loopStart.integerValue == 180 && editor.loopDraftRevision == "reviewed" && editor.loopNeedsReload, "A later loop write completion cannot authorize newer text at its revision")
    editor.loopSaveWork?.cancel();editor.onRequest=nil
    try sampleLoopRetryCompletionChecks()
    try sampleLoopReloadChecks()
  }
  static func sampleLoopRetryCompletionChecks() throws {
    for action in ["Return", "checkbox", "handle release"] {
      let editor = SampleEditor(frame: .zero)
      editor.update(["frames": 2048, "loop": true, "loopStart": 64, "loopEnd": 1024], samples: [["index": 1, "name": "Loops"]], revision: "retry:0")
      var calls = [[String: Any]](), replies = [([String: Any]) -> Void](), finished = [Bool]()
      editor.onRequest = { _, params, reply in calls.append(params); replies.append(reply) }
      editor.waveform.moveLoopMarker(.start, to: 100, finished: true)
      let original = calls[0] as NSDictionary
      replies.removeFirst()(["error": ["code": -32002, "message": "Busy", "data": ["writeOutcome": "notCommitted"]]])
      editor.finishAutomaticEdit { finished.append($0) }
      switch action {
      case "Return":
        editor.loopStart.stringValue = "120"
        editor.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: editor.loopStart)); editor.loopFieldChanged()
      case "checkbox": editor.sustaining.state = .on; editor.loopModeChanged(editor.sustaining)
      default: editor.waveform.moveLoopMarker(.start, to: 120, finished: true)
      }
      pumpMainRunLoop(0.08)
      try require(calls.count == 2 && original.isEqual(to: calls[1]) && editor.loopsBusy && finished.isEmpty,
        "\(action) keeps the pending exact-payload retry alive while Save waits")
      replies.removeFirst()(["result": ["revision": "retry:1", "data": ["loopsChanged": true]]])
      if action == "handle release" {
        pumpMainRunLoop(0.08)
        try require(calls.count == 3 && calls[2]["expectedRevision"] as? String == "retry:1", "Continuous handle movement follows the acknowledged retry")
        replies.removeFirst()(["result": ["revision": "retry:2", "data": ["loopsChanged": true]]])
        try require(finished == [true] && !editor.hasDraft, "A completed handle retry releases Save")
      } else {
        try require(finished == [false] && editor.loopNeedsReload && !editor.loopsBusy, "Newer fields settle Save with retained review instead of stranding it")
      }
    }
  }
}
