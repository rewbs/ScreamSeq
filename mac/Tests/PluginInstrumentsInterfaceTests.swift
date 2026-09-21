import AppKit
extension InterfaceTests {
  static var aliasData:[String:Any]{["plugin":"synth","name":"Shared synth","isInstrument":true,
    "assignments":[["instrument":1,"channel":2,"available":true],["instrument":2,"channel":7,"available":true]],
    "instruments":[["instrument":1,"name":"Lead","owner":"synth"],["instrument":2,"name":"Bass","owner":"synth"],
      ["instrument":3,"name":"Drums","owner":""],["instrument":4,"name":"Other synth","owner":"other"]]]}
  static func pluginInstrumentsFixture()->PluginInstrumentsEditor {
    let view=PluginInstrumentsEditor(plugin:"synth");view.onRequest={_,_,reply in reply(["result":["revision":"song:1","data":aliasData]])};view.load();view.onRequest=nil;return view
  }
  static func pluginInstrumentsChecks() throws {
    let view=PluginInstrumentsEditor(plugin:"synth")
    var calls=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    view.onRequest={method,params,reply in calls.append((method,params));replies.append(reply)}
    view.apply(dryRun:false);try require(calls.isEmpty,"Unloaded alias panel cannot submit")
    view.load();view.load();view.add()
    try require(calls.count==1 && view.pending && !view.applyButton.isEnabled,"Pending alias reads cannot duplicate or edit")
    replies.removeFirst()(["result":["revision":"song:1","data":aliasData]])
    try require(view.routes.count==2 && view.routes[1].channel==7 && view.available==[3],"Available instruments exclude other owners and draft duplicates")
    let row=view.tableView(view.table,viewFor:view.table.tableColumns[0],row:0) as! PluginInstrumentRow
    let old=row.onChannel
    view.add();try require(view.routes.last==PluginInstrumentRoute(instrument:3,channel:1) && !view.addButton.isEnabled,"Add uses an existing unassigned instrument and unused MIDI channel")
    old?(5);try require(view.routes[0].channel==2,"Recycled row handlers cannot retarget a changed draft")
    view.edit(row:0,channel:16,generation:view.generation)
    view.edit(row:0,instrument:4,generation:view.generation)
    try require(view.routes[0]==PluginInstrumentRoute(instrument:1,channel:16),"Other plugins' instruments cannot be stolen by the native panel")
    view.apply(dryRun:true)
    let expected=view.routes
    try require(calls.last?.0=="plugin.instruments.set" && calls.last?.1["plugin"] as? String=="synth" && calls.last?.1["expectedRevision"] as? String=="song:1" && (calls.last?.1["assignments"] as? [[String:Int]])?[0]["channel"]==16,"Alias edits share the stable-identity revision API")
    view.edit(row:0,remove:true,generation:view.generation);view.apply(dryRun:false)
    try require(view.routes==expected && calls.count==2,"Pending routing cannot change or submit twice")
    replies.removeFirst()(["result":["revision":"song:1","data":["routing":aliasData,"wouldChange":true]]])
    try require(view.routes==expected,"Preview preserves the unsaved draft")
    view.apply(dryRun:false);replies.removeFirst()(["error":["message":"Song changed; reload"]])
    try require(view.routes==expected && view.revision=="song:1" && view.status.stringValue=="Song changed; reload","Stale alias edits retain their draft without silent rebase")
    view.load();var mismatched=aliasData;mismatched["plugin"]="neighbor"
    replies.removeFirst()(["result":["revision":"song:2","data":mismatched]])
    try require(view.revision=="song:1" && view.routes==expected,"Replies for another plugin never replace this editor")
    view.load();replies.removeFirst()(["result":["revision":"song:3","data":aliasData]])
    view.edit(row:1,remove:true,generation:view.generation);view.edit(row:0,remove:true,generation:view.generation);view.apply(dryRun:false)
    var empty=aliasData;empty["assignments"]=[[String:Any]]()
    replies.removeFirst()(["result":["revision":"song:4","data":["routing":empty,"wouldChange":true]]])
    try require(view.routes.isEmpty && view.revision=="song:4" && view.status.stringValue.contains("Undo effect change"),"Explicit empty list unassigns the plugin through one saved operation")
    let editor=PluginEditor(frame:.zero);try require(!editor.instrumentsButton.isEnabled,"Empty plugin panel disables instrument routing")
    editor.update(model:PatternModel(["nativePlugins":[["name":"Synth","isInstrument":true,"instrumentAssignments":[["instrument":1,"channel":2],["instrument":2,"channel":7]]]]]),values:[])
    var selected = -1;editor.onInstruments={selected=$0};editor.instrumentsButton.invoke()
    try require(selected==0 && editor.instrumentsButton.title=="Assigned instruments (2)…","Native action selects the visible instrument plugin and reports alias count")
    let source=InstrumentPluginEditor(instrument:1,model:PatternModel(["revisionToken":"source:1","nativePlugins":[["instanceID":"synth","name":"Fixture synth","isInstrument":true,"instrumentAssignments":[["instrument":1,"channel":7]]]]]))
    let host=NSWindow(contentRect:NSRect(x:0,y:0,width:570,height:260),styleMask:[.titled],backing:.buffered,defer:false);host.contentView=source;source.layoutSubtreeIfNeeded()
    try require(source.bounds.width>=560 && source.channel.selectedTag()==7,"Assignment editor keeps its usable window width and recalls the alias MIDI channel")
    var assignment:[String:Any]=[:]
    source.onRequest={method,params,reply in assignment=params;reply(["result":["revision":"source:2","data":[:]]])}
    source.apply()
    try require(assignment["plugin"] as? String=="synth" && assignment["instrument"] as? Int==1 && assignment["channel"] as? Int==7 && assignment["expectedRevision"] as? String=="source:1","Instrument inspector dispatches the atomic stable-target assignment API")
    let create=InstrumentPluginEditor(instrument:0,model:PatternModel(["revisionToken":"create:1","nativePlugins":[["instanceID":"synth","name":"Fixture synth","isInstrument":true]]]))
    var creation=[(String,[String:Any])](),creationReplies=[([String:Any])->Void]()
    create.onRequest={method,params,reply in creation.append((method,params));creationReplies.append(reply)}
    let createHost=NSWindow(contentRect:create.frame,styleMask:[.titled],backing:.buffered,defer:false);createHost.contentView=create
    createHost.makeFirstResponder(create.name);(create.name.currentEditor() as? NSTextView)?.string="Edited trigger name"
    create.apply();create.apply()
    try require(creation.count==1 && creation[0].0=="instrument.create" && creation[0].1["empty"] as? Bool==true && creation[0].1["name"] as? String=="Edited trigger name","New plugin trigger creates one empty instrument, without sample mapping")
    creationReplies.removeFirst()(["result":["revision":"create:2","data":["instrument":5]]])
    try require(creation.count==2 && creation[1].0=="instrument.plugin.set" && creation[1].1["instrument"] as? Int==5 && creation[1].1["expectedRevision"] as? String=="create:2","Creation continues with the exact new instrument and fresh revision")
    creationReplies.removeFirst()(["error":["message":"Plugin unavailable"]]);create.apply()
    try require(creation.count==3 && creation[2].0=="instrument.plugin.set" && create.instrument==5,"Failed assignment retries the same instrument without creating duplicates")
    creationReplies.removeFirst()(["result":["revision":"create:3","data":[:]]])
    try require(create.applyButton.title=="Apply assignment","After creation the action describes assignment edits")
    let instrument=InstrumentEditor(frame:.zero);var enabled:[String:Any]=[:]
    instrument.onApply={enabled=$0};instrument.enabled.state = .on;instrument.toggleEnvelopeEnabled()
    try require(enabled["enabled"] as? Bool==true && enabled["envelope"] as? Int==0 && enabled.count==2,"Enable envelope commits only this switch immediately without saving unrelated drafts")
    let actions=ContextActions.controls(in:source)
    try require(actions.items.contains(where:{$0.title=="Apply assignment"}),"Context actions expose the same editor commands")

  }
}
