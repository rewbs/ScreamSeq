import AppKit

final class GraphCommandsEditor:NSView {
  let target=NSPopUpButton(),graph=NSPopUpButton(),kind=NSPopUpButton(),column=NSPopUpButton()
  let row=NSTextField(string:"0"),offset=NSTextField(string:"0"),amount=NSTextField(string:"100"),wet=NSTextField(string:"100")
  let tails=NSButton(checkboxWithTitle:"Let stopped effects finish their tails",target:nil,action:nil)
  let status=Theme.label("Choose a channel or group and a pattern row",size:11,color:Theme.muted)
  let context=Theme.label("",size:13,weight:.semibold)
  var onRequest:((String,[String:Any],@escaping ([String:Any])->Void)->Void)?
  var onContext:(()->(PatternModel,Int,Int))?,onGraph:((String)->Void)?
  var preferredTarget:String?,preferredColumn=0,preferredRow:Int?
  private(set) var pending=false,revision="",captured=PatternModel([:]),patternID="",data=[String:Any]()
  private var baseline=[String]()
  var hasDraft:Bool{!baseline.isEmpty && fields != baseline}
  private var fields:[String]{[target.selectedItem?.representedObject as? String ?? "",graph.selectedItem?.representedObject as? String ?? "",String(kind.indexOfSelectedItem),String(column.indexOfSelectedItem),row.stringValue,offset.stringValue,amount.stringValue,wet.stringValue,String(tails.state.rawValue)]}
  private var commands:[[String:Any]]{(data["commands"] as? [[String:Any]] ?? []).filter{$0["pattern"] as? String==patternID}}
  private var bus:String{target.selectedItem?.representedObject as? String ?? ""}
  override init(frame:NSRect){super.init(frame:frame)
    kind.addItems(withTitles:["row","start","stop","clear","amount","wet"]);column.addItems(withTitles:(1...8).map{"Graph lane \($0)"})
    target.target=self;target.action = #selector(selectionChanged);column.target=self;column.action = #selector(selectionChanged);row.target=self;row.action = #selector(selectionChanged)
    for (field,label) in [(row,"Graph command row"),(offset,"Graph command offset percent of row"),(amount,"Graph Amount percent"),(wet,"Graph wet mix percent")]{field.setAccessibilityLabel(label);field.fixed(width:100)}
    target.setAccessibilityLabel("Graph command channel or group");graph.setAccessibilityLabel("Command subgraph");kind.setAccessibilityLabel("Graph command action");column.setAccessibilityLabel("Graph command lane")
    status.maximumNumberOfLines=3;status.lineBreakMode = .byWordWrapping
    func labeled(_ label:String,_ control:NSView)->NSView{let title=Theme.label(label,size:12);title.fixed(width:95);return stack(.horizontal,[title,control])}
    let content=stack(.vertical,[context,labeled("Channel / group",target),labeled("Lane",column),labeled("Action",kind),labeled("Subgraph",graph),
      stack(.horizontal,[labeled("Row",row),labeled("Offset %",offset)]),
      stack(.horizontal,[labeled("Amount %",amount),labeled("Wet %",wet)]),tails,
      stack(.horizontal,[ActionButton("Apply command"){[weak self] in self?.apply()},ActionButton("Remove"){[weak self] in self?.apply(remove:true)},ActionButton("Open graph"){[weak self] in if let self,let id=self.graph.selectedItem?.representedObject as? String{self.onGraph?(id)}}]),
      stack(.horizontal,[ActionButton("Enable lane"){[weak self] in self?.enableLane()},ActionButton("Enable mixer"){[weak self] in self?.mutate("mixer.enable",[:])},ActionButton("Reload from cursor"){[weak self] in self?.capture()}]),
      Theme.label("R: this row · S: until stopped · X: stop named · CLR: clear persistent\nA: update Amount · W: update wet mix. Repeating S updates the existing chain.\nProcessing order: row-only → persistent → ordinary channel graph.",size:11,color:Theme.muted),status,NSView()],spacing:10)
    content.stretchAcrossAxis();content.fill(self,inset:12)
  }
  required init?(coder:NSCoder){fatalError()}
  func capture(){guard !pending,let onRequest,let current=onContext?()else{return};captured=current.0;let selectedRow=preferredRow ?? current.1
    patternID=captured.patterns.first{$0["index"] as? Int==captured.pattern}?["id"] as? String ?? "";pending=true
    onRequest("graph.get",["includeState":false]){[weak self] response in guard let self else{return};self.pending=false
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any]else{self.showError(response);return}
      self.revision=result["revision"] as? String ?? "";guard self.captured.revisionToken.isEmpty || self.revision==self.captured.revisionToken else{self.status.stringValue="Song changed; reload from cursor";return};self.data=data
      self.context.stringValue="Pattern \(self.captured.pattern) · Graph commands"
      let mixer=data["mixer"] as? [String:Any] ?? [:],buses=mixer["buses"] as? [[String:Any]] ?? [],library=data["library"] as? [[String:Any]] ?? []
      self.target.removeAllItems();for b in buses{let item=NSMenuItem(title:b["name"] as? String ?? "Bus",action:nil,keyEquivalent:"");item.representedObject=b["id"];self.target.menu?.addItem(item)}
      self.graph.removeAllItems();for d in library{let item=NSMenuItem(title:"\(d["number"] as? Int ?? 0) · \(d["name"] as? String ?? "Subgraph")",action:nil,keyEquivalent:"");item.representedObject=d["id"];self.graph.menu?.addItem(item)}
      let selected=self.preferredTarget ?? current.0.tracks.first{$0["index"] as? Int==current.2}?["id"] as? String
      if let index=self.target.itemArray.firstIndex(where:{$0.representedObject as? String==selected}){self.target.selectItem(at:index)}
      self.column.selectItem(at:max(0,min(7,self.preferredColumn)));self.row.integerValue=selectedRow;self.selectionChanged()
      self.status.stringValue=buses.isEmpty ? "Enable the mixer to add channel and group lanes" : "Ready · editing this panel preserves the pattern cursor"
    }
  }
  @objc func selectionChanged(){let r=row.integerValue,c=column.indexOfSelectedItem
    if let event=commands.first(where:{$0["target"] as? String==bus && ($0["position"] as? Int ?? 0)/65536==r && $0["column"] as? Int==c}){
      kind.selectItem(withTitle:event["kind"] as? String ?? "row");if let index=graph.itemArray.firstIndex(where:{$0.representedObject as? String==event["graph"] as? String}){graph.selectItem(at:index)}
      offset.doubleValue=Double((event["position"] as? Int ?? 0)%65536)*100/65536;amount.doubleValue=(event["amount"] as? Double ?? 1)*100;wet.doubleValue=(event["wet"] as? Double ?? 1)*100;tails.state=(event["tails"] as? Bool ?? false) ? .on : .off
    }else{offset.stringValue="0"}
    baseline=fields
  }
  func enableLane(){guard !bus.isEmpty else{return};mutate("graph.commands.set",["pattern":captured.pattern,"lanes":[["target":bus,"count":max(column.indexOfSelectedItem+1,(data["lanes"] as? [[String:Any]] ?? []).first{$0["target"] as? String==bus}?["count"] as? Int ?? 0)]]])}
  func apply(remove:Bool=false){guard !pending,!bus.isEmpty,let r=Int(row.stringValue),r>=0,r<captured.rows,let percent=Double(offset.stringValue),percent.isFinite,percent>=0,percent<100,let a=Double(amount.stringValue),a.isFinite,a>=0,a<=100,let w=Double(wet.stringValue),w.isFinite,w>=0,w<=100 else{status.stringValue="Use a valid row, offset from 0 to below 100%, and Amount/Wet from 0 to 100%";return}
    let column=self.column.indexOfSelectedItem,kind=self.kind.titleOfSelectedItem ?? "row",graph=self.graph.selectedItem?.representedObject as? String ?? ""
    guard remove || kind=="clear" || !graph.isEmpty else{status.stringValue="Create or select a subgraph first";return}
    var events=commands.filter{!($0["target"] as? String==bus && ($0["position"] as? Int ?? 0)/65536==r && $0["column"] as? Int==column)}.map{event -> [String:Any] in var c=event;c.removeValue(forKey:"pattern");return c}
    if !remove{events.append(["target":bus,"graph":kind=="clear" ? "" : graph,"position":r*65536+min(65535,Int((percent*65536/100).rounded())),"column":column,"kind":kind,"amount":a/100,"wet":w/100,"tails":tails.state == .on])}
    let existing=(data["lanes"] as? [[String:Any]] ?? []).first{$0["target"] as? String==bus}?["count"] as? Int ?? 0
    mutate("graph.commands.set",["pattern":captured.pattern,"lanes":[["target":bus,"count":max(existing,column+1)]],"commands":events])
  }
  private func showError(_ response:[String:Any]){status.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Graph command failed"}
  private func mutate(_ method:String,_ params:[String:Any]){guard !pending,let onRequest else{return};pending=true;var p=params;p["expectedRevision"]=revision;preferredTarget=bus;preferredColumn=column.indexOfSelectedItem;preferredRow=row.integerValue
    onRequest(method,p){[weak self] response in guard let self else{return};self.pending=false;guard response["result"] != nil else{self.showError(response);return};self.baseline=[];self.capture()}}
}
