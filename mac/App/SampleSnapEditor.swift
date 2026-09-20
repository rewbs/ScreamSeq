import AppKit

extension SampleEditor {
  func makeSnapControls() -> NSView {
    snapMode.addItems(withTitles:["Zero crossings","Frame grid"])
    snapMode.setAccessibilityLabel("Sample boundary snapping mode");snapMode.fixed(width:155)
    snapMode.target=self;snapMode.action=#selector(updateSnapOptions)
    snapStatus.maximumNumberOfLines=2;snapStatus.lineBreakMode = .byWordWrapping
    updateSnapOptions()
    return stack(.horizontal,[labeled("SNAP",snapMode),snapRadiusGroup,snapStepGroup,snapOriginGroup,NSView()],spacing:14)
  }
  func makeSnapActions() -> NSView {
    stack(.horizontal,[ActionButton("Snap selection") { [weak self] in self?.snapBoundaries(loop:false) },
      ActionButton("Snap loop") { [weak self] in self?.snapBoundaries(loop:true) },NSView(),snapAutomatically],spacing:10)
  }
  @objc func updateSnapOptions() {
    let grid=snapMode.indexOfSelectedItem==1
    snapRadiusGroup.isHidden=grid;snapStepGroup.isHidden = !grid;snapOriginGroup.isHidden = !grid
  }
  func snapAfterSelection() {
    guard snapAutomatically.state == .on else { return }
    if snapBusy { snapPending=true } else { snapBoundaries(loop:false) }
  }
  private var snapSignature:[String] {
    [String(snapMode.indexOfSelectedItem),snapRadius.stringValue,snapStep.stringValue,snapOrigin.stringValue,selectedChannels]
  }
  func snapBoundaries(loop:Bool) {
    guard !snapBusy,!drawingBusy,let onRequest,let revision=sampleRevision else { return }
    let fields=loop ? [loopStart,loopEnd] : [selectionStart,selectionEnd]
    let original=fields.map(\.stringValue)
    guard let first=Int(original[0]),let last=Int(original[1]),first>=0,first<=last,last<=waveform.frames else {
      snapStatus.stringValue="Enter boundaries inside the sample before snapping.";return
    }
    var params:[String:Any]=["sample":index,"positions":[first,last]]
    if snapMode.indexOfSelectedItem==1 {
      guard let step=Int(snapStep.stringValue),(1...268435456).contains(step),
        let origin=Int(snapOrigin.stringValue),(0...waveform.frames).contains(origin) else {
        snapStatus.stringValue="Enter a positive grid step and an origin inside the sample.";return
      }
      params["mode"]="grid";params["step"]=step;params["origin"]=origin
    } else {
      guard let radius=Int(snapRadius.stringValue),(0...65536).contains(radius) else {
        snapStatus.stringValue="Search radius must be 0…65,536 frames.";return
      }
      params["mode"]="zero";params["radius"]=radius;params["channels"]=selectedChannels
    }
    let sample=index,generation=sampleGeneration,selection=selectionVersion,signature=snapSignature
    waveform.cancelStroke();snapBusy=true;snapStatus.stringValue="Finding boundaries…"
    onRequest("sample.snap.get",params) { [weak self] response in
      guard let self else { return };self.snapBusy=false
      defer { if self.snapPending { self.snapPending=false;self.snapAfterSelection() } }
      guard sample==self.index,generation==self.sampleGeneration,self.sampleRevision==revision else { return }
      guard self.snapSignature==signature,fields.map(\.stringValue)==original,
        loop || self.selectionVersion==selection else {
        self.snapStatus.stringValue="Boundaries or snap settings changed; the result was discarded.";return
      }
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any],
        let positions=data["positions"] as? [[String:Any]],positions.count==2,
        let start=positions[0]["after"] as? Int,let end=positions[1]["after"] as? Int else {
        self.snapStatus.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Snapping failed.";return
      }
      guard result["revision"] as? String==revision else { self.snapStatus.stringValue="The sample changed; try snapping again.";return }
      guard start>=0,start<=end,end<=self.waveform.frames,!loop || start<end else {
        self.snapStatus.stringValue="Snapping would collapse or reverse the loop; widen its boundaries.";return
      }
      let matched=positions.filter { $0["matched"] as? Bool == true }.count
      if loop {
        self.loopStart.stringValue=String(start);self.loopEnd.stringValue=String(end)
        self.snapStatus.stringValue="Matched \(matched)/2 boundaries · \(start) → \(end). Apply to save the loop."
      } else {
        self.waveform.selection=start...end
        self.snapStatus.stringValue="Matched \(matched)/2 boundaries · selected \(start) → \(end)."
      }
    }
  }
}
