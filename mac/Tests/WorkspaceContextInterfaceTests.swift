import AppKit

extension InterfaceTests {
  static func workspaceContextChecks() throws {
    let pane=WorkspaceContextPanel(frame:NSRect(x:0,y:0,width:200,height:700))
    let originalButtons=pane.buttons
    let choices:[WorkspaceContextPanel.Choice]=[.init(id:"pattern-a",title:"Pattern 0"),.init(id:"stop",title:"Stop",enabled:false)]
    pane.choices(pane.patterns,choices,selected:"pattern-a",stamp:"song:1")
    pane.update(editor:"pattern",title:"PATTERN",actions:[.init(id:"notes",title:"Precise notes…")],available:true,stamp:"song:1")
    let item=pane.patterns.item(at:0)!
    pane.choices(pane.patterns,choices,selected:"pattern-a",stamp:"song:2")
    try require(pane.patterns.item(at:0) === item && !pane.patterns.item(at:1)!.isEnabled,
      "Refreshing context choices retains native menu items and disabled order markers")
    var selections=[String](),actions=[String]()
    pane.onChoice={kind,id,stamp in selections.append("\(kind)/\(id)/\(stamp)")};pane.onAction={actions.append($0)}
    pane.menuWillOpen(pane.patterns.menu!)
    pane.choices(pane.patterns,[.init(id:"replacement",title:"Pattern 0")],selected:"replacement",stamp:"song:3")
    pane.update(editor:"samples",title:"SAMPLES",actions:[],available:true,stamp:"song:3")
    NSApp.sendAction(pane.patterns.action!,to:pane.patterns.target,from:pane.patterns)
    try require(pane.editor=="pattern" && selections==["pattern/pattern-a/song:2"],
      "A tracked context menu keeps its original stable identity and revision while the model changes")
    pane.menuDidClose(pane.patterns.menu!)
    pane.buttons[0].performClick(nil)
    try require(actions==["notes"],"Context shortcuts dispatch the displayed action")
    pane.update(editor:"samples",title:"SAMPLES",actions:[.init(id:"record",title:"Record audio…",enabled:false)],available:true,stamp:"song:3")
    pane.buttons[0].invoke()
    try require(zip(originalButtons,pane.buttons).allSatisfy{$0 === $1} && pane.selectors.isHidden && !pane.sampleScroll.isHidden && actions==["notes"],
      "Changing editors retains controls, updates visible tools and prevents disabled action dispatch")
    let samples=(0..<80).map{WorkspaceContextPanel.Choice(id:"sample-\($0)",title:"Sample \($0)")}
    pane.sampleCatalogue(samples,selected:"sample-70",stamp:"song:3")
    try require(pane.samples.numberOfRows==80 && pane.samples.selectedRow==70 && selections.count==1,
      "Sample context covers the entire catalogue and a model refresh never performs a selection action")
    pane.samples.selectRowIndexes(IndexSet(integer:71),byExtendingSelection:false)
    try require(selections.last=="sample/sample-71/song:3","Sample selection uses stable identity beyond the former sixteen-item sidebar limit")
    pane.update(editor:"samples",title:"SAMPLES",actions:[],available:false,stamp:"song:3")
    pane.samples.selectRowIndexes(IndexSet(integer:72),byExtendingSelection:false)
    try require(selections.last=="sample/sample-71/song:3" && !pane.samples.isEnabled,"Busy context controls cannot navigate to another target")

    let workspace=DockWorkspace(patternView:NSView())
    workspace.register(WorkspacePanel(id:"notes",title:"Notes",view:NSView()),location:"right")
    var visibility=[Bool]()
    workspace.onContextVisibility={visibility.append($0)}
    workspace.setContextPanelVisible(false);let hidden=workspace.state
    try require(hidden["contextPanelVisible"] as? Bool==false && visibility==[false],"Context visibility participates in saved workspace presentation")
    workspace.setContextPanelVisible(false)
    try require(visibility==[false],"An unchanged context visibility does not rebuild the workspace")
    workspace.setContextPanelVisible(true)
    try require(workspace.restore(hidden) && !workspace.contextPanelVisible,"Restoring a hidden layout restores context visibility")
    for invalid:Any in [0,1,"false",NSNull()] {
      var malformed=hidden;malformed["contextPanelVisible"]=invalid;malformed["locations"]=["notes":"hide"]
      try require(!workspace.restore(malformed) && !workspace.contextPanelVisible && workspace.locations["notes"]=="right",
        "Malformed context visibility rejects before changing any panel placement")
    }
    var legacy=hidden;legacy.removeValue(forKey:"contextPanelVisible")
    try require(workspace.restore(legacy) && workspace.contextPanelVisible,"Layouts predating the context pane restore its visible default")
  }
}
