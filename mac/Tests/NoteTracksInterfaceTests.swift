import AppKit
extension InterfaceTests {
  static func noteTrackModel() -> PatternModel {
    PatternModel(["channels":8,"volumeLetters":[".","v"],"revisionToken":"pinned-song", "tracks": (0..<8).map { ["index":$0,"id":"n\($0 + 1)","mute":$0 == 1] as [String:Any] },
      "trackLayout":["maximumColumns":12,"noteTracks":[["id":"n20","name":"Chords","channels":[0,1,2],"color":0x448877]],"destinations":[["id":"n19","name":"Master"]]]])
  }
  static func noteTrackChecks() throws {
    let model = noteTrackModel(), grid = PatternView()
    grid.model = model
    try require(grid.headerHeight == 58 && grid.muted == [1] && model.noteTrackByChannel[2]?.id == "n20" && model.noteTrackByChannel[3] == nil, "Grouped columns map to shared track identity and persistent mute")
    let tools = PatternToolsPanel(frame:.zero);tools.onContext = { (model,["cursorChannel":1]) }
    tools.scope.selectItem(at:4)
    let params = try tools.parameters()
    try require(params["scope"] as? String == "note-track" && params["track"] as? String == "n20" && params["startChannel"] == nil,"Pattern tools address the whole logical note track by ID")
    let create = NoteTrackEditor(model:model,channels:[],creating:true)
    create.nameField.stringValue = "New chords"; create.countField.stringValue = "5"
    var requests = [(String,[String:Any])](), completions = [([String:Any])->Void]()
    create.onRequest = { method,params,reply in requests.append((method,params));completions.append(reply) }
    create.apply();try require(requests.isEmpty,"Creating columns obeys remaining format capacity")
    create.countField.stringValue = "3";create.destination.selectItem(at:1);create.apply();create.apply()
    try require(requests.count == 1 && create.pending && requests[0].0 == "track.create" && requests[0].1["columns"] as? Int == 3 && requests[0].1["output"] as? String == "n19", "Create sends one bounded request to selected output")
    completions[0](["error":["code":-32001,"message":"Song changed"]])
    try require(!create.pending && !create.completed,"Failed create can be retried without false success")
    create.apply();try require(requests[1].1["expectedRevision"] as? String == "pinned-song","Retries keep their original document revision")
    completions[1](["result":["changed":true]]);create.apply()
    try require(requests.count == 2 && create.completed,"Completed creation cannot accidentally duplicate a track")
    let group = NoteTrackEditor(model:model,channels:[3,4],creating:false)
    var captured:[String:Any] = [:]
    group.onRequest = { method,params,_ in captured=params }
    group.apply();try require(captured["channels"] as? [Int] == [3,4] && captured["expectedRevision"] as? String == "pinned-song", "Grouping uses pinned column selection")
    grid.frame = NSRect(x:0,y:0,width:960,height:510)
    var painted = model
    for (row,channel,note) in [(0,0,49),(0,1,53),(0,2,56),(4,0,51),(4,1,54),(4,2,58)] { painted.replaceCell(row,channel,with:[UInt8(note),1,1,48,0,0]) }
    grid.model=painted;grid.cursorChannel=2;grid.cursorRow=4;grid.playRow=0;grid.playPattern=0
    guard let image=grid.offscreenImage() else { throw InterfaceFailure(message:"Metal offscreen grid failed") }
    try require(image.width==1920 && image.height==1020 && grid.submittedFrames==0,"Offscreen Metal snapshot uses no presented frames")
    if let index=CommandLine.arguments.firstIndex(of:"--snapshots"),index+1<CommandLine.arguments.count {
      let folder=URL(fileURLWithPath:CommandLine.arguments[index+1],isDirectory:true)
      try FileManager.default.createDirectory(at:folder,withIntermediateDirectories:true)
      let bitmap=NSBitmapImageRep(cgImage:image)
      try bitmap.representation(using:.png,properties:[:])!.write(to:folder.appendingPathComponent("NoteTrackGrid.png"))
      grid.firstChannel=1
      let clipped=NSBitmapImageRep(cgImage:grid.offscreenImage()!)
      try clipped.representation(using:.png,properties:[:])!.write(to:folder.appendingPathComponent("NoteTrackGridScrolled.png"))
    }
    var flat = model;flat.noteTracks=[];flat.noteTrackByChannel=[:];flat.mutedColumns=[];grid.model=flat
    try require(grid.headerHeight==36 && grid.muted.isEmpty,"Ungrouped documents restore flat grid geometry and mute state")
  }
}
