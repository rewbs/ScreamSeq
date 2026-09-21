import AppKit
extension AppController {
  func showPatternContextMenu(_ event:NSEvent) {
    let channel=patternView.cursorChannel
    let menu=NSMenu(title:"Pattern");menu.autoenablesItems=false
    let location=NSMenuItem(title:"Row \(patternView.cursorRow) · Channel \(channel+1)",action:nil,keyEquivalent:"");location.isEnabled=false;menu.addItem(location)
    menu.addItem(ContextAction("Find effect…",key:"?",enabled:!busy){[weak self] in self?.showPatternCommands()})
    menu.addItem(ContextAction("Edit parameter / pitch effect…",key:"e",modifiers:[.command,.shift],enabled:!busy){[weak self] in self?.showPatternPerformance()})
    menu.addItem(ContextAction("Precise notes & retriggers…",key:"n",modifiers:[.command,.shift],enabled:!busy){[weak self] in self?.showPreciseNotes()})
    let columns=NSMenu(title:"Native effect columns");columns.autoenablesItems=false
    for count in 0...8 {
      let item=ContextAction(count==0 ? "None" : "\(count) columns",enabled:!busy){[weak self] in self?.setEffectColumns(channel:channel,count:count)}
      item.state=model.extraColumns(channel)==count ? .on : .off;columns.addItem(item)
    }
    ContextActions.appendMenu(columns,to:menu)
    menu.addItem(.separator())
    menu.addItem(ContextAction("Copy selection",key:"c",modifiers:.command){[weak self] in self?.patternView.copy(nil)})
    menu.addItem(ContextAction("Paste",key:"v",modifiers:.command,enabled:!busy){[weak self] in self?.patternView.paste(nil)})
    menu.addItem(ContextAction("Mute / unmute channel"){[weak self] in self?.patternView.onMute?(channel)})
    if let commands=NSApp.mainMenu?.items.first(where:{$0.title=="Pattern"})?.submenu?.copy() as? NSMenu {ContextActions.appendMenu(commands,to:menu,title:"Pattern tools")}
    if let playback=NSApp.mainMenu?.items.first(where:{$0.title=="Playback"})?.submenu?.copy() as? NSMenu {ContextActions.appendMenu(playback,to:menu)}
    appendGlobalContextCommands(menu)
    NSMenu.popUpContextMenu(menu,with:event,for:patternView)
  }
  func appendGlobalContextCommands(_ menu:NSMenu) {
    if let commands=NSApp.mainMenu?.copy() as? NSMenu {ContextActions.appendMenu(commands,to:menu,title:"All commands & shortcuts")}
  }
  func installContextMenus() {
    workspaceContextMonitor=NSEvent.addLocalMonitorForEvents(matching:.rightMouseDown){[weak self] event in
      guard let self,let window=event.window,let content=window.contentView,
        let hit=content.hitTest(content.convert(event.locationInWindow,from:nil)) else{return event}
      if hit===self.patternView || hit.isDescendant(of:self.patternView){return event}
      // Preserve native text editing/spelling and popup-control menus.
      if hit is NSTextView || (hit as? NSTextField)?.isEditable==true || hit is NSPopUpButton {return event}
      var ancestor:NSView?=hit,panel:WorkspacePanel?
      while let view=ancestor {if let found=view as? WorkspacePanel {panel=found;break};ancestor=view.superview}
      guard let root=panel?.content ?? (window===self.window ? nil : content) else{return event}
      let menu=NSMenu();menu.autoenablesItems=false
      if hit===self.pluginEditor.parameters || hit.isDescendant(of:self.pluginEditor.parameters) {
        let row=self.pluginEditor.parameters.row(at:self.pluginEditor.parameters.convert(event.locationInWindow,from:nil))
        if self.pluginEditor.filteredValues.indices.contains(row),self.model.nativePlugins.indices.contains(self.pluginEditor.selected) {
          let parameter=self.pluginEditor.filteredValues[row],plugin=self.model.nativePlugins[self.pluginEditor.selected]
          if let id=parameter["id"] as? Int,let identity=plugin["instanceID"] as? String {
            for (label,kind) in [("PS · Set this parameter in pattern…","parameter-set"),("PL · Slide this parameter in pattern…","parameter-slide")] {
              menu.addItem(ContextAction(label,enabled:!self.busy && parameter["writable"] as? Bool != false && (kind=="parameter-set" || parameter["canSlide"] as? Bool==true)){[weak self] in self?.openPatternPerformance(kind:kind,target:(identity,id))})
            }
          }
        }
      }
      if let canvas=hit as? SignalCanvas {canvas.selectForContext(event)}
      let title=panel.map{$0.title+" actions"} ?? "Editor actions"
      ContextActions.appendMenu(ContextActions.controls(in:root,title:title),to:menu)
      if let panel {
        let placement=NSMenu(title:"Panel");placement.autoenablesItems=false
        placement.addItem(ContextAction(panel.pinned ? "Follow cursor" : "Pin current target"){[weak panel] in panel?.pinned.toggle()})
        placement.addItem(ContextAction("Return to opening row"){[weak panel] in panel?.onReturn?()})
        for (label,whereTo) in [("Dock right","right"),("Dock below","bottom"),("Float","float"),("Hide","hide")] {placement.addItem(ContextAction(label){[weak panel] in panel?.onPlace?(whereTo)})}
        ContextActions.appendMenu(placement,to:menu)
      }
      self.appendGlobalContextCommands(menu)
      NSMenu.popUpContextMenu(menu,with:event,for:hit);return nil
    }
  }
}
