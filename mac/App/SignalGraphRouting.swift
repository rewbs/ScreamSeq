import AppKit

extension SignalGraphEditor {
  // Rack inserts execute on their owning bus. Moving a chain changes ownership,
  // never replaces processors or routes the entire master back into a channel.
  func insertMove(_ a:String,_ b:String)->[String:Any]? {
    guard let plugin=songNodePlugin[b],
          let target=songNodeBus[a],let destination=buses.first(where:{$0["id"] as? String==target}) else{return nil}
    let moving:[String]
    if detachedEffects.contains(plugin){moving=[plugin]}
    else{guard let owner=buses.first(where:{effectiveInserts($0).contains(plugin)})else{return nil};let inserts=effectiveInserts(owner);guard let start=inserts.firstIndex(of:plugin)else{return nil};moving=Array(inserts[start...])}
    let remaining=effectiveInserts(destination).filter{!moving.contains($0)}
    // A channel socket is before its inserts; a processor socket is after it.
    let before:String?
    if let after=songNodePlugin[a] {
      guard !moving.contains(after),let index=remaining.firstIndex(of:after)else{return nil}
      before=index+1<remaining.count ? remaining[index+1]:nil
    }else{before=remaining.first}
    return ["plugins":moving,"target":target,"before":before as Any? ?? NSNull()]
  }
  func cableDescription(_ a:String,_ b:String,out:UInt32,input:UInt32,modulation:Bool)->String {
    if graphID==nil,!modulation,out==0,input==0,!canvas.addingMainInput,let move=insertMove(a,b),let ids=move["plugins"] as? [String] {
      let names=ids.map{id in rackPlugins.first{$0["id"] as? String==id}?["name"] as? String ?? id}.joined(separator:" → ")
      let target=buses.first{$0["id"] as? String==move["target"] as? String}?["name"] as? String ?? "channel"
      return "Release to move \(names) to \(target) · one Undo step"
    }
    let port=canvas.nodes.first{$0.id==b}?.inputs.first{$0.number==input && $0.modulation==modulation}
    return "Add \(canvas.nodes.first{$0.id==a}?.title ?? a) → \(canvas.nodes.first{$0.id==b}?.title ?? b) / \(port?.label ?? "Main in")\(port?.active==false ? " · enables on playback":"") · existing cables stay"
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
    if graphID==nil,connectSongControl(a,b,out:out,input:input,modulation:modulation){return}
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
    if out==0,input==0,let move=insertMove(a,b),!canvas.addingMainInput {connectionKind.selectItem(withTitle:"Main output");mutate("mixer.inserts.move",move);return}
    if songNodePlugin[b] != nil,input==0 {
      if canvas.addingMainInput {connectionKind.selectItem(withTitle:"Mix into main");connectionGain.doubleValue=0;connectSong(a,b)}
      else{status.stringValue="Move an effect chain onto a channel wire, or Option-drag to mix another channel into Main in"};return
    }
    if songNodeGraph[b] != nil,input==0 {status.stringValue="Use Assign for a reusable channel copy; double-click it to rewire its internal nodes";return}
    outputPort.stringValue=String(out);inputPort.stringValue=String(input);connectionGain.doubleValue=0
    if input>0 {connectionKind.selectItem(withTitle:songNodePlugin[b] != nil ? "Plugin sidechain":"Graph sidechain")}
    else if songNodePlugin[a] != nil && (out>0 || songNodeBus[a]==nil) {connectionKind.selectItem(withTitle:"Plugin auxiliary")}
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
    if out==0,input==0,kind != "plugin-input",let move=insertMove(a,b) {mutate("mixer.inserts.move",move);return}
    guard ["output","send","graph-input","graph-output","plugin-input","plugin-output"].contains(kind) else {
      status.stringValue="Move a chain by dragging its first input to another channel’s output. Open a reusable subgraph to edit its wires.";return
    }
    if songNodePlugin[b] != nil,input==0,kind != "plugin-input" {status.stringValue="Drop on a bus input to reroute this output, or drag a channel output to the effect input to move its chain";return}
    selectConnection(index);outputPort.stringValue=String(out);inputPort.stringValue=String(input)
    updateSongConnection(index,source:a,target:b)
  }
}
