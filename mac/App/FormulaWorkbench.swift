import AppKit

typealias EnvelopeRequest = (String,[String:Any],@escaping ([String:Any])->Void)->Void

enum FormulaCatalog {
  static var symbols=[[String:String]](), notes="", loading=false
  static var waiting=[()->Void]()
  static func load(_ request:EnvelopeRequest?,done:@escaping ()->Void = {}) {
    if !symbols.isEmpty {done();return}
    guard let request else{return};waiting.append(done);guard !loading else{return};loading=true
    request("automation.formula.reference",[:]) { reply in
      loading=false
      if let data=(reply["result"] as? [String:Any])?["data"] as? [String:Any] {
        symbols=data["symbols"] as? [[String:String]] ?? [];notes=data["notes"] as? String ?? ""
      }
      let callbacks=waiting;waiting=[];callbacks.forEach{$0()}
    }
  }
  static func completions(_ text:String,range:NSRange)->[String] {
    let value=text as NSString
    guard range.location != NSNotFound,range.location>=0,range.length>=0,range.location<=value.length,range.length<=value.length-range.location else{return []}
    let prefix=value.substring(with:range)
    return symbols.filter{($0["name"] ?? "").hasPrefix(prefix)}.compactMap{$0["insert"]}
  }
  static func suggest(_ view:NSTextView?) {
    guard let view,view.window?.isKeyWindow==true else{return}
    let range=view.rangeForUserCompletion
    guard range.location != NSNotFound,range.length>=2,!completions(view.string,range:range).isEmpty else{return}
    view.complete(nil)
  }
}

final class FormulaCodeView:NSTextView {
  override func completions(forPartialWordRange charRange:NSRange,indexOfSelectedItem index:UnsafeMutablePointer<Int>)->[String]? {
    index.pointee = -1;return FormulaCatalog.completions(string,range:charRange)
  }
  override func keyDown(with event:NSEvent) {
    if event.modifierFlags.contains(.control),event.keyCode==49{complete(nil);return}
    super.keyDown(with:event)
  }
}

final class FormulaReferenceView:NSView,NSTableViewDataSource,NSTableViewDelegate,NSSearchFieldDelegate {
  let search=NSSearchField(),table=NSTableView(),notes=Theme.label("Loading reference…",size:11,color:Theme.muted)
  var filtered=[[String:String]](),onInsert:((String)->Void)?
  override init(frame:NSRect){
    super.init(frame:frame)
    search.placeholderString="Find a value or function";search.delegate=self
    let column=NSTableColumn(identifier:.init("symbol"));column.title="Values and functions";table.addTableColumn(column)
    table.delegate=self;table.dataSource=self;table.rowHeight=53;table.target=self;table.doubleAction = #selector(insert)
    table.setAccessibilityLabel("Formula values and functions")
    let scroll=verticalScrollView();scroll.documentView=table
    notes.maximumNumberOfLines=0;notes.lineBreakMode = .byWordWrapping
    let body=stack(.vertical,[search,scroll,notes],spacing:6);body.stretchAcrossAxis();body.fill(self)
  }
  required init?(coder:NSCoder){fatalError()}
  func reload(){notes.stringValue=FormulaCatalog.notes;filter()}
  func controlTextDidChange(_ notification:Notification){filter()}
  func filter(){let q=search.stringValue;filtered=FormulaCatalog.symbols.filter{q.isEmpty || ($0["name"] ?? "").localizedCaseInsensitiveContains(q) || ($0["description"] ?? "").localizedCaseInsensitiveContains(q)};table.reloadData()}
  func numberOfRows(in tableView:NSTableView)->Int{filtered.count}
  func tableView(_ tableView:NSTableView,viewFor tableColumn:NSTableColumn?,row:Int)->NSView?{
    let s=filtered[row],title=Theme.label(s["insert"] ?? "",size:12,weight:.semibold)
    title.font = .monospacedSystemFont(ofSize:12,weight:.semibold)
    let detail=Theme.label(s["description"] ?? "",size:11,color:Theme.muted);detail.maximumNumberOfLines=2;detail.lineBreakMode = .byWordWrapping
    return stack(.vertical,[title,detail],spacing:2)
  }
  @objc func insert(){guard filtered.indices.contains(table.selectedRow),let text=filtered[table.selectedRow]["insert"] else{return};onInsert?(text)}
}

final class FormulaPreviewView:NSView {
  var values=[[Double]](){didSet{needsDisplay=true}}
  override func draw(_ dirtyRect:NSRect){
    Theme.bg.setFill();bounds.fill();guard values.count>1,let first=values.first,let last=values.last,last[0]>first[0] else{return}
    let box=bounds.insetBy(dx:8,dy:8),path=NSBezierPath();path.lineWidth=1.5
    for (i,p) in values.enumerated() where p.count==2 {
      let point=NSPoint(x:box.minX+(p[0]-first[0])/(last[0]-first[0])*box.width,y:box.minY+max(0,min(1,p[1]))*box.height)
      if i==0{path.move(to:point)}else{path.line(to:point)}
    }
    Theme.accent.setStroke();path.stroke()
  }
}

