import AppKit

extension SignalGraphEditor {
  static let graphClipboardType=NSPasteboard.PasteboardType("org.screamseq.graph-nodes.v1")
  var graphSelectionIDs:[String] {canvas.selection.sorted().filter{id in nodes.contains{$0["id"] as? String==id} || processingGroups.contains{$0["id"] as? String==id}}}
  var graphClipboardReason:String? {graphID==nil ? "Enter a reusable definition to copy or paste its nodes":nil}
  func copyGraphSelection(cutting:Bool=false) {
    guard let graphID else{status.stringValue=graphClipboardReason ?? "Enter a definition";return}
    guard !hasDraft,!loading else{status.stringValue="Finish the current graph edit before copying";return}
    let ids=graphSelectionIDs;guard !ids.isEmpty else{chooseVisibleNode(title:"Choose a node to copy"){[weak self] _ in self?.copyGraphSelection(cutting:cutting)};return}
    let document=projectionDocument,capturedRevision=revision
    func store(_ result:[String:Any]) {
      guard self.projectionDocument==document,let data=result["data"] as? [String:Any],let fragment=data["fragment"] as? [String:Any],let bytes=try? JSONSerialization.data(withJSONObject:["version":1,"document":document,"fragment":fragment])else{return}
      self.graphPasteboard.clearContents();self.graphPasteboard.setData(bytes,forType:Self.graphClipboardType);self.status.stringValue=cutting ? "Cut nodes · paste into a definition · Undo restores originals":"Copied nodes and their internal cables"
    }
    if cutting{mutate("graph.selection.cut",["graph":graphID,"nodes":ids],after:store)}
    else{requestGraph("graph.selection.copy",["graph":graphID,"nodes":ids],document:document){[weak self] reply in guard let self else{return};guard self.revision==capturedRevision,let result=reply["result"] as? [String:Any],result["revision"] as? String==capturedRevision else{self.showExternalFailure((reply["error"] as? [String:Any])?["message"] as? String ?? "The song changed; retry this operation");return};store(result)}}
  }
  func duplicateGraphSelection() {
    guard let graphID else{duplicateSongObject();return}
    guard !hasDraft,!loading else{status.stringValue="Finish the current graph edit before duplicating";return}
    let ids=graphSelectionIDs;guard !ids.isEmpty else{chooseVisibleNode(title:"Choose a node to duplicate"){[weak self] _ in self?.duplicateGraphSelection()};return}
    let selected=canvas.nodes.filter{ids.contains($0.id)},x=(selected.map(\.x).min() ?? 0)+36,y=(selected.map(\.y).min() ?? 0)+36
    var p:[String:Any]=["graph":graphID,"nodes":ids,"x":x,"y":y];if let processingGroupID{p["parent"]=processingGroupID}
    mutate("graph.selection.duplicate",p){[weak self] result in self?.revealPastedNode(result)}
  }
  func duplicateSongObject() {
    guard !hasDraft,!loading else{status.stringValue="Finish the current graph edit before duplicating";return}
    let ids=canvas.selection.isEmpty ? selectedID.map{[$0]} ?? []:Array(canvas.selection)
    guard ids.count==1,let id=ids.first else{
      if ids.isEmpty{chooseTarget(title:"Duplicate a processor or modulation source",entries:canvas.nodes.compactMap{node in guard songNodePlugin[node.id] != nil || songSource(node.id) != nil else{return nil};return .init(id:node.id,title:node.title,detail:node.detail,keywords:node.id)}){[weak self] id in guard let self else{return};self.selectedID=id;self.canvas.selectNodes([id]);self.duplicateSongObject()}}
      else{status.stringValue="Duplicate one song processor or source at a time; copy a selection inside its reusable definition"}
      return
    }
    let node=canvas.nodes.first{$0.id==id},position:[String:Any]=["x":min(100000,(node?.x ?? 0)+36),"y":min(100000,(node?.y ?? 0)+36)]
    if let plugin=songNodePlugin[id] {
      mutate("plugin.duplicate",["plugin":plugin,"position":position]){[weak self] result in
        guard let self,let identity=(result["data"] as? [String:Any])?["plugin"] as? String else{return}
        self.selectedID="plugin:"+identity;self.canvas.selected=self.selectedID;self.revealAddedNode();self.inspect();self.frameSelection();self.status.stringValue="Independent processor created · assign notes or connect its audio"
      };return
    }
    if var source=songSource(id) {
      source.removeValue(forKey:"id");source.removeValue(forKey:"template");source["x"]=position["x"];source["y"]=position["y"]
      mutate("graph.song.source.add",["source":source]){[weak self] result in
        guard let self,let identity=(result["data"] as? [String:Any])?["node"] as? String else{return}
        self.selectedID="source:"+identity;self.canvas.selected=self.selectedID;self.revealAddedNode();self.inspect();self.frameSelection();self.status.stringValue="Independent modulation source created · existing cables retained"
      };return
    }
    status.stringValue="Select a processor or modulation source to duplicate; use Make independent for a reusable copy"
  }
  func pasteGraphSelection() {
    guard let graphID else{status.stringValue=graphClipboardReason ?? "Enter a definition";return}
    guard !hasDraft,!loading else{status.stringValue="Finish the current graph edit before pasting";return}
    guard let bytes=graphPasteboard.data(forType:Self.graphClipboardType),bytes.count<=32*1024*1024,let payload=(try? JSONSerialization.jsonObject(with:bytes)) as? [String:Any],payload["version"] as? Int==1,let fragment=payload["fragment"] as? [String:Any]else{status.stringValue="Copy graph nodes first";return}
    let needed=Set((fragment["nodes"] as? [[String:Any]] ?? []).flatMap{$0["envelopes"] as? [[String:Any]] ?? []}.compactMap{$0["pattern"] as? String}).sorted()
    let sameDocument=payload["document"] as? String==projectionDocument
    let document=projectionDocument,context=viewContext
    func commit(_ mapping:[[String:Any]]) {guard self.projectionDocument==document,self.viewContext==context else{return};var p:[String:Any]=["graph":graphID,"fragment":fragment,"patternMap":mapping,"x":max(0,self.scroll.documentVisibleRect.minX+40),"y":max(0,self.scroll.documentVisibleRect.minY+40)];if let parent=self.processingGroupID{p["parent"]=parent};self.mutate("graph.selection.paste",p){[weak self] result in self?.revealPastedNode(result)}}
    if sameDocument || needed.isEmpty {commit(needed.map{["source":$0,"target":$0]});return}
    let patterns=data["patterns"] as? [[String:Any]] ?? []
    func choose(_ remaining:ArraySlice<String>,_ mapping:[[String:Any]]) {
      guard let source=remaining.first else{commit(mapping);return}
      self.chooseTarget(title:"Map copied pattern envelope \(source)",entries:patterns.compactMap{p in guard let id=p["id"] as? String else{return nil};return .init(id:id,title:"\(p["index"] ?? "") · \(p["name"] as? String ?? "Pattern")",detail:"Use these copied envelope points in this pattern",keywords:id)}){target in choose(remaining.dropFirst(),mapping+[["source":source,"target":target]])}
    }
    choose(needed[...],[])
  }
  func revealPastedNode(_ result:[String:Any]) {guard let node=(result["data"] as? [String:Any])?["node"] as? String,!node.isEmpty else{return};selectedID=node;canvas.selected=node;inspect();frameSelection();status.stringValue="Pasted independent nodes · external cables were not duplicated"}
}
