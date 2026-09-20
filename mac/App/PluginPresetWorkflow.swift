import Foundation

// Native preset dialogs and external clients share the revision-checked API.
// The chosen plugin and song revision stay pinned across asynchronous file reads.
final class PluginPresetWorkflow {
  private(set) var busy = false
  var onRequest: ((String, [String: Any], @escaping ([String: Any]) -> Void) -> Void)?
  var onFinish: ((Bool, String) -> Void)?
  func save(path: String, name: String, plugin: String, revision: String) {
    guard !busy, let onRequest else { return }; busy = true
    onRequest("plugin.preset.save", ["path": path, "name": name, "plugin": plugin, "expectedRevision": revision, "overwrite": true]) { [weak self] reply in
      guard let self else { return }
      guard let result = reply["result"] as? [String: Any], let data = result["data"] as? [String: Any], data["written"] as? Bool == true else {
        self.fail(reply); return
      }
      self.finish(true, "Saved preset · \(name)")
    }
  }
  func load(path: String, plugin: String, revision: String) {
    guard !busy, let onRequest else { return }; busy = true
    onRequest("plugin.preset.inspect", ["path": path]) { [weak self] reply in
      guard let self else { return }
      guard let result = reply["result"] as? [String: Any], let data = result["data"] as? [String: Any],
        let presetRevision = data["presetRevision"] as? String else { self.fail(reply); return }
      guard result["revision"] as? String == revision else {
        self.finish(false, "The song changed while choosing a preset. Choose it again to use the current song."); return
      }
      onRequest("plugin.preset.load", ["path": path, "plugin": plugin, "expectedRevision": revision, "expectedPresetRevision": presetRevision]) { [weak self] reply in
        guard let self else { return }
        guard let result = reply["result"] as? [String: Any], let loaded = result["data"] as? [String: Any], loaded["loaded"] as? Bool == true else {
          self.fail(reply); return
        }
        self.finish(true, "Loaded preset · \(data["name"] as? String ?? "Preset") · Undo effect change to restore")
      }
    }
  }
  private func fail(_ reply: [String: Any]) {
    finish(false, (reply["error"] as? [String: Any])?["message"] as? String ?? "The preset operation could not be completed.")
  }
  private func finish(_ success: Bool, _ message: String) { busy = false; onFinish?(success, message) }
}
