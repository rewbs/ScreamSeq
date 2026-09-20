import AppKit

// A panel owns its view and context even while detached or behind another tab.
final class WorkspacePanel: NSView {
  let id: String, title: String, content = NSView()
  let target = Theme.label("", size: 10, color: Theme.muted)
  var pinned = false { didSet { pin.state = pinned ? .on : .off; pin.title = pinned ? "Pinned" : "Follow"; onPin?(pinned) } }
  var onPin: ((Bool) -> Void)?, onFollow: (() -> Void)?, onReturn: (() -> Void)?
  var onPlace: ((String) -> Void)?
  private var pin: ActionButton!, placement: NSPopUpButton!
  private let header = NSView()
  init(id: String, title: String, view: NSView, height: CGFloat? = nil) {
    self.id = id; self.title = title; super.init(frame: .zero)
    wantsLayer = true; layer?.backgroundColor = Theme.bg.cgColor
    setAccessibilityLabel(title + " panel")
    pin = ActionButton("Follow") { [weak self] in guard let self else { return }; self.pinned.toggle(); if !self.pinned { self.onFollow?() } }
    pin.toolTip = "Pin this panel's target independently of other panels"
    pin.setAccessibilityLabel("Pin " + title)
    let follow = ActionButton("Cursor", symbol: "scope") { [weak self] in self?.pinned = false; self?.onFollow?() }
    follow.toolTip = "Inspect the current editing cursor"
    let back = ActionButton("Return", symbol: "arrow.uturn.backward") { [weak self] in self?.onReturn?() }
    back.toolTip = "Return the pattern cursor to this panel's opening position"
    placement = NSPopUpButton(); placement.addItems(withTitles: ["Panel", "Dock right", "Dock below", "Float", "Hide"])
    placement.target = self; placement.action = #selector(place); placement.setAccessibilityLabel("Move " + title + " panel")
    let label = Theme.label(title, size: 11, weight: .semibold)
    label.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    target.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
    stack(.horizontal, [label, target, NSView(), pin, follow, back, placement], spacing: 5).fill(header, inset: 4)
    addSubview(header); addSubview(content)
    if let height {
      let scroll = verticalScrollView(); scroll.hasHorizontalScroller = true; scroll.documentView = view; scroll.fill(content)
      view.translatesAutoresizingMaskIntoConstraints = false
      NSLayoutConstraint.activate([view.leadingAnchor.constraint(equalTo: scroll.contentView.leadingAnchor),
        view.topAnchor.constraint(equalTo: scroll.contentView.topAnchor), view.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor),
        view.heightAnchor.constraint(greaterThanOrEqualTo: scroll.contentView.heightAnchor), view.heightAnchor.constraint(greaterThanOrEqualToConstant: height)])
    } else { view.fill(content) }
  }
  required init?(coder: NSCoder) { fatalError() }
  override var isFlipped: Bool { true }
  override func layout() { super.layout(); header.frame = NSRect(x: 0, y: 0, width: bounds.width, height: 32); content.frame = NSRect(x: 0, y: 33, width: bounds.width, height: max(0,bounds.height-33)) }
  func showFocus(_ focused: Bool) { layer?.borderWidth = focused ? 1 : 0; layer?.borderColor = Theme.accent.cgColor }
  @objc private func place() { let action = ["", "right", "bottom", "float", "hide"][placement.indexOfSelectedItem]; placement.selectItem(at: 0); if !action.isEmpty { onPlace?(action) } }
}

final class WorkspaceTabs: NSView {
  let picker = NSPopUpButton(), host = NSView()
  var panels = [WorkspacePanel](), selected: String?, onSelect: ((String) -> Void)?
  override init(frame: NSRect) {
    super.init(frame: frame); addSubview(picker); addSubview(host)
    picker.target = self; picker.action = #selector(changed); picker.setAccessibilityLabel("Choose docked panel")
  }
  required init?(coder: NSCoder) { fatalError() }
  override var isFlipped: Bool { true }
  override func layout() { super.layout(); picker.frame = NSRect(x: 4,y: 2,width: max(100,bounds.width-8),height: 25); host.frame = NSRect(x: 0,y: 30,width: bounds.width,height: max(0,bounds.height-30)) }
  func reload() {
    picker.removeAllItems(); for panel in panels { picker.addItem(withTitle: panel.title); picker.lastItem?.representedObject = panel.id }
    if !panels.contains(where: { $0.id == selected }) { selected = panels.first?.id }
    showSelected()
  }
  func showSelected() {
    if let current = host.subviews.first as? WorkspacePanel, current.id == selected { return }
    for child in host.subviews { child.removeFromSuperview() }
    if let index = panels.firstIndex(where: { $0.id == selected }) { picker.selectItem(at: index); panels[index].fill(host) }
  }
  @objc func changed() { selected = picker.selectedItem?.representedObject as? String; showSelected(); if let selected { onSelect?(selected) } }
}

