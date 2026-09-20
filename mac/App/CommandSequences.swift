import AppKit

struct WorkspaceStroke: Equatable {
  let key:String,modifiers:NSEvent.ModifierFlags
  static let mask:NSEvent.ModifierFlags=[.command,.control,.option,.shift]
  init?(_ event:NSEvent){guard let key=event.charactersIgnoringModifiers?.lowercased(),key.count==1 else{return nil};self.key=key;modifiers=event.modifierFlags.intersection(Self.mask)}
  init?(_ value:String){let parts=value.lowercased().split(separator:"+",omittingEmptySubsequences:false).map(String.init);guard let last=parts.last else{return nil};let named=["space":" ","tab":"\t","return":"\r"]
    key=named[last] ?? last;guard key.count==1 else{return nil};var flags:NSEvent.ModifierFlags=[]
    for name in parts.dropLast(){let flag:NSEvent.ModifierFlags;switch name{case "cmd":flag = .command;case "ctrl":flag = .control;case "opt":flag = .option;case "shift":flag = .shift;default:return nil};guard !flags.contains(flag)else{return nil};flags.insert(flag)};modifiers=flags
  }
  var encoded:String{var parts=[String]();for (flag,name) in [(NSEvent.ModifierFlags.command,"cmd"),(.control,"ctrl"),(.option,"opt"),(.shift,"shift")] where modifiers.contains(flag){parts.append(name)};parts.append([" ":"space","\t":"tab","\r":"return"][key] ?? key);return parts.joined(separator:"+")}
}
final class WorkspaceSequences {
  var bindings=[String:[WorkspaceStroke]](),prefix=[WorkspaceStroke]()
  var onRun:((String)->Void)?,onHint:((String)->Void)?
  private var timeout:DispatchWorkItem?
  func load(){let raw=UserDefaults.standard.dictionary(forKey:"workspaceSequences") as? [String:[String]] ?? [:];bindings=raw.compactMapValues{values in let strokes=values.compactMap(WorkspaceStroke.init);return strokes.count==values.count && (2...4).contains(strokes.count) ? strokes : nil}}
  func save(){UserDefaults.standard.set(bindings.mapValues{$0.map(\.encoded)},forKey:"workspaceSequences")}
  func cancel(){timeout?.cancel();timeout=nil;prefix=[];onHint?("")}
  func conflict(_ strokes:[WorkspaceStroke],except id:String)->Bool{bindings.contains{key,value in key != id && (value.starts(with:strokes)||strokes.starts(with:value))}}
  func handle(_ event:NSEvent)->Bool {
    guard event.type == .keyDown,!event.isARepeat,let stroke=WorkspaceStroke(event)else{return false}
    if event.keyCode==53 && !prefix.isEmpty{cancel();return true}
    let candidate=prefix+[stroke],matches=bindings.filter{$0.value.starts(with:candidate)}
    guard !matches.isEmpty else{if prefix.isEmpty{return false};cancel();NSSound.beep();return true}
    timeout?.cancel();prefix=candidate
    if let exact=matches.first(where:{$0.value.count==candidate.count}){cancel();onRun?(exact.key);return true}
    onHint?(candidate.map(\.encoded).joined(separator:" → ")+" …")
    let work=DispatchWorkItem{[weak self] in self?.cancel()};timeout=work;DispatchQueue.main.asyncAfter(deadline:.now()+1.5,execute:work);return true
  }
  deinit{timeout?.cancel()}
}

extension WorkspaceCommandPalette {
  func shortcutCommands()->[[String:Any]] {entries.map{entry in let key=entry.item.keyEquivalent;let single=key.isEmpty ? [] : [WorkspaceStrokeString(key,entry.item.keyEquivalentModifierMask)];return ["id":entry.id,"name":entry.path,"keys":sequences.bindings[entry.id]?.map(\.encoded) ?? single]}}
  func setShortcut(_ id:String,keys:[String],persist:Bool=true)->String?{
    guard let entry=entries.first(where:{$0.id==id}),keys.count<=4 else{return "Choose a known command and at most four keys"}
    let strokes=keys.compactMap(WorkspaceStroke.init);guard strokes.count==keys.count else{return "Use keys such as cmd+g, ctrl+opt+r, or space"}
    if let first=strokes.first{guard !first.modifiers.intersection([.command,.control,.option]).isEmpty else{return "The first key needs cmd, ctrl or opt to preserve note entry"}
      if entries.contains(where:{$0.id != id && $0.item.keyEquivalent==first.key && $0.item.keyEquivalentModifierMask.intersection(WorkspaceStroke.mask)==first.modifiers}){return "That first key already runs a command"}
      if sequences.conflict(strokes,except:id){return "That sequence overlaps another shortcut"}
    }
    sequences.cancel();sequences.bindings.removeValue(forKey:id)
    entry.item.keyEquivalent="";entry.item.keyEquivalentModifierMask=[]
    if strokes.count==1{entry.item.keyEquivalent=strokes[0].key;entry.item.keyEquivalentModifierMask=strokes[0].modifiers}
    else if strokes.count>1{sequences.bindings[id]=strokes}
    if persist{sequences.save();var bindings=UserDefaults.standard.dictionary(forKey:"workspaceShortcuts") as? [String:[String:Any]] ?? [:];bindings[id]=["key":entry.item.keyEquivalent,"modifiers":entry.item.keyEquivalentModifierMask.rawValue];UserDefaults.standard.set(bindings,forKey:"workspaceShortcuts")}
    table.reloadData();return nil
  }
  func recordSequence(_ event:NSEvent){
    if event.keyCode==36{guard recordedSequence.count>=2,filtered.indices.contains(table.selectedRow)else{status.stringValue="Record two to four keys, then Return";return};if let error=setShortcut(filtered[table.selectedRow].id,keys:recordedSequence){status.stringValue=error;return};recording=false;status.stringValue="Command sequence saved";return}
    guard let stroke=WorkspaceStroke(event)else{return}
    if recordedSequence.isEmpty && stroke.modifiers.intersection([.command,.control,.option]).isEmpty{status.stringValue="The first key needs ⌘, ⌃ or ⌥";return}
    guard recordedSequence.count<4 else{status.stringValue="Four keys recorded · Return saves · Escape cancels";return};recordedSequence.append(stroke.encoded);status.stringValue=recordedSequence.joined(separator:" → ")+" · Return saves"
  }
}
func WorkspaceStrokeString(_ key:String,_ flags:NSEvent.ModifierFlags)->String {
  var parts=[String]();for (flag,name) in [(NSEvent.ModifierFlags.command,"cmd"),(.control,"ctrl"),(.option,"opt"),(.shift,"shift")] where flags.contains(flag){parts.append(name)};parts.append([" ":"space","\t":"tab","\r":"return"][key] ?? key);return parts.joined(separator:"+")
}
