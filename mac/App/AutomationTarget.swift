import AppKit

extension PatternAutomationEditor {
  func useLastTouched() {
    guard !hasDraft, !loading else {
      status.stringValue = "Apply or reload the draft before learning a target."; return
    }
    guard let send = onRequest, !revision.isEmpty,
      let patternID = model.patterns.first(where: { $0["index"] as? Int == model.pattern })?["id"] as? String else {
      status.stringValue = "Reload the pattern before learning a target."; return
    }
    let generation = draftGeneration, document = revision.split(separator: ":").first
    var readRevision: String?
    loading = true
    status.stringValue = "Finding the last touched parameter…"
    // Keep all responses local until the complete, same-revision read succeeds.
    // A gesture made while waiting must survive, including its original target.
    func fail(_ message: String) { loading = false; status.stringValue = message }
    func read(_ method: String, _ params: [String: Any], _ done: @escaping (Any) -> Void) {
      send(method, params) { [weak self] reply in
        guard let self else { return }
        guard self.draftGeneration == generation, !self.hasDraft else {
          fail("Your draft changed while learning. Apply or reload it first."); return
        }
        guard let result = reply["result"] as? [String: Any], let token = result["revision"] as? String,
          let data = result["data"] else {
          fail((reply["error"] as? [String: Any])?["message"] as? String ?? "Cannot read the automation target."); return
        }
        guard token.split(separator: ":").first == document, readRevision == nil || readRevision == token else {
          fail("Song changed while learning. Reload and try again."); return
        }
        readRevision = token; done(data)
      }
    }
    read("document.get", [:]) { raw in
      guard let data = raw as? [String: Any] else { fail("Cannot read the song."); return }
      var next = PatternModel(data)
      guard let pattern = next.patterns.first(where: { $0["id"] as? String == patternID })?["index"] as? Int else {
        fail("This pattern is no longer available. Reload first."); return
      }
      next.pattern = pattern
      read("automation.target.get", [:]) { raw in
        guard let target = (raw as? [String: Any])?["target"] as? [String: Any] else {
          fail("Move a plugin parameter first, then choose Use last touched."); return
        }
        guard target["available"] as? Bool == true, let instance = target["plugin"] as? String,
          let parameter = target["parameter"] as? Int,
          let slot = next.nativePlugins.firstIndex(where: { $0["instanceID"] as? String == instance }),
          target["slot"] as? Int == slot else {
          fail(target["reason"] as? String ?? "The last touched parameter is unavailable."); return
        }
        read("automation.pattern.get", ["pattern": pattern]) { raw in
          guard let envelopes = raw as? [String: Any], envelopes["patternID"] as? String == patternID,
            let rows = envelopes["rows"] as? Int, rows > 0 else {
            fail("The pattern changed while learning. Reload first."); return
          }
          read("plugin.parameters.get", ["slot": slot]) { raw in
            guard let parameters = raw as? [[String: Any]], parameters.contains(where: { $0["id"] as? Int == parameter }) else {
              fail("The plugin no longer exposes this parameter."); return
            }
            self.model = next; self.revision = readRevision!; self.pluginIndex = slot
            self.heading.stringValue = "Pattern \(pattern) · automation"
            self.plugin.removeAllItems()
            for (i, item) in next.nativePlugins.enumerated() { self.plugin.addItem(withTitle: "\(i + 1). \(item["name"] ?? "Plugin")") }
            self.plugin.selectItem(at: slot)
            self.lanes = envelopes["lanes"] as? [[String: Any]] ?? []
            self.canvas.rows = rows; self.canvas.selected = nil
            self.toolStart.stringValue = "0"; self.toolEnd.stringValue = String(rows)
            self.search.stringValue = ""; self.values = parameters
            self.table.deselectAll(nil); self.filter()
            self.loading = false
            let row = self.filtered.firstIndex { $0["id"] as? Int == parameter }!
            self.table.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
            self.table.scrollRowToVisible(row)
            self.status.stringValue = "Selected \(target["pluginName"] ?? "plugin") · \(target["name"] ?? "parameter"). "
              + (self.laneID == nil ? "Create points, then Apply to save an envelope." : "Existing envelope loaded; changes still require Apply.")
          }
        }
      }
    }
  }
}
