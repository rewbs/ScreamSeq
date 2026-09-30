import AppKit
extension InterfaceTests {
  static func navigationChecks() throws {
    let input=PatternView();input.model=PatternModel(["rows":64,"channels":1,"samples":[["index":1],["index":2],["index":3]]]);input.model.cells=[49,3,0,0,0,0];input.column=1
    let inputToken=input.contextToken
    key(input,36,"\r");try require(input.instrument==3 && input.contextToken != inputToken,"Return uses the instrument under the cursor and invalidates input context")
    key(input,126,"",flags:.option);try require(input.instrument==2,"Option Up selects previous instrument")
    key(input,125,"",flags:.option);try require(input.instrument==3,"Option Down selects next instrument")
    key(input,75,"/");try require(input.octave==3,"Keypad divide lowers octave using OpenMPT convention")
    key(input,67,"*");try require(input.octave==4,"Keypad multiply raises octave")
    key(input,124,"",flags:.option);try require(input.octave==5,"Laptop octave shortcut")
    // Cut must reach the view through the responder chain and first place a
    // complete clipboard payload on the pasteboard, including precise hits.
    input.model.preciseNotes = PatternModel(["preciseNotes":[["channel":0,"position":8192,"note":49,"instrument":3,"velocity":90]]]).preciseNotes
    let savedClipboard=(NSPasteboard.general.pasteboardItems ?? []).map { item in item.types.reduce(into:[NSPasteboard.PasteboardType:Data]()) {if let data=item.data(forType:$1) {$0[$1]=data}} }
    var clipboardCount = -1
    defer {if NSPasteboard.general.changeCount==clipboardCount {
      let items=savedClipboard.map { values -> NSPasteboardItem in let item=NSPasteboardItem();for (type,data) in values {item.setData(data,forType:type)};return item }
      NSPasteboard.general.clearContents();NSPasteboard.general.writeObjects(items)
    }}
    var cut=[String:Any]();input.onRowShift={cut=$0};input.cut(nil)
    let deadline=Date().addingTimeInterval(2)
    while cut.isEmpty && Date()<deadline {RunLoop.current.run(until:Date().addingTimeInterval(0.01))}
    clipboardCount=NSPasteboard.general.changeCount
    let clipboard=NSPasteboard.general.string(forType:.string) ?? ""
    try require(cut["operation"] as? String=="clear" && clipboard.contains("notes") && clipboard.contains("8192"),"Cut copies precise events before requesting guarded clear")
    let grid = PatternView()
    grid.model = PatternModel(["patterns":[["index":0,"rows":64],["index":2,"rows":8]],"channels":8])
    grid.cursorRow=31;grid.cursorChannel=3;grid.column=4;grid.playPattern=0;grid.playRow=48
    var changes = [Bool]()
    grid.onFollowChanged = { changes.append($0) }
    let token=grid.contextToken, original=grid.navigation
    let base: [String:Any] = ["expectedRevision":"song","expectedContext":token]
    func prepare(_ extra: [String:Any], contextToken: String? = nil) throws -> EditorNavigation {
      try original.prepared(base.merging(extra){_,new in new},revision:"song",contextToken:contextToken ?? token,patterns:grid.model.patterns,channels:8)
    }
    let short = try prepare(["pattern":2])
    try require(short.pattern==2 && short.row==7 && !short.following && short.column==4, "Changing patterns clamps an omitted row and defaults to independent editing")
    let moved = try prepare(["row":5,"channel":4,"following":false])
    grid.navigate(moved,clearSelection:true)
    try require(grid.cursorRow==5 && grid.cursorChannel==4 && grid.playRow==48 && grid.playPattern==0 && changes==[false],"Edit navigation and following state leave the playhead untouched")
    grid.isFollowing=false;try require(changes.count==1,"No redundant follow UI notifications")
    grid.isFollowing=true
    let event=NSEvent(cgEvent:CGEvent(scrollWheelEvent2Source:nil,units:.pixel,wheelCount:1,wheel1:-20,wheel2:0,wheel3:0)!)!
    grid.scrollWheel(with:event)
    try require(!grid.isFollowing && changes==[false,true,false],"Manual scrolling immediately updates follow observers")
    let oldToken=grid.contextToken;grid.selectRegion(from:(1,1),to:(8,4))
    try require(grid.contextToken != oldToken,"Changing selection invalidates the cursor token")
    let selection=grid.automationSelection
    grid.navigate(grid.navigation,clearSelection:false)
    try require(grid.automationSelection==selection,"A no-op/follow-only navigation preserves the selection")
    for extra: [String:Any] in [["row":true],["row":1.5],["row":Double.nan],["row":-1],["row":64],["channel":8],["column":5],["pattern":1],["following":1],["following":"false"],["typo":0]] {
      do { _ = try prepare(extra);throw InterfaceFailure(message:"Malformed navigation accepted") }
      catch let failure as EditorNavigation.Failure { try require(failure.code == -32602,"Invalid navigation is rejected without dispatch") }
    }
    do { _ = try prepare(["row":1],contextToken:"new selection");throw InterfaceFailure(message:"Stale cursor accepted") }
    catch let failure as EditorNavigation.Failure { try require(failure.code == -32001,"Stale cursor has a revision error") }
    try require(original.row==31 && grid.playRow==48,"Preparing rejected navigation changes no UI or playback state")
  }
}
