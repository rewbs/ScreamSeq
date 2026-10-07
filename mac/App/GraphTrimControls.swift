import AppKit

final class GraphTrimControls:NSStackView,NSTextFieldDelegate {
  let port=NSPopUpButton(),gain=NSTextField(string:"0"),link=NSPopUpButton()
  let modulationSource=NSPopUpButton(),minimum=NSTextField(string:"0"),maximum=NSTextField(string:"6")
  let hint=Theme.label("Linked trims move in opposite directions. Loudness may still change.",size:10,color:Theme.muted)
  var read:(([String:Any],@escaping ([String:Any])->Void)->Void)?
  var edit:(([String:Any])->Void)?
  private var target=[String:Any](),identity="",revision="",ports=[[String:Any]](),request=0
  private var sources=[[String:Any]]()
  private var selectedKey="",shownGain="",loading=false
  var hasDraft:Bool {(gain.currentEditor() != nil && gain.stringValue != shownGain) || minimum.currentEditor() != nil || maximum.currentEditor() != nil}
  init(){super.init(frame:.zero);orientation = .vertical;alignment = .leading;spacing=5
    minimum.fixed(width:56);maximum.fixed(width:56);minimum.setAccessibilityLabel("Trim modulation minimum dB");maximum.setAccessibilityLabel("Trim modulation maximum dB");modulationSource.setAccessibilityLabel("Trim modulation source")
    gain.fixed(width:70);gain.delegate=self;gain.target=self;gain.action=#selector(commitGain)
    port.target=self;port.action=#selector(selectPort);link.target=self;link.action=#selector(selectLink)
    port.setAccessibilityLabel("Audio trim port");gain.setAccessibilityLabel("Audio port trim in decibels");link.setAccessibilityLabel("Inverse compensation port")
    hint.lineBreakMode = .byWordWrapping;hint.maximumNumberOfLines=0;hint.preferredMaxLayoutWidth=240
    addArrangedSubview(Theme.label("PORT TRIMS",size:10,color:Theme.muted));addArrangedSubview(port)
    addArrangedSubview(stack(.horizontal,[Theme.label("Trim · dB",size:11),gain,ActionButton("Reset"){[weak self] in self?.setGain(0)}]))
    addArrangedSubview(stack(.horizontal,[Theme.label("Link",size:11),link]));addArrangedSubview(hint)
    addArrangedSubview(Theme.label("Modulation · source and dB range",size:10,color:Theme.muted));addArrangedSubview(modulationSource)
    addArrangedSubview(stack(.horizontal,[minimum,Theme.label("to",size:11),maximum,ActionButton("Apply"){[weak self] in self?.commitModulation()}]));stretchAcrossAxis()
  }
  required init?(coder:NSCoder){fatalError()}
  func context(graph:String?,node:String?,revision:String){
    guard let node else{identity="";target=[:];isHidden=true;return}
    let key="\(graph ?? "song")/\(node)"
    if identity != key {window?.makeFirstResponder(nil)}
    if identity==key && self.revision==revision{return}
    if hasDraft && identity==key{return}
    identity=key;self.revision=revision;target=["graph":graph as Any? ?? NSNull(),"node":node];request+=1;let generation=request
    loading=true;read?(target){[weak self] response in
      guard let self,self.request==generation,self.identity==key else{return};self.loading=false
      guard let data=(response["result"] as? [String:Any] ?? response)["data"] as? [String:Any],let rows=data["ports"] as? [[String:Any]] else{self.isHidden=true;return}
      self.sources=data["sources"] as? [[String:Any]] ?? [];self.ports=rows;self.isHidden=rows.isEmpty;self.port.removeAllItems()
      for row in rows{self.port.addItem(withTitle:(row["output"] as? Bool==true ? "Out · ":"In · ")+(row["name"] as? String ?? "Audio"));self.port.lastItem?.representedObject=row["key"]}
      self.port.selectItem(at:rows.firstIndex{$0["key"] as? String==self.selectedKey} ?? 0);self.showPort()
    }
  }
  private func showPort(){guard ports.indices.contains(port.indexOfSelectedItem)else{return};let row=ports[port.indexOfSelectedItem];selectedKey=row["key"] as? String ?? "";shownGain=String(format:"%.2f",(row["gainDB"] as? NSNumber)?.doubleValue ?? 0);gain.stringValue=shownGain
    link.removeAllItems();link.addItem(withTitle:"Independent");link.lastItem?.representedObject=""
    for p in ports where (p["output"] as? Bool)==(!(row["output"] as? Bool ?? false)){link.addItem(withTitle:p["name"] as? String ?? "Audio");link.lastItem?.representedObject=p["key"]}
    let edges=row["modulation"] as? [[String:Any]] ?? [];let edge=edges.first ?? [:]
    modulationSource.removeAllItems();modulationSource.addItem(withTitle:"None");modulationSource.lastItem?.representedObject=""
    for source in sources{modulationSource.addItem(withTitle:source["name"] as? String ?? "Source");modulationSource.lastItem?.representedObject=source["id"]}
    modulationSource.selectItem(at:modulationSource.itemArray.firstIndex{($0.representedObject as? String)==(edge["source"] as? String ?? "")} ?? 0)
    minimum.stringValue=String(format:"%.2f",(edge["minimumDB"] as? NSNumber)?.doubleValue ?? 0);maximum.stringValue=String(format:"%.2f",(edge["maximumDB"] as? NSNumber)?.doubleValue ?? 6)
    for control in [modulationSource,minimum,maximum] as [NSControl]{control.isEnabled=edges.count<=1;control.toolTip=edges.count>1 ? "Multiple sources are configured through the graph API":nil}
    link.selectItem(at:link.itemArray.firstIndex{($0.representedObject as? String)==(row["linkTo"] as? String ?? "")} ?? 0)
  }
  private func commitModulation(){guard !loading,modulationSource.isEnabled,!selectedKey.isEmpty,let lo=Double(minimum.stringValue),let hi=Double(maximum.stringValue),lo.isFinite,hi.isFinite,(-96...96).contains(lo),(-96...96).contains(hi) else{return}
    var p=target;p["expectedRevision"]=revision;p["port"]=selectedKey;let source=modulationSource.selectedItem?.representedObject as? String ?? ""
    p["modulation"]=source.isEmpty ? []:[["source":source,"minimumDB":lo,"maximumDB":hi]];edit?(p)
  }
  @objc private func selectPort(){showPort()}
  @objc private func selectLink(){guard !loading,!selectedKey.isEmpty else{return};var p=target;p["expectedRevision"]=revision;p["port"]=selectedKey;let key=link.selectedItem?.representedObject as? String ?? "";p["linkTo"]=key.isEmpty ? NSNull():key as Any;edit?(p)}
  private func setGain(_ value:Double){guard !loading,value.isFinite,(-48...48).contains(value),!selectedKey.isEmpty else{return};var p=target;p["expectedRevision"]=revision;p["port"]=selectedKey;p["gainDB"]=value;shownGain=gain.stringValue;edit?(p)}
  @objc private func commitGain(){guard gain.stringValue != shownGain,let value=Double(gain.stringValue),value.isFinite,(-48...48).contains(value)else{return};setGain(value)}
  func controlTextDidEndEditing(_ notification:Notification){commitGain()}
}
