import AppKit
extension AppController {
  @objc func showPreciseNotes() {
    guard !busy,model.editable else{return}
    workspaceReturnPoints["notes"]=patternView.navigation
    workspace?.show("notes"); followWorkspacePanel("notes",force:true)
  }

  func clearPreciseNotes(row:Int,channel:Int) {
    guard !busy else{return}
    let pattern=model.pattern,revision=session.automationRevision
    handleAutomation("pattern.notes.get",params:["pattern":pattern]){[weak self] reply in
      guard let self,let result=reply["result"] as? [String:Any],result["revision"] as? String==revision,let data=result["data"] as? [String:Any],let events=data["events"] as? [[String:Any]] else{return}
      let replacement=events.filter{!($0["channel"] as? Int==channel && ($0["position"] as? Int ?? 0)/65536==row)}
      self.handleAutomation("pattern.notes.set",params:["pattern":pattern,"events":replacement,"clearRows":[["row":row,"channel":channel]],"expectedRevision":revision]){[weak self] reply in
        if let error=reply["error"] as? [String:Any] {self?.statusLabel.stringValue=error["message"] as? String ?? "Could not clear precise notes"}
      }
    }
  }
  func startLiveRecordingIfArmed() {
    guard midiArmed,session.playing,recordingTakeID==nil,!busy else{return}
    let first=patternView.cursorChannel
    let count=max(1,min(midiRecordColumns,model.channels-first))
    handleAutomation("recording.start",params:["channels":Array(first..<(first+count)),"instrument":patternView.instrument,
      "quantization":midiQuantization,"latencyMS":midiLatencyMS,"expectedRevision":session.automationRevision]){[weak self] reply in
      guard let self else{return}
      if let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any] {self.recordingTakeID=data["take"] as? String;self.statusLabel.stringValue="Recording precise notes"}
      else {self.statusLabel.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Could not start recording"}
    }
  }
  @objc func retryRecordingFinish() {recordingFinishing=false;finishLiveRecording()}
  @objc func finishLiveRecording() {
    guard !busy,!recordingFinishing,let take=recordingTakeID ?? session.recordingTakeID else{return}
    recordingFinishing=true
    handleAutomation("recording.stop",params:["take":take,"expectedRevision":session.automationRevision]){[weak self] reply in
      guard let self else{return}
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any] else{self.recordingFailure(reply);return}
      let drops=(data["missingTime"] as? Int ?? 0)+(data["exhaustedVoices"] as? Int ?? 0)+(data["overflow"] as? Int ?? 0)
      self.handleAutomation("recording.commit",params:["take":take,"replaceRows":true,"expectedRevision":self.session.automationRevision]){[weak self] reply in
        guard let self else{return}
        if reply["result"] != nil {self.recordingTakeID=nil;self.recordingFinishing=false;self.statusLabel.stringValue=drops>0 ? "Recording saved · \(drops) input events could not be captured; check the take" : "Recording saved · Undo restores the previous notes"}
        else {self.recordingFailure(reply)}
      }
    }
  }
  private func recordingFailure(_ reply:[String:Any]) {
    // Retain the take for API inspection; do not retry the same rejected commit
    // on every UI frame. Finish Recording Take explicitly retries it.
    recordingFinishing=true
    statusLabel.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Recording retained for review"
  }
  @objc func discardLiveRecording() {
    guard !busy,let take=recordingTakeID ?? session.recordingTakeID else{return}
    handleAutomation("recording.discard",params:["take":take,"expectedRevision":session.automationRevision]){[weak self] reply in
      guard let self else{return}
      if reply["result"] != nil {self.recordingTakeID=nil;self.recordingFinishing=false;self.statusLabel.stringValue="Recording take discarded"}
      else {self.recordingFailure(reply)}
    }
  }
}
