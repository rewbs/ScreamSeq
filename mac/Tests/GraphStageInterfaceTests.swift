import AppKit
extension InterfaceTests {
  static func graphStageChecks() throws {
    let stage=SignalStagePresentation()
    func copy(_ graph:String,_ role:String,_ x:Double,_ y:Double)->SignalCanvasNode {var n=SignalCanvasNode(id:"graph:track:\(role):\(graph)",title:graph,detail:role,kind:"audio",x:x,y:y);n.role=role;return n}
    let nodes=[copy("A","Persistent",500,60),copy("B","Persistent",100,200),copy("A","Row",50,450),copy("C","Ordinary",850,70)]
    func activity(_ graph:String,_ role:String,_ order:Int,_ tail:Bool=false)->[String:Any] {["graph":graph,"target":"track","role":role,"order":order,"tail":tail]}
    let initial=[activity("A","persistent",2),activity("B","persistent",1),activity("C","ordinary",3)]
    let first=stage.update(nodes:nodes,activity:initial,playing:true,names:["track":"Bass"])!
    let persistent=first.first{$0.role=="Persistent"}!,row=first.first{$0.role=="Row"}!
    try require(first.count==3 && persistent.members==[nodes[0].id,nodes[1].id] && persistent.audible==[nodes[1].id,nodes[0].id],"Occupied row/persistent/ordinary stages preserve stable copy identities while audible order comes from host rank")
    try require(persistent.summary=="Audible order: B → A" && persistent.paths.count==3 && row.paths.isEmpty && row.inactive==[nodes[2].id],"Transient stage paths show the actual ranked audible chain and dim an inactive configured copy")
    try require(persistent.rect.contains(nodes[0].rect) && persistent.rect.contains(nodes[1].rect) && persistent.titleRect.minY>max(nodes[0].rect.maxY,nodes[1].rect.maxY),"Derived hulls retain all manually positioned cards and place stage labels in the existing row gap")
    let generation=stage.generation
    try require(stage.update(nodes:nodes,activity:Array(initial.reversed()),playing:true,names:["track":"Bass","unused":"Other"])==nil && stage.generation==generation,"Equivalent activity polls reuse cached bounds and paths instead of preparing geometry during drawing")
    let canvas=SignalCanvas(frame:NSRect(x:0,y:0,width:1200,height:700));canvas.update(nodes,edges:[]);canvas.updateStages(first);canvas.selected=nodes[0].id
    let next=stage.update(nodes:nodes,activity:[activity("A","persistent",1),activity("B","persistent",2),activity("C","ordinary",3)],playing:true,names:["track":"Bass"])!
    let reordered=next.first{$0.role=="Persistent"}!,dirty=canvas.stageDirtyRects(for:next)
    try require(reordered.rect==persistent.rect && reordered.audible==[nodes[0].id,nodes[1].id] && dirty.count==2 && dirty.allSatisfy{$0.intersects(persistent.rect)},"A host reorder changes only the affected stage geometry while its occupied bounds stay fixed")
    let crop=NSRect(x:0,y:0,width:1200,height:700)
    func bitmap()->NSBitmapImageRep {NSBitmapImageRep(bitmapDataPlanes:nil,pixelsWide:1200,pixelsHigh:700,bitsPerSample:8,samplesPerPixel:4,hasAlpha:true,isPlanar:false,colorSpaceName:.deviceRGB,bytesPerRow:0,bitsPerPixel:0)!}
    func paint(_ rep:NSBitmapImageRep,_ rect:NSRect) {NSGraphicsContext.saveGraphicsState();defer{NSGraphicsContext.restoreGraphicsState()};NSGraphicsContext.current=NSGraphicsContext(bitmapImageRep:rep);NSGraphicsContext.current!.cgContext.clip(to:rect);canvas.draw(rect)}
    let partial=bitmap();paint(partial,crop);canvas.updateStages(next);for rect in dirty{paint(partial,rect)}
    let full=bitmap();paint(full,crop)
    try require(Data(bytes:partial.bitmapData!,count:partial.bytesPerRow*partial.pixelsHigh)==Data(bytes:full.bitmapData!,count:full.bytesPerRow*full.pixelsHigh),"Repainting only a reordered stage matches a full bitmap, including paths, labels and inactive card colors")
    try require(canvas.selected==nodes[0].id && canvas.nodes.map(\.rect)==nodes.map(\.rect) && canvas.edges.isEmpty,"Stage activity changes no editable route, node coordinate or selection")
    let tail=stage.update(nodes:nodes,activity:[activity("A","persistent",0,true)],playing:true,names:["track":"Bass"])!
    try require(tail.first{$0.role=="Persistent"}?.summary.contains("Tails: A")==true && tail.first{$0.role=="Persistent"}?.paths.isEmpty==true,"Tail-only copies remain explicitly visible without inventing an audible rank")
    let stopped=stage.update(nodes:nodes,activity:[],playing:false,names:["track":"Bass"])!
    try require(stopped.allSatisfy{$0.summary.hasPrefix("Stopped") && $0.paths.isEmpty && $0.inactive.isEmpty},"Stopped transport shows configuration without diagnosing silence or inventing playback order")
    try require(stage.update(nodes:[],activity:initial,playing:true,names:[:])?.isEmpty==true,"A graph with no assigned copies gains no empty stage boxes")

    try graphEmptyViewportChecks()

    let editor=SignalGraphEditor(frame:.zero)
    let song:[String:Any]=["library":[["id":"A","number":1,"name":"Delay","nodes":[]],["id":"B","number":2,"name":"Filter","nodes":[]]],"commands":[["target":"track","graph":"A","kind":"start"],["target":"track","graph":"B","kind":"start"]],"layout":[["node":nodes[0].id,"x":500.0,"y":60.0],["node":nodes[1].id,"x":100.0,"y":200.0]],"mixer":["buses":[["id":"track","name":"Bass","kind":"track","output":"master"],["id":"master","name":"Master","kind":"master","output":""]]]]
    var writes=0;editor.onRequest={_,_,_ in writes+=1};editor.update(song)
    editor.selectedID=nodes[0].id;editor.canvas.selected=nodes[0].id
    let saved=editor.canvas.nodes.map{($0.id,$0.rect)},edges=editor.canvas.edges.map{$0.source+"→"+$0.target}
    editor.showActivity(initial,playing:true)
    try require(editor.canvas.stages.count==1 && editor.canvas.stages[0].audible==[nodes[1].id,nodes[0].id] && writes==0,"Host activity projects only occupied visible stages without a song or API mutation")
    try require(editor.canvas.nodes.count==saved.count && zip(editor.canvas.nodes,saved).allSatisfy{$0.0.id==$0.1.0 && $0.0.rect==$0.1.1} && editor.canvas.edges.map{$0.source+"→"+$0.target}==edges && editor.selectedID==nodes[0].id,"Audible order presentation preserves saved layout, editable endpoints and current selection")
  }
}

