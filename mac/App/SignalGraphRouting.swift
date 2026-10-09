import AppKit

extension SignalGraphEditor {
  // Rack inserts execute on their owning bus. Moving a chain changes ownership,
  // never replaces processors or routes the entire master back into a channel.
  func insertMove(_ a:String,_ b:String)->[String:Any]? {
    guard let plugin=songNodePlugin[b],
          let target=songNodeBus[a],let destination=insertChain(target) else{return nil}
    let moving:[String]
    if let owner=songNodeBus[b],let inserts=insertChain(owner),let start=inserts.firstIndex(of:plugin){moving=Array(inserts[start...])}
    else if detachedEffects.contains(plugin){moving=[plugin]}else{return nil}
    let remaining=destination.filter{!moving.contains($0)}
    // A channel socket is before its inserts; a processor socket is after it.
    let before:String?
    if let after=songNodePlugin[a] {
      guard !moving.contains(after),let index=remaining.firstIndex(of:after)else{return nil}
      before=index+1<remaining.count ? remaining[index+1]:nil
    }else{before=remaining.first}
    return ["plugins":moving,"target":target,"before":before as Any? ?? NSNull()]
  }
  func audioSocketsConnected(_ a:String,_ b:String,out:UInt32,input:UInt32)->Bool {
    let from=realPort(a,out,output:true,modulation:false),to=realPort(b,input,output:false,modulation:false)
    let edges=ungroupedEdges+(graphID==nil ? songPortEdges:[])
    if edges.contains(where:{!$0.modulation && $0.source==from.node && $0.target==to.node && $0.output==from.number && $0.input==to.number}) {return true}
    // Channel-card patching names the bus tap after its inserts. Existing tap
    // cables are drawn from the final processor, so compare their stored IDs too.
    if graphID==nil,from.number==0,songNodePlugin[from.node]==nil,let bus=songNodeBus[from.node],let plugin=songNodePlugin[to.node] {
      return (mixer["sidechains"] as? [[String:Any]] ?? []).contains{$0["source"] as? String==bus && $0["plugin"] as? String==plugin && ($0["input"] as? NSNumber)?.uint32Value==to.number}
    }
    return false
  }
  func socketPatchHelp(_ key:GraphBoundaryPort)->String? {
    guard graphID==nil,key.output,!key.modulation,key.number==0,songNodePlugin[key.node]==nil,songNodeGraph[key.node]==nil,
      let id=songNodeBus[key.node],let bus=buses.first(where:{$0["id"] as? String==id}),!effectiveInserts(bus).isEmpty else{return nil}
    return "New cables use \(busLabel(bus))’s post-insert bus output; the existing chain stays in place"
  }
  func songBusInputFeedback(_ a:String,_ b:String,out:UInt32,input:UInt32)->String? {
    guard graphID==nil,out==0,songNodePlugin[a]==nil,let owner=songNodeBus[a],songNodeBus[b]==owner,songNodePlugin[b] != nil,
      !audioSocketsConnected(a,b,out:out,input:input)else{return nil}
    return "This channel output is after its inserts; feeding its own insert would create feedback. Use Reconnect cut main input/output… to restore the serial path."
  }
  func cableDescription(_ a:String,_ b:String,out:UInt32,input:UInt32,modulation:Bool)->String {
    if !modulation,audioSocketsConnected(a,b,out:out,input:input){return "These sockets are already connected · existing cable and gain stay unchanged"}
    if !modulation,let reason=songBusInputFeedback(a,b,out:out,input:input){return reason}
    let port=canvas.nodes.first{$0.id==b}?.inputs.first{$0.number==input && $0.modulation==modulation}
    let tap=socketPatchHelp(.init(node:a,number:out,output:true,modulation:modulation)).map{" · "+$0} ?? ""
    return "Add \(canvas.nodes.first{$0.id==a}?.title ?? a) → \(canvas.nodes.first{$0.id==b}?.title ?? b) / \(port?.label ?? "Main in")\(port?.active==false ? " · enables on playback":"") · existing cables stay"+tap
  }
  func connectSongPlugins(_ a:String,_ b:String,output:UInt32,input:UInt32,gain:Double=0,enabled:Bool=true,replacing:[String:Any]?=nil) {
    guard let source=songNodePlugin[a],let target=songNodePlugin[b] else{status.stringValue="Choose two plugin audio sockets";return}
    guard source != target else{status.stringValue="A processor cannot connect to itself";return}
    var value:[String:Any]=["source":source,"output":output,"target":target,"input":input,"gainDB":gain,"enabled":enabled]
    if let replacing {value["replace"]=replacing}
    mutate("mixer.plugin.connection.set",value)
  }
  func modulationBase(node:String,parameter:UInt32)->Double {
    if let peer=(definition?["modulation"] as? [[String:Any]] ?? []).first(where:{$0["target"] as? String==node && ($0["parameter"] as? NSNumber)?.uint32Value==parameter}) {return peer["base"] as? Double ?? 0}
    guard let value=(portCatalogs[node]?["parameters"] as? [[String:Any]] ?? []).first(where:{($0["id"] as? NSNumber)?.uint32Value==parameter}) else{return 0}
    let lo=value["min"] as? Double ?? 0,hi=value["max"] as? Double ?? 1,current=value["value"] as? Double ?? 0
    return hi>lo ? max(0,min(1,(current-lo)/(hi-lo))):0
  }
  func connectPorts(_ a:String,_ b:String,out:UInt32,input:UInt32,modulation:Bool) {
    // Capture the visible ports now; mutate queues a refresh in progress with
    // this revision rather than dropping a completed cable gesture.
    if graphID==nil {
      let from=realPort(a,out,output:true,modulation:modulation),to=realPort(b,input,output:false,modulation:modulation)
      if from.node != a || to.node != b {connectPorts(from.node,to.node,out:from.number,input:to.number,modulation:modulation);return}
    }
    if out==Self.notePort || input==Self.notePort {guard out==Self.notePort,input==Self.notePort,!modulation else{status.stringValue="Notes connect only to Notes sockets";return};connectNotes(a,b);return}
    if !modulation,audioSocketsConnected(a,b,out:out,input:input){status.stringValue="These sockets are already connected · existing cable and gain stay unchanged";return}
    if !modulation,let reason=songBusInputFeedback(a,b,out:out,input:input){status.stringValue=reason;return}
    if graphID==nil,connectSongControl(a,b,out:out,input:input,modulation:modulation){return}
    if graphID==nil,songNodeGraph[a] != nil {status.stringValue="This is one reusable copy in the channel’s serial path. Use the channel output or combined graph-stage sockets, or open the copy to patch its internal nodes.";return}
    let choices=canvas.nodes.map{($0.title,$0.id)}
    picker(source,choices,select:a);picker(destination,choices,select:b)
    outputPort.stringValue=String(out);inputPort.stringValue=String(input);refreshPortChoices()
    if graphID != nil {
      connectionKind.selectItem(withTitle:modulation ? "Modulation":"Audio")
      outputPort.stringValue=String(out);inputPort.stringValue=String(input);parameter.stringValue=String(input)
      // A fresh socket gesture should not inherit an unrelated selected wire's gain.
      let realTarget=realPort(b,input,output:false,modulation:modulation)
      connectionGain.doubleValue=1;minimum.doubleValue=0;maximum.doubleValue=modulation ? 0:1;base.doubleValue=modulation ? modulationBase(node:realTarget.node,parameter:realTarget.number):0;connectionEnabled.state = .on;connectionQuantized.state = .off
      connect(a,b);return
    }
    if stageTarget(a) != nil || stageTarget(b) != nil,stageEndpoint(a) != nil,stageEndpoint(b) != nil {connectStage(a,b,output:out,input:input);return}
    if out==0,input==0,mixer["masterOutputDisconnected"] as? Bool==true,let master=buses.first(where:{$0["kind"] as? String=="master"}),let id=master["id"] as? String,b==id,songNodeBus[a]==id {
      let last=effectiveInserts(master).last.map{"plugin:"+$0};if a==last {mutate("mixer.bus.set",["bus":id,"mainOutputConnected":true]);return}
    }
    if songNodePlugin[a] != nil,songNodePlugin[b] != nil {connectSongPlugins(a,b,output:out,input:input);return}
    if songNodePlugin[b] != nil,input==0 {
      connectionKind.selectItem(withTitle:"Mix into main");connectionGain.doubleValue=0;connectSong(a,b);return
    }
    if songNodeGraph[b] != nil,input==0 {status.stringValue="Use Assign for a reusable channel copy; double-click it to rewire its internal nodes";return}
    outputPort.stringValue=String(out);inputPort.stringValue=String(input);connectionGain.doubleValue=0
    if input>0 {connectionKind.selectItem(withTitle:songNodePlugin[b] != nil ? "Plugin sidechain":"Graph sidechain")}
    else if songNodePlugin[a] != nil && (out>0 || songNodeBus[a]==nil || !buses.contains(where:{$0["id"] as? String==songNodeBus[a]})) {connectionKind.selectItem(withTitle:"Plugin auxiliary")}
    else if out>0 {connectionKind.selectItem(withTitle:"Graph auxiliary")}
    else {connectionKind.selectItem(withTitle:"Main output")}
    connectSong(a,b)
  }
  func rewire(_ index:Int,source a:String,target b:String,out:UInt32,input:UInt32,modulation:Bool) {
    if canvas.edges.indices.contains(index),let reason=canvas.edges[index].readOnlyReason{status.stringValue=reason;return}
    guard canvas.edges.indices.contains(index)else{return}
    if graphID==nil {
      let from=realPort(a,out,output:true,modulation:modulation),to=realPort(b,input,output:false,modulation:modulation)
      if from.node != a || to.node != b {rewire(index,source:from.node,target:to.node,out:from.number,input:to.number,modulation:modulation);return}
    }
    let old=canvas.edges[index]
    if graphID==nil,songConnections.indices.contains(index),songConnections[index]["kind"] as? String=="note" {guard out==Self.notePort,input==Self.notePort,!modulation else{status.stringValue="Rewiring preserves note-event cables";return};connectNotes(a,b,replacing:index);return}
    if old.source==a && old.target==b && old.output==out && old.input==input {return}
    if graphID != nil {
      let original=definitionEdgeIndices[index]
      let from=realPort(a,out,output:true,modulation:modulation),to=realPort(b,input,output:false,modulation:modulation)
      let a=from.node,b=to.node,out=from.number,input=to.number
      let apply:(Bool)->Void={[weak self] quantized in guard let self else{return};let baseline=self.modulationBase(node:b,parameter:input)
        self.updateDefinition{d in
          var audio=d["audio"] as? [[String:Any]] ?? [],mods=d["modulation"] as? [[String:Any]] ?? []
          if original<audio.count {audio[original].merge(["source":a,"target":b,"output":out,"input":input]){_,new in new};d["audio"]=audio}
          else if mods.indices.contains(original-audio.count) {
            let i=original-audio.count
            mods[i].merge(["source":a,"target":b,"parameter":input,"base":baseline,"quantized":quantized]){_,new in new};d["modulation"]=mods
          }
        }
      }
      if modulation {let mods=definition?["modulation"] as? [[String:Any]] ?? [],i=original-(definition?["audio"] as? [[String:Any]] ?? []).count;let existing=mods.indices.contains(i) ? mods[i]:[:];let same=existing["target"] as? String==b && (existing["parameter"] as? NSNumber)?.uint32Value==input;let chosen=existing["quantized"] as? Bool==true;if same{apply(chosen)}else{withRecipeParameterMode(node:b,parameter:input,apply)}}else{apply(false)}
      return
    }
    if songConnections.indices.contains(index),["modulation","follower-input"].contains(songConnections[index]["kind"] as? String ?? "") {
      selectConnection(index);outputPort.stringValue=String(out);inputPort.stringValue=String(input);parameter.stringValue=String(input)
      updateSongControl(index,source:a,target:b);return
    }
    // Do not turn a fixed insert wire into an unrelated bus-output mutation.
    guard songConnections.indices.contains(index)else{return}
    let kind=songConnections[index]["kind"] as? String ?? ""
    if kind=="plugin-connection" || kind=="stage-connection" {selectConnection(index);outputPort.stringValue=String(out);inputPort.stringValue=String(input);updateSongConnection(index,source:a,target:b);return}
    guard ["output","send","graph-input","graph-output","plugin-input","plugin-output"].contains(kind) else {
      status.stringValue="This cable follows the insert order. Use Move insert chain… or drag the processor onto another cable; sockets add audio.";return
    }
    if songNodePlugin[b] != nil,input==0,kind != "plugin-input" {status.stringValue="This cable targets a bus input. Drag from the effect’s socket to add a separate input, or use Move insert chain…";return}
    selectConnection(index);outputPort.stringValue=String(out);inputPort.stringValue=String(input)
    updateSongConnection(index,source:a,target:b)
  }
}
