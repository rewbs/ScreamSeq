import AppKit

final class WorkspaceCommandPalette: NSObject, NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate, NSWindowDelegate {
  struct Entry { let item: NSMenuItem, path: String; var id: String { path + "/" + NSStringFromSelector(item.action ?? #selector(NSObject.description)) } }
  let search = NSSearchField(), table = NSTableView(), status = Theme.label("Return runs · ↑/↓ choose · Escape closes",size:11,color:Theme.muted)
  var entries = [Entry](), filtered = [Entry](), window: NSPanel?
  var onShortcutsChanged:(()->Void)?
  var recording = false
  var capturingSequence=false,recordedSequence=[String]()
  let sequences=WorkspaceSequences()
  private var monitor: Any?, previousWindow: NSWindow?, previousResponder: NSResponder?
  private var defaults = [String: [String: Any]]()
  func collect() {
    entries.removeAll()
    func visit(_ menu: NSMenu, _ prefix: String) {
      for item in menu.items where !item.isSeparatorItem {
        if let child = item.submenu { visit(child, prefix.isEmpty ? child.title : prefix + " / " + child.title) }
        else if item.action != nil { entries.append(Entry(item:item,path:prefix + " / " + item.title)) }
      }
    }
    if let menu = NSApp.mainMenu { visit(menu, "") }
    for entry in entries where defaults[entry.id] == nil { defaults[entry.id] = ["key":entry.item.keyEquivalent,"modifiers":entry.item.keyEquivalentModifierMask.rawValue] }
    sequences.load();sequences.onRun = {[weak self] id in guard let entry=self?.entries.first(where:{$0.id==id}),entry.item.isEnabled,let action=entry.item.action else{return};NSApp.sendAction(action,to:entry.item.target,from:entry.item)}
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
        if event.keyCode == 125 || event.keyCode == 126 {let row=max(0,min(self.filtered.count-1,self.table.selectedRow+(event.keyCode==125 ? 1 : -1)));self.table.selectRowIndexes(IndexSet(integer:row),byExtendingSelection:false);self.table.scrollRowToVisible(row);return nil}
        if event.keyCode == 36 {self.run();return nil};return event
      }
    }
    search.stringValue="";filter();window?.center();window?.makeKeyAndOrderFront(nil);window?.makeFirstResponder(search)
  }
  func controlTextDidChange(_ obj: Notification) {filter()}
  func filter() {let words=search.stringValue.lowercased().split(separator:" ");filtered=entries.filter{entry in words.allSatisfy{entry.path.lowercased().contains($0)}};table.reloadData();if !filtered.isEmpty {table.selectRowIndexes(IndexSet(integer:0),byExtendingSelection:false)}}
  func numberOfRows(in tableView:NSTableView)->Int{filtered.count}
  func tableView(_ tableView:NSTableView,viewFor tableColumn:NSTableColumn?,row:Int)->NSView? {
    guard filtered.indices.contains(row) else{return nil};let entry=filtered[row],mask=entry.item.keyEquivalentModifierMask
    let shortcut=(mask.contains(.control) ? "⌃" : "")+(mask.contains(.option) ? "⌥" : "")+(mask.contains(.shift) ? "⇧" : "")+(mask.contains(.command) ? "⌘" : "")+entry.item.keyEquivalent.uppercased()
    let label=tableView.makeView(withIdentifier:.init("command"),owner:self) as? NSTextField ?? Theme.label("",size:12)
    let sequence=sequences.bindings[entry.id]?.map(\.encoded).joined(separator:" → ");label.identifier = .init("command");label.stringValue=entry.path+(sequence.map{"     "+$0} ?? (entry.item.keyEquivalent.isEmpty ? "" : "     "+shortcut));return label
  }
  @objc func run(){guard filtered.indices.contains(table.selectedRow) else{return};let item=filtered[table.selectedRow].item;close();NSApp.sendAction(item.action!,to:item.target,from:item)}
  private func bind(_ event:NSEvent){
    guard filtered.indices.contains(table.selectedRow),let key=event.charactersIgnoringModifiers?.lowercased(),key.count==1 else{return}
    let mask=event.modifierFlags.intersection([.command,.control,.option,.shift]);guard !mask.intersection([.command,.control,.option]).isEmpty else{status.stringValue="Include ⌘, ⌃ or ⌥ so note entry stays available";return}
    let chosen=filtered[table.selectedRow]
    let value=WorkspaceStroke(event)?.encoded ?? ""
    if let error=setShortcut(chosen.id,keys:[value]){status.stringValue=error;return}
    recording=false;status.stringValue="Shortcut saved";table.reloadData()
  }
  private func reset(){guard filtered.indices.contains(table.selectedRow) else{return};let entry=filtered[table.selectedRow]
    guard let value=defaults[entry.id] else{return};let key=value["key"] as? String ?? "",flags=NSEvent.ModifierFlags(rawValue:value["modifiers"] as? UInt ?? 0)
    if let error=setShortcut(entry.id,keys:key.isEmpty ? [] : [WorkspaceStrokeString(key,flags)]){status.stringValue=error;return}
    var bindings=UserDefaults.standard.dictionary(forKey:"workspaceShortcuts") as? [String:[String:Any]] ?? [:];bindings.removeValue(forKey:entry.id);UserDefaults.standard.set(bindings,forKey:"workspaceShortcuts");status.stringValue="Default shortcut restored"
  }
  private func close(){recording=false;window?.orderOut(nil);previousWindow?.makeKeyAndOrderFront(nil);previousWindow?.makeFirstResponder(previousResponder)}
  func windowShouldClose(_ sender:NSWindow)->Bool{close();return false}
  deinit{if let monitor{NSEvent.removeMonitor(monitor)}}
}
