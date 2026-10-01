import AppKit

final class WorkspaceCommandPalette: NSObject, NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate, NSWindowDelegate {
  struct Entry { let item: NSMenuItem, path: String; var id: String { (item as? ContextAction)?.commandID ?? (path + "/" + NSStringFromSelector(item.action ?? #selector(NSObject.description))) } }
  let search = NSSearchField(), table = NSTableView(), status = Theme.label("Return runs · ↑/↓ choose · Escape closes",size:11,color:Theme.muted)
  var entries = [Entry](), filtered = [Entry](), window: NSPanel?
  var onShortcutsChanged:(()->Void)?
  var shortcutAllowed:((String)->Bool)?
  var additionalMenus: (() -> [NSMenu])?
  private var additionalIDs = Set<String>()
  var recording = false
  var capturingSequence=false,recordedSequence=[String]()
  let sequences=WorkspaceSequences()
  private var monitor: Any?, previousWindow: NSWindow?, previousResponder: NSResponder?
  private var defaults = [String: [String: Any]]()
  func collect() {
    GraphCommand.migrateLegacyBindings()
    entries.removeAll()
    func visit(_ menu: NSMenu, _ prefix: String) {
      for item in menu.items where !item.isSeparatorItem {
        if let child = item.submenu { visit(child, prefix.isEmpty ? child.title : prefix + " / " + child.title) }
        else if item.action != nil {
          let entry=Entry(item:item,path:prefix + " / " + item.title)
          if (item as? ContextAction)?.commandID == nil || !entries.contains(where:{$0.id==entry.id}) {entries.append(entry)}
        }
      }
    }
    if let menu = NSApp.mainMenu { visit(menu, "") }
    let globalCount = entries.count
    for menu in additionalMenus?() ?? [] { visit(menu, menu.title) }
    additionalIDs = Set(entries.dropFirst(globalCount).map(\.id))
    for entry in entries where defaults[entry.id] == nil { defaults[entry.id] = ["key":entry.item.keyEquivalent,"modifiers":entry.item.keyEquivalentModifierMask.rawValue] }
    sequences.load();sequences.isAvailable = {[weak self] id in self?.shortcutAllowed?(id) ?? true}
    sequences.onRun = {[weak self] id in guard let self else{return};self.collect();guard let entry=self.entries.first(where:{$0.id==id}),let action=entry.item.action else{return};guard self.isAvailable(entry.item,from:NSApp.keyWindow?.firstResponder ?? NSApp.keyWindow) else{NSSound.beep();return};NSApp.sendAction(action,to:entry.item.target,from:entry.item)}
    let bindings = UserDefaults.standard.dictionary(forKey:"workspaceShortcuts") as? [String:[String:Any]] ?? [:]
    for entry in entries { if let binding = bindings[entry.id],let key = binding["key"] as? String,let mods = binding["modifiers"] as? UInt { entry.item.keyEquivalent = key; entry.item.keyEquivalentModifierMask = .init(rawValue:mods) };if sequences.bindings[entry.id] != nil{entry.item.keyEquivalent=""} }
  }
  func show() {
    previousWindow = NSApp.keyWindow; previousResponder = previousWindow?.firstResponder; collect()
    if window == nil {
      let win = NSPanel(contentRect:NSRect(x:0,y:0,width:780,height:510),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
      win.title = "Commands"; win.isReleasedWhenClosed = false; win.delegate = self; window = win
      search.placeholderString = "Search commands, panels, layouts…"; search.delegate = self; search.setAccessibilityLabel("Search all commands")
      let column = NSTableColumn(identifier:.init("command")); column.width = 720; table.addTableColumn(column); table.headerView = nil
      table.dataSource = self; table.delegate = self; table.rowHeight = 28; table.target = self; table.doubleAction = #selector(run)
      table.setAccessibilityLabel("Commands and keyboard shortcuts")
      let scroll = NSScrollView(); scroll.documentView = table; scroll.hasVerticalScroller = true
      let view = stack(.vertical,[search,scroll,stack(.horizontal,[status,NSView(),ActionButton("Set shortcut…"){[weak self] in self?.recording = true; self?.capturingSequence=false;self?.status.stringValue = "Press a shortcut with ⌘, ⌃ or ⌥ · Escape cancels"},ActionButton("Set sequence…"){[weak self] in self?.recording=true;self?.capturingSequence=true;self?.recordedSequence=[];self?.status.stringValue="Press two to four keys · Return saves · Escape cancels"},ActionButton("Reset shortcut"){[weak self] in self?.reset()},ActionButton("Run"){[weak self] in self?.run()}])],spacing:8)
      view.stretchAcrossAxis();view.fill(win.contentView!,inset:12)
      monitor = NSEvent.addLocalMonitorForEvents(matching:.keyDown){[weak self] event in
        guard let self,self.window?.isKeyWindow == true else{return event}
        if event.keyCode == 53 { if self.recording {self.recording=false;self.status.stringValue="Shortcut unchanged"} else {self.close()};return nil }
        if self.recording {if self.capturingSequence{self.recordSequence(event)}else{self.bind(event)};return nil}
        if event.keyCode == 125 || event.keyCode == 126 {guard !self.filtered.isEmpty else{return nil};let row=max(0,min(self.filtered.count-1,self.table.selectedRow+(event.keyCode==125 ? 1 : -1)));self.table.selectRowIndexes(IndexSet(integer:row),byExtendingSelection:false);self.table.scrollRowToVisible(row);return nil}
        if event.keyCode == 36 {self.run();return nil};return event
      }
    }
    search.stringValue="";filter();window?.center();window?.makeKeyAndOrderFront(nil);window?.makeFirstResponder(search)
  }
  func handleAdditionalShortcut(_ event: NSEvent) -> Bool {
    let bindings = UserDefaults.standard.dictionary(forKey: "workspaceShortcuts") as? [String: [String: Any]] ?? [:]
    guard event.type == .keyDown, let stroke = WorkspaceStroke(event), !stroke.modifiers.intersection([.command,.control,.option]).isEmpty,
      let entry=entries.first(where: { additionalIDs.contains($0.id) && (shortcutAllowed?($0.id) ?? true) && bindings[$0.id] != nil && $0.item.keyEquivalent.lowercased()==stroke.key && $0.item.keyEquivalentModifierMask.intersection(WorkspaceStroke.mask)==stroke.modifiers }) else { return false }
    let identity = entry.id; collect()
    guard let fresh = entries.first(where: { $0.id == identity }), fresh.item.isEnabled, let action=fresh.item.action else { return true }
    NSApp.sendAction(action,to:fresh.item.target,from:fresh.item); return true
  }
  func controlTextDidChange(_ obj: Notification) {filter()}
  func filter() {let words=search.stringValue.lowercased().split(separator:" ");filtered=entries.filter{entry in words.allSatisfy{entry.path.lowercased().contains($0)}};table.reloadData();if !filtered.isEmpty {table.selectRowIndexes(IndexSet(integer:0),byExtendingSelection:false)}}
  func numberOfRows(in tableView:NSTableView)->Int{filtered.count}
  func tableView(_ tableView:NSTableView,viewFor tableColumn:NSTableColumn?,row:Int)->NSView? {
    guard filtered.indices.contains(row) else{return nil};let entry=filtered[row],mask=entry.item.keyEquivalentModifierMask
    let shortcut=(mask.contains(.control) ? "⌃" : "")+(mask.contains(.option) ? "⌥" : "")+(mask.contains(.shift) ? "⇧" : "")+(mask.contains(.command) ? "⌘" : "")+entry.item.keyEquivalent.uppercased()
    let label=tableView.makeView(withIdentifier:.init("command"),owner:self) as? NSTextField ?? Theme.label("",size:12)
    let sequence=sequences.bindings[entry.id]?.map(\.encoded).joined(separator:" → ");label.identifier = .init("command");label.stringValue=entry.path+(sequence.map{"     "+$0} ?? (entry.item.keyEquivalent.isEmpty ? "" : "     "+shortcut));label.textColor=entry.item.isEnabled ? Theme.text:Theme.muted;label.toolTip=entry.item.toolTip;return label
  }
  // Menu items are only validated when their menu opens, so isEnabled can be
  // stale. Ask the object that would receive the action, as the menu would.
  func isAvailable(_ item:NSMenuItem,from responder:NSResponder?)->Bool {
    // Context actions capture their availability when the menu is built. Unlike
    // responder-chain menu items, they have no later Cocoa validator to call.
    if item is ContextAction && !item.isEnabled{return false}
    guard let action=item.action else{return false}
    var receiver:AnyObject?=item.target
    if receiver == nil {
      var next=responder
      while let candidate=next,receiver == nil {if candidate.responds(to:action){receiver=candidate};next=candidate.nextResponder}
      if receiver == nil,let window=(responder as? NSWindow) ?? (responder as? NSView)?.window,let delegate=window.delegate,delegate.responds(to:action){receiver=delegate}
      if receiver == nil,NSApp.responds(to:action){receiver=NSApp}
      if receiver == nil,let delegate=NSApp.delegate,delegate.responds(to:action){receiver=delegate}
    }
    guard let receiver,receiver.responds(to:action) else{return false}
    if let validator=receiver as? NSMenuItemValidation{return validator.validateMenuItem(item)}
    if let validator=receiver as? NSUserInterfaceValidations{return validator.validateUserInterfaceItem(item)}
    return true
  }
  @objc func run(){guard filtered.indices.contains(table.selectedRow) else{return};let item=filtered[table.selectedRow].item
    guard let action=item.action,isAvailable(item,from:previousResponder ?? previousWindow) else{NSSound.beep();status.stringValue=item.toolTip ?? "\(item.title) is not available right now";return}
    close();NSApp.sendAction(action,to:item.target,from:item)}
  private func bind(_ event:NSEvent){
    guard filtered.indices.contains(table.selectedRow),let key=event.charactersIgnoringModifiers?.lowercased(),key.count==1 else{return}
    let mask=event.modifierFlags.intersection([.command,.control,.option,.shift]);guard !mask.intersection([.command,.control,.option]).isEmpty else{status.stringValue="Include ⌘, ⌃ or ⌥ so note entry stays available";return}
    let chosen=filtered[table.selectedRow]
    let value=WorkspaceStroke(event)?.encoded ?? ""
    if let error=setShortcut(chosen.id,keys:[value]){status.stringValue=error;return}
    recording=false;status.stringValue="Shortcut saved";table.reloadData()
  }
  func resetShortcut(_ id:String)->String? {
    guard let entry=entries.first(where:{$0.id==id}),let value=defaults[id] else{return "Choose a known command"}
    let key=value["key"] as? String ?? "",flags=NSEvent.ModifierFlags(rawValue:value["modifiers"] as? UInt ?? 0)
    if !key.isEmpty,entries.contains(where:{$0.id != id && $0.item.keyEquivalent==key && $0.item.keyEquivalentModifierMask.intersection(WorkspaceStroke.mask)==flags.intersection(WorkspaceStroke.mask)}) {return "The default key is assigned to another command"}
    // Restoring the shipped local M/Q/F bindings is valid even though new
    // user bindings require a modifier to protect tracker note entry.
    sequences.cancel();sequences.bindings.removeValue(forKey:id);sequences.save()
    var bindings=UserDefaults.standard.dictionary(forKey:"workspaceShortcuts") as? [String:[String:Any]] ?? [:]
    bindings.removeValue(forKey:id);UserDefaults.standard.set(bindings,forKey:"workspaceShortcuts")
    entry.item.keyEquivalent=key;entry.item.keyEquivalentModifierMask=flags
    table.reloadData();onShortcutsChanged?();return nil
  }
  private func reset(){guard filtered.indices.contains(table.selectedRow) else{return}
    status.stringValue=resetShortcut(filtered[table.selectedRow].id) ?? "Default shortcut restored"
  }
  private func close(){recording=false;window?.orderOut(nil);previousWindow?.makeKeyAndOrderFront(nil);previousWindow?.makeFirstResponder(previousResponder)}
  func windowShouldClose(_ sender:NSWindow)->Bool{close();return false}
  deinit{if let monitor{NSEvent.removeMonitor(monitor)}}
}
