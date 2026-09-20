import AppKit

final class PluginPortRow: NSTableCellView {
  let toggle = NSButton(checkboxWithTitle: "", target: nil, action: nil)
  var onChange: ((Bool) -> Void)?
  override init(frame: NSRect) {
    super.init(frame: frame)
    toggle.target = self; toggle.action = #selector(changed); toggle.fill(self, inset: 7)
  }
  required init?(coder: NSCoder) { fatalError() }
  @objc private func changed() { onChange?(toggle.state == .on) }
}

final class PluginPortsEditor: NSView, NSTableViewDataSource, NSTableViewDelegate {
  let table = NSTableView(), status = Theme.label("", size: 12, color: Theme.muted)
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var slot = 0, revision = "", identity: String?, loading = false
  var buses = [[String: Any]]()
  override init(frame: NSRect) {
    super.init(frame: frame)
    table.addTableColumn(NSTableColumn(identifier: .init("port")))
    table.headerView = nil; table.rowHeight = 38; table.dataSource = self; table.delegate = self
    table.setAccessibilityLabel("Plugin audio inputs and outputs")
    let scroll = verticalScrollView(); scroll.documentView = table
    let content = stack(.vertical, [
      stack(.horizontal, [Theme.label("Plugin audio buses", size: 20, weight: .semibold), NSView(),
        ActionButton("Reload") { [weak self] in self?.load() }]),
      Theme.label("Enable outputs here, then choose their destinations in the Mixer.", size: 12),
      scroll, status,
      Theme.label("Main buses stay enabled. Changes stop playback and have one plugin Undo.", size: 11, color: Theme.muted)
    ], spacing: 14)
    content.stretchAcrossAxis(); content.fill(self, inset: 20)
  }
  required init?(coder: NSCoder) { fatalError() }
  func open(slot: Int) { guard !loading else { return }; self.slot = slot; identity = nil; buses = []; table.reloadData(); load() }
  func load() {
    guard !loading, let onRequest else { return }
    loading = true
    onRequest("plugin.buses.get", ["slot": slot]) { [weak self] reply in self?.receive(reply) }
  }
  private func receive(_ reply: [String: Any]) {
    loading = false
    guard let result = reply["result"] as? [String: Any], let data = result["data"] as? [String: Any] else {
      status.stringValue = (reply["error"] as? [String: Any])?["message"] as? String ?? "Could not read plugin buses."
      table.reloadData(); return
    }
    if let found = data["plugin"] as? String {
      if let identity, identity != found {
        buses = []; table.reloadData(); status.stringValue = "The plugin moved. Reopen Audio buses from the plugin you want."; return
      }
      identity = found
    }
    revision = result["revision"] as? String ?? revision
    buses = data["buses"] as? [[String: Any]] ?? []
    status.stringValue = "\(buses.filter { $0["active"] as? Bool == true }.count) enabled buses · mono and stereo supported"
    table.reloadData()
  }
  func setActive(row: Int, enabled: Bool) {
    guard !loading, buses.indices.contains(row), let onRequest,
      let index = buses[row]["index"] as? Int, index > 0, buses[row]["supported"] as? Bool == true,
      let direction = buses[row]["direction"] as? String else { return }
    var indices = buses.filter { $0["direction"] as? String == direction && $0["active"] as? Bool == true }
      .compactMap { $0["index"] as? Int }.filter { $0 != 0 && $0 != index }
    if enabled { indices.append(index) }
    loading = true; table.reloadData()
    onRequest("plugin.buses.set", ["slot": slot, direction == "input" ? "inputs" : "outputs": indices.sorted(), "expectedRevision": revision]) {
      [weak self] reply in self?.receive(reply)
    }
  }
  func numberOfRows(in tableView: NSTableView) -> Int { buses.count }
  func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
    guard buses.indices.contains(row) else { return nil }
    let id = NSUserInterfaceItemIdentifier("port")
    let cell = tableView.makeView(withIdentifier: id, owner: self) as? PluginPortRow ?? PluginPortRow(frame: .zero)
    cell.identifier = id
    let bus = buses[row], index = bus["index"] as? Int ?? 0, channels = bus["channels"] as? Int ?? 0
    let input = bus["direction"] as? String == "input"
    let name = bus["name"] as? String ?? "", label = name.isEmpty ? "\(input ? "Input" : "Output") \(index + 1)" : name
    let format = channels == 1 ? "Mono" : channels == 2 ? "Stereo" : "\(channels) channels · unsupported"
    cell.toggle.title = "\(input ? "IN" : "OUT")  \(index + 1) · \(label) · \(format)"
    cell.toggle.state = bus["active"] as? Bool == true ? .on : .off
    cell.toggle.isEnabled = !loading && index > 0 && bus["supported"] as? Bool == true
    cell.onChange = { [weak self] enabled in self?.setActive(row: row, enabled: enabled) }
    return cell
  }
}
