import AppKit
import UniformTypeIdentifiers

extension AppController {
  var sampleBrowser: SampleBrowser? { sampleBrowserWindow?.contentView as? SampleBrowser }
  var sampleLibraryImportCanBegin: Bool {
    assetCreationCanBegin && !instrumentEditor.creation.needsCompletion && sampleBrowser?.libraryChanges.needsCompletion != true
  }
  func configureSampleBrowserCompletion(_ browser: SampleBrowser) {
    browser.imports.onTarget = { [weak self] in
      guard let self, !self.shuttingDown, !self.model.revisionToken.isEmpty,
        SampleRecordingRead.validAssets(["samples": self.model.samples, "instruments": self.model.instruments]) else { return nil }
      func ids(_ items: [[String: Any]]) -> [Int: String] {
        Dictionary(uniqueKeysWithValues: items.map { (SampleRecordingRead.integer($0["index"])!, $0["id"] as! String) })
      }
      return .init(context: .init(document: String(self.model.revisionToken.split(separator: ":").first ?? ""), revision: self.model.revisionToken),
        samples: ids(self.model.samples), instruments: ids(self.model.instruments),
        relatedState: self.instrumentCreationRelatedState + "|sample:\(self.sampleEditor.index)", instrumentDraft: self.instrumentEditor.creationDraft)
    }
    browser.imports.canBegin = { [weak self] in self?.sampleLibraryImportCanBegin == true }
    browser.imports.canReveal = { [weak self, weak browser] in
      guard let self, let browser else { return false }
      return NSApp.keyWindow === browser.window && !(browser.window?.firstResponder is NSTextView) &&
        self.sampleEditor.focusedSampleField == nil && !self.instrumentEditor.creationFields.contains { $0.currentEditor() != nil } && self.patternView.deferredKeyCount == 0
    }
    browser.imports.onImported = { [weak self] sample, identity, instrument in
      guard let self, self.sampleLibraryImportCanBegin,
        self.model.samples.contains(where: { $0["index"] as? Int == sample && $0["id"] as? String == identity }) else { return }
      self.sampleEditor.index = sample
      if instrument > 0 { self.patternView.instrument = instrument }
      else if self.model.instruments.isEmpty { self.patternView.instrument = sample }
      self.showEditor(1); self.refreshAssets()
    }
    browser.imports.onRequest = { [weak self, weak browser] method, params, reply in
      guard let self, let browser else { reply(AutomationServer.notAdmittedBusy("The sample browser is unavailable.")); return }
      self.submitSampleBrowserRequest(browser, method: method, params: params, reply: reply)
    }
    browser.libraryChanges.onState = { [weak self] in self?.sampleLibrary.status ?? [:] }
    browser.libraryChanges.canBegin = { [weak self, weak browser] in
      guard let self, let browser else { return false }
      return !self.shuttingDown && !browser.imports.needsCompletion
    }
    browser.libraryChanges.onRequest = browser.imports.onRequest
  }
  func chooseSampleLibraryFiles(_ browser: SampleBrowser) {
    guard let window = browser.window, window.attachedSheet == nil, NSApp.modalWindow == nil,
      let owner = browser.imports.begin(instruments: browser.createInstruments.state == .on) else { return }
    let panel = NSOpenPanel(); panel.title = "Load samples"; panel.allowsMultipleSelection = true; panel.allowsOtherFileTypes = true
    panel.allowedContentTypes = SampleLibraryIndex.extensions.sorted().compactMap { UTType(filenameExtension: $0) }
    panel.beginSheetModal(for: window) { [weak browser] response in
      let paths = response == .OK ? panel.urls.map(\.path) : nil
      DispatchQueue.main.async { [weak browser] in browser?.imports.finishChoosing(owner, paths: paths) }
    }
  }
  func chooseSampleLibraryFolders(_ browser: SampleBrowser) {
    guard let window = browser.window, window.attachedSheet == nil, NSApp.modalWindow == nil,
      let owner = browser.libraryChanges.begin("sample.library.roots.set", params: ["roots": sampleLibrary.roots]) else { return }
    let panel = NSOpenPanel(); panel.title = "Add sample folders"; panel.canChooseDirectories = true; panel.canChooseFiles = false; panel.allowsMultipleSelection = true
    panel.beginSheetModal(for: window) { [weak browser] response in
      let roots = response == .OK ? (owner.params["roots"] as? [String] ?? []) + panel.urls.map(\.path) : nil
      DispatchQueue.main.async { [weak browser] in browser?.libraryChanges.finishChoosing(owner, roots: roots) }
    }
  }
  func submitSampleBrowserRequest(_ browser: SampleBrowser, method: String, params: [String: Any], reply: @escaping ([String: Any]) -> Void) {
    guard !shuttingDown else { reply(AutomationServer.notAdmittedBusy("The application is shutting down.")); return }
    if (busy || sessionReading) && !method.hasPrefix("sample.library.") {
      deferUntilIdle({ [weak self, weak browser] in
        guard let self, let browser else { reply(AutomationServer.notAdmittedBusy("The sample browser is unavailable.")); return }
        self.submitSampleBrowserRequest(browser, method: method, params: params, reply: reply)
      }, cancel: { reply(AutomationServer.notAdmittedBusy("The application is shutting down; no import was submitted.")) }); return
    }
    if method == "sample.importMany" {
      guard let owner = browser.imports.operation, owner.phase == .writing, NSDictionary(dictionary: params).isEqual(to: owner.params),
        browser.imports.originalInputIsCurrent(owner) else {
        reply(AutomationServer.error(-32001, "Song or browser input changed before import.", data: ["writeOutcome": "notCommitted"])); return
      }
    } else if method == "sample.library.roots.set" || method == "sample.library.rescan" {
      guard let owner = browser.libraryChanges.operation, owner.phase == .writing, owner.method == method,
        NSDictionary(dictionary: params).isEqual(to: owner.params), browser.libraryChanges.originalInputIsCurrent(owner), browser.window?.attachedSheet == nil else {
        reply(AutomationServer.error(-32001, "Library or browser input changed before submission.", data: ["writeOutcome": "notCommitted"])); return
      }
    }
    handleAutomation(method, params: params) { [weak self] response in
      if method == "document.get", response["error"] == nil { self?.refreshAll() }
      reply(response)
    }
  }
}
