import AppKit
extension InterfaceTests {
  static var performanceData:[String:Any] {
    ["pattern":0,"rows":64,"columns":[["channel":0,"count":2]],
     "bindings":[["id":1,"plugin":"gain","parameter":7,"name":"Filter motion","resolved":true,"canSlide":true]],
     "commands":[["channel":0,"track":"n2","position":16384,"duration":98304,"column":1,"kind":"parameter-slide","binding":1,"value":0.75]]]
  }
  static func performanceModel()->PatternModel {
    PatternModel(["channels":3,"rows":64,"extraEffectColumns":[2,0,8],
      "performanceCommands":performanceData["commands"]!,"nativePlugins":[["instanceID":"gain","name":"Fixture gain"]],
      "patterns":[["index":0,"rows":64]]])
  }
  static func patternPerformanceFixture()->PatternPerformanceEditor {
    let editor=PatternPerformanceEditor(frame:.zero);editor.onContext={(performanceModel(),0,0,6)}
    editor.onRequest={method,_,reply in
      reply(["result":["revision":"song:1","data":method=="pattern.performance.get" ? performanceData as Any : [["id":7,"name":"Gain","canSlide":true]] as Any]])
    };editor.capture();editor.onRequest=nil;return editor
  }
  static func patternPerformanceChecks() throws {
    let editor=PatternPerformanceEditor(frame:.zero);var calls=[(String,[String:Any])](),replies=[([String:Any])->Void]()
    editor.onContext={(performanceModel(),0,0,6)}
    editor.onRequest={method,params,reply in calls.append((method,params));replies.append(reply)}
    editor.capture();editor.capture()
    try require(calls.count==1 && editor.pending && !editor.applyButton.isEnabled,"Pending command read cannot duplicate or apply")
    replies.removeFirst()(["result":["revision":"song:1","data":performanceData]])
    try require(calls.count==2 && calls.last!.0=="plugin.parameters.get","Binding resolves the selected plugin parameter list")
    replies.removeFirst()(["result":["revision":"song:1","data":[["id":7,"name":"Gain","canSlide":true]]]])
    try require(editor.effectColumn.selectedTag()==1 && editor.value.stringValue=="75" && editor.offset.stringValue=="0.25" && editor.duration.stringValue=="1.5","Command editor recovers fractional timing and full value")
    editor.value.stringValue="NaN";editor.apply(dryRun:false)
    try require(calls.count==2,"Nonfinite values cannot reach API")
    editor.value.stringValue="90";editor.apply(dryRun:true);editor.apply(dryRun:false)
    let request=calls.last!.1,command=(request["commands"] as! [[String:Any]])[0]
    try require(calls.count==3 && request["expectedRevision"] as? String=="song:1" && request["dryRun"] as? Bool==true &&
      command["track"]==nil && command["column"] as? Int==1 && command["position"] as? Int==16384 && command["duration"] as? Int==98304,
      "Preview sends exact native timing, binding and captured revision without duplicate submission")
    replies.removeFirst()(["error":["message":"Song changed"]])
    try require(editor.revision=="song:1" && editor.value.stringValue=="90","Stale rejection retains the user's draft and old revision")
    editor.kind.selectItem(at:4);editor.apply(dryRun:false)
    try require((calls.last!.1["commands"] as? [[String:Any]])?.isEmpty==true && calls.last!.1["bindings"]==nil,"Clear removes only selected cell without modifying bindings")
    replies.removeFirst()(["result":["revision":"song:2","data":[:]]])
    try require(editor.commands.isEmpty && editor.revision=="song:2","Saved clear updates captured command data")
    editor.kind.selectItem(at:3);editor.changedKind();editor.value.stringValue="-7.125";editor.pitchRange.stringValue="12";editor.duration.stringValue="0.125";editor.apply(dryRun:true)
    let bend=(calls.last!.1["commands"] as! [[String:Any]])[0]
    try require(bend["kind"] as? String=="pitch-slide" && bend["binding"] as? Int==0 && bend["pitchRange"] as? Int==12 && bend["duration"] as? Int==8192 && calls.last!.1["bindings"]==nil,"Pitch editor retains signed semitones, fine duration and explicit plugin wheel range without inventing a parameter binding")
    replies.removeFirst()(["result":["revision":"song:2","data":[:]]])
    let grid=PatternView();grid.frame=NSRect(x:0,y:0,width:720,height:400);grid.model=performanceModel();grid.column=4
    key(grid,124,"");try require(grid.column==5 && grid.cursorChannel==0,"Right arrow enters first extra effect subcolumn")
    key(grid,124,"");try require(grid.column==6 && grid.currentCommandHelp.contains("Slide binding 1"),"Extra command has contextual timing help")
    key(grid,124,"");try require(grid.column==0 && grid.cursorChannel==1,"Arrow leaves the final extra subcolumn")
    grid.cursorChannel=2;grid.column=12;grid.revealCursor()
    try require(grid.channelX(2)+grid.fieldOffset(12)+grid.fieldWidth(12)<=Float(grid.bounds.width),"Last effect remains reachable inside a very wide track")
    var opened=0,cleared=[Int]();grid.onNativeEffect={opened += 1};grid.onClearNativeEffect={cleared=[$0,$1,$2]}
    key(grid,36,"\r");key(grid,51,"")
    try require(opened==1 && cleared==[0,2,7],"Return and Delete operate on the precise extra effect cell")
    grid.cursorChannel=0;grid.column=6;grid.firstChannel=0;grid.revealCursor()
    if let argument=CommandLine.arguments.firstIndex(of:"--snapshots"),argument+1<CommandLine.arguments.count {
      let directory=URL(fileURLWithPath:CommandLine.arguments[argument+1],isDirectory:true)
      try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true)
      let snapshot=PatternView();snapshot.frame=grid.frame;snapshot.model=performanceModel();snapshot.column=6
      guard let image=snapshot.offscreenImage(),let bytes=NSBitmapImageRep(cgImage:image).representation(using:.png,properties:[:]) else{throw NSError(domain:"Offscreen effect grid",code:1)}
      try bytes.write(to:directory.appendingPathComponent("PatternEffectsGrid.png"))
    }
    let navigation=EditorNavigation()
    let moved=try navigation.prepared(["expectedRevision":"r","expectedContext":navigation.token,"channel":2,"column":12],
      revision:"r",contextToken:navigation.token,patterns:[["index":0,"rows":64]],channels:3,extraColumns:[2,0,8])
    try require(moved.column==12,"Agent context navigation reaches every effect subcolumn")
  }
}
