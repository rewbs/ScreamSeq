import AppKit
import UniformTypeIdentifiers

extension AppController {
  func showPluginPreset(slot: Int, saving: Bool) {
    guard !busy, window.attachedSheet == nil, !presetWorkflow.busy, model.nativePlugins.indices.contains(slot),
      let identity = model.nativePlugins[slot]["instanceID"] as? String else { return }
    let revision = session.automationRevision
    let name = model.nativePlugins[slot]["name"] as? String ?? "Plugin"
    let type = UTType(filenameExtension: "screamseq-preset", conformingTo: .data) ?? .data
    presetWorkflow.onRequest = { [weak self] method, params, reply in self?.handleAutomation(method, params: params, reply: reply) }
    presetWorkflow.onFinish = { [weak self] success, message in
      guard let self else { return }; self.statusLabel.stringValue = message
      if success { self.refreshPlugins() }
      else { self.show(NSError(domain: "ScreamSeq", code: 1, userInfo: [NSLocalizedDescriptionKey: message])) }
    }
    if saving {
      let panel = NSSavePanel(); panel.allowedContentTypes = [type]; panel.canCreateDirectories = true
      panel.title = "Save plugin preset"; panel.message = "Save the current settings for \(name)."
      panel.nameFieldStringValue = name.replacingOccurrences(of: "/", with: "-") + ".screamseq-preset"
      panel.beginSheetModal(for: window) { [weak self] response in
        guard response == .OK, let url = panel.url else { return }
        DispatchQueue.main.async { self?.presetWorkflow.save(path: url.path, name: url.deletingPathExtension().lastPathComponent, plugin: identity, revision: revision) }
      }
    } else {
      let panel = NSOpenPanel(); panel.allowedContentTypes = [type, UTType(filenameExtension: "resonance-preset", conformingTo: .data) ?? .data]; panel.allowsMultipleSelection = false
      panel.canChooseDirectories = false; panel.title = "Load plugin preset"
      panel.message = "Choose settings for \(name). Loading stops playback and can be undone with Undo effect change."
      panel.beginSheetModal(for: window) { [weak self] response in
        guard response == .OK, let url = panel.url else { return }
        DispatchQueue.main.async { self?.presetWorkflow.load(path: url.path, plugin: identity, revision: revision) }
      }
    }
  }
}
