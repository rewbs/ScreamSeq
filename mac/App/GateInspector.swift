import AppKit

struct GateShapeModel {
  static let shapes=["direct","linear","ease-in","ease-out","ease-in-out"]
  static let defaults:[String:Any]=["target":"volume","floor":0.0,"ceiling":1.0,"period":0.5,"phase":0.0,"width":0.5,"rise":1.0/32,"fall":1.0/32,"riseShape":"linear","fallShape":"linear","linked":true,"binding":0,"pitchRange":2]
  var parameters=defaults
  func number(_ key:String)->Double {(parameters[key] as? NSNumber)?.doubleValue ?? 0}
  var target:String {parameters["target"] as? String ?? "volume"}
  func effective(_ edge:String)->Double {parameters[edge+"Shape"] as? String=="direct" ? 0 : min(number(edge),number("period")*(edge=="rise" ? number("width") : 1-number("width")))}
  func value(_ beat:Double)->Double {
    let low=number("floor"),high=number("ceiling"),width=number("width"),period=number("period")
    if width<=0{return low};if width>=1{return high};guard period>0 else{return low}
    let phase=(beat+number("phase")).truncatingRemainder(dividingBy:period)
    let p=phase<0 ? phase+period:phase,edge=period*width,rise=effective("rise"),fall=effective("fall")
    func ease(_ t:Double,_ name:String)->Double {switch parameters[name] as? String {case "ease-in":return t*t;case "ease-out":return t*(2-t);case "ease-in-out":return t*t*(3-2*t);default:return t}}
    if rise>0,p<rise{return low+(high-low)*ease(p/rise,"riseShape")}
    if p<edge{return high}
    if fall>0,p<edge+fall{return high+(low-high)*ease((p-edge)/fall,"fallShape")}
    return low
  }
  static func parse(_ text:String)->Double? {let parts=text.split(separator:"/",omittingEmptySubsequences:false);guard parts.count==1 || parts.count==2,let a=Double(parts[0].trimmingCharacters(in:.whitespaces)),a.isFinite else{return nil};if parts.count==1{return a};guard let b=Double(parts[1].trimmingCharacters(in:.whitespaces)),b.isFinite,b != 0 else{return nil};let v=a/b;return v.isFinite ? v:nil}
}

