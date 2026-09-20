import AppKit
extension InterfaceTests {
  static func navigationChecks() throws {
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