final class FormulaWorkbench:NSWindowController,NSTextViewDelegate {
  let code=FormulaCodeView(),reference=FormulaReferenceView(frame:.zero),preview=FormulaPreviewView(frame:.zero)
  let status=Theme.label("",size:12,color:Theme.muted)
  var onUse:((String)->Bool)?,onRequest:EnvelopeRequest?
  var previewParams=[String:Any](),pointIndex=0,generation=0,validSource:String?,work:DispatchWorkItem?
  static var referenceWindow:NSWindow?
  init(source:String,title:String,points:[[String:Any]],selected:Int,rows:Int,rowsPerBeat:Int,span:Int?=nil,request:EnvelopeRequest?,use:@escaping (String)->Bool){
    let window=NSWindow(contentRect:NSRect(x:0,y:0,width:960,height:660),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    super.init(window:window);window.title=title;window.minSize=NSSize(width:740,height:510);window.isReleasedWhenClosed=false
    onRequest=request;onUse=use;pointIndex=selected
    status.maximumNumberOfLines=3;status.lineBreakMode = .byWordWrapping;status.preferredMaxLayoutWidth=540;status.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    previewParams=["points":points,"rows":rows,"rowsPerBeat":rowsPerBeat,"samples":1024];if let span{previewParams["span"]=span}
    code.allowsUndo=true;code.isRichText=false;code.isAutomaticQuoteSubstitutionEnabled=false;code.isAutomaticDashSubstitutionEnabled=false
    code.isAutomaticSpellingCorrectionEnabled=false;code.isContinuousSpellCheckingEnabled=false
    code.font = .monospacedSystemFont(ofSize:15,weight:.regular);code.backgroundColor=Theme.bg;code.textColor=Theme.text
    code.insertionPointColor=Theme.accent;code.textContainerInset=NSSize(width:10,height:10);code.delegate=self;code.string=source
    code.isVerticallyResizable=true;code.isHorizontallyResizable=false;code.autoresizingMask=[.width];code.textContainer?.widthTracksTextView=true
    code.setAccessibilityLabel("Expanded envelope formula")
    let scroll=verticalScrollView();scroll.documentView=code;scroll.heightAnchor.constraint(greaterThanOrEqualToConstant:150).isActive=true
    preview.fixed(height:150);preview.setAccessibilityLabel("Formula draft preview")
    let useButton=ActionButton("Use formula"){[weak self] in self?.apply()};useButton.keyEquivalent="\r";useButton.keyEquivalentModifierMask=[.command]
    let left=stack(.vertical,[Theme.label("Formula",size:18,weight:.semibold),scroll,Theme.label("Preview · normalized value",size:11,color:Theme.muted),preview,
      stack(.horizontal,[ActionButton("Complete ⌃Space"){[weak self] in self?.code.complete(nil)},NSView(),ActionButton("Cancel"){[weak self] in self?.close()},useButton],spacing:6),status],spacing:8);left.stretchAcrossAxis()
    reference.fixed(width:330)
    let body=stack(.horizontal,[left,reference],spacing:14);left.heightAnchor.constraint(equalTo:body.heightAnchor).isActive=true;reference.heightAnchor.constraint(equalTo:body.heightAnchor).isActive=true
    let root=NSView();body.fill(root,inset:14);window.contentView=root
    reference.onInsert={[weak self] text in guard let self else{return};self.window?.makeFirstResponder(self.code);self.code.insertText(text,replacementRange:self.code.selectedRange())}
    FormulaCatalog.load(request){[weak self] in self?.reference.reload()}
    window.center();window.makeKeyAndOrderFront(nil);window.makeFirstResponder(code);updatePreview()
  }
  required init?(coder:NSCoder){fatalError()}
  func textDidChange(_ notification:Notification){updatePreview();FormulaCatalog.suggest(code)}
  func updatePreview(){
    generation+=1;let token=generation,source=code.string;validSource=nil;work?.cancel()
    guard let request=onRequest,var points=previewParams["points"] as? [[String:Any]],points.indices.contains(pointIndex) else{return}
    points[pointIndex]["formula"]=source;var params=previewParams;params["points"]=points
    let work=DispatchWorkItem{[weak self] in guard let self,self.generation==token else{return};request("automation.formula.preview",params){[weak self] reply in
      guard let self,self.generation==token else{return}
      if let data=(reply["result"] as? [String:Any])?["data"] as? [String:Any],let values=data["values"] as? [[Double]]{
        self.validSource=source;self.preview.values=values;self.status.stringValue="Valid · Use formula returns this draft to its original envelope."
      }else{self.status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Preview unavailable"}
    }};self.work=work;DispatchQueue.main.asyncAfter(deadline:.now()+0.12,execute:work)
  }
  func apply(){guard validSource==code.string else{status.stringValue="Wait for a valid formula preview before applying.";return};if onUse?(code.string)==true{close()}else{status.stringValue="The envelope or selection changed. Your formula remains here; copy it or reopen the original point."}}
  static func showReference(_ request:EnvelopeRequest?){
    let view=FormulaReferenceView(frame:.zero),root=NSView();view.fill(root,inset:12)
    let window=NSWindow(contentRect:NSRect(x:0,y:0,width:640,height:700),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    window.title="Envelope formula reference";window.minSize=NSSize(width:440,height:450);window.isReleasedWhenClosed=false;window.contentView=root
    referenceWindow?.close();referenceWindow=window;window.center();window.makeKeyAndOrderFront(nil);FormulaCatalog.load(request){view.reload()}
  }
}
