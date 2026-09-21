import AppKit

// A draft owns its graph, source, pattern and revision even if the graph selection changes.
final class GraphEnvelopeEditor:NSView,NSTextFieldDelegate {
  let canvas=AutomationCanvas(frame:.zero),pattern=NSPopUpButton(),curve=NSPopUpButton(),snap=NSPopUpButton()
  let row=NSTextField(string:"0"),value=NSTextField(string:"50"),formula=NSTextField(string:"mix(start,end,t)")
  let heading=Theme.label("Pattern curve",size:12,weight:.semibold),status=Theme.label("",size:10,color:Theme.muted)
  let enabled=NSButton(checkboxWithTitle:"Enabled",target:nil,action:nil)
  let curves=["step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"]
  var onRequest:((String,[String:Any],@escaping ([String:Any])->Void)->Void)?,onChanged:(()->Void)?
  private(set) var graph:String?,node:String?,patternIndex=0,revision="",hasDraft=false,loading=false
  private var patterns=[[String:Any]](),rowsPerBeat=4,generation=0,previewGeneration=0
  private var previewWork:DispatchWorkItem?
  override init(frame:NSRect){
    super.init(frame:frame)
    pattern.target=self;pattern.action = #selector(changePattern);pattern.setAccessibilityLabel("Graph automation pattern")
    curve.addItems(withTitles:["Step","Linear","Smooth","Exponential","Logarithmic","Step at start","Exponential reversed","Logarithmic reversed","Scripted"]);curve.selectItem(at:1)
    curve.target=self;curve.action = #selector(changeCurve);curve.setAccessibilityLabel("Graph automation outgoing curve")
    snap.addItems(withTitles:["Row","½ row","¼ row","1/256 row"]);snap.target=self;snap.action = #selector(changeSnap)
    enabled.state = .on;enabled.target=self;enabled.action = #selector(markDraft)
    row.fixed(width:65);value.fixed(width:60);row.setAccessibilityLabel("Graph automation point row");value.setAccessibilityLabel("Graph automation point percent")
    formula.setAccessibilityLabel("Graph automation formula");formula.delegate=self;row.delegate=self;value.delegate=self
    formula.font = .monospacedSystemFont(ofSize:11,weight:.regular);formula.isHidden=true
    canvas.onEdit = {[weak self] in self?.markDraft()};canvas.onSelect = {[weak self] in self?.showPoint()};canvas.onViewport = {[weak self] in self?.preview()}
    canvas.setAccessibilityLabel("Subgraph pattern automation curve");canvas.heightAnchor.constraint(greaterThanOrEqualToConstant:140).isActive=true
    let controls=stack(.horizontal,[heading,pattern,curve,snap,enabled,NSView(),ActionButton("−"){[weak self] in self?.canvas.zoom(0.5)},ActionButton("+"){[weak self] in self?.canvas.zoom(2)},ActionButton("Fit"){[weak self] in self?.canvas.fit()}],spacing:4)
    let footer=stack(.horizontal,[Theme.label("Row",size:11),row,Theme.label("%",size:11),value,ActionButton("Set point"){[weak self] in self?.setPoint()},ActionButton("Delete"){[weak self] in self?.canvas.removeSelected()},ActionButton("Ramp"){[weak self] in self?.ramp()},NSView(),ActionButton("Reload / discard"){[weak self] in self?.load()},ActionButton("Apply"){[weak self] in self?.apply()}],spacing:4)
    let body=stack(.vertical,[controls,formula,canvas,footer,status],spacing:4);body.stretchAcrossAxis();body.fill(self,inset:6)
    for popup in [pattern,curve,snap]{popup.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
    heading.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
  }
  required init?(coder:NSCoder){fatalError()}
  func context(graph:String?,node:[String:Any]?,patterns:[[String:Any]],revision:String){
    guard !hasDraft,!loading else{return}
    let id=node?["kind"] as? String=="automation" ? node?["id"] as? String : nil
    guard let graph,let id else{self.graph=nil;self.node=nil;isHidden=true;previewGeneration+=1;return}
    isHidden=false
    let changed=self.graph != graph || self.node != id
    self.patterns=patterns;pattern.removeAllItems()
    for p in patterns{let item=NSMenuItem(title:"Pattern \(p["index"] ?? 0) · \(p["name"] as? String ?? "")",action:nil,keyEquivalent:"");item.representedObject=p["index"];pattern.menu?.addItem(item)}
    if let index=patterns.firstIndex(where:{$0["index"] as? Int==patternIndex}){pattern.selectItem(at:index)}else{patternIndex=patterns.first?["index"] as? Int ?? 0}
    self.graph=graph;self.node=id;heading.stringValue=node?["name"] as? String ?? "Pattern curve"
    if changed || self.revision != revision{load()}
  }
  @objc func changePattern(){
    guard !hasDraft,!loading else{if let i=patterns.firstIndex(where:{$0["index"] as? Int==patternIndex}){pattern.selectItem(at:i)};status.stringValue="Apply or discard this curve before changing pattern.";return}
    patternIndex=pattern.selectedItem?.representedObject as? Int ?? 0;canvas.fit();load()
  }
  func load(){
    guard !loading,let graph,let node,let onRequest else{return}
    loading=true;generation+=1;previewGeneration+=1;let token=generation
    onRequest("graph.automation.get",["graph":graph,"node":node,"pattern":patternIndex]){[weak self] response in
      guard let self else{return};self.loading=false
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any]else{self.error(response);return}
      guard token==self.generation else{self.status.stringValue="The draft changed while loading. Reload to discard it.";return}
      self.revision=result["revision"] as? String ?? "";self.hasDraft=false
      self.canvas.rows=data["rows"] as? Int ?? 64;self.rowsPerBeat=data["rowsPerBeat"] as? Int ?? 4
      self.enabled.state=data["enabled"] as? Bool==false ? .off : .on
      self.canvas.points=(data["points"] as? [[String:Any]] ?? []).map{EnvelopePoint(position:$0["position"] as? Int ?? 0,value:$0["value"] as? Double ?? 0,curve:$0["curve"] as? String ?? "linear",formula:$0["formula"] as? String ?? "")}
      self.canvas.selected=nil;self.showPoint();self.preview();self.status.stringValue="Pattern \(self.patternIndex) · drag points, then Apply. An absent curve outputs zero."
    }
  }
  @objc func markDraft(){hasDraft=true;generation+=1;status.stringValue="Draft for pattern \(patternIndex) · Apply saves one Undo step.";preview()}
  @objc func changeSnap(){canvas.snap=[256,128,64,1][max(0,snap.indexOfSelectedItem)]}
  @objc func changeCurve(){canvas.curve=curves[max(0,curve.indexOfSelectedItem)];if let i=canvas.selected,canvas.points.indices.contains(i){let p=canvas.points[i];canvas.replaceSelected(position:p.position,value:p.value,curve:canvas.curve)}}
  func showPoint(){
    guard let i=canvas.selected,canvas.points.indices.contains(i)else{formula.isHidden=true;return}
    let p=canvas.points[i];row.stringValue=String(format:"%.8g",Double(p.position)/256);value.stringValue=String(format:"%.6g",p.value*100)
    curve.selectItem(at:curves.firstIndex(of:p.curve) ?? 1);canvas.curve=p.curve;formula.isHidden=p.curve != "scripted";formula.stringValue=p.formula
  }
  func controlTextDidChange(_ notification:Notification){
    if notification.object as? NSTextField === formula,let i=canvas.selected,canvas.points.indices.contains(i){canvas.points[i].formula=formula.stringValue};markDraft()
  }
  func setPoint(){
    guard let r=Double(row.stringValue),let v=Double(value.stringValue),r.isFinite,v.isFinite,r>=0,r<Double(canvas.rows),v>=0,v<=100 else{status.stringValue="Use a row inside the pattern and 0–100%.";return}
    canvas.replaceSelected(position:Int((r*256).rounded()),value:v/100,curve:curves[max(0,curve.indexOfSelectedItem)])
  }
  func ramp(){canvas.points=[EnvelopePoint(position:0,value:0,curve:"linear"),EnvelopePoint(position:canvas.rows*256-1,value:1,curve:"linear")];canvas.selected=0;showPoint();markDraft()}
  func apply(){
    guard !loading,hasDraft,let graph,let node,let onRequest else{return}
    loading=true;let token=generation
    onRequest("graph.automation.set",["expectedRevision":revision,"graph":graph,"node":node,"pattern":patternIndex,"enabled":enabled.state == .on,"points":canvas.points.map(\.dictionary)]){[weak self] response in
      guard let self else{return};self.loading=false
      guard let result=response["result"] as? [String:Any]else{self.error(response);return}
      self.revision=result["revision"] as? String ?? self.revision
      if self.generation==token{self.hasDraft=false;self.status.stringValue="Curve saved · playback uses this curve on the next start.";self.onChanged?()}
      else{self.status.stringValue="Earlier draft saved; newer edits are still pending."}
    }
  }
  private func error(_ response:[String:Any]){status.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Curve operation failed"}
  func preview(){
    previewGeneration+=1;let token=previewGeneration;previewWork?.cancel()
    guard canvas.points.contains(where:{$0.curve=="scripted"}),let onRequest else{return}
    let params:[String:Any]=["points":canvas.points.map(\.dictionary),"rows":canvas.rows,"rowsPerBeat":rowsPerBeat,"start":canvas.visibleStart,"end":canvas.horizontalEnd,"samples":1024]
    let work=DispatchWorkItem{[weak self] in guard let self,token==self.previewGeneration else{return};onRequest("automation.formula.preview",params){[weak self] response in
      guard let self,token==self.previewGeneration else{return}
      if let data=(response["result"] as? [String:Any])?["data"] as? [String:Any],let values=data["values"] as? [[Double]]{self.canvas.previewValues=values.filter{$0.count==2}.map{($0[0],$0[1])}}else{self.error(response)}
    }}
    previewWork=work;DispatchQueue.main.asyncAfter(deadline:.now()+0.15,execute:work)
  }
}
