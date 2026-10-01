import AppKit

// Session monitor state is independent of graph selection, visibility and Undo.
// Serialize requests so Stop clicked during a pending switch is always last.
final class GraphListenControls:NSView {
  let label=Theme.label("",size:11,weight:.medium),gain=NSTextField(string:"0")
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  var currentRevision:(()->String)?,nameForPort:((String)->String)?
  var onState:((String?)->Void)?,onError:((String)->Void)?
  private(set) var port:String?,pending=false
  private var queued:(port:String?,gain:Double)?,revision=""
  private var publishedName:String?,hasPublishedState=false
  override init(frame:NSRect) {
    super.init(frame:frame);isHidden=true
    gain.fixed(width:48);gain.setAccessibilityLabel("Listen gain in dB");gain.target=self;gain.action=#selector(changeGain)
    let stop=ActionButton("Stop listening",symbol:"headphones"){[weak self] in self?.select(nil)}
    let row=stack(.horizontal,[label,NSView(),Theme.label("Monitor dB",size:10,color:Theme.muted),gain,stop],spacing:6)
    row.fill(self,inset:3)
  }
  required init?(coder:NSCoder){fatalError()}
  func select(_ port:String?,gain:Double=0) {queued=(port,gain);send()}
  @objc private func changeGain() {
    guard let port,let value=Double(gain.stringValue),value.isFinite,(-60...12).contains(value) else{onError?("Monitor gain must be between −60 and +12 dB");return}
    select(port,gain:value)
  }
  private func send() {
    guard !pending,let request=queued,let onRequest else{return};queued=nil;pending=true
    var params:[String:Any]=["port":request.port as Any? ?? NSNull(),"expectedRevision":revision.isEmpty ? currentRevision?() ?? "":revision]
    if request.port != nil {params["gainDB"]=request.gain}
    onRequest("graph.listen.set",params){[weak self] response in
      guard let self else{return};self.pending=false
      if let result=response["result"] as? [String:Any] {
        self.revision=result["revision"] as? String ?? "";self.update(result["data"] as? [String:Any] ?? [:])
      } else {
        self.onError?((response["error"] as? [String:Any])?["message"] as? String ?? "Monitor unavailable")
        if (response["error"] as? [String:Any])?["code"] as? Int == -32001 {
          if self.queued==nil {self.queued=request};self.pending=true
          onRequest("graph.listen.get",[:]){[weak self] reply in
            guard let self else{return};self.pending=false
            if let result=reply["result"] as? [String:Any] {self.revision=result["revision"] as? String ?? "";self.send()}
          };return
        }
        self.revision=""
      }
      self.send()
    }
  }
  func update(_ value:[String:Any]) {
    guard !pending else{return}
    port=value["port"] as? String
    if isHidden != (port==nil) {isHidden=port==nil}
    let available=value["available"] as? Bool ?? true
    let rawName=port.map{nameForPort?($0) ?? $0}
    let name=rawName.map{$0+(available ? "":" · unavailable in current route")}
    let text=available ? "Listening: "+(name ?? "Normal mix")+(value["pending"] as? Bool==true ? " · switching":"") : "Tap unavailable: "+(rawName ?? "Host port")+" · Stop listening to return to normal mix"
    if gain.isEnabled != available {gain.isEnabled=available}
    if label.stringValue != text {label.stringValue=text}
    if label.toolTip != port {label.toolTip=port}
    if gain.currentEditor()==nil {
      let text=String(format:"%.1f",value["gainDB"] as? Double ?? 0)
      if gain.stringValue != text {gain.stringValue=text}
    }
    // Telemetry arrives several times per second. Reassigning an unchanged
    // AppKit control value can still invalidate its native layout/restoration.
    if !hasPublishedState || publishedName != name {
      hasPublishedState=true;publishedName=name;onState?(name)
    }
  }
}

