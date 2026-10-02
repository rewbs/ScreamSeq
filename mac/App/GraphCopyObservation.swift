import AppKit

struct GraphObservedCopy:Hashable {
  let graph:String,target:String,role:String,instrument:String,channel:Int?
  init?(_ value:[String:Any]) {
    guard let graph=value["graph"] as? String,let role=value["role"] as? String,["row","persistent","ordinary","instrument"].contains(role) else{return nil}
    self.graph=graph;self.role=role;target=value["target"] as? String ?? "";instrument=value["instrument"] as? String ?? "";channel=value["channel"] as? Int
  }
  var key:String {[graph,target,role,instrument,channel.map(String.init) ?? "inspector"].joined(separator:"/")}
  func title(buses:[[String:Any]],instruments:[[String:Any]])->String {
    if !instrument.isEmpty {let item=instruments.first{$0["id"] as? String==instrument};return (item?["name"] as? String ?? instrument)+" · "+(channel.map{"Channel \($0+1)"} ?? "Inspector")}
    return (buses.first{$0["id"] as? String==target}?["name"] as? String ?? target)+" · "+role.capitalized
  }
}

// Choosing a measurement does not change assignments, processing, or Undo.
// A removed selected copy stays selected/unavailable until explicitly changed.
final class GraphCopyObservation:NSView {
  let picker=NSPopUpButton(),detail=Theme.label("",size:10,color:Theme.muted)
  var onChange:(()->Void)?
  private var selection=[String:String](),graph:String?
  private var items=[(String,String)]()
  private(set) var copies=[GraphObservedCopy]()
  var selected:String? {graph.flatMap{selection[$0]}}
  override init(frame:NSRect) {
    super.init(frame:frame);isHidden=true
    picker.setAccessibilityLabel("Observed graph processor copy");picker.target=self;picker.action=#selector(change)
    picker.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    stack(.horizontal,[Theme.label("Observe",size:10,color:Theme.muted),picker,detail,NSView()],spacing:6).fill(self)
    toolTip="Meters and scopes show one actual processor copy. Editing still updates the shared definition."
  }
  required init?(coder:NSCoder){fatalError()}
  @objc private func change(){guard let graph,let key=picker.selectedItem?.representedObject as? String else{return};selection[graph]=key;onChange?()}
  func select(_ key:String){guard let graph else{return};selection[graph]=key;onChange?()}
  func enter(_ copy:GraphObservedCopy){selection[copy.graph]=copy.key}
  func resetDocument(){selection=[:];graph=nil;copies=[];items=[];picker.removeAllItems();isHidden=true}

  func update(graph:String?,ports:[[String:Any]],preferred:String?,buses:[[String:Any]],instruments:[[String:Any]]) {
    self.graph=graph;isHidden=graph==nil
    guard let graph else{return}
    var seen=Set<GraphObservedCopy>()
    copies=ports.compactMap{($0["copy"] as? [String:Any]).flatMap(GraphObservedCopy.init)}.filter{$0.graph==graph && seen.insert($0).inserted}
    if selection[graph]==nil,let copy=copies.first(where:{$0.target==preferred || $0.instrument==preferred}) ?? copies.first {selection[graph]=copy.key}
    let titles=copies.map{$0.title(buses:buses,instruments:instruments)}
    let counts=Dictionary(titles.map{($0,1)},uniquingKeysWith:+)
    var next=zip(copies,titles).map{copy,title in (title+((counts[title] ?? 0)>1 ? " · "+(copy.instrument.isEmpty ? copy.target:copy.instrument):""),copy.key)}
    if let selected,!copies.contains(where:{$0.key==selected}) {next.insert(("Selected copy unavailable",selected),at:0)}
    if next.isEmpty {next=[("No prepared copy","")]}
    if !next.elementsEqual(items,by:{$0.0==$1.0 && $0.1==$1.1}) {
      items=next;picker.removeAllItems()
      for (title,key) in next {let item=NSMenuItem(title:title,action:nil,keyEquivalent:"");item.representedObject=key;item.toolTip="Exact copy: "+key;picker.menu?.addItem(item)}
    }
    if let selected,let i=items.firstIndex(where:{$0.1==selected}){picker.selectItem(at:i)}
    picker.isEnabled = !copies.isEmpty
    let missingSelection=selected.map{key in !copies.contains(where:{$0.key==key})} ?? false
    let label=missingSelection ? "Selected copy unavailable · choose another":copies.isEmpty ? "Play or audition an assigned use to prepare it":"One copy · edits affect all uses"
    if detail.stringValue != label {detail.stringValue=label}
  }
}

extension SignalGraphEditor {
  func applyCopyObservation() {
    let ports=lastSignalData["ports"] as? [[String:Any]] ?? []
    copyObservation.update(graph:graphID,ports:ports,preferred:graphTarget,buses:buses,instruments:sampleInstruments)
    var data=lastSignalData
    data["ports"]=ports.compactMap { raw -> [String:Any]? in
      let copy=(raw["copy"] as? [String:Any]).flatMap(GraphObservedCopy.init)
      guard graphID==nil ? copy==nil : (copy != nil && copy?.key==copyObservation.selected) else{return nil}
      guard copy != nil else{return raw}
      var result=raw
      func local(_ key:String)->String {key.hasPrefix("node:") ? String(key.dropFirst(5)):key}
      result["node"]=(raw["node"] as? String).map(local)
      if let node=result["node"] as? String,let title=nodes.first(where:{$0["id"] as? String==node})?["name"] as? String {
        let type=(raw["kind"] as? String)=="control" ? "Control":(raw["direction"] as? String)=="output" ? "Output":"Input"
        result["name"]=title+" · "+type+" \(raw["port"] ?? 0)"+(raw["route"] == nil ? "":" contribution")
      }
      if var route=raw["route"] as? [String:Any] {for key in ["source","target"] {route[key]=(route[key] as? String).map(local)};result["route"]=route}
      return result
    }
    signalReadings.update(data);signalReadings.aliases=boundaryPorts;canvas.signalReadings=signalReadings
  }
  func chooseObservedCopy() {
    guard graphID != nil else{status.stringValue="Open a reusable graph to choose its observed channel copy";return}
    chooseTarget(title:"Observe graph copy",entries:copyObservation.copies.map{.init(id:$0.key,title:$0.title(buses:buses,instruments:sampleInstruments),detail:"Exact audio and control readings · editing stays shared",keywords:$0.role+" "+$0.target+" "+$0.instrument)}){[weak self] key in self?.copyObservation.select(key)}
  }
}