final class DockWorkspace: NSView, NSWindowDelegate {
  let vertical = NSSplitView(), upper = NSSplitView(), lower = NSSplitView()
  let pattern = NSView(), right = WorkspaceTabs(), bottom = WorkspaceTabs(), secondary = WorkspaceTabs()
  private(set) var panels = [String: WorkspacePanel](), locations = [String: String]()
  private var floating = [String: NSWindow]()
  private(set) var focusLayout = false
  var onSelection: ((String) -> Void)?, onLayout: (() -> Void)?
  init(patternView: NSView) {
    super.init(frame: .zero)
    vertical.isVertical = false; upper.isVertical = true; lower.isVertical = true
    for (split, name) in [(vertical,"vertical"),(upper,"upper"),(lower,"lower")] { split.dividerStyle = .thin; split.autosaveName = "ResonanceWorkspace-" + name }
    upper.addArrangedSubview(pattern); upper.addArrangedSubview(right)
    lower.addArrangedSubview(bottom); lower.addArrangedSubview(secondary)
    vertical.addArrangedSubview(upper); vertical.addArrangedSubview(lower); vertical.fill(self)
    patternView.fill(pattern)
    for tabs in [right,bottom,secondary] { tabs.onSelect = { [weak self] id in self?.onSelection?(id) } }
  }
  required init?(coder: NSCoder) { fatalError() }
  func register(_ panel: WorkspacePanel, location: String) {
    panels[panel.id] = panel; panel.onPlace = { [weak self, weak panel] whereTo in if let panel { self?.place(panel.id, at: whereTo) } }
    place(panel.id, at: location, select: false)
  }
  func place(_ id: String, at destination: String, select: Bool = true) {
    guard let panel = panels[id] else { return }
    for tabs in [right,bottom,secondary] { tabs.panels.removeAll { $0.id == id }; tabs.reload() }
    panel.removeFromSuperview(); floating[id]?.orderOut(nil); floating[id]?.contentView = nil
    locations[id] = destination
    if destination == "float" {
      let win = floating[id] ?? NSWindow(contentRect: NSRect(x: 0,y: 0,width: 1060,height: 800),styleMask: [.titled,.closable,.resizable,.miniaturizable],backing: .buffered,defer: false)
      win.title = panel.title; win.contentView = panel; win.isReleasedWhenClosed = false; win.delegate = self; win.minSize = NSSize(width: 640,height: 400)
      if floating[id] == nil { win.center(); win.setFrameAutosaveName("ResonancePanel-" + id) }; floating[id] = win
      if select { win.makeKeyAndOrderFront(nil) }
    } else if destination != "hide" {
      let tabs = destination == "right" ? right : destination == "secondary" ? secondary : bottom
      tabs.panels.append(panel); if select { tabs.selected = id }; tabs.reload()
      if destination == "right" { right.isHidden = false } else { lower.isHidden = false }
      vertical.adjustSubviews(); upper.adjustSubviews(); lower.adjustSubviews()
    }
    if select { onSelection?(id) }; onLayout?()
  }
  func show(_ id: String, focus: Bool = false) {
    guard let panel = panels[id] else { return }
    if focusLayout && locations[id] != "float" { setFocusLayout(false) }
    if locations[id] == "float" { floating[id]?.makeKeyAndOrderFront(nil) }
    else {
      if locations[id] == "hide" { place(id, at: "right") }
      for tabs in [right,bottom,secondary] where tabs.panels.contains(where: { $0.id == id }) {
        tabs.isHidden = false; if tabs !== right { lower.isHidden = false }; tabs.selected = id; tabs.showSelected()
      }
      vertical.adjustSubviews(); upper.adjustSubviews(); lower.adjustSubviews()
    }
    if focus { focusFirst(in: panel) }; onSelection?(id)
  }
  func focusFirst(in view: NSView) {
    func find(_ root: NSView) -> NSView? {
      for child in root.subviews { if child.acceptsFirstResponder && !(child is NSScrollView) { return child }; if let found = find(child) { return found } }; return nil
    }
    view.window?.makeFirstResponder(find((view as? WorkspacePanel)?.content ?? view) ?? view)
  }
  var visibleIDs: [String] { [focusLayout ? nil : right.selected, focusLayout ? nil : bottom.selected, focusLayout ? nil : secondary.selected].compactMap{$0} + floating.filter { $0.value.isVisible }.map(\.key) }
  func containsFocus(_ id: String) -> Bool {
    guard let panel = panels[id], let responder = panel.window?.firstResponder as? NSView else { return false }
    // Field editors belong to the window; their delegate identifies the edited control.
    if let field = responder as? NSTextView, let delegate = field.delegate as? NSView { return delegate.isDescendant(of: panel) }
    return responder.isDescendant(of: panel)
  }
  func focusedPanel() -> WorkspacePanel? { panels.values.first { containsFocus($0.id) } }
  func refreshFocus() { for panel in panels.values { panel.showFocus(containsFocus(panel.id)) } }
  func setFocusLayout(_ focused: Bool) {
    focusLayout = focused
    if focused {
      upper.removeArrangedSubview(right); right.removeFromSuperview()
      vertical.removeArrangedSubview(lower); lower.removeFromSuperview()
    } else {
      if right.superview == nil { upper.addArrangedSubview(right) }
      if lower.superview == nil { vertical.addArrangedSubview(lower) }
      right.isHidden = false; lower.isHidden = false
    }
    vertical.adjustSubviews(); upper.adjustSubviews()
  }
  func preset(_ name: String) {
    setFocusLayout(name == "Pattern focus")
    if focusLayout { onLayout?(); return }
    if name == "Sound design" { show("samples"); show("automation") }
    if name == "Compose" { show("notes"); show(panels["graph"] == nil ? "mixer" : "graph"); show("automation") }
    vertical.adjustSubviews(); upper.adjustSubviews(); lower.adjustSubviews()
    DispatchQueue.main.async { [weak self] in
      guard let self, !self.focusLayout else { return }; self.vertical.setPosition(self.bounds.height * 0.60, ofDividerAt: 0)
      self.upper.setPosition(max(400,self.bounds.width - 660),ofDividerAt: 0); self.lower.setPosition(self.bounds.width * 0.55,ofDividerAt: 0)
    }
    onLayout?()
  }
  var state: [String: Any] { ["locations":locations,"right":right.selected ?? "","bottom":bottom.selected ?? "","secondary":secondary.selected ?? "",
    "pins":panels.mapValues(\.pinned),"focusLayout":focusLayout,
    "vertical":upper.frame.height/max(1,vertical.bounds.height),"upper":pattern.frame.width/max(1,upper.bounds.width),"lower":bottom.frame.width/max(1,lower.bounds.width)] }
  func restore(_ state: [String: Any]) {
    if let places = state["locations"] as? [String:String] { for (id,place) in places where ["right","bottom","secondary","float","hide"].contains(place) { self.place(id,at:place,select:false) } }
    for (id,pin) in state["pins"] as? [String:Bool] ?? [:] { panels[id]?.pinned = pin }
    for (tabs,key) in [(right,"right"),(bottom,"bottom"),(secondary,"secondary")] { tabs.selected = state[key] as? String; tabs.reload() }
    let focus = state["focusLayout"] as? Bool ?? false; setFocusLayout(focus)
    vertical.adjustSubviews(); upper.adjustSubviews(); lower.adjustSubviews()
    for (split,key) in [(vertical,"vertical"),(upper,"upper"),(lower,"lower")] where split.arrangedSubviews.count > 1 { let value = max(0.15,min(0.85,state[key] as? Double ?? 0.6)); split.setPosition((split.isVertical ? split.bounds.width : split.bounds.height) * value,ofDividerAt:0) }
    for win in floating.values where win.contentView != nil { win.orderFront(nil) }
  }
  func windowShouldClose(_ sender: NSWindow) -> Bool { if let id = floating.first(where: { $0.value === sender })?.key { place(id,at:"right",select:false) }; return false }
}