final class GraphSignalScope:NSView {
  let title=Theme.label("Signal scope",size:11,weight:.medium),detail=Theme.label("",size:10,color:Theme.muted)
  let plot=GraphSignalPlot(frame:.zero)
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  var currentRevision:(()->String)?
  var nameForPort:((String)->String)?
  private(set) var requestedPort:String?,spectrum=false
  private var activePort:String?,pending=false,revision=""
  override init(frame:NSRect) {
    super.init(frame:frame)
    plot.fixed(height:128)
    let close=ActionButton("Close"){[weak self] in self?.show(port:nil)}
    let content=stack(.vertical,[stack(.horizontal,[title,NSView(),close]),plot,detail],spacing:3)
    content.stretchAcrossAxis();content.fill(self,inset:5);isHidden=true
  }
  required init?(coder:NSCoder){fatalError()}
  func show(port:String?,spectrum:Bool=false) {
    requestedPort=port;self.spectrum=spectrum;isHidden=port==nil
    title.stringValue=(spectrum ? "Spectrum":"Waveform")+" · "+(port.map{nameForPort?($0) ?? $0} ?? "Host port")
    if port != activePort {plot.value=[:];detail.stringValue="Starting capture…"}
    synchronize()
  }
  private func synchronize() {
    guard !pending,requestedPort != activePort,let onRequest else{return}
    let captured=requestedPort;pending=true
    onRequest("graph.scope.watch",["port":captured as Any? ?? NSNull(),"expectedRevision":revision.isEmpty ? currentRevision?() ?? "":revision]){[weak self] response in
      guard let self else{return};self.pending=false
      if let result=response["result"] as? [String:Any] {
        self.activePort=captured;self.revision=result["revision"] as? String ?? self.revision
        self.synchronize()
      } else {
        self.detail.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Capture unavailable"
        self.revision=""
        if (response["error"] as? [String:Any])?["code"] as? Int == -32001 {
          // Closing a hidden graph must also retire capture after a concurrent
          // edit. Its inspector revision may no longer receive refreshes.
          self.pending=true
          onRequest("graph.scope.get",[:]){[weak self] response in
            guard let self else{return};self.pending=false
            if let result=response["result"] as? [String:Any] {self.revision=result["revision"] as? String ?? "";self.synchronize()}
          }
        }
      }
    }
  }
  func poll(visible:Bool) {
    if !visible {requestedPort=nil;isHidden=true;synchronize();return}
    guard !pending,requestedPort != nil || activePort != nil,let onRequest else{return}
    pending=true
    onRequest("graph.scope.get",["spectrum":spectrum]){[weak self] response in
      guard let self else{return};self.pending=false
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any] else{return}
      self.revision=result["revision"] as? String ?? ""
      let observed=data["port"] as? String
      if observed != self.activePort {self.activePort=observed}
      if self.requestedPort != self.activePort {self.synchronize();return}
      guard self.requestedPort != nil else{return}
      let available=data["available"] as? Bool ?? true,fresh=data["fresh"] as? Bool ?? true,active=data["active"] as? Bool==true
      var display=data
      if !available || !fresh {display["waveform"]=[];display["spectrum"]=[];display["frames"]=0;display["fftFrames"]=0}
      self.plot.spectrum=self.spectrum;self.plot.value=display
      let rate=data["sampleRate"] as? Double ?? 48000,frames=display["frames"] as? Int ?? 0
      let state = !available ? "Tap unavailable in current route · choose another port or close" : !fresh ? (active ? "Waiting for current-route samples":"Stopped · no current capture") : active ? (frames>0 ? String(format:"%.1f ms",Double(frames)*1000/max(1,rate)):"Waiting for samples"):"Stopped · retained capture"
      self.detail.stringValue="\(state) · \(data["dropped"] ?? 0) dropped capture frames · \(data["invalid"] ?? 0) invalid samples"
      self.detail.toolTip=observed
    }
  }
}