extension InterfaceTests {
  static func graphEmptyViewportChecks() throws {
    let editor=SignalGraphEditor(frame:NSRect(x:0,y:0,width:1100,height:700))
    let host=NSWindow(contentRect:editor.frame,styleMask:[.borderless],backing:.buffered,defer:false);host.contentView=editor
    let song:[String:Any]=["layout":[["node":"near","x":40.0,"y":60.0],["node":"far","x":4000.0,"y":60.0],["node":"master","x":4600.0,"y":60.0]],"mixer":["buses":[["id":"near","name":"Near","kind":"track","output":"master"],["id":"far","name":"Far","kind":"track","output":"master"],["id":"master","name":"Master","kind":"master","output":""]]]]
    var writes=0;editor.onRequest={_,_,_ in writes+=1};editor.update(song);editor.layoutSubtreeIfNeeded();editor.scroll.magnification=1;editor.canvas.scroll(.zero)
    try require(!editor.recoverEmptyViewport(),"A context with a visible card preserves its viewport and zoom")
    editor.filter.selectItem(at:2);editor.changeFilter();editor.canvas.scroll(.zero)
    let coordinates=editor.canvas.nodes.map(\.rect)
    try require(editor.canvas.nodes.allSatisfy{!$0.rect.intersects(editor.scroll.documentVisibleRect)},"Filtered fixture retains saved cards entirely outside the old viewport")
    try require(editor.recoverEmptyViewport() && editor.canvas.nodes.contains{$0.rect.intersects(editor.scroll.documentVisibleRect)},"Wholly empty filtered/floating viewport reveals an existing card without a manual Home command")
    try require(editor.canvas.nodes.map(\.rect)==coordinates && editor.scroll.magnification==1 && writes==0,"Empty-viewport recovery keeps manual layout, working zoom, and document history intact")
    let recovered=editor.scroll.contentView.bounds.origin
    try require(!editor.recoverEmptyViewport() && editor.scroll.contentView.bounds.origin==recovered,"Repeated resize/layout does not reframe a useful viewport")
    editor.canvas.scroll(.zero);editor.update(song)
    try require(editor.scroll.contentView.bounds.origin == .zero,"Passive graph data refresh does not snap back from an intentional empty-space pan")
    editor.hasDraft=true
    try require(!editor.recoverEmptyViewport(),"An active graph edit prevents automatic viewport recovery")
  }
}
