import AppKit

final class AudioSettingsEditor:NSView {
  let device=NSPopUpButton(),period=NSPopUpButton(),status=Theme.label("Choose an output and preferred period.",size:12,color:Theme.muted)
  var onRequest:((String,[String:Any],@escaping([String:Any])->Void)->Void)?
  private(set) var devices=[[String:Any]](),settings=[String:Any](),pending=false
  private let periods=[0,64,128,256,512]
  private var selected="",frames=128
  override init(frame:NSRect){super.init(frame:frame)
    device.target=self;device.action = #selector(selection);device.setAccessibilityLabel("Audio output device")
    period.target=self;period.action = #selector(selection);period.setAccessibilityLabel("Preferred audio buffer period")
    period.addItems(withTitles:periods.map{$0==0 ? "Lowest supported":"\($0) frames"});period.selectItem(at:2)
    let content=stack(.vertical,[Theme.label("Audio output",size:20,weight:.semibold),
      Theme.label("Output device",size:12),stack(.horizontal,[device,ActionButton("Refresh outputs"){[weak self] in self?.refresh()}]),
      Theme.label("Preferred buffer period",size:12),period,
      Theme.label("The device chooses its supported period and sample rate. Switching output stops playback. Settings last for this session.",size:12,color:Theme.muted),
      status,NSView(),stack(.horizontal,[ActionButton("Use current settings"){[weak self] in self?.reload()},NSView(),ActionButton("Switch output"){[weak self] in self?.apply()}])],spacing:12)
    content.stretchAcrossAxis();content.fill(self,inset:20);status.cell?.wraps=true;status.maximumNumberOfLines=4
  }
  required init?(coder:NSCoder){fatalError()}
  private func request(_ method:String,_ params:[String:Any]=[:],done:@escaping([String:Any])->Void){guard !pending,let onRequest else{return};pending=true;device.isEnabled=false;period.isEnabled=false
    onRequest(method,params){[weak self] response in guard let self else{return};self.pending=false;self.device.isEnabled=true;self.period.isEnabled=true
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any] else{self.status.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Audio settings unavailable; your selection is retained.";return};done(data)
    }
  }
  func load(){refresh{[weak self] in self?.reload()}}
  func refresh(_ done:(()->Void)?=nil){request("audio.devices.get"){[weak self] data in guard let self else{return};self.devices=data["devices"] as? [[String:Any]] ?? [];self.showOutputs();self.status.stringValue="Outputs refreshed · your selection is retained.";done?()}}
  func reload(){request("audio.settings.get"){[weak self] data in guard let self else{return};self.settings=data;self.selected=data["endpoint"] as? String ?? "";self.frames=data["periodFrames"] as? Int ?? 128;self.showOutputs();self.period.selectItem(at:self.periods.firstIndex(of:self.frames) ?? 2);self.describe()}}
  private func showOutputs(){device.removeAllItems();let defaultItem=NSMenuItem(title:"System default",action:nil,keyEquivalent:"");defaultItem.representedObject="";device.menu?.addItem(defaultItem)
    for output in devices {let item=NSMenuItem(title:(output["name"] as? String ?? "Output")+(output["default"] as? Bool==true ? " (default)":""),action:nil,keyEquivalent:"");item.representedObject=output["id"];device.menu?.addItem(item)}
    if !selected.isEmpty,!devices.contains(where:{$0["id"] as? String==selected}){let missing=NSMenuItem(title:"Unavailable · "+selected,action:nil,keyEquivalent:"");missing.representedObject=selected;device.menu?.addItem(missing)}
    device.selectItem(at:device.itemArray.firstIndex{$0.representedObject as? String==selected} ?? 0)
  }
  @objc func selection(){selected=device.selectedItem?.representedObject as? String ?? "";frames=periods[max(0,period.indexOfSelectedItem)]}
  func apply(){guard let revision=settings["audioRevision"] as? String else{status.stringValue="Read current settings before switching output.";return};selection()
    request("audio.settings.set",["endpoint":selected,"periodFrames":frames,"expectedAudioRevision":revision]){[weak self] data in self?.settings=data;self?.describe()}
  }
  private func describe(){if let checked=settings["checked"] as? [String:Any]{status.stringValue="Checked output · \(checked["sampleRate"] ?? 0) Hz · actual period \(checked["periodFrames"] ?? 0) frames · buffer \(checked["bufferFrames"] ?? 0) frames. Press Play to resume."}
    else{status.stringValue=settings["inspection"] as? Bool==true ? "Settings retained; the silent test host did not open hardware.":"Choose an output and preferred period. Switching stops playback."}}
  var snapshot:[String:Any]{["visible":window?.isVisible==true,"pending":pending,"endpoint":selected,"periodFrames":frames,"audioRevision":settings["audioRevision"] ?? "","devices":devices,"status":status.stringValue]}
}