final class GraphSignalPlot:NSView {
  var spectrum=false
  var value=[String:Any]() {didSet{needsDisplay=true;setAccessibilityValue(value["available"] as? Bool==false ? "Host port unavailable in current route":value["fresh"] as? Bool==false ? "Waiting for current-route capture":value["active"] as? Bool==true ? "Live host-port capture":"Stopped host-port capture")}}
  override init(frame:NSRect){super.init(frame:frame);setAccessibilityElement(true);setAccessibilityRole(.image);setAccessibilityLabel("Measured signal scope")}
  required init?(coder:NSCoder){fatalError()}
  override func draw(_ dirtyRect:NSRect) {
    Theme.bg.setFill();bounds.fill();let rect=bounds.insetBy(dx:48,dy:16)
    guard rect.width>0,rect.height>0 else{return}
    let attrs:[NSAttributedString.Key:Any]=[.font:NSFont.monospacedSystemFont(ofSize:9,weight:.regular),.foregroundColor:Theme.muted]
    func label(_ text:String,_ point:NSPoint){(text as NSString).draw(at:point,withAttributes:attrs)}
    Theme.border.setStroke();let grid=NSBezierPath();grid.lineWidth=0.5
    for n in 0...4{let y=rect.minY+rect.height*CGFloat(n)/4;grid.move(to:NSPoint(x:rect.minX,y:y));grid.line(to:NSPoint(x:rect.maxX,y:y))};grid.stroke()
    if spectrum {
      let bins=value["spectrum"] as? [Double] ?? [],count=value["fftFrames"] as? Double ?? 0,rate=value["sampleRate"] as? Double ?? 48000
      guard count>0,bins.count>1,rate.isFinite,rate>40 else{label("Waiting for a contiguous capture",NSPoint(x:rect.minX,y:rect.midY));return}
      label("0 dB",NSPoint(x:0,y:rect.maxY-6));label("−96",NSPoint(x:0,y:rect.minY))
      let low=20.0,high=rate/2,path=NSBezierPath();var started=false
      for (index,level) in bins.enumerated() {
        let frequency=Double(index)*rate/count;guard frequency>=low,frequency<=high,level.isFinite,level>=0 else{continue}
        let x=rect.minX+rect.width*log(frequency/low)/log(high/low),y=rect.minY+rect.height*max(0,min(1,(20*log10(max(1e-12,level))+96)/96))
        let p=NSPoint(x:x,y:y);if started{path.line(to:p)}else{path.move(to:p);started=true}
      }
      Theme.gold.setStroke();path.lineWidth=1.1;path.stroke()
      for frequency in [20.0,100,1000,10000] where frequency<high {label(frequency<1000 ? "\(Int(frequency)) Hz":"\(Int(frequency/1000))k",NSPoint(x:rect.minX+rect.width*log(frequency/low)/log(high/low),y:0))}
    } else {
      let buckets=value["waveform"] as? [[String:Any]] ?? []
      guard !buckets.isEmpty else{label("Waiting for samples",NSPoint(x:rect.minX,y:rect.midY));return}
      let extrema=buckets.flatMap{($0["minimum"] as? [Double] ?? [])+($0["maximum"] as? [Double] ?? [])}.filter{$0.isFinite}.map{abs($0)}
      let range=pow(2,ceil(log2(max(0.001,extrema.max() ?? 0))))
      label(String(format:"%.3g",range),NSPoint(x:0,y:rect.maxY-6));label(String(format:"−%.3g",range),NSPoint(x:0,y:rect.minY))
      for channel in 0..<2 {
        let path=NSBezierPath();path.lineWidth=1
        for (index,bucket) in buckets.enumerated() {
          guard let minima=bucket["minimum"] as? [Double],let maxima=bucket["maximum"] as? [Double],minima.count==2,maxima.count==2,minima[channel].isFinite,maxima[channel].isFinite else{continue}
          let x=rect.minX+rect.width*Double(index)/Double(max(1,buckets.count-1))
          path.move(to:NSPoint(x:x,y:rect.midY+rect.height/2*max(-1,min(1,minima[channel]/range))))
          path.line(to:NSPoint(x:x,y:rect.midY+rect.height/2*max(-1,min(1,maxima[channel]/range))))
        }
        (channel==0 ? Theme.accent:NSColor.systemBlue.withAlphaComponent(0.65)).setStroke();path.stroke()
      }
      label("L / R · auto scale (linear full-scale units) · min/max buckets",NSPoint(x:rect.minX,y:0))
    }
  }
}
