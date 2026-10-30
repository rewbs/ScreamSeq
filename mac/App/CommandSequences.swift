import AppKit

struct WorkspaceStroke: Equatable {
  let key:String,modifiers:NSEvent.ModifierFlags
  static let mask:NSEvent.ModifierFlags=[.command,.control,.option,.shift]
  init?(_ event:NSEvent){guard let key=event.charactersIgnoringModifiers?.lowercased(),key.count==1 else{return nil};self.key=key;modifiers=event.modifierFlags.intersection(Self.mask)}
  init?(_ value:String){var text=value.lowercased()
    // "+" is both the separator and a key: "cmd+shift++" and "+" name the plus key.
    // The stored format is unchanged, so existing saved bindings still load.
    let plus=text.hasSuffix("+");if plus{text.removeLast();guard text.isEmpty || text.hasSuffix("+") else{return nil};if !text.isEmpty{text.removeLast()}}
    var parts=text.isEmpty ? [] : text.split(separator:"+",omittingEmptySubsequences:false).map(String.init);if plus{parts.append("+")}
    guard let last=parts.last else{return nil};let named=["space":" ","tab":"\t","return":"\r","up":"\u{F700}","down":"\u{F701}","left":"\u{F702}","right":"\u{F703}"]
    key=named[last] ?? last;guard key.count==1 else{return nil};var flags:NSEvent.ModifierFlags=[]
    for name in parts.dropLast(){let flag:NSEvent.ModifierFlags;switch name{case "cmd":flag = .command;case "ctrl":flag = .control;case "opt":flag = .option;case "shift":flag = .shift;default:return nil};guard !flags.contains(flag)else{return nil};flags.insert(flag)};modifiers=flags
  }
  var encoded:String{var parts=[String]();for (flag,name) in [(NSEvent.ModifierFlags.command,"cmd"),(.control,"ctrl"),(.option,"opt"),(.shift,"shift")] where modifiers.contains(flag){parts.append(name)};parts.append([" ":"space","\t":"tab","\r":"return","\u{F700}":"up","\u{F701}":"down","\u{F702}":"left","\u{F703}":"right"][key] ?? key);return parts.joined(separator:"+")}
}
final class WorkspaceSequences {
  var bindings=[String:[WorkspaceStroke]](),prefix=[WorkspaceStroke]()
  var onRun:((String)->Void)?,onHint:((String)->Void)?
  var isAvailable:((String)->Bool)?
  private var timeout:DispatchWorkItem?
  func load(){let raw=UserDefaults.standard.dictionary(forKey:"workspaceSequences") as? [String:[String]] ?? [:];bindings=raw.compactMapValues{values in let strokes=values.compactMap(WorkspaceStroke.init);return strokes.count==values.count && (2...4).contains(strokes.count) ? strokes : nil}}
  func save(){UserDefaults.standard.set(bindings.mapValues{$0.map(\.encoded)},forKey:"workspaceSequences")}
  func cancel(){timeout?.cancel();timeout=nil;prefix=[];onHint?("")}
  func conflict(_ strokes:[WorkspaceStroke],except id:String)->Bool{bindings.contains{key,value in key != id && (value.starts(with:strokes)||strokes.starts(with:value))}}
  func handle(_ event:NSEvent)->Bool {
    guard event.type == .keyDown,!event.isARepeat,let stroke=WorkspaceStroke(event)else{return false}
    if event.keyCode==53 && !prefix.isEmpty{cancel();return true}
    let candidate=prefix+[stroke],matches=bindings.filter{(isAvailable?($0.key) ?? true) && $0.value.starts(with:candidate)}
    guard !matches.isEmpty else{if prefix.isEmpty{return false};cancel();NSSound.beep();return true}
    timeout?.cancel();prefix=candidate
    if let exact=matches.first(where:{$0.value.count==candidate.count}){cancel();onRun?(exact.key);return true}
    onHint?(candidate.map(\.encoded).joined(separator:" → ")+" …")
    let work=DispatchWorkItem{[weak self] in self?.cancel()};timeout=work;DispatchQueue.main.asyncAfter(deadline:.now()+1.5,execute:work);return true
  }
  deinit{timeout?.cancel()}
}

