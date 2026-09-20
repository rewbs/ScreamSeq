import AppKit

struct PluginInstrumentRoute: Equatable {
  var instrument: Int, channel: Int
  var encoded: [String:Any] { ["instrument":instrument,"channel":channel] }
}
final class PluginInstrumentRow: NSTableCellView {
  let instrument=NSPopUpButton(), channel=NSPopUpButton()
  var remove: ActionButton!
  var onInstrument: ((Int)->Void)?,onChannel: ((Int)->Void)?,onRemove: (()->Void)?
  override init(frame:NSRect) {
    super.init(frame:frame)
    instrument.target=self;instrument.action=#selector(changeInstrument);instrument.setAccessibilityLabel("Tracker instrument")
    channel.target=self;channel.action=#selector(changeChannel);channel.setAccessibilityLabel("MIDI channel")
    for index in 1...16 {channel.addItem(withTitle:"MIDI \(index)");channel.lastItem?.tag=index}
    channel.fixed(width:96);instrument.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    remove=ActionButton("Remove"){[weak self] in self?.onRemove?()}
    stack(.horizontal,[instrument,channel,remove!],spacing:12).fill(self,inset:5)
  }
  required init?(coder:NSCoder){fatalError()}
  @objc func changeInstrument(){onInstrument?(instrument.selectedTag())}
  @objc func changeChannel(){onChannel?(channel.selectedTag())}
}
final class PluginInstrumentsEditor: NSView,NSTableViewDataSource,NSTableViewDelegate {
  let plugin:String,table=NSTableView(),status=Theme.label("",size:12,color:Theme.muted),name=Theme.label("",size:13)
  private(set) var revision:String?,pending=false,routes=[PluginInstrumentRoute](),inventory=[[String:Any]](),generation=0
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  var addButton:ActionButton!,previewButton:ActionButton!,applyButton:ActionButton!,reloadButton:ActionButton!
  init(plugin:String) {
    self.plugin=plugin;super.init(frame:.zero)
    table.addTableColumn(NSTableColumn(identifier:.init("routing")));table.headerView=nil;table.rowHeight=42;table.dataSource=self;table.delegate=self
    table.setAccessibilityLabel("Tracker instruments sharing this plugin")
    let scroll=verticalScrollView();scroll.documentView=table
    let explanation=Theme.label("All listed instruments share this plugin’s state, automation and audio outputs. MIDI channels can select different parts in a multitimbral plugin.",size:12,color:Theme.muted)
    explanation.maximumNumberOfLines=3;explanation.lineBreakMode = .byWordWrapping;explanation.preferredMaxLayoutWidth=576
    explanation.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    status.maximumNumberOfLines=3;status.lineBreakMode = .byWordWrapping;status.preferredMaxLayoutWidth=576
    status.setContentCompressionResistancePriority(.defaultLow,for:.horizontal);status.heightAnchor.constraint(greaterThanOrEqualToConstant:48).isActive=true
    addButton=ActionButton("Add instrument"){[weak self] in self?.add()}
    previewButton=ActionButton("Preview"){[weak self] in self?.apply(dryRun:true)}
    applyButton=ActionButton("Apply"){[weak self] in self?.apply(dryRun:false)}
    reloadButton=ActionButton("Reload"){[weak self] in self?.load()}
    let content=stack(.vertical,[stack(.horizontal,[Theme.label("Plugin instruments",size:22,weight:.semibold),NSView(),reloadButton!]),name,explanation,scroll,
      stack(.horizontal,[addButton!,NSView(),previewButton!,applyButton!]),status],spacing:14)
    content.stretchAcrossAxis();content.fill(self,inset:24);updateControls()
  }
  required init?(coder:NSCoder){fatalError()}
  func load() {
    guard !pending,let onRequest else{return};pending=true;updateControls();status.stringValue="Reading instruments…"
    onRequest("plugin.instruments.get",["plugin":plugin]){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],let revision=result["revision"] as? String,self.accepts(data) else {self.failure(reply);return}
      self.revision=revision;self.show(data);self.status.stringValue="Add existing tracker instruments, then Apply. Changes stop playback and use Undo effect change.";self.updateControls()
    }
  }
  private func accepts(_ data:[String:Any])->Bool { data["plugin"] as? String==plugin && data["isInstrument"] as? Bool==true }
  private func show(_ data:[String:Any]) {
    name.stringValue=data["name"] as? String ?? "Instrument plugin"
    inventory=data["instruments"] as? [[String:Any]] ?? []
    routes=(data["assignments"] as? [[String:Any]] ?? []).compactMap { item in
      guard let instrument=item["instrument"] as? Int,let channel=item["channel"] as? Int else{return nil}
      return PluginInstrumentRoute(instrument:instrument,channel:channel)
    };generation += 1;table.reloadData()
  }
  var available: [Int] {
    inventory.compactMap { item in
      guard let index=item["instrument"] as? Int,let owner=item["owner"] as? String,owner.isEmpty || owner==plugin,
        !routes.contains(where:{$0.instrument==index}) else{return nil};return index
    }
  }
  func add() {
    guard !pending,revision != nil,let instrument=available.first else{return}
    let channel=(1...16).first(where:{value in !routes.contains(where:{$0.channel==value})}) ?? 1
    routes.append(.init(instrument:instrument,channel:channel));changed()
  }
  func edit(row:Int,instrument:Int?=nil,channel:Int?=nil,remove:Bool=false,generation:Int) {
    guard !pending,self.generation==generation,routes.indices.contains(row) else{return}
    if remove {routes.remove(at:row)}
    else {
      if let instrument {guard instrument==routes[row].instrument || available.contains(instrument) else{return}}
      if let channel {guard (1...16).contains(channel) else{return}}
      if let instrument {routes[row].instrument=instrument}
      if let channel {routes[row].channel=channel}
    };changed()
  }
  private func changed(){generation += 1;table.reloadData();status.stringValue="Draft changed. Preview or Apply to save these assignments.";updateControls()}
  func apply(dryRun:Bool) {
    guard !pending,let revision,let onRequest else{return};pending=true;updateControls();status.stringValue=dryRun ? "Checking assignments…" : "Applying assignments…"
    onRequest("plugin.instruments.set",["plugin":plugin,"expectedRevision":revision,"assignments":routes.map(\.encoded),"dryRun":dryRun]){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],let routing=data["routing"] as? [String:Any],self.accepts(routing),let revision=result["revision"] as? String else {self.failure(reply);return}
      self.revision=revision;if !dryRun {self.show(routing)}
      self.status.stringValue=(data["wouldChange"] as? Bool==true) ? (dryRun ? "Assignments are valid. Apply saves them and stops playback." : "Assignments saved. Undo effect change restores the previous routing.") : "These assignments are already saved.";self.updateControls()
    }
  }
  private func failure(_ reply:[String:Any]){status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Could not read this instrument plugin. Reopen the panel from the plugin you want.";updateControls()}
  private func updateControls(){reloadButton?.isEnabled = !pending;addButton?.isEnabled = !pending && revision != nil && !available.isEmpty
    previewButton?.isEnabled = !pending && revision != nil;applyButton?.isEnabled = !pending && revision != nil;table.reloadData()}
  func numberOfRows(in tableView:NSTableView)->Int{routes.count}
  func tableView(_ tableView:NSTableView,viewFor tableColumn:NSTableColumn?,row:Int)->NSView? {
    guard routes.indices.contains(row) else{return nil};let id=NSUserInterfaceItemIdentifier("instrumentRoute")
    let cell=tableView.makeView(withIdentifier:id,owner:self) as? PluginInstrumentRow ?? PluginInstrumentRow(frame:.zero);cell.identifier=id
    let route=routes[row],allowed=Set(available+[route.instrument]);cell.instrument.removeAllItems()
    for item in inventory {guard let index=item["instrument"] as? Int,allowed.contains(index) else{continue};cell.instrument.addItem(withTitle:"\(index). \(item["name"] as? String ?? "Instrument")");cell.instrument.lastItem?.tag=index}
    if cell.instrument.menu?.item(withTag:route.instrument)==nil {cell.instrument.addItem(withTitle:"Missing instrument \(route.instrument)");cell.instrument.lastItem?.tag=route.instrument}
    cell.instrument.selectItem(withTag:route.instrument);cell.channel.selectItem(withTag:route.channel)
    cell.instrument.isEnabled = !pending;cell.channel.isEnabled = !pending;cell.remove.isEnabled = !pending
    let generation=self.generation
    cell.onInstrument = {[weak self] value in self?.edit(row:row,instrument:value,generation:generation)}
    cell.onChannel = {[weak self] value in self?.edit(row:row,channel:value,generation:generation)}
    cell.onRemove = {[weak self] in self?.edit(row:row,remove:true,generation:generation)}
    return cell
  }
}
