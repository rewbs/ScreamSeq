import AppKit

extension InterfaceTests {
  static func songSettingsChecks() throws {
    let editor=SongSettingsEditor(frame:.zero)
    var document="song-a",revision="r1",writes=[[String:Any]]()
    var completions=[([String:Any])->Void]()
    var values:[String:Any] = ["title":"Original","tempo":125.123456789,"speed":6,"channels":8]
    editor.onContext={(document,revision)}
    editor.onRequest={method,params,reply in
      if method=="document.get" {reply(["result":["revision":revision,"data":values]])}
      else {writes.append(params);completions.append(reply)}
    }
    editor.load(reload:true)
    editor.tempo.stringValue="unfinished"
    var boundaries=[Bool]();editor.finishAutomaticEdit{boundaries.append($0)}
    try require(writes.isEmpty && boundaries == [false] && editor.tempo.stringValue=="unfinished","Invalid song settings retain text and settle Save without a write")
    editor.load(reload:true);editor.title.stringValue="First"
    editor.apply();try require(writes.count==1 && writes[0]["title"] as? String=="First" && writes[0]["tempo"]==nil,
      "Changing only the title must preserve the original native tempo")
    editor.title.stringValue="Newer";boundaries=[];editor.finishAutomaticEdit{boundaries.append($0)}
    editor.apply();try require(writes.count==1 && boundaries.isEmpty,"A duplicate Apply during pending work cannot settle Save early")
    revision="r2";completions.removeFirst()(["result":["revision":revision,"data":[:]]])
    try require(writes.count==2 && writes[1]["title"] as? String=="Newer" && writes[1]["expectedRevision"] as? String=="r2" && boundaries.isEmpty,
      "A waiting Save resumes the newer retained draft after a successful completion")
    revision="r3";completions.removeFirst()(["result":["revision":revision,"data":[:]]])
    try require(boundaries==[true] && !editor.hasDraft && editor.title.stringValue=="Newer","Save completes only after both accepted generations")
    editor.channels.stringValue="7";editor.apply()
    completions.removeFirst()(["error":["code":-32602,"message":"Rejected channels"]])
    try require(editor.channels.stringValue=="7" && editor.hasDraft && !editor.pending,"A refused channel edit retains its raw draft")
    revision="other-edit";let count=writes.count;editor.apply()
    try require(writes.count==count && editor.channels.stringValue=="7","Stale Song Settings cannot silently target a newer song revision")
    editor.load(reload:false);try require(editor.channels.stringValue=="7","Capturing current revision retains field text")
    editor.apply();completions.removeFirst()(["error":["code":-32002,"message":"Outcome unavailable"]])
    try require(editor.uncertain,"An uncertain admitted write requires review before another submission")
    editor.apply();try require(writes.count==count+1,"An uncertain write is never automatically replayed")
    editor.load(reload:true);revision="after-review";editor.acceptObservedState()
    try require(editor.uncertain,"Accepting observed state rejects a revision changed after the read")
    editor.load(reload:false);editor.acceptObservedState()
    try require(!editor.uncertain && editor.channels.stringValue=="7","Explicit review preserves field text while retiring uncertain outcome ownership")
    editor.apply();document="song-b";revision="new-song";boundaries=[];editor.finishAutomaticEdit{boundaries.append($0)}
    completions.removeFirst()(["result":["revision":"old-song-completed","data":[:]]])
    try require(boundaries==[false] && editor.hasDraft && editor.stale,"A late old-song completion cannot authorize Save in a different document")

    // A read is also asynchronous: reload may not erase text typed while it ran.
    var read:(([String:Any])->Void)?
    editor.onRequest={_,_,reply in read=reply}
    editor.load(reload:true);editor.title.stringValue="During read"
    read?(["result":["revision":revision,"data":values]])
    try require(editor.title.stringValue=="During read" && editor.hasDraft,"Reload preserves newer raw input")
    editor.load(reload:true);read?(["error":["code":-32003,"message":"Read failed"]])
    try require(!editor.pending && editor.title.stringValue=="During read","Failed read retains the draft and releases pending ownership")
    values["title"]="Reloaded";editor.load(reload:true);read?(["result":["revision":revision,"data":values]])
    try require(editor.title.stringValue=="Reloaded" && !editor.hasDraft,"An explicit unchanged reload discards the draft")

    let panel=SongSettingsWindow();panel.editor.onContext={(document,revision)}
    panel.editor.onRequest={_,_,reply in reply(["result":["revision":revision,"data":values]])}
    panel.editor.load(reload:true);panel.editor.title.stringValue="Keep while hidden"
    panel.close()
    try require(panel.editor.title.stringValue=="Keep while hidden" && panel.editor.hasDraft,"Closing the retained settings window preserves its owner and draft")

    let failed=SongSettingsEditor(frame:.zero);failed.onContext={(document,revision)}
    failed.onRequest={_,_,reply in read=reply};failed.load(reload:true);failed.title.stringValue="Before first read"
    boundaries=[];failed.finishAutomaticEdit{boundaries.append($0)}
    read?(["error":["code":-32003,"message":"Initial read failed"]])
    try require(boundaries==[false] && failed.hasDraft && !failed.pending,"An initial read failure retains new input and fails its waiting boundary")
  }
}
