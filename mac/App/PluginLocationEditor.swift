import AppKit
import UniformTypeIdentifiers

// A location belongs to one captured document and stable processor identity.
// Reads never execute a bundle; scanning and reconnecting are explicit actions.
final class PluginLocationEditor: NSView, NSTextFieldDelegate {
  let path=NSTextField(string:""),candidates=NSPopUpButton()
  let saved=Theme.label("Reading saved location…",size:11),status=Theme.label("",size:11,color:Theme.muted)
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  var onContext:(()->(document:String,revision:String))?
  var onClose:(()->Void)?
  let target:[String:String]
  private let method:String
  private(set) var capturedDocument="",revision="",pending=false,uncertain=false
  private var generation=0,verified=[[String:Any]](),observedRevision:String?
  private var buttons=[NSButton]()
  private var observedGeneration: Int?, observedPath: String?
  private var hasMarkedText: Bool { (path.currentEditor() as? NSTextView)?.hasMarkedText() == true }
  init(target:[String:String]) {
    self.target=target;method=target["plugin"] == nil ? "graph.plugin.path":"plugin.path"
    super.init(frame:.zero)
    path.placeholderString="Choose a VST3 bundle to scan";path.delegate=self
    path.setAccessibilityLabel("VST3 bundle path");candidates.setAccessibilityLabel("Verified matching VST3 bundles")
    candidates.addItem(withTitle:"Choose a verified matching bundle…")
    candidates.target=self;candidates.action=#selector(selectCandidate)
    for control in [path,candidates] as [NSControl] {control.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
    candidates.cell?.lineBreakMode = .byTruncatingMiddle
    saved.isSelectable=true
    for label in [saved,status] {label.maximumNumberOfLines=0;label.lineBreakMode = .byWordWrapping;label.preferredMaxLayoutWidth=620;label.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)}
    let help=Theme.label("Scan a local VST3 bundle, then choose a verified match. Reconnect preserves the saved sound and routing, stops playback, and can be undone.",size:11,color:Theme.muted)
    help.maximumNumberOfLines=0;help.lineBreakMode = .byWordWrapping;help.preferredMaxLayoutWidth=620
    help.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    let browse=ActionButton("Browse…"){[weak self] in self?.browse()}
    buttons=[ActionButton("Read current location"){[weak self] in self?.load()},
      ActionButton("Scan bundle"){[weak self] in self?.scan()},
      ActionButton("Check reconnect"){[weak self] in self?.reconnect(dryRun:true)},
      ActionButton("Reconnect"){[weak self] in self?.reconnect()},
      ActionButton("Accept reviewed state"){[weak self] in self?.acceptObservedState()}]
    let content=stack(.vertical,[saved,help,stack(.horizontal,[path,browse]),candidates,
      stack(.horizontal,Array(buttons.prefix(4))),buttons[4],status,
      ActionButton("Close"){[weak self] in self?.onClose?()}],spacing:12)
    content.stretchAcrossAxis();content.fill(self,inset:16);controls()
  }
  required init?(coder:NSCoder){fatalError()}
  func controlTextDidChange(_ notification:Notification){generation+=1;controls()}
  private func browse() {
    let panel=NSOpenPanel();panel.canChooseFiles=true;panel.canChooseDirectories=false
    panel.treatsFilePackagesAsDirectories=false;panel.allowedContentTypes=[UTType(filenameExtension:"vst3",conformingTo:.bundle) ?? .bundle]
    let finish:(NSApplication.ModalResponse)->Void = {[weak self] response in
      guard let self,response == .OK,let url=panel.url else{return}
      self.path.stringValue=url.path;self.generation+=1;self.controls()
    }
    if let window {panel.beginSheetModal(for:window,completionHandler:finish)}else{panel.begin(completionHandler:finish)}
  }
  @objc func selectCandidate() {
    let index=candidates.indexOfSelectedItem-1
    guard verified.indices.contains(index),let value=verified[index]["path"] as? String else{return}
    path.stringValue=value;generation+=1;controls()
  }
  private var selection:[String:Any]? {
    let index=candidates.indexOfSelectedItem-1
    guard verified.indices.contains(index),verified[index]["path"] as? String==path.stringValue else{return nil}
    return verified[index]
  }
  private func contextMatches()->Bool {onContext?().document==capturedDocument && !capturedDocument.isEmpty}
  private func admit(write:Bool)->Bool {
    guard !pending,onRequest != nil else{return false}
    guard !hasMarkedText else {status.stringValue="Finish composing the path before continuing.";return false}
    if capturedDocument.isEmpty {capturedDocument=onContext?().document ?? ""}
    guard contextMatches()else{status.stringValue="This location belongs to a different document. Its fields have been retained.";return false}
    if write {
      guard !uncertain else{status.stringValue="Read the current location and accept the reviewed state before another operation.";return false}
      guard !revision.isEmpty,onContext?().revision==revision else{status.stringValue="The song changed. Read the current location before continuing; your path will be retained.";return false}
    }
    return true
  }
  func load() {
    guard admit(write:false)else{return}
    read(method+".get",params:target)
  }
  func scan() {
    guard admit(write:true),!path.stringValue.isEmpty else{return}
    read(method+".scan",params:target.merging(["path":path.stringValue,"expectedRevision":revision]){_,new in new})
  }
  private func read(_ operation:String,params:[String:Any]) {
    let oldGeneration=generation,oldPath=path.stringValue,oldSelection=selection?["path"] as? String
    pending=true;observedRevision=nil;controls()
    onRequest?(operation,params){[weak self] reply in
      guard let self else{return};self.pending=false;defer{self.controls()}
      guard self.contextMatches()else{self.status.stringValue="Document changed during the read. Fields retained.";return}
      guard let result=reply["result"] as? [String:Any],let revision=result["revision"] as? String,
        let data=result["data"] as? [String:Any],self.target.allSatisfy({data[$0.key] as? String==$0.value}),
        let descriptor=data["descriptor"] as? [String:Any],let savedPath=descriptor["path"] as? String,
        let rawEntries=data["candidates"] as? [[String:Any]],
        let entries=self.locationCandidates(rawEntries),entries.allSatisfy({
          guard let path=$0["path"] as? String,let hash=$0["moduleSHA256"] as? String else{return false}
          return !path.isEmpty && hash.count==64 && hash.allSatisfy{"0123456789abcdef".contains($0)}
        }) else {
        if operation.hasSuffix(".scan") {self.uncertain=true}
        self.status.stringValue=self.error(reply,"Could not read the plugin location.");return
      }
      guard self.generation==oldGeneration,self.path.stringValue==oldPath,
        self.selection?["path"] as? String==oldSelection,!self.hasMarkedText else {
        if operation.hasSuffix(".scan") {self.uncertain=true}
        self.status.stringValue="Newer path fields kept at their captured revision. Read the current location again to review them.";return
      }
      self.revision=revision;self.observedRevision=revision;self.saved.stringValue="Saved location: "+savedPath
      // Preserve a candidate selected while the request was in flight. A scan
      // lists all verified matches; it must never silently pick the first one.
      let chosen=self.generation==oldGeneration ? oldSelection:self.selection?["path"] as? String
      self.verified=entries;self.candidates.removeAllItems();self.candidates.addItem(withTitle:"Choose a verified matching bundle…")
      for entry in entries {self.candidates.addItem(withTitle:entry["path"] as! String)}
      if let chosen,let index=entries.firstIndex(where:{$0["path"] as? String==chosen}) {self.candidates.selectItem(at:index+1)}
      if oldPath.isEmpty,self.path.stringValue==oldPath,self.generation==oldGeneration {self.path.stringValue=savedPath}
      self.observedGeneration=self.generation;self.observedPath=self.path.stringValue
      self.status.stringValue=self.uncertain ? "Current state read. Review the saved path, then accept this state before retrying.":"Choose a verified match to reconnect. Scanning does not change the song."
    }
  }
  func acceptObservedState() {
    guard !pending,!hasMarkedText,uncertain,contextMatches(),let observedRevision,onContext?().revision==observedRevision,
      observedGeneration==generation,observedPath==path.stringValue else {
      status.stringValue="Read the current location again before accepting the reviewed state.";return
    }
    uncertain=false;status.stringValue="Current state accepted. Your path is retained.";controls()
  }
  func reconnect(dryRun:Bool=false) {
    guard admit(write:true)else{return}
    guard let selected=selection,let hash=selected["moduleSHA256"] as? String else{status.stringValue="Scan and select a verified matching bundle for this exact path.";return}
    var params:[String:Any]=target;params["path"]=path.stringValue;params["expectedModuleSHA256"]=hash
    params["expectedRevision"]=revision;params["dryRun"]=dryRun
    let submittedPath=path.stringValue,submittedGeneration=generation,submittedSelection=selection?["path"] as? String
    pending=true;observedRevision=nil;controls()
    onRequest?(method+".set",params){[weak self] reply in
      guard let self else{return};self.pending=false;defer{self.controls()}
      guard self.contextMatches()else{self.uncertain = !dryRun;self.status.stringValue="The original document's operation completed after a document change. Fields retained.";return}
      guard let result=reply["result"] as? [String:Any],let revision=result["revision"] as? String,
        let data=result["data"] as? [String:Any],self.target.allSatisfy({data[$0.key] as? String==$0.value}),
        data["path"] as? String==submittedPath,data["moduleSHA256"] as? String==hash,data["dryRun"] as? Bool==dryRun else {
        let detail=(reply["error"] as? [String:Any])?["data"] as? [String:Any]
        self.uncertain = !dryRun && detail?["writeOutcome"] as? String != "notCommitted"
        self.status.stringValue=self.error(reply,"The result is unavailable. Read the current location before retrying.");return
      }
      if !dryRun {self.saved.stringValue="Saved location: "+submittedPath}
      guard self.generation==submittedGeneration,self.path.stringValue==submittedPath,
        self.selection?["path"] as? String==submittedSelection,!self.hasMarkedText else {
        self.status.stringValue="Operation completed for the submitted path. Newer input keeps its captured revision; read the current location before continuing.";return
      }
      self.revision=revision
      self.status.stringValue=dryRun ? "Verified match; the song was not changed.":"Location accepted. Your current path field is retained. Undo restores the previous location."
    }
  }
  private func locationCandidates(_ entries:[[String:Any]])->[[String:Any]]? {
    var result=[[String:Any]]()
    for entry in entries {
      guard let descriptor=entry["descriptor"] as? [String:Any],let path=descriptor["path"] as? String,
        let hash=entry["moduleSHA256"] as? String else{return nil}
      result.append(["path":path,"moduleSHA256":hash])
    }
    return result
  }
  private func error(_ reply:[String:Any],_ fallback:String)->String {(reply["error"] as? [String:Any])?["message"] as? String ?? fallback}
  private func controls() {
    guard buttons.count==5 else{return}
    buttons[0].isEnabled = !pending
    buttons[1].isEnabled = !pending && !uncertain
    buttons[2].isEnabled = !pending && !uncertain && selection != nil
    buttons[3].isEnabled=buttons[2].isEnabled
    buttons[4].isHidden = !uncertain;buttons[4].isEnabled = !pending && observedRevision != nil
  }
}

final class PluginLocationWindow:NSWindow {
  let editor:PluginLocationEditor
  init(target:[String:String]) {
    editor=PluginLocationEditor(target:target)
    super.init(contentRect:NSRect(x:0,y:0,width:700,height:360),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    title="Plugin location";isReleasedWhenClosed=false;contentView=editor;minSize=NSSize(width:680,height:360)
    editor.onClose={[weak self] in self?.performClose(nil)}
  }
}
