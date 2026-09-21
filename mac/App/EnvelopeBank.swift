import AppKit

// A bank window captures one target and draft. It never follows selection changes.
final class EnvelopeBankWindow:NSWindowController,NSTableViewDataSource,NSTableViewDelegate,NSTextFieldDelegate {
  let request:EnvelopeRequest,target:[String:Any]
  let sourceShape:[String:Any]?
  let canReplace:()->Bool,onApplied:()->Void
  let scope=NSPopUpButton(),table=NSTableView(),name=NSTextField(string:"New envelope")
  let canvas=AutomationCanvas(frame:.zero),curve=NSPopUpButton(),formula=NSTextField(string:""),position=NSTextField(string:"0"),value=NSTextField(string:"50")
  let status=Theme.label("",size:11,color:Theme.muted),link=Theme.label("",size:11,color:Theme.muted)
  let curves=["step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"]
  var revision:String,catalogueRevision="",linkedTemplate="",entries=[[String:Any]](),selected:[String:Any]?,draft=[String:Any]()
  var pending=false,dirty=false,generation=0,previewGeneration=0,workbench:FormulaWorkbench?,previewWork:DispatchWorkItem?
  var buttons=[NSButton](),formulaRow:NSStackView!
  init(title:String,target:[String:Any],shape:[String:Any]?,revision:String,request:@escaping EnvelopeRequest,canReplace:@escaping ()->Bool={true},applied:@escaping ()->Void={}) {
    self.target=target;sourceShape=shape;self.revision=revision;self.request=request;self.canReplace=canReplace;onApplied=applied
    let window=NSWindow(contentRect:NSRect(x:0,y:0,width:940,height:640),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    super.init(window:window);window.title="Envelope bank · \(title)";window.minSize=NSSize(width:860,height:580);window.isReleasedWhenClosed=false
    scope.addItems(withTitles:["This song · linked templates","App catalogue · copies only"]);scope.target=self;scope.action = #selector(changeScope)
    table.addTableColumn(NSTableColumn(identifier:.init("envelope")));table.headerView=nil;table.rowHeight=28;table.delegate=self;table.dataSource=self
    table.setAccessibilityLabel("Envelope templates");let list=verticalScrollView();list.documentView=table;list.fixed(width:250)
    name.placeholderString="Envelope name";name.delegate=self;name.setAccessibilityLabel("Envelope template name")
    curve.addItems(withTitles:["Step","Linear","Smooth","Exponential","Logarithmic","Step at start","Exponential reversed","Logarithmic reversed","Scripted"]);curve.target=self;curve.action = #selector(changeCurve)
    position.fixed(width:75);value.fixed(width:65);position.setAccessibilityLabel("Template point position in rows");value.setAccessibilityLabel("Template point percent")
    formula.delegate=self;formula.font = .monospacedSystemFont(ofSize:12,weight:.regular);formula.setAccessibilityLabel("Template point formula")
    formulaRow=stack(.horizontal,[formula,ActionButton("Expand…"){[weak self] in self?.expandFormula()},ActionButton("Reference"){[weak self] in FormulaWorkbench.showReference(self?.request)}],spacing:4)
    canvas.snap=1;canvas.onEdit={[weak self] in self?.markDraft()};canvas.onSelect={[weak self] in self?.showPoint()};canvas.onViewport={[weak self] in self?.preview()};canvas.heightAnchor.constraint(greaterThanOrEqualToConstant:180).isActive=true
    func button(_ title:String,_ action:@escaping ()->Void)->NSButton {let b=ActionButton(title,action:action);buttons.append(b);return b}
    let saveCurrent=button("Save current envelope…"){[weak self] in self?.saveCurrent()}
    let saveMaster=button("Save song template"){[weak self] in self?.saveMaster()}
    let copy=button("Use independent copy"){[weak self] in self?.use(linked:false)}
    let linked=button("Use linked"){[weak self] in self?.use(linked:true)}
    let independent=button("Make current independent"){[weak self] in self?.unlink()}
    let publish=button("Publish copy to catalogue"){[weak self] in self?.publish(overwrite:false)}
    let overwrite=button("Replace catalogue entry…"){[weak self] in self?.publish(overwrite:true)}
    let remove=button("Remove song template"){[weak self] in self?.remove()}
    let importButton=button("Copy into song bank"){[weak self] in self?.importEntry()}
    let controls=stack(.horizontal,[Theme.label("Row",size:11),position,Theme.label("%",size:11),value,ActionButton("Set point"){[weak self] in self?.setPoint()},ActionButton("Delete"){[weak self] in self?.canvas.removeSelected()},NSView(),curve],spacing:4)
    let help=Theme.label("Select a song template to edit its master. Saving updates linked uses in this song in one Undo. Catalogue entries change only when explicitly published. Pattern copies fit the whole pattern; instruments use tick points with a maximum half-unit conversion error.",size:11,color:Theme.muted)
    help.maximumNumberOfLines=0;help.lineBreakMode = .byWordWrapping;help.preferredMaxLayoutWidth=600;help.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    status.maximumNumberOfLines=3;status.lineBreakMode = .byWordWrapping;status.preferredMaxLayoutWidth=880;status.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    link.setContentCompressionResistancePriority(.defaultLow,for:.horizontal)
    let right=stack(.vertical,[name,canvas,controls,formulaRow!,stack(.horizontal,[saveMaster,NSView(),ActionButton("Reload / discard"){[weak self] in self?.reload()}],spacing:6),stack(.horizontal,[copy,linked,importButton,NSView()],spacing:6),help],spacing:8);right.stretchAcrossAxis()
    let editors=stack(.horizontal,[list,right],spacing:12);right.heightAnchor.constraint(equalTo:editors.heightAnchor).isActive=true;list.heightAnchor.constraint(equalTo:editors.heightAnchor).isActive=true
    let body=stack(.vertical,[stack(.horizontal,[scope,NSView(),saveCurrent],spacing:8),link,editors,stack(.horizontal,[independent,remove,NSView()],spacing:6),stack(.horizontal,[publish,overwrite,NSView()],spacing:6),status],spacing:10);body.stretchAcrossAxis();let root=NSView();body.fill(root,inset:16);window.contentView=root
    formulaRow.isHidden=true;window.center();window.makeKeyAndOrderFront(nil);reload()
  }
  required init?(coder:NSCoder){fatalError()}
  var isCatalogue:Bool{scope.indexOfSelectedItem==1}
  func call(_ method:String,_ params:[String:Any],write:Bool=false,done:@escaping ([String:Any])->Void){
    guard !pending else{return};pending=true;buttons.forEach{$0.isEnabled=false};var params=params
    if write{params["expectedRevision"]=revision}
    request(method,params){[weak self] reply in
      guard let self else{return};self.pending=false;self.updateControls()
      guard let result=reply["result"] as? [String:Any],let data=result["data"] as? [String:Any],let token=result["revision"] as? String else{self.status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Envelope operation failed";return}
      if self.revision.isEmpty || write{self.revision=token}
      guard token==self.revision else{self.status.stringValue="Song changed. Your draft is preserved; reopen the bank from the original editor to refresh its target.";return}
      done(data)
    }
  }
  func reload(){guard !pending else{return};dirty=false;generation+=1
    call("envelope.bank.list",["target":target]){[weak self] data in guard let self else{return};self.linkedTemplate=data["linkedTemplate"] as? String ?? ""
      let entries=data["entries"] as? [[String:Any]] ?? [];let linked=entries.first{$0["id"] as? String==self.linkedTemplate}?["name"] as? String
      self.link.stringValue=linked.map{"Current envelope is linked to “\($0)” in this song."} ?? "Current envelope is independent."
      if self.isCatalogue {self.call("envelope.catalogue.list",[:]){[weak self] cat in guard let self else{return};self.catalogueRevision=cat["revision"] as? String ?? "";self.show(cat["entries"] as? [[String:Any]] ?? [])}}
      else{self.show(entries)}
    }
  }
  func updateControls(){for b in buttons {let songOnly=["Save song template","Use independent copy","Use linked","Remove song template","Publish copy to catalogue","Replace catalogue entry…"].contains(b.title);b.isHidden=(songOnly && isCatalogue) || (b.title=="Copy into song bank" && !isCatalogue);b.isEnabled = !pending && (!songOnly || selected != nil) && (b.title != "Make current independent" || !linkedTemplate.isEmpty)}}
  func show(_ items:[[String:Any]]){let id=selected?["id"] as? String;entries=items;selected=nil;table.reloadData();if !items.isEmpty{let index=items.firstIndex{$0["id"] as? String==id} ?? 0;table.selectRowIndexes(IndexSet(integer:index),byExtendingSelection:false);select(index)}else{draft=[:];canvas.points=[];status.stringValue="Save the current envelope to start this bank."};showPoint();updateControls()}
  @objc func changeScope(){guard !dirty,!pending else{scope.selectItem(at:isCatalogue ? 0:1);status.stringValue="Save or discard the template draft before changing banks.";return};selected=nil;reload()}
  func numberOfRows(in tableView:NSTableView)->Int{entries.count}
  func tableView(_ tableView:NSTableView,viewFor tableColumn:NSTableColumn?,row:Int)->NSView?{Theme.label(entries[row]["name"] as? String ?? "Envelope",size:12)}
  func tableView(_ tableView:NSTableView,shouldSelectRow row:Int)->Bool{if dirty||pending{status.stringValue="Save or discard the template draft before changing selection.";return false};return true}
  func tableViewSelectionDidChange(_ notification:Notification){if entries.indices.contains(table.selectedRow){select(table.selectedRow)}}
  func select(_ index:Int){selected=entries[index];draft=selected?["shape"] as? [String:Any] ?? [:];name.stringValue=selected?["name"] as? String ?? "";canvas.rows=max(1,((draft["span"] as? Int ?? 16384)+255)/256);canvas.points=(draft["points"] as? [[String:Any]] ?? []).map{EnvelopePoint(position:$0["position"] as? Int ?? 0,value:$0["value"] as? Double ?? 0,curve:$0["curve"] as? String ?? "linear",formula:$0["formula"] as? String ?? "")};canvas.selected=nil;dirty=false;generation+=1;preview();showPoint();status.stringValue=isCatalogue ? "Catalogue preview. Copy into the song bank to edit or use it." : "Edit the master here, then Save song template. Linked uses update together."}
  func markDraft(){guard !isCatalogue,selected != nil else{if entries.indices.contains(table.selectedRow){select(table.selectedRow)};return};dirty=true;generation+=1;draft["points"]=canvas.points.map(\.dictionary);status.stringValue="Unsaved master template changes · Save updates every linked use in this song.";preview()}
  func controlTextDidChange(_ notification:Notification){if notification.object as? NSTextField === formula,let i=canvas.selected,canvas.points.indices.contains(i){canvas.points[i].formula=formula.stringValue;FormulaCatalog.suggest(formula.currentEditor() as? NSTextView)};markDraft()}
  func control(_ control:NSControl,textView:NSTextView,completions words:[String],forPartialWordRange range:NSRange,indexOfSelectedItem index:UnsafeMutablePointer<Int>)->[String]{guard control === formula else{return words};index.pointee = -1;return FormulaCatalog.completions(textView.string,range:range)}
  @objc func changeCurve(){canvas.curve=curves[max(0,curve.indexOfSelectedItem)];if let i=canvas.selected,canvas.points.indices.contains(i){let p=canvas.points[i];canvas.replaceSelected(position:p.position,value:p.value,curve:canvas.curve);showPoint()}}
  func showPoint(){guard let i=canvas.selected,canvas.points.indices.contains(i) else{formulaRow.isHidden=true;return};let p=canvas.points[i];position.stringValue=String(format:"%.8g",Double(p.position)/256);value.stringValue=String(format:"%.6g",p.value*100);curve.selectItem(at:curves.firstIndex(of:p.curve) ?? 1);formula.stringValue=p.formula;formulaRow.isHidden=p.curve != "scripted";if !formulaRow.isHidden{FormulaCatalog.load(request)}}
  func setPoint(){guard let row=Double(position.stringValue),row.isFinite,let v=Double(value.stringValue),v.isFinite,row>=0,row*256<Double(draft["span"] as? Int ?? 0),v>=0,v<=100 else{status.stringValue="Use a point within the template duration and 0–100%.";return};canvas.replaceSelected(position:Int((row*256).rounded()),value:v/100,curve:curves[max(0,curve.indexOfSelectedItem)]);showPoint()}
  func preview(){previewGeneration+=1;let token=previewGeneration;previewWork?.cancel();let points=canvas.points.map(\.dictionary),rows=canvas.rows,beat=draft["rowsPerBeat"] as? Int ?? 4
    let work=DispatchWorkItem{[weak self] in guard let self else{return};self.request("automation.formula.preview",["points":points,"rows":rows,"rowsPerBeat":beat,"span":self.draft["span"] ?? rows*256,"samples":1024]){[weak self] reply in guard let self,self.previewGeneration==token else{return};if let data=(reply["result"] as? [String:Any])?["data"] as? [String:Any],let values=data["values"] as? [[Double]]{self.canvas.previewValues=values.compactMap{$0.count==2 ? ($0[0],$0[1]):nil}}else{self.status.stringValue=(reply["error"] as? [String:Any])?["message"] as? String ?? "Preview unavailable"}}};previewWork=work;DispatchQueue.main.asyncAfter(deadline:.now()+0.12,execute:work)
  }
  func expandFormula(){if let workbench,workbench.window?.isVisible==true{workbench.window?.makeKeyAndOrderFront(nil);return};guard !isCatalogue,let i=canvas.selected,canvas.points.indices.contains(i),canvas.points[i].curve=="scripted" else{return};let token=generation;workbench?.close();workbench=FormulaWorkbench(source:canvas.points[i].formula,title:"Song template · \(name.stringValue)",points:canvas.points.map(\.dictionary),selected:i,rows:canvas.rows,rowsPerBeat:draft["rowsPerBeat"] as? Int ?? 4,span:draft["span"] as? Int,request:request){[weak self] text in guard let self,self.generation==token,self.canvas.selected==i else{return false};self.canvas.points[i].formula=text;self.formula.stringValue=text;self.markDraft();return true}}
  func askName(_ title:String,initial:String,done:@escaping (String)->Void){guard let window else{return};let alert=NSAlert();alert.messageText=title;alert.addButton(withTitle:"Save");alert.addButton(withTitle:"Cancel");let field=NSTextField(string:initial);field.frame=NSRect(x:0,y:0,width:330,height:24);alert.accessoryView=field;alert.beginSheetModal(for:window){response in if response == .alertFirstButtonReturn{done(field.stringValue)}};alert.window.makeFirstResponder(field)}
  func saveCurrent(){guard !dirty,canReplace() else{status.stringValue="Save or discard the master draft first.";return};askName("Save current envelope into this song",initial:"New envelope"){[weak self] name in guard let self else{return};var p:[String:Any]=["name":name];if let shape=self.sourceShape{p["shape"]=shape}else{p["target"]=self.target};self.call("envelope.bank.save",p,write:true){[weak self] data in self?.scope.selectItem(at:0);self?.selected=["id":data["id"] ?? ""];self?.reload()}}}
  func saveMaster(){guard !isCatalogue,let id=selected?["id"] else{status.stringValue="Select a song template to save its master.";return};let token=generation;call("envelope.bank.save",["id":id,"name":name.stringValue,"shape":draft],write:true){[weak self] _ in guard let self else{return};if self.generation==token{self.dirty=false;self.reload()}else{self.status.stringValue="Saved. Newer draft edits remain unsaved."}}}
  func use(linked:Bool){guard !dirty,!isCatalogue,let id=selected?["id"],canReplace() else{status.stringValue="Save the template first and keep the original editor unchanged. Catalogue entries must be copied into the song bank.";return};call("envelope.bank.apply",["template":id,"target":target,"linked":linked],write:true){[weak self] _ in guard let self else{return};if self.canReplace(){self.onApplied()};self.close()}}
  func unlink(){guard !dirty,canReplace() else{status.stringValue="Save/discard this draft and reopen from the unchanged envelope editor.";return};call("envelope.bank.unlink",["target":target],write:true){[weak self] _ in guard let self else{return};if self.canReplace(){self.onApplied()};self.close()}}
  func remove(){guard !dirty,!isCatalogue,let id=selected?["id"] else{return};call("envelope.bank.remove",["id":id],write:true){[weak self] _ in self?.selected=nil;self?.reload()}}
  func importEntry(){guard isCatalogue,let id=selected?["id"] else{status.stringValue="Choose an envelope in the app catalogue.";return};call("envelope.catalogue.import",["catalogueID":id,"expectedCatalogueRevision":catalogueRevision],write:true){[weak self] data in self?.scope.selectItem(at:0);self?.selected=["id":data["id"] ?? ""];self?.reload()}}
  func publish(overwrite:Bool){guard !dirty,!isCatalogue,let id=selected?["id"] else{status.stringValue="Save and select a song template before publishing.";return};call("envelope.catalogue.list",[:]){[weak self] cat in guard let self else{return};let publish:(String?)->Void={ [weak self] catalogID in guard let self else{return};var p:[String:Any]=["template":id,"expectedCatalogueRevision":cat["revision"] ?? ""];if let catalogID{p["catalogueID"]=catalogID};self.call("envelope.catalogue.publish",p,write:true){[weak self] _ in self?.status.stringValue="Published. This catalogue copy has no live link to any song."}}
      if !overwrite{publish(nil);return};guard let window=self.window else{return};let entries=cat["entries"] as? [[String:Any]] ?? [];guard !entries.isEmpty else{self.status.stringValue="The catalogue is empty. Publish a new copy first.";return};let alert=NSAlert();alert.messageText="Replace a catalogue entry";alert.informativeText="This explicitly overwrites the selected library copy. Existing songs keep their own templates.";alert.addButton(withTitle:"Replace");alert.addButton(withTitle:"Cancel");let popup=NSPopUpButton(frame:NSRect(x:0,y:0,width:330,height:26));popup.addItems(withTitles:entries.map{$0["name"] as? String ?? "Envelope"});alert.accessoryView=popup;alert.beginSheetModal(for:window){response in if response == .alertFirstButtonReturn{publish(entries[popup.indexOfSelectedItem]["id"] as? String)}}
    }
  }
}