extension WorkspaceCommandPalette {
  func shortcutKeys(_ entry:Entry)->[String] {
    if Self.isTransport(entry) {return transportBinding ?? KeyboardSettings.legacyPlayStopKeys}
    if let keys=sequences.bindings[entry.id] {return keys.map(\.encoded)}
    return entry.item.keyEquivalent.isEmpty ? []:[WorkspaceStrokeString(entry.item.keyEquivalent,entry.item.keyEquivalentModifierMask)]
  }
  func shortcutCommands()->[[String:Any]] {entries.map{["id":$0.id,"name":$0.path,"keys":shortcutKeys($0)]}}
  func validateShortcut(_ id:String,keys:[String],allowUnmodified:Bool=false)->String? {
    guard let entry=entries.first(where:{$0.id==id}),keys.count<=4 else{return "Choose a known command and at most four keys"}
    let strokes=keys.compactMap(WorkspaceStroke.init);guard strokes.count==keys.count else{return "Use keys such as cmd+g, ctrl+opt+r, or space"}
    if let first=strokes.first {
      let localTransport=Self.isTransport(entry) && strokes.count==1 && first.modifiers.isEmpty && [" ","\r"].contains(first.key)
      guard allowUnmodified || localTransport || !first.modifiers.intersection([.command,.control,.option]).isEmpty else{return "The first key needs cmd, ctrl or opt to preserve note entry"}
      for other in entries where other.id != id {
        let existing=shortcutKeys(other).compactMap(WorkspaceStroke.init)
        // Bare Return transports only in PatternView. Graph/panel responders
        // retain their own Return action, so that local binding does not collide.
        if localTransport,first.key=="\r",isContextCommand(other.id),existing==strokes {continue}
        if !existing.isEmpty && (existing.starts(with:strokes) || strokes.starts(with:existing)) {return "That sequence overlaps another shortcut"}
      }
      if sequences.conflict(strokes,except:id){return "That sequence overlaps another shortcut"}
    }
    return nil
  }
  func setShortcut(_ id:String,keys:[String],persist:Bool=true,allowUnmodified:Bool=false)->String? {
    if let error=validateShortcut(id,keys:keys,allowUnmodified:allowUnmodified){return error}
    guard let entry=entries.first(where:{$0.id==id}) else{return "Choose a known command"}
    let strokes=keys.compactMap(WorkspaceStroke.init),normalized=strokes.map(\.encoded)
    guard shortcutKeys(entry) != normalized else{return nil}
    sequences.cancel();sequences.bindings.removeValue(forKey:id)
    entry.item.keyEquivalent="";entry.item.keyEquivalentModifierMask=[]
    if Self.isTransport(entry) {transportBinding=normalized}
    else if strokes.count==1{entry.item.keyEquivalent=strokes[0].key;entry.item.keyEquivalentModifierMask=strokes[0].modifiers}
    if strokes.count>1{sequences.bindings[id]=strokes}
    sessionBindings[id]=["key":strokes.count==1 ? strokes[0].key:"","modifiers":strokes.count==1 ? strokes[0].modifiers.rawValue:0]
    if persist && persistPreferences {
      sequences.save();var bindings=UserDefaults.standard.dictionary(forKey:"workspaceShortcuts") as? [String:[String:Any]] ?? [:]
      bindings[id]=sessionBindings[id]
      UserDefaults.standard.set(bindings,forKey:"workspaceShortcuts")
    }
    table.reloadData();onShortcutsChanged?();return nil
  }
  func recordSequence(_ event:NSEvent){
    if event.keyCode==36{guard recordedSequence.count>=2,filtered.indices.contains(table.selectedRow)else{status.stringValue="Record two to four keys, then Return";return};if let error=setShortcut(filtered[table.selectedRow].id,keys:recordedSequence){status.stringValue=error;return};recording=false;status.stringValue="Command sequence saved";return}
    guard let stroke=WorkspaceStroke(event)else{return}
    if recordedSequence.isEmpty && stroke.modifiers.intersection([.command,.control,.option]).isEmpty{status.stringValue="The first key needs ⌘, ⌃ or ⌥";return}
    guard recordedSequence.count<4 else{status.stringValue="Four keys recorded · Return saves · Escape cancels";return};recordedSequence.append(stroke.encoded);status.stringValue=recordedSequence.joined(separator:" → ")+" · Return saves"
  }
}
func WorkspaceStrokeString(_ key:String,_ flags:NSEvent.ModifierFlags)->String {
  var parts=[String]();for (flag,name) in [(NSEvent.ModifierFlags.command,"cmd"),(.control,"ctrl"),(.option,"opt"),(.shift,"shift")] where flags.contains(flag){parts.append(name)};parts.append([" ":"space","\t":"tab","\r":"return","\u{F700}":"up","\u{F701}":"down","\u{F702}":"left","\u{F703}":"right"][key] ?? key);return parts.joined(separator:"+")
}