final class GateValueControl:NSView,NSTextFieldDelegate {
  let label=Theme.label("",size:11),value=NSTextField(string:""),slider=NSSlider(value:0,minValue:0,maxValue:1,target:nil,action:nil)
  var onChange:((Double)->Void)?
  var onInvalid:(()->Void)?
  private(set) var isValid=true
  var lower=0.0,upper=1.0,scale=1.0,logarithmic=false,integral=false
  private(set) var scalar=0.0
  init(_ title:String){super.init(frame:.zero);label.stringValue=title;for v in [label,slider,value]{addSubview(v)};value.font = .monospacedDigitSystemFont(ofSize:11,weight:.regular);value.delegate=self;value.target=self;value.action=#selector(entered);slider.target=self;slider.action=#selector(slid);slider.isContinuous=true;slider.setAccessibilityLabel(title);value.setAccessibilityLabel(title+" exact value")}
  required init?(coder:NSCoder){fatalError()}
  override var isFlipped:Bool{true}
  override var intrinsicContentSize:NSSize{NSSize(width:280,height:35)}
  override func layout(){super.layout();let w=bounds.width;label.frame=NSRect(x:0,y:8,width:105,height:19);value.frame=NSRect(x:max(110,w-83),y:4,width:83,height:24);slider.frame=NSRect(x:105,y:5,width:max(12,w-197),height:24)}
  func configure(_ scalar:Double,min:Double,max:Double,scale:Double=1,log:Bool=false,integer:Bool=false){self.lower=min;self.upper=max;self.scale=scale;logarithmic=log;integral=integer;set(scalar)}
  func set(_ scalar:Double){isValid=true;value.textColor=Theme.text;self.scalar=scalar;value.stringValue=String(format:"%.9g",scalar*scale);slider.doubleValue=position(scalar)}
  func position(_ v:Double)->Double {guard upper>lower else{return 0};if logarithmic{return max(0,min(1,log(max(lower,v)/lower)/log(upper/lower)))};return max(0,min(1,(v-lower)/(upper-lower)))}
  @objc func slid(){isValid=true;value.textColor=Theme.text;var v=logarithmic ? lower*pow(upper/lower,slider.doubleValue) : lower+(upper-lower)*slider.doubleValue;if integral{v=v.rounded()};scalar=v;value.stringValue=String(format:"%.9g",v*scale);onChange?(v)}
  @objc func entered(){guard let parsed=GateShapeModel.parse(value.stringValue) else{isValid=false;value.textColor = .systemRed;onInvalid?();return};isValid=true;let v=parsed/scale;guard v.isFinite else{return};value.textColor=Theme.text;scalar=v;slider.doubleValue=position(v);onChange?(integral ? v.rounded():v)}
  func controlTextDidEndEditing(_ obj:Notification){entered()}
}

final class GateWaveformView:NSView {
  var model=GateShapeModel(){didSet{needsDisplay=true}}
  override var isFlipped:Bool{true}
  override var intrinsicContentSize:NSSize{NSSize(width:280,height:180)}
  override func draw(_ dirtyRect:NSRect){
    Theme.panel.setFill();bounds.fill();let area=NSRect(x:42,y:16,width:max(1,bounds.width-55),height:max(1,bounds.height-49))
    let low=model.number("floor"),high=model.number("ceiling"),period=max(1.0/65536,model.number("period")),span=period*2
    let lo=low==high ? low-0.5:low,hi=low==high ? high+0.5:high
    func x(_ beat:Double)->CGFloat{area.minX+CGFloat(beat/span)*area.width}
    func y(_ v:Double)->CGFloat{area.maxY-CGFloat((v-lo)/(hi-lo))*area.height}
    let attrs:[NSAttributedString.Key:Any]=[.font:NSFont.monospacedDigitSystemFont(ofSize:10,weight:.regular),.foregroundColor:Theme.muted]
    Theme.border.setStroke();let grid=NSBezierPath();grid.lineWidth=0.5
    for i in 0...4 {let xx=area.minX+CGFloat(i)*area.width/4;grid.move(to:NSPoint(x:xx,y:area.minY));grid.line(to:NSPoint(x:xx,y:area.maxY));let text=String(format:"%.4g",span*Double(i)/4);text.draw(at:NSPoint(x:min(xx-5,bounds.width-35),y:area.maxY+4),withAttributes:attrs)}
    for v in [lo,(lo+hi)/2,hi]{grid.move(to:NSPoint(x:area.minX,y:y(v)));grid.line(to:NSPoint(x:area.maxX,y:y(v)));let scale=model.target=="pitch" ? 1.0:100.0;String(format:"%.4g",v*scale).draw(at:NSPoint(x:1,y:y(v)-6),withAttributes:attrs)}
    grid.stroke();"Beats after command".draw(at:NSPoint(x:area.minX,y:bounds.height-15),withAttributes:attrs)
    var boundaries=[0.0,span];let phase=model.number("phase").truncatingRemainder(dividingBy:period),edge=period*model.number("width")
    if model.number("width")>0,model.number("width")<1 {for cycle in -2...4 {let start=Double(cycle)*period-phase
      for t in [start,start+model.effective("rise"),start+edge,start+edge+model.effective("fall")]{if t>0,t<span{boundaries.append(t)}}
      for (t,rising) in [(start,true),(start+edge,false)] where t>=0 && t<=span {Theme.muted.setFill();let r=NSRect(x:x(t)-2.5,y:5,width:5,height:5);(rising ? NSBezierPath(ovalIn:r):NSBezierPath(rect:r)).fill()}
    }}
    boundaries=Array(Set(boundaries)).sorted();let wave=NSBezierPath();wave.lineWidth=1.5;var began=false
    NSGraphicsContext.saveGraphicsState();area.clip()
    for i in 0..<boundaries.count-1 {let a=boundaries[i],b=boundaries[i+1],eps=min((b-a)/4,period*1e-9)
      for j in 0...32 {let beat=a+(b-a)*Double(j)/32,probe=j==0 ? a+eps:j==32 ? b-eps:beat;let point=NSPoint(x:x(beat),y:y(model.value(probe)));if began{wave.line(to:point)}else{wave.move(to:point);began=true}}
    };Theme.accent.setStroke();wave.stroke();NSGraphicsContext.restoreGraphicsState()
  }
}

final class GateInspector:NSView {
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  var onContext:(()->(PatternModel,Int,Int,Int))?
  var onDefinition:((Int,Int,Int,Int)->Void)?
  let waveform=GateWaveformView(),status=Theme.label("Choose a pattern FX cell",size:11,color:Theme.muted)
  let target=NSPopUpButton(),plugin=NSPopUpButton(),parameter=NSPopUpButton(),riseShape=NSPopUpButton(),fallShape=NSPopUpButton()
  let linked=NSButton(checkboxWithTitle:"Link rise and fall",target:nil,action:nil)
  private(set) var controls=[String:GateValueControl]()
  private(set) var hasDraft=false,pending=false
  private var generation=0,captureGeneration=0
  private var model=GateShapeModel(),context:PatternModel?,revision:String?,pattern=0,row=0,channel=0,column=0
  private var commands=[[String:Any]](),bindings=[[String:Any]](),plugins=[[String:Any]](),parameters=[[String:Any]]()
  private var selectedPlugin:String?,selectedParameter:Int?,sourceRow:Int?,sourceColumn=0
  private var targetTitle=Theme.label("Volume multiplier",size:11,color:Theme.muted),effective=Theme.label("",size:10,color:Theme.muted)
  private var applyButton:ActionButton!,stopButton:ActionButton!,definitionButton:ActionButton!
  private let body=NSStackView()
  override init(frame:NSRect){super.init(frame:frame)
    for text in ["Volume","Pitch","Pan","Plugin parameter"]{target.addItem(withTitle:text)};target.target=self;target.action=#selector(targetChanged);target.setAccessibilityLabel("Gate target")
    for p in [riseShape,fallShape]{p.addItems(withTitles:["Direct","Linear","Ease in","Ease out","Ease in/out"]);p.target=self;p.action=#selector(shapeChanged)}
    riseShape.setAccessibilityLabel("Rise shape");fallShape.setAccessibilityLabel("Fall shape")
    plugin.target=self;plugin.action=#selector(pluginChanged);plugin.setAccessibilityLabel("Gate plugin")
    parameter.target=self;parameter.action=#selector(parameterChanged);parameter.setAccessibilityLabel("Gate plugin parameter")
    linked.target=self;linked.action=#selector(linkChanged)
    body.orientation = .vertical;body.alignment = .leading;body.spacing=6;body.fill(self,inset:10)
    func add(_ view:NSView){body.addArrangedSubview(view);view.translatesAutoresizingMaskIntoConstraints=false;view.widthAnchor.constraint(equalTo:body.widthAnchor).isActive=true}
    add(stack(.horizontal,[Theme.label("Target",size:11),target],spacing:8));add(targetTitle);add(plugin);add(parameter);add(waveform);waveform.heightAnchor.constraint(equalToConstant:180).isActive=true
    for (key,title) in [("floor","Floor (%)"),("ceiling","Ceiling (%)"),("period","Period (beats)"),("phase","Phase (beats)"),("width","Gate width (%)"),("rise","Rise (beats)"),("fall","Fall (beats)"),("offset","Onset (beats)"),("pitchRange","Bend range (st)")]{
      let c=GateValueControl(title);controls[key]=c;add(c);c.heightAnchor.constraint(equalToConstant:35).isActive=true
      c.onChange={[weak self] value in self?.change(key,value)}
      c.onInvalid={[weak self] in self?.hasDraft=true;self?.generation += 1;self?.applyButton?.isEnabled=false}
    }
    add(stack(.horizontal,[Theme.label("Rise",size:11),riseShape,Theme.label("Fall",size:11),fallShape],spacing:6));add(linked);add(effective)
    applyButton=ActionButton("Apply gate",symbol:"checkmark"){[weak self] in self?.apply(stop:false)}
    stopButton=ActionButton("Write stop here",symbol:"stop"){[weak self] in self?.apply(stop:true)}
    definitionButton=ActionButton("Go to definition",symbol:"arrow.uturn.backward"){[weak self] in guard let self,let row=self.sourceRow else{return};self.onDefinition?(self.pattern,row,self.channel,self.sourceColumn)}
    add(stack(.horizontal,[applyButton,stopButton],spacing:8));add(ActionButton("Stop all gates here",symbol:"stop.circle"){[weak self] in self?.apply(stop:true,all:true)});add(definitionButton);add(status)
    status.lineBreakMode = .byWordWrapping;status.maximumNumberOfLines=3;status.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    effective.lineBreakMode = .byWordWrapping;effective.maximumNumberOfLines=2
    targetTitle.lineBreakMode = .byWordWrapping;targetTitle.maximumNumberOfLines=2
    setAccessibilityLabel("Gate inspector");refresh()
  }
  required init?(coder:NSCoder){fatalError()}
  override var isFlipped:Bool{true}
  func invalidate(){captureGeneration+=1;revision=nil;context=nil;pending=false;hasDraft=false;status.stringValue="Choose a pattern FX cell in this song";refresh()}
  func capture(){
    guard !pending,let c=onContext?(),let onRequest else{return};captureGeneration+=1;let token=captureGeneration
    context=c.0;pattern=c.0.pattern;row=c.1;channel=c.2;column=max(0,(c.3-3)/2);revision=c.0.revisionToken;pending=true
    let capturedRevision=revision
    onRequest("pattern.effects.get",["pattern":pattern]){[weak self] reply in
      guard let self,self.captureGeneration==token else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],result["revision"] as? String==capturedRevision,let data=result["data"] as? [String:Any] else{self.failure(reply);return}
      self.commands=data["commands"] as? [[String:Any]] ?? [];self.bindings=data["bindings"] as? [[String:Any]] ?? [];self.plugins=c.0.nativePlugins
      self.plugin.removeAllItems();for p in self.plugins{self.plugin.addItem(withTitle:p["name"] as? String ?? "Plugin");self.plugin.lastItem?.representedObject=p["instanceID"]}
      let own=self.commands.first{($0["channel"] as? Int)==self.channel&&($0["position"] as? Int ?? 0)/65536==self.row&&($0["column"] as? Int)==self.column}
      let inherited=self.commands.filter{($0["channel"] as? Int)==self.channel&&($0["position"] as? Int ?? 0)/65536<self.row&&($0["native"] as? String)=="gate"}.max{($0["position"] as? Int ?? 0)<($1["position"] as? Int ?? 0)}
      let source=(own?["native"] as? String)=="gate" ? own:own==nil ? inherited:nil
      self.sourceColumn=source?["column"] as? Int ?? self.column
      for control in self.controls.values{control.set(control.scalar)}
      self.sourceRow=source.map{($0["position"] as? Int ?? 0)/65536};self.model=GateShapeModel()
      if let fields=source?["parameters"] as? [String:Any]{self.model.parameters.merge(fields){_,new in new}}
      self.controls["offset"]?.scalarValue(Double((own?["position"] as? Int ?? 0)%65536)/65536/Double(max(1,c.0.rowsPerBeat)))
      let binding=(self.model.parameters["binding"] as? NSNumber)?.intValue ?? 0
      let bound=self.bindings.first{($0["id"] as? Int)==binding};self.selectedPlugin=bound?["plugin"] as? String;self.selectedParameter=bound?["parameter"] as? Int
      self.hasDraft=false;self.generation+=1
      self.status.stringValue=self.sourceRow.map{$0==self.row ? "Editing gate at row \($0) · FX \(self.column+1)":"Shape from row \($0) · Apply writes a new gate here"} ?? "Apply writes a gate at row \(self.row) · FX \(self.column+1)"
      self.refresh();if self.model.target=="parameter"{self.loadParameters()}
    }
  }
  func selectNewGate(){model=GateShapeModel();hasDraft=true;generation+=1;refresh()}
  private func change(_ key:String,_ value:Double){guard value.isFinite else{return};hasDraft=true;generation+=1
    if key != "offset"{model.parameters[key]=value}
    if model.parameters["linked"] as? Bool==true,(key=="rise" || key=="fall"){model.parameters[key=="rise" ? "fall":"rise"]=value}
    refresh(except:key)
  }
  @objc private func targetChanged(){let choices=["volume","pitch","pan","parameter"];model.parameters["target"]=choices[max(0,target.indexOfSelectedItem)];model.parameters["binding"]=0
    model.parameters["floor"]=model.target=="pan" ? -1.0:0.0;model.parameters["ceiling"]=model.target=="pitch" ? 12.0:1.0
    if model.target=="pitch"{model.parameters["pitchRange"]=12};hasDraft=true;generation+=1;refresh();if model.target=="parameter"{loadParameters()}
  }
  @objc private func shapeChanged(_ sender:NSPopUpButton){let key=sender === riseShape ? "riseShape":"fallShape";model.parameters[key]=GateShapeModel.shapes[max(0,sender.indexOfSelectedItem)];if linked.state == .on{model.parameters[key=="riseShape" ? "fallShape":"riseShape"]=model.parameters[key]};hasDraft=true;generation+=1;refresh()}
  @objc private func linkChanged(){model.parameters["linked"]=linked.state == .on;if linked.state == .on{model.parameters["fall"]=model.parameters["rise"];model.parameters["fallShape"]=model.parameters["riseShape"]};hasDraft=true;generation+=1;refresh()}
  @objc private func pluginChanged(){selectedPlugin=plugin.selectedItem?.representedObject as? String;selectedParameter=nil;hasDraft=true;generation+=1;loadParameters()}
  @objc private func parameterChanged(){selectedParameter=parameter.selectedItem?.representedObject as? Int;hasDraft=true;generation+=1}
  private func loadParameters(){guard let onRequest else{return}
    if selectedPlugin==nil{selectedPlugin=plugins.first?["instanceID"] as? String}
    guard let id=selectedPlugin,let slot=plugins.firstIndex(where:{$0["instanceID"] as? String==id}) else{status.stringValue="Add an instrument or effect plugin to choose a parameter.";return}
    plugin.selectItem(at:slot);let token=captureGeneration,chosen=selectedParameter
    onRequest("plugin.parameters.get",["slot":slot]){[weak self] reply in
      guard let self,self.captureGeneration==token,self.selectedPlugin==id else{return}
      guard let result=reply["result"] as? [String:Any],result["revision"] as? String==self.revision,let data=result["data"] as? [[String:Any]] else{self.failure(reply);return}
      self.parameters=data.filter{$0["writable"] as? Bool != false};self.parameter.removeAllItems()
      for p in self.parameters{self.parameter.addItem(withTitle:p["name"] as? String ?? "Parameter");self.parameter.lastItem?.representedObject=p["id"]}
      if let chosen,let i=self.parameters.firstIndex(where:{$0["id"] as? Int==chosen}){self.parameter.selectItem(at:i)}
      self.selectedParameter=self.parameter.selectedItem?.representedObject as? Int
    }
  }
  private func refresh(except:String?=nil){
    target.selectItem(at:["volume","pitch","pan","parameter"].firstIndex(of:model.target) ?? 0)
    plugin.isHidden=model.target != "parameter";parameter.isHidden=plugin.isHidden
    let pitch=model.target=="pitch",pan=model.target=="pan",period=max(1.0/65536,min(256,model.number("period")))
    targetTitle.stringValue=pitch ? "Pitch offset · match the plugin bend range":pan ? "Absolute channel pan · −100% left / +100% right":model.target=="parameter" ? "Absolute parameter value · underlying automation continues":"Volume multiplier · 100% preserves underlying volume"
    for (key,c) in controls where key != except && c.isValid {
      switch key {
      case "floor","ceiling":c.label.stringValue=(key=="floor" ? "Floor":"Ceiling")+(pitch ? " (st)":" (%)");c.configure(model.number(key),min:pitch ? -96:pan ? -1:0,max:pitch ? 96:model.target=="volume" ? 4:1,scale:pitch ? 1:100)
      case "period":c.configure(model.number(key),min:1.0/64,max:16,log:true)
      case "phase":c.configure(model.number(key),min:-period,max:period)
      case "width":c.configure(model.number(key),min:0,max:1,scale:100)
      case "rise","fall":c.configure(model.number(key),min:0,max:period*2)
      case "pitchRange":c.isHidden = !pitch;c.configure(model.number(key),min:1,max:96,integer:true)
      case "offset":c.configure(c.scalar,min:0,max:(1.0-1.0/65536)/Double(max(1,context?.rowsPerBeat ?? 4)))
      default:break
      }
    }
    for (key,popup) in [("riseShape",riseShape),("fallShape",fallShape)]{popup.selectItem(at:GateShapeModel.shapes.firstIndex(of:model.parameters[key] as? String ?? "linear") ?? 1)}
    linked.state=model.parameters["linked"] as? Bool==true ? .on:.off
    controls["rise"]?.slider.isEnabled=model.parameters["riseShape"] as? String != "direct";controls["fall"]?.slider.isEnabled=model.parameters["fallShape"] as? String != "direct"
    let valid=controls.values.allSatisfy{$0.isValid} && model.number("period")>=1.0/65536 && model.number("period")<=256 && model.number("floor")<=model.number("ceiling") && model.number("floor") >= (pitch ? -96:pan ? -1:0) && model.number("ceiling") <= (pitch ? 96:model.target=="volume" ? 4:1) && model.number("width")>=0 && model.number("width")<=1 && abs(model.number("phase"))<=65536 && ["rise","fall"].allSatisfy{model.number($0)>=0 && model.number($0)<=256}
    if valid{waveform.model=model}
    effective.stringValue=model.number("width")==0 ? "Constant floor":model.number("width")==1 ? "Constant ceiling":String(format:"Effective rise %.5g b · fall %.5g b",model.effective("rise"),model.effective("fall"))
    applyButton?.isEnabled = !pending && revision != nil && valid;stopButton?.isEnabled = !pending && revision != nil;definitionButton?.isEnabled=sourceRow != nil
  }
  func apply(stop:Bool,all:Bool=false){
    window?.makeFirstResponder(nil)
    guard !pending,controls.values.allSatisfy({$0.isValid}),let revision,let context,let onRequest else{return}
    var fields=model.parameters,newBinding:[String:Any]?
    if model.target=="parameter",!all {
      guard let plugin=selectedPlugin,let parameter=selectedParameter else{status.stringValue="Choose a plugin and parameter.";return}
      let existing=bindings.first{($0["plugin"] as? String)==plugin&&($0["parameter"] as? Int)==parameter}
      guard let id=existing?["id"] as? Int ?? (1...255).first(where:{n in !bindings.contains{$0["id"] as? Int==n}}) else{status.stringValue="All parameter bindings are in use.";return}
      fields["binding"]=id;if existing==nil{newBinding=["id":id,"plugin":plugin,"parameter":parameter,"name":self.parameter.titleOfSelectedItem ?? "Gate parameter"]}
    }else{fields["binding"]=0}
    if stop{fields=["target":all ? "all":model.target,"binding":all ? 0:fields["binding"] ?? 0]}
    let onset=controls["offset"]?.scalar ?? 0,units=onset*Double(max(1,context.rowsPerBeat))*65536
    guard units>=0,units<=65535 else{status.stringValue="Onset must stay within this row.";return}
    var replacement=commands.filter{!($0["channel"] as? Int==channel&&($0["position"] as? Int ?? 0)/65536==row&&$0["column"] as? Int==column)}.map{$0.filter{$0.key != "track"}}
    replacement.append(["kind":"native","native":stop ? "gate-stop":"gate","parameters":fields,"channel":channel,"column":column,"position":row*65536+Int(units.rounded())])
    var request:[String:Any]=["pattern":pattern,"commands":replacement,"expectedRevision":revision]
    if let newBinding{request["bindings"]=[newBinding]}
    let sent=generation,token=captureGeneration;pending=true;refresh()
    onRequest("pattern.effects.set",request){[weak self] reply in
      guard let self,self.captureGeneration==token else{return};self.pending=false
      guard let result=reply["result"] as? [String:Any],let next=result["revision"] as? String else{self.failure(reply);return}
      self.revision=next;self.commands=replacement;if let newBinding{self.bindings.append(newBinding)}
      if self.generation==sent{self.hasDraft=false;self.model.parameters=stop ? self.model.parameters:fields;self.sourceRow=stop ? nil:self.row;self.sourceColumn=self.column}
      self.status.stringValue=stop ? "Stop gate saved · Undo restores the previous cell":"Gate saved · Undo restores the previous cell";self.refresh()
    }
  }
  private func failure(_ reply:[String:Any]){status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "The song changed. Your draft is retained; Follow reloads the current cell.";refresh()}
}
private extension GateValueControl {func scalarValue(_ value:Double){set(value)}}
