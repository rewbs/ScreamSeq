import AppKit

final class ContextAction: NSMenuItem {
  private let run: () -> Void
  init(_ title:String, key:String="", modifiers:NSEvent.ModifierFlags=[], enabled:Bool=true, run:@escaping()->Void) {
    self.run=run;super.init(title:title,action:nil,keyEquivalent:key)
    target=self;action=#selector(invoke);keyEquivalentModifierMask=modifiers;isEnabled=enabled
  }
  required init(coder:NSCoder){fatalError()}
  @objc private func invoke(){run()}
}

/// The same controls back the visible UI and its context actions. Disabled
/// actions stay disabled, and collapsed tool sections retain their own submenu.
enum ContextActions {
  static func controls(in root:NSView,title:String="Panel actions") -> NSMenu {
    let menu=NSMenu(title:title);menu.autoenablesItems=false
    func visit(_ view:NSView,into target:NSMenu) {
      if let section=view as? ToolSection {
        let child=controls(in:section.content,title:section.toggle.title)
        if !child.items.isEmpty {let item=NSMenuItem(title:child.title,action:nil,keyEquivalent:"");item.submenu=child;target.addItem(item)}
        return
      }
      if view is NSTableView {return} // Row actions must capture a specific row.
      if let button=view as? NSButton,!(button is NSPopUpButton),!button.title.isEmpty {
        let item=ContextAction(button.title,enabled:button.isEnabled){[weak button] in button?.performClick(nil)}
        item.toolTip=button.toolTip
        if button.state == .on {item.state = .on}
        if let source=matchingCommand(button.title) {item.keyEquivalent=source.keyEquivalent;item.keyEquivalentModifierMask=source.keyEquivalentModifierMask}
        target.addItem(item);return
      }
      for child in view.subviews {visit(child,into:target)}
    }
    visit(root,into:menu);return menu
  }
  static func matchingCommand(_ title:String) -> NSMenuItem? {
    func clean(_ text:String)->String {text.replacingOccurrences(of:"…",with:"").lowercased()}
    func find(_ menu:NSMenu)->NSMenuItem? {
      for item in menu.items {if let sub=item.submenu,let found=find(sub){return found};if item.action != nil && clean(item.title)==clean(title){return item}}
      return nil
    }
    return NSApp.mainMenu.flatMap(find)
  }
  static func appendMenu(_ menu:NSMenu,to parent:NSMenu,title:String?=nil) {
    guard !menu.items.isEmpty else{return}
    let item=NSMenuItem(title:title ?? menu.title,action:nil,keyEquivalent:"");item.submenu=menu;parent.addItem(item)
  }
}
