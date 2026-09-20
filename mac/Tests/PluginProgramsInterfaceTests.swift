import AppKit
extension InterfaceTests {
  static var programsData:[String:Any]{["plugin":"unit-synth","name":"Multitimbral instrument","catalogRevision":"programs:fixture","programs":[
    ["id":"vst3:0:17:0","name":"Warm pad","group":"Root / Factory","loadable":true],
    ["id":"vst3:7:18:2","name":"Bright brass","group":"Layer / Factory","loadable":true],
    ["id":"vst3:9:19:1","name":"Vintage keys","group":"External bank","loadable":false]]]}
  static func pluginProgramsFixture()->PluginProgramsEditor {
    let view=PluginProgramsEditor(plugin:"unit-synth");view.onRequest={_,_,reply in reply(["result":["revision":"song:1","data":programsData]])};view.load();view.onRequest=nil
    view.table.selectRowIndexes(IndexSet(integer:1),byExtendingSelection:false);return view
  }
  static func pluginProgramsChecks() throws {
    let view=PluginProgramsEditor(plugin:"unit-synth");var calls=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    view.onRequest={method,params,reply in calls.append((method,params));replies.append(reply)}
    view.apply(dryRun:false);try require(calls.isEmpty && !view.loadButton.isEnabled,"Unloaded program editor cannot mutate")
    view.load();view.load();try require(calls.count==1 && !view.reloadButton.isEnabled,"Pending catalog read cannot duplicate")
    replies.removeFirst()(["result":["revision":"song:1","data":programsData]])
    view.table.selectRowIndexes(IndexSet(integer:2),byExtendingSelection:false)
    view.apply(dryRun:false);try require(calls.count==1 && !view.loadButton.isEnabled,"Unselectable vendor programs are shown without exposing a load action")
    view.search.stringValue="layer";view.filter();try require(view.filtered.count==1 && view.selected==nil,"Search matches unit groups without loading a program")
    view.table.selectRowIndexes(IndexSet(integer:0),byExtendingSelection:false);view.apply(dryRun:true)
    let request=calls.last!.1
    try require(calls.last!.0=="plugin.programs.load" && request["program"] as? String=="vst3:7:18:2" && request["plugin"] as? String=="unit-synth" && request["expectedRevision"] as? String=="song:1" && request["expectedCatalogRevision"] as? String=="programs:fixture","Filtered selection uses stable program, unit, plugin and revision identifiers")
    view.apply(dryRun:false);try require(calls.count==2 && !view.search.isEnabled,"Pending program check blocks repeated loads and search changes")
    replies.removeFirst()(["result":["revision":"song:1","data":["plugin":"unit-synth","program":["id":"vst3:7:18:2"],"loaded":false]]])
    try require(view.status.stringValue.contains("Selection is valid") && view.loadButton.isEnabled,"Dry check leaves selected program available for explicit loading")
    view.apply(dryRun:false);replies.removeFirst()(["error":["message":"Plugin programs changed; reload"]])
    try require(view.revision=="song:1" && view.selected?["id"] as? String=="vst3:7:18:2" && view.status.stringValue.contains("changed"),"Stale program failure retains selection without silent rebase")
    view.load();var mismatch=programsData;mismatch["plugin"]="neighbor";replies.removeFirst()(["result":["revision":"song:2","data":mismatch]])
    try require(view.revision=="song:1","Another plugin's reply cannot replace this browser")
    view.load();replies.removeFirst()(["result":["revision":"song:3","data":programsData]])
    view.apply(dryRun:false);replies.removeFirst()(["result":["revision":"song:4","data":["plugin":"unit-synth","program":["id":"vst3:7:18:2"],"loaded":true]]])
    try require(view.revision=="song:4" && view.status.stringValue.contains("Undo effect change"),"Successful program loads report history behavior")
    view.load();var empty=programsData;empty["programs"]=[[String:Any]]();replies.removeFirst()(["result":["revision":"song:5","data":empty]])
    try require(!view.loadButton.isEnabled && view.status.stringValue.contains("no standard"),"Empty vendor catalogs explain native preset alternative")
    let editor=PluginEditor(frame:.zero);try require(!editor.programsButton.isEnabled,"Empty rack disables program browser")
    editor.update(model:PatternModel(["nativePlugins":[["name":"Synth","instanceID":"unit-synth"]]]),values:[])
    var slot = -1;editor.onPrograms={slot=$0};editor.programsButton.invoke();try require(slot==0,"Programs opens for visible plugin")
  }
}
