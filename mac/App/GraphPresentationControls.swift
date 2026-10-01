import AppKit

final class GraphPresentationControls:NSView,NSTextFieldDelegate {
  let title=NSTextField(),text=NSTextField(),width=NSTextField(),height=NSTextField(),color=NSPopUpButton()
  let collapsed=NSButton(checkboxWithTitle:"Collapse frame",target:nil,action:nil)
  var onEdit:(([String:Any])->Void)?,onRemove:(()->Void)?
  private var identity:String?,shown=[String:String](),isShowing=false
  var dirty:Bool {identity != nil && [title,text,width,height].contains{shown[$0.identifier?.rawValue ?? ""] != $0.stringValue}}
  override init(frame:NSRect){super.init(frame:frame)
    for (field,key,label) in [(title,"title","Annotation title"),(text,"text","Comment text"),(width,"width","Annotation width"),(height,"height","Annotation height")] {field.identifier = .init(key);field.delegate=self;field.setAccessibilityLabel(label);field.target=self;field.action=#selector(commit(_:))}
    text.maximumNumberOfLines=0;text.lineBreakMode = .byWordWrapping;text.fixed(height:70)
    for (name,value) in [("Sage",0x658b82),("Blue",0x6488a5),("Amber",0xa18b60),("Violet",0x8c719f),("Rose",0xa57783),("Grey",0x858c93)]{color.addItem(withTitle:name);color.lastItem?.representedObject=value}
    color.target=self;color.action=#selector(colorChanged);collapsed.target=self;collapsed.action=#selector(collapseChanged)
    let content=stack(.vertical,[Theme.label("VISUAL ANNOTATION · SOUND UNCHANGED",size:10,color:Theme.muted),title,text,stack(.horizontal,[Theme.label("Size",size:11),width,height]),stack(.horizontal,[color,collapsed]),ActionButton("Remove annotation, keep contents"){[weak self] in self?.onRemove?()}],spacing:6)
    content.stretchAcrossAxis();content.fill(self)
  }
  required init?(coder:NSCoder){fatalError()}
  func context(_ region:[String:Any]?){
    guard let region else{identity=nil;shown=[:];return}
    let id=region["id"] as? String
    if id==identity && dirty{return};identity=id;isShowing=true
    for (field,key) in [(title,"title"),(text,"text"),(width,"width"),(height,"height")]{field.stringValue=region[key].map{String(describing:$0)} ?? (key=="width" ? "320":key=="height" ? "180":"");shown[key]=field.stringValue}
    text.isHidden=region["kind"] as? String != "comment";collapsed.isHidden=region["kind"] as? String=="comment";collapsed.state=region["collapsed"] as? Bool==true ? .on:.off
    let value=region["color"] as? Int ?? 0x658b82
    if let i=color.itemArray.firstIndex(where:{$0.representedObject as? Int==value}){color.selectItem(at:i)}
    isShowing=false
  }
  func controlTextDidEndEditing(_ notification:Notification){if let field=notification.object as? NSTextField{commit(field)}}
  @objc func commit(_ field:NSTextField){guard !isShowing,identity != nil,let key=field.identifier?.rawValue,shown[key] != field.stringValue else{return}
    let value:Any
    if key=="width"||key=="height"{guard let number=Double(field.stringValue),number.isFinite,number >= (key=="width" ? 120:60),number<=100000 else{field.stringValue=shown[key] ?? "";return};value=number}else{value=field.stringValue}
    shown[key]=field.stringValue;onEdit?([key:value])
  }
  @objc func colorChanged(){guard !isShowing,let value=color.selectedItem?.representedObject as? Int else{return};onEdit?(["color":value])}
  @objc func collapseChanged(){guard !isShowing else{return};onEdit?(["collapsed":collapsed.state == .on])}
}
