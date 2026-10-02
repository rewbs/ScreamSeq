import AppKit

// A bounded, lazy projection of canonical song edits. Nothing in this cache is
// a new automation lane, DSP connection, or serializable processor.
final class GraphParameterProvenance {
  static let readOnlyReason="Existing automation sets the base value. Open its source to edit it; this reference wire cannot be patched or cut."
  var target:(plugin:String,parameter:UInt32)?
  var sources=[String:[String:Any]](),ordered=[String](),revision="",requestKey="",loadedKey=""
  var positions=[String:NSPoint]()
  var generation=0,total=0,offset=0,hasReturn=false,revealPending=false
  func reset(){generation+=1;positions=[:];target=nil;sources=[:];ordered=[];revision="";requestKey="";loadedKey="";total=0;offset=0;hasReturn=false;revealPending=false}
}
final class GraphProvenanceControls:NSView {
  let title=Theme.label("Existing automation",size:13,weight:.semibold)
  let summary=Theme.label("",size:11,color:Theme.muted)
  var onOpen:(()->Void)?,onActivity:(()->Void)?,onNext:(()->Void)?
  lazy var nextPage=ActionButton("Next sources"){[weak self] in self?.onNext?()}
  override init(frame:NSRect){super.init(frame:frame);summary.maximumNumberOfLines=0;summary.lineBreakMode = .byWordWrapping;summary.preferredMaxLayoutWidth=240;summary.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    let content=stack(.vertical,[title,summary,ActionButton("Edit source…",prominent:true){[weak self] in self?.onOpen?()},ActionButton("Inspect effective value…"){[weak self] in self?.onActivity?()},nextPage],spacing:8);content.stretchAcrossAxis();content.fill(self)
    isHidden=true
  }
  required init?(coder:NSCoder){fatalError()}
}
extension SignalGraphEditor {
  func configureProvenance(){
    provenanceControls.onNext={[weak self] in self?.nextProvenancePage()}
    rackControls.onSources={[weak self] plugin,parameter in self?.showParameterProvenance(plugin:plugin,parameter:parameter)}
    provenanceControls.onOpen={[weak self] in guard let self,let source=self.selectedProvenance else{return};self.openProvenanceSource(source)}
    provenanceControls.onActivity={[weak self] in guard let self,let source=self.selectedProvenance,let plugin=source["plugin"] as? String,let id=(source["parameter"] as? NSNumber)?.uint32Value else{return};self.rackControls.onActivity?(plugin,id)}
  }
  var selectedProvenance:[String:Any]? {
    if let selectedID,let source=provenance.sources[selectedID]{return source}
    if let index=canvas.selectedEdge,canvas.edges.indices.contains(index){return provenance.sources[canvas.edges[index].source]}
    return nil
  }
  func showParameterProvenance(plugin:String,parameter:UInt32){
    guard graphID==nil,!hasDraft else{return}
    if provenance.target?.plugin != plugin || provenance.target?.parameter != parameter {provenance.reset()}
    provenance.target=(plugin,parameter);provenance.revealPending=true;exposedParameters["plugin:"+plugin]=parameter
    graphFilterState.prepare(filterContext);graphFilterState.revealed.insert("plugin:"+plugin)
    rebuild();inspect();revealParameterProvenance()
  }
  private func revealParameterProvenance(){
    guard provenance.revealPending,!hasDraft,graphID==nil,let target=provenance.target,provenance.revision==revision,
      provenance.loadedKey=="\(projectionDocument)/\(revision)/\(target.plugin)/\(target.parameter)/\(provenance.offset)" else{return}
    provenance.revealPending=false
    let ids=Set(provenance.ordered+["plugin:"+target.plugin])
    graphFilterState.prepare(filterContext);graphFilterState.revealed.formUnion(ids)
    rebuild()
    let sources=canvas.nodes.filter{provenance.sources[$0.id] != nil}
    let selected=sources.count==1 ? sources.first?.id : "plugin:"+target.plugin
    selectedID=selected;canvas.selected=selected;canvas.selectedEdge=nil;inspect();configureConnectionInspector()
    let framed=sources.count==1 ? sources:canvas.nodes.filter{ids.contains($0.id)}
    if let first=framed.first{frameCanvas(framed.dropFirst().reduce(first.rect){$0.union($1.rect)}.insetBy(dx:-30,dy:-30),maximumScale:1)}
    window?.makeFirstResponder(canvas)
  }
  func hideParameterProvenance(){
    if selectedProvenance != nil{selectedID=provenance.target.map{"plugin:"+$0.plugin};canvas.selectedEdge=nil}
    provenance.reset();rebuild();inspect();configureConnectionInspector()
  }
  func nextProvenancePage(){
    guard provenance.total>64,provenance.target != nil else{return}
    if selectedProvenance != nil{selectedID=provenance.target.map{"plugin:"+$0.plugin};canvas.selectedEdge=nil}
    provenance.offset=provenance.offset+64<provenance.total ? provenance.offset+64:0
    provenance.generation+=1;provenance.sources=[:];provenance.ordered=[];provenance.revealPending=true;rebuild();inspect()
  }
  func loadParameterProvenance(){
    guard graphID==nil,let target=provenance.target,let request=onRequest else{return}
    let key="\(projectionDocument)/\(revision)/\(target.plugin)/\(target.parameter)/\(provenance.offset)"
    guard provenance.requestKey != key else{return}
    provenance.requestKey=key;let generation=provenance.generation,document=projectionDocument,capturedRevision=revision
    request("graph.provenance.get",["plugin":target.plugin,"parameter":target.parameter,"limit":64,"offset":provenance.offset]){[weak self] reply in
      guard let self,self.provenance.generation==generation,self.projectionDocument==document,self.revision==capturedRevision,
        self.provenance.target?.plugin==target.plugin,self.provenance.target?.parameter==target.parameter else{return}
      guard let result=reply["result"] as? [String:Any],let payload=result["data"] as? [String:Any] else{self.status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Existing automation sources unavailable";return}
      if let responseRevision=result["revision"] as? String,responseRevision != capturedRevision {self.provenance.requestKey="";return}
      let rows=payload["sources"] as? [[String:Any]] ?? []
      self.provenance.sources=[:];self.provenance.ordered=[]
      for row in rows {guard let key=row["key"] as? String else{continue};let id="provenance:"+key;self.provenance.sources[id]=row;self.provenance.ordered.append(id)}
      self.provenance.total=payload["total"] as? Int ?? rows.count;self.provenance.revision=capturedRevision;self.provenance.loadedKey=key
      if rows.isEmpty,self.provenance.offset>0{self.provenance.offset=0;self.loadParameterProvenance();return}
      self.rebuild();self.inspect();self.configureConnectionInspector();self.revealParameterProvenance()
      if rows.isEmpty {self.status.stringValue="This parameter has no pattern envelope, pattern command, or recorded automation. Additive graph modulation remains visible separately."}
    }
  }
  func appendParameterProvenance(to display:inout [SignalCanvasNode],edges:inout [SignalCanvasEdge]){
    guard let target=provenance.target,let owner=display.first(where:{$0.id=="plugin:"+target.plugin}) else{return}
    let layouts=data["layout"] as? [[String:Any]] ?? []
    for id in provenance.ordered {
      guard let source=provenance.sources[id] else{continue}
      var node=SignalCanvasNode(id:id,title:provenanceTitle(source),detail:provenanceDetail(source),kind:"provenance",x:max(8,owner.x-245),y:max(8,owner.y),role:"SETS BASE · EXISTING SOURCE",inputs:[],outputs:[SignalCanvasPort(label:"Sets base",modulation:true,unavailable:GraphParameterProvenance.readOnlyReason)])
      if let point=provenance.positions[id]{node.x=point.x;node.y=point.y}
      else if let saved=layouts.first(where:{$0["node"] as? String==id}) {node.x=saved["x"] as? Double ?? node.x;node.y=saved["y"] as? Double ?? node.y}
      else {for _ in 0..<512 {if !display.contains(where:{$0.rect.insetBy(dx:-12,dy:-12).intersects(node.rect)}){break};node.y+=98}}
      display.append(node)
      edges.append(.init(source:id,target:owner.id,label:"Sets base",modulation:true,input:target.parameter,enabled:source["enabled"] as? Bool != false,readOnlyReason:GraphParameterProvenance.readOnlyReason))
      songConnections.append(["kind":"provenance","key":source["key"] ?? "","plugin":target.plugin,"parameter":target.parameter])
    }
  }
  func provenanceTitle(_ source:[String:Any])->String {
    switch source["kind"] as? String {
    case "envelope":return "Pattern \(source["pattern"] ?? "?") envelope"
    case "pattern-commands":return "P\(source["pattern"] ?? "?") · Track \((source["channel"] as? Int ?? 0)+1) · FX \((source["column"] as? Int ?? 0)+1)"
    default:return "Recorded automation"
    }
  }
  func provenanceDetail(_ source:[String:Any])->String {
    if source["kind"] as? String=="recorded" {return "\(source["count"] ?? 0) points · song time"}
    let a=Double(source["position"] as? Int ?? 0)/65536,b=Double(source["endPosition"] as? Int ?? 0)/65536
    return String(format:"Rows %.2f–%.2f",a,b)+(source["enabled"] as? Bool==false ? " · disabled":"")
  }
  func inspectParameterProvenance(){
    guard let source=selectedProvenance else{provenanceControls.isHidden=true;return}
    provenanceControls.nextPage.isHidden=provenance.total<=64
    provenanceControls.nextPage.title="Sources \(provenance.offset+1)…\(provenance.offset+provenance.ordered.count) / \(provenance.total) · Next"
    provenanceControls.isHidden=false;provenanceControls.title.stringValue=provenanceTitle(source)
    let count=source["count"] as? Int ?? 0,omitted=source["omittedCommands"] as? Int ?? 0
    let visits=(source["orders"] as? [[String:Any]] ?? []).map{"\($0["sequence"] ?? 0):\($0["order"] ?? 0)"}.joined(separator:", ")
    provenanceControls.summary.stringValue=provenanceDetail(source)+" · \(count) events\nThis existing source sets the parameter's base; it does not add another modulation. Graph contributions are summed after the current base, then clamped. Disabled sources remain visible.\n"+(visits.isEmpty ? "":"Song order uses: "+visits+"\n")+(omitted>0 ? "First \(count-omitted) commands shown; open the pattern for all \(count).\n":"")+(provenance.total>provenance.ordered.count ? "Showing sources \(provenance.offset+1)…\(provenance.offset+provenance.ordered.count) of \(provenance.total) for this parameter.\n":"")+"Reference wires cannot be cut or repatched."
  }
  func openProvenanceSource(_ source:[String:Any]){
    guard !hasDraft else{status.stringValue="Finish the current graph edit before opening its source.";return}
    guard provenance.revision==revision else{status.stringValue="The song changed. Wait for its source references to refresh.";loadParameterProvenance();return}
    if source["kind"] as? String=="pattern-commands" {
      let commands=source["commands"] as? [[String:Any]] ?? []
      if commands.count==1 {var reference=source;reference.merge(commands[0]){_,new in new};bridgeProvenance(reference);return}
      let entries=commands.enumerated().map{index,command in GraphAddMenu.Entry(id:String(index),title:String(format:"Row %.4g",Double(command["position"] as? Int ?? 0)/65536),detail:"\(command["kind"] as? String ?? "Parameter") · \(command["value"] ?? 0)",keywords:"\(command["position"] ?? 0)")}
      chooseTarget(title:"Edit pattern command",entries:entries){[weak self] key in guard let self,let i=Int(key),commands.indices.contains(i)else{return};var reference=source;reference.merge(commands[i]){_,new in new};self.bridgeProvenance(reference)}
    }else{bridgeProvenance(source)}
  }
  private func bridgeProvenance(_ source:[String:Any]){rememberPanelReturn();onSourceReference?(source)}
  func returnToProvenance(){onReveal?();restorePanelReturn();window?.makeFirstResponder(canvas);if panelReturn==nil{revealAddedNode()}}
}
