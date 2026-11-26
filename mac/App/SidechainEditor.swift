import AppKit

final class SidechainEditor: NSView, NSTableViewDataSource, NSTableViewDelegate {
  let plugin = NSPopUpButton(), input = NSPopUpButton(), source = NSPopUpButton()
  let table = NSTableView(), gain = NSTextField(string: "0")
  let pre = NSButton(checkboxWithTitle: "Pre-fader", target: nil, action: nil)
  let enabled = NSButton(checkboxWithTitle: "Enabled", target: nil, action: nil)
  let status = Theme.label("", size: 12, color: Theme.muted)
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onConfigurePlugin: ((Int) -> Void)?
  var buses = [[String: Any]](), plugins = [[String: Any]](), routes = [[String: Any]](), ports = [[String: Any]]()
  var revision = "", loading = false
  var pluginID: String?, inputIndex: Int?
  var selectedRoutes: [[String: Any]] {
    routes.filter { $0["plugin"] as? String == pluginID && $0["input"] as? Int == inputIndex }
  }
  override init(frame: NSRect) {
    super.init(frame: frame)
    plugin.target = self; plugin.action = #selector(selectPlugin)
    input.target = self; input.action = #selector(selectInput)
    plugin.setAccessibilityLabel("Sidechain receiving effect"); input.setAccessibilityLabel("Sidechain input bus")
    source.setAccessibilityLabel("Sidechain source track or bus"); gain.setAccessibilityLabel("Sidechain gain in decibels"); gain.fixed(width: 70)
    table.addTableColumn(NSTableColumn(identifier: .init("source"))); table.headerView = nil
    table.rowHeight = 35; table.dataSource = self; table.delegate = self; table.setAccessibilityLabel("Sidechain sources")
    let scroll = verticalScrollView(); scroll.documentView = table
    let explanation = Theme.label("Sources follow mute and solo. For a quiet trigger, use a pre-fader send and lower its track fader.", size: 11, color: Theme.muted)
    explanation.lineBreakMode = .byWordWrapping; explanation.maximumNumberOfLines = 2
    let content = stack(.vertical, [
      stack(.horizontal, [Theme.label("Sidechains", size: 20, weight: .semibold), NSView(),
        ActionButton("Reload") { [weak self] in self?.load() }]),
      stack(.horizontal, [Theme.label("Effect", size: 12), plugin,
        ActionButton("Audio buses…") { [weak self] in self?.configure() }]),
      stack(.horizontal, [Theme.label("Input", size: 12), input]),
      scroll,
      stack(.horizontal, [source, gain, Theme.label("dB", size: 12), pre, enabled]),
      stack(.horizontal, [ActionButton("Add / update source") { [weak self] in self?.apply() },
        ActionButton("Remove selected") { [weak self] in self?.remove() }, NSView()]),
      status, explanation,
      Theme.label("Routing changes stop playback. Undo is shared with other song edits.", size: 11, color: Theme.muted)
    ], spacing: 14)
    content.stretchAcrossAxis(); content.fill(self, inset: 20)
  }
  required init?(coder: NSCoder) { fatalError() }
  func load() {
    guard !loading, let onRequest else { return }; loading = true
    onRequest("mixer.get", [:]) { [weak self] reply in
      guard let self, let data = self.receive(reply) else { return }; self.update(data)
    }
  }
  private func receive(_ reply: [String: Any]) -> [String: Any]? {
    loading = false
    guard let result = reply["result"] as? [String: Any], let data = result["data"] as? [String: Any] else {
      status.stringValue = (reply["error"] as? [String: Any])?["message"] as? String ?? "Sidechain edit failed. Reload and try again."
      return nil
    }
    revision = result["revision"] as? String ?? revision; return data
  }
  func update(_ data: [String: Any]) {
    buses = data["buses"] as? [[String: Any]] ?? []
    plugins = (data["plugins"] as? [[String: Any]] ?? []).filter { $0["instrument"] as? Bool != true }
    routes = data["sidechains"] as? [[String: Any]] ?? []
    if !plugins.contains(where: { $0["id"] as? String == pluginID }) { pluginID = plugins.first?["id"] as? String }
    plugin.removeAllItems()
    for p in plugins { let item = NSMenuItem(title: p["name"] as? String ?? "Effect", action: nil, keyEquivalent: ""); item.representedObject = p["id"] as? String; plugin.menu?.addItem(item) }
    if let item = plugin.itemArray.first(where: { $0.representedObject as? String == pluginID }) { plugin.select(item) }
    source.removeAllItems()
    for b in buses { let item = NSMenuItem(title: b["name"] as? String ?? "Bus", action: nil, keyEquivalent: ""); item.representedObject = b["id"] as? String; source.menu?.addItem(item) }
    updatePorts()
    if buses.isEmpty { status.stringValue = "Enable the Mixer, then add an effect with an auxiliary input." }
  }
  @objc private func selectPlugin() {
    guard !loading, let id = plugin.selectedItem?.representedObject as? String else { return }
    pluginID = id; inputIndex = nil; updatePorts()
  }
  private func updatePorts() {
    let selected = plugins.first { $0["id"] as? String == pluginID }
    ports = (selected?["audioBuses"] as? [[String: Any]] ?? []).filter { $0["direction"] as? String == "input" && ($0["index"] as? Int ?? 0) > 0 }
    // Keep missing saved input routes visible so they can be removed.
    for route in routes where route["plugin"] as? String == pluginID {
      if let index = route["input"] as? Int, !ports.contains(where: { $0["index"] as? Int == index }) {
        ports.append(["index": index, "name": "Unavailable input", "active": false])
      }
    }
    if !ports.contains(where: { $0["index"] as? Int == inputIndex }) { inputIndex = ports.first?["index"] as? Int }
    input.removeAllItems()
    for port in ports {
      let title = "\((port["index"] as? Int ?? 0) + 1) · \(port["name"] as? String ?? "Input")\(port["active"] as? Bool == true ? "" : " · disabled")"
      let item = NSMenuItem(title: title, action: nil, keyEquivalent: ""); item.representedObject = port["index"] as? Int; input.menu?.addItem(item)
    }
    if let item = input.itemArray.first(where: { $0.representedObject as? Int == inputIndex }) { input.select(item) }
    table.reloadData(); resetSource()
    status.stringValue = ports.isEmpty ? "This effect has no auxiliary input. Choose a compatible effect." : "\(selectedRoutes.count) sources feed this input"
  }
  @objc private func selectInput() {
    guard !loading, let index = input.selectedItem?.representedObject as? Int else { return }
    inputIndex = index; table.reloadData(); resetSource()
  }
  private func resetSource() { gain.stringValue = "0"; pre.state = .off; enabled.state = .on; table.deselectAll(nil) }
  private func configure() {
    guard !loading, let slot = plugins.first(where: { $0["id"] as? String == pluginID })?["slot"] as? Int else { return }
    onConfigurePlugin?(slot)
  }
  private func write(_ sources: [[String: Any]]) {
    guard !loading, let onRequest, let pluginID, let inputIndex else { return }
    loading = true
    onRequest("mixer.sidechains.set", ["plugin": pluginID, "input": inputIndex, "sources": sources, "expectedRevision": revision]) { [weak self] reply in
      guard let self, self.receive(reply) != nil else { return }; self.load()
    }
  }
  func apply() {
    guard !loading, let sourceID = source.selectedItem?.representedObject as? String, let value = Double(gain.stringValue), value.isFinite else { return }
    var sources = selectedRoutes.filter { $0["source"] as? String != sourceID }.map { clean($0) }
    sources.append(["source": sourceID, "gainDB": value, "preFader": pre.state == .on, "enabled": enabled.state == .on]); write(sources)
  }
  private func clean(_ route: [String: Any]) -> [String: Any] {
    route.filter { ["source", "gainDB", "preFader", "enabled"].contains($0.key) }
  }
  func remove() {
    let index = table.selectedRow; guard selectedRoutes.indices.contains(index) else { return }
    var sources = selectedRoutes.map { clean($0) }; sources.remove(at: index); write(sources)
  }
  func numberOfRows(in tableView: NSTableView) -> Int { selectedRoutes.count }
  func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
    guard selectedRoutes.indices.contains(row) else { return nil }
    let route = selectedRoutes[row], sourceID = route["source"] as? String
    let name = buses.first { $0["id"] as? String == sourceID }?["name"] as? String ?? "Unavailable source"
    return Theme.label("\(name) · \(String(format: "%.1f", (route["gainDB"] as? NSNumber)?.doubleValue ?? 0)) dB · \(route["preFader"] as? Bool == true ? "pre" : "post")\(route["enabled"] as? Bool == false ? " · disabled" : "")", size: 12)
  }
  func tableViewSelectionDidChange(_ notification: Notification) {
    let index = table.selectedRow; guard selectedRoutes.indices.contains(index) else { return }
    let route = selectedRoutes[index]
    if let item = source.itemArray.first(where: { $0.representedObject as? String == route["source"] as? String }) { source.select(item) }
    gain.doubleValue = (route["gainDB"] as? NSNumber)?.doubleValue ?? 0
    pre.state = route["preFader"] as? Bool == true ? .on : .off; enabled.state = route["enabled"] as? Bool == true ? .on : .off
  }
}
