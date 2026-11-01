import AppKit

extension AppController {
  var audioRevision:String {"audio:\(audioSettingsIdentity):\(audioSettingsGeneration)"}
  func audioSettingsData()->[String:Any]{
    let info=session.audioOutputInformation(),id=info["device"] as? Int ?? 0
    let active=info["active"] as? Bool==true
    let activeEndpoint=active ? session.devices().first{$0["id"] as? Int==id}?["uid"] as? String ?? "":""
    return ["audioRevision":audioRevision,"endpoint":audioEndpoint,"periodFrames":audioPeriod,"checked":checkedAudio as Any? ?? NSNull(),"active":active,"activeEndpoint":activeEndpoint,"sampleRate":info["sampleRate"] ?? 48000,"actualPeriodFrames":info["periodFrames"] ?? 128,"bufferFrames":info["bufferFrames"] ?? 128,"inspection":automationTest,"window":audioSettingsEditor.snapshot]
  }
  func handleAudioAutomation(_ method:String,params:[String:Any],reply:@escaping AutomationServer.Reply)->Bool{
    guard method.hasPrefix("audio.") else{return false}
    guard !busy,!sessionReading else{reply(AutomationServer.notAdmittedBusy("Audio settings are busy; no change was queued."));return true}
    let wasPlaying=session.playing
    do {
      func reject(_ message:String,_ code:Int = -32602)->NSError{NSError(domain:"Audio",code:code,userInfo:[NSLocalizedDescriptionKey:message])}
      var data:[String:Any]
      switch method {
      case "audio.settings.get":guard params.isEmpty else{throw reject("audio.settings.get accepts no parameters")};data=audioSettingsData()
      case "audio.devices.get":guard params.isEmpty else{throw reject("audio.devices.get accepts no parameters")};data=["devices":session.devices().map{["id":$0["uid"] ?? "","name":$0["name"] ?? "Output","default":$0["default"] ?? false]}]
      case "audio.settings.set":
        guard Set(params.keys).isSubset(of:["endpoint","periodFrames","expectedAudioRevision","dryRun"]),let expected=params["expectedAudioRevision"] as? String,!expected.isEmpty,expected.utf8.count<=200 else{throw reject("Supply expectedAudioRevision and complete audio settings")}
        guard expected==audioRevision else{throw reject("Audio settings changed; reload before switching output",-32001)}
        guard let endpoint=params["endpoint"] as? String,endpoint.utf8.count<=4096,!endpoint.contains("\0"),let period=params["periodFrames"] as? NSNumber,CFGetTypeID(period) != CFBooleanGetTypeID(),[0.0,64,128,256,512].contains(period.doubleValue) else{throw reject("Use an output endpoint and a preferred period of 0, 64, 128, 256 or 512 frames")}
        let dry:Bool
        if let raw=params["dryRun"] {guard let n=raw as? NSNumber,CFGetTypeID(n)==CFBooleanGetTypeID() else{throw reject("dryRun must be boolean")};dry=n.boolValue}else{dry=false}
        let same=endpoint==audioEndpoint && period.intValue==audioPeriod
        if same && !dry{data=audioSettingsData();break}
        let devices=session.devices(),output=devices.first{$0["uid"] as? String==endpoint}
        guard endpoint.isEmpty || output != nil else{throw reject("Selected output is unavailable; refresh outputs")}
        if dry{data=audioSettingsData();data["proposed"]=["endpoint":endpoint,"periodFrames":period.intValue,"checked":NSNull()];break}
        guard !pluginRecordingInput.protectsDocument,session.parameterRecordingTakeID==nil,session.recordingTakeID==nil,session.sampleRecordingTakeID==nil else{throw reject("Finish the retained recording before switching audio output",-32003)}
        if !automationTest {
          try session.configureDevice(UInt(output?["id"] as? Int ?? 0),buffer:period.uintValue)
          var checked=session.audioOutputInformation();let id=checked["device"] as? Int ?? 0
          checked["endpoint"]=session.devices().first{$0["id"] as? Int==id}?["uid"] ?? "";checkedAudio=checked
          pendingNotes.removeAll();auditionOwnership.clear();inspectorPendingNotes.removeAll()
        }
        audioEndpoint=endpoint;audioPeriod=period.intValue;audioSettingsGeneration+=1;data=audioSettingsData()
      default:throw reject("Unknown audio method",-32601)
      }
      reply(["result":["revision":audioRevision,"data":data,"changed":false,"playbackStopped":wasPlaying && !session.playing]])
    }catch{let error=error as NSError;reply(AutomationServer.error(error.code>=0 ? -32003:error.code,error.localizedDescription))}
    return true
  }
  @objc func audioSettings(){
    if let settingsWindow{settingsWindow.makeKeyAndOrderFront(nil);return}
    let window=NSWindow(contentRect:NSRect(x:0,y:0,width:600,height:390),styleMask:[.titled,.closable,.resizable],backing:.buffered,defer:false)
    window.title="Audio settings";window.minSize=NSSize(width:560,height:380);window.isReleasedWhenClosed=false;window.delegate=self;settingsWindow=window
    audioSettingsEditor.onRequest = {[weak self] method,params,reply in self?.handleAutomation(method,params:params,reply:reply)}
    window.contentView=audioSettingsEditor;window.center();window.makeKeyAndOrderFront(nil);audioSettingsEditor.load()
  }
}
