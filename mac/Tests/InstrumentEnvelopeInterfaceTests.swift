import AppKit
extension InterfaceTests {
  static var instrumentEnvelopeData:[String:Any] { ["instrument":"n7","envelope":"volume","index":1,"name":"Soft keys","editable":true,"maxPoints":25,
    "points":[[0,0],[4,64],[8,32],[12,0]],"loop":false,"sustain":true,"sustainPoint":1,"sustainEnd":2,"releaseNode":255] }
  static func instrumentEnvelopeFixture()->InstrumentEnvelopeToolsEditor {
    let view=InstrumentEnvelopeToolsEditor(instrument:"n7",kind:"volume")
    view.onRequest={_,_,reply in reply(["result":["revision":"env:1","data":instrumentEnvelopeData]])};view.load();view.onRequest=nil
    view.operation.selectItem(at:5);view.changeOperation();return view
  }
  static func instrumentEnvelopeChecks() throws {
    let clipboard=InstrumentEnvelopeClipboard(),view=InstrumentEnvelopeToolsEditor(instrument:"n7",kind:"volume",clipboard:InstrumentEnvelopeClipboard())
    try require(view.labels.allSatisfy{!$0.isEditable && !$0.isBordered},"Tool setting labels cannot look or act like editable values")
    var calls=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    view.onRequest={method,params,reply in calls.append((method,params));replies.append(reply)}
    view.previewTool();view.apply();try require(calls.isEmpty,"Unloaded envelope tools cannot submit")
    view.load();view.load();try require(calls.count==1 && view.pending && !view.operation.isEnabled,"Envelope reads disable competing controls")
    replies.removeFirst()(["result":["revision":"env:1","data":instrumentEnvelopeData]])
    try require(view.end.stringValue=="13" && !view.canvas.canEdit() && !view.applyButton.isEnabled,"Loaded envelope shows exact tick bounds without implicit edits")
    view.start.stringValue="0.5";view.previewTool();try require(calls.count==1,"Fractional tick range rejects before API")
    view.start.stringValue="0";view.copyRange()
    let clip:[String:Any]=["span":13,"points":[[0,0],[4,64],[8,32],[12,0]],"units":"ticks"]
    replies.removeFirst()(["result":["revision":"env:1","data":clip]])
    try require(view.clipboard.clip?["span"] as? Int==13,"Envelope clipboard uses source tick span")
    view.operation.selectItem(at:1);view.changeOperation();view.previewTool();view.apply()
    try require(calls.count==3 && calls.last?.1["instrument"] as? String=="n7" && calls.last?.1["expectedRevision"] as? String=="env:1","Preview pins stable instrument identity and revision")
    var after=instrumentEnvelopeData;after["points"]=[[0,64],[4,0],[8,32],[12,64]]
    let preview:[String:Any]=["before":instrumentEnvelopeData,"after":after,"wouldChange":true,"clippedValues":0,"roundedValues":0,"reanchoredMarkers":0]
    replies.removeFirst()(["result":["revision":"env:1","data":preview]])
    try require(view.canvas.points==after["points"] as? [[Int]] && view.applyButton.isEnabled,"Preview draws exact API output and arms Apply")
    view.copyRange();try require(calls.count==3,"Copy cannot misrepresent an unsaved preview as saved points")
    view.end.stringValue="14";view.apply();try require(calls.count==3 && view.status.stringValue.contains("Preview again"),"Settings changed after preview cannot commit the old command")
    view.end.stringValue="13";view.apply();view.apply()
    try require(calls.count==4 && calls.last?.1["dryRun"] as? Bool==false && !view.applyButton.isEnabled,"Apply submits the exact preview once")
    replies.removeFirst()(["error":["message":"Song changed; reload"]])
    try require(view.preview != nil && view.revision=="env:1" && view.canvas.points==after["points"] as? [[Int]],"Stale envelope edits retain preview without rebasing")
    view.load();var wrong=instrumentEnvelopeData;wrong["instrument"]="n8"
    replies.removeFirst()(["result":["revision":"env:2","data":wrong]])
    try require(view.revision=="env:1" && view.preview != nil,"Mismatched instrument replies never retarget the editor")
    view.apply();replies.removeFirst()(["result":["revision":"env:2","data":preview]])
    try require(view.preview==nil && view.revision=="env:2" && !view.applyButton.isEnabled,"Applied preview becomes the saved baseline")
    view.copyRange();replies.removeFirst()(["result":["revision":"env:3","data":clip]])
    try require(view.status.stringValue.contains("song changed"),"Copy refuses a stale source without changing the baseline revision")
    clipboard.clip=clip;let destination=InstrumentEnvelopeToolsEditor(instrument:"n8",kind:"pan",clipboard:clipboard)
    try require(destination.clipboard.clip?["span"] as? Int==13,"Native clipboard can pass between instrument and envelope windows")
    let editor=InstrumentEditor(frame:.zero);var selected = -1;editor.onEnvelopeTools={selected=$0};editor.envelopeType.selectItem(at:2);editor.envelopeToolsButton.invoke()
    try require(selected==2,"Instrument editor opens tools for its selected envelope")
  }
}
