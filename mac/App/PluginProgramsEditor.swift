import AppKit

final class PluginProgramsEditor: NSView, NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate {
  let plugin: String, table=NSTableView(), search=NSSearchField()
  let name=Theme.label("",size:13), status=Theme.label("",size:12,color:Theme.muted)
  private(set) var revision:String?,catalogRevision:String?,pending=false,programs=[[String:Any]](),filtered=[[String:Any]]()
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  var loadButton:ActionButton!,checkButton:ActionButton!,reloadButton:ActionButton!
  init(plugin:String) {
    self.plugin=plugin;super.init(frame:.zero)
    let column=NSTableColumn(identifier:.init("program"));table.addTableColumn(column);table.headerView=nil;table.rowHeight=46
    table.dataSource=self;table.delegate=self;table.allowsEmptySelection=true;table.setAccessibilityLabel("Plugin factory programs")
    let scroll=verticalScrollView();scroll.documentView=table
    search.placeholderString="Search programs and groups";search.delegate=self;search.setAccessibilityLabel("Search plugin programs")
    let explanation=Theme.label("Programs supplied through the plugin’s standard AU or VST3 interface. Select a program, then Load. Loading stops playback and uses Undo effect change.",size:12,color:Theme.muted)
    for label in [explanation,status] {label.maximumNumberOfLines=3;label.lineBreakMode = .byWordWrapping;label.preferredMaxLayoutWidth=570;label.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
    status.heightAnchor.constraint(greaterThanOrEqualToConstant:44).isActive=true
    loadButton=ActionButton("Load program"){[weak self] in self?.apply(dryRun:false)}
    checkButton=ActionButton("Check selection"){[weak self] in self?.apply(dryRun:true)}
    reloadButton=ActionButton("Reload list"){[weak self] in self?.load()}
    let content=stack(.vertical,[stack(.horizontal,[Theme.label("Factory programs",size:22,weight:.semibold),NSView(),reloadButton!]),name,explanation,search,scroll,
      stack(.horizontal,[NSView(),checkButton!,loadButton!]),status],spacing:14)
    content.stretchAcrossAxis();content.fill(self,inset:24);controls()
  }
  required init?(coder:NSCoder){fatalError()}
  var selected:[String:Any]? {filtered.indices.contains(table.selectedRow) ? filtered[table.selectedRow] : nil}
  func load() {
    guard !pending,let onRequest else{return};pending=true;controls();status.stringValue="Reading program list…"
    onRequest("plugin.programs.get",["plugin":plugin]){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],data["plugin"] as? String==self.plugin,
        let revision=result["revision"] as? String,let catalog=data["catalogRevision"] as? String,let entries=data["programs"] as? [[String:Any]] else {self.failure(reply);return}
      self.revision=revision;self.catalogRevision=catalog;self.programs=entries;self.name.stringValue=data["name"] as? String ?? "Plugin"
      self.filter();self.status.stringValue=entries.isEmpty ? "This plugin exposes no standard factory programs. You can still load saved ScreamSeq presets." : "Select a program to inspect or load. Selection alone leaves the sound unchanged.";self.controls()
    }
  }
  func filter() {
    guard !pending else{return}
    let identity=selected?["id"] as? String,query=search.stringValue.trimmingCharacters(in:.whitespacesAndNewlines)
    filtered=programs.filter{entry in query.isEmpty || ["name","group"].contains{(entry[$0] as? String ?? "").localizedCaseInsensitiveContains(query)}}
    table.reloadData();table.deselectAll(nil)
    if let identity,let row=filtered.firstIndex(where:{$0["id"] as? String==identity}) {table.selectRowIndexes(IndexSet(integer:row),byExtendingSelection:false)}
    controls()
  }
  func controlTextDidChange(_ notification:Notification){filter()}
  func tableViewSelectionDidChange(_ notification:Notification){controls()}
  func tableView(_ tableView:NSTableView,shouldSelectRow row:Int)->Bool{!pending}
  func apply(dryRun:Bool) {
    guard !pending,let revision,let catalogRevision,let selected,selected["loadable"] as? Bool==true,let id=selected["id"] as? String,let onRequest else{return}
    pending=true;controls();status.stringValue=dryRun ? "Checking selection…" : "Preparing program…"
    onRequest("plugin.programs.load",["plugin":plugin,"program":id,"expectedRevision":revision,"expectedCatalogRevision":catalogRevision,"dryRun":dryRun]){[weak self] reply in
      guard let self else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],data["plugin"] as? String==self.plugin,
        (data["program"] as? [String:Any])?["id"] as? String==id,let revision=result["revision"] as? String else {self.failure(reply);return}
      self.revision=revision
      self.status.stringValue=dryRun ? "Selection is valid. Loading will ask the plugin to prepare it." : "Program loaded. Undo effect change restores the previous settings."
      self.controls()
    }
  }
  private func failure(_ reply:[String:Any]) {status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Could not read this plugin’s programs. Reload the list to try again.";controls()}
  private func controls(){reloadButton?.isEnabled = !pending;search.isEnabled = !pending
    let canLoad = !pending && revision != nil && catalogRevision != nil && selected?["loadable"] as? Bool==true
    loadButton?.isEnabled=canLoad;checkButton?.isEnabled=canLoad}
  func numberOfRows(in tableView:NSTableView)->Int{filtered.count}
  func tableView(_ tableView:NSTableView,viewFor tableColumn:NSTableColumn?,row:Int)->NSView? {
    guard filtered.indices.contains(row) else{return nil};let id=NSUserInterfaceItemIdentifier("factoryProgram")
    let cell=tableView.makeView(withIdentifier:id,owner:self) as? NSTableCellView ?? NSTableCellView()
    if cell.identifier==nil {cell.identifier=id;let label=Theme.label("",size:12);label.maximumNumberOfLines=2;label.lineBreakMode = .byTruncatingTail;label.fill(cell,inset:5);cell.textField=label}
    let item=filtered[row],unavailable=item["loadable"] as? Bool != true
    cell.textField?.stringValue="\(item["name"] as? String ?? "Program")\n\(item["group"] as? String ?? "")\(unavailable ? " · No supported selector" : "")"
    cell.textField?.textColor=unavailable ? Theme.muted : Theme.text;return cell
  }
}
