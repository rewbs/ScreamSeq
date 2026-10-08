import AppKit

extension SignalGraphEditor {
  func updateQuantizationControl() {
    connectionQuantized.title=graphID==nil ? "Quantize to parameter steps":"Quantize target (all sources)"
    connectionQuantized.isEnabled=true
    connectionQuantized.toolTip="Round the summed modulation to the parameter's supported steps."
    guard connectionKind.titleOfSelectedItem=="Modulation",connectionQuantized.state == .on,
      let node=chosen(destination),let parameter=UInt32(self.parameter.stringValue) else{return}
    let target=realPort(node,parameter,output:false,modulation:true)
    let catalog=portCatalogs[target.node]?["parameters"] as? [[String:Any]] ?? []
    guard let value=catalog.first(where:{($0["id"] as? NSNumber)?.uint32Value==target.number}),value["canSlide"] as? Bool==false else{return}
    connectionQuantized.title="Discrete values (required)"
    connectionQuantized.isEnabled=false
    connectionQuantized.toolTip="This stepped parameter requires quantization. The summed value rounds to its supported steps; continuous values cannot be sent."
  }

  // A recipe may have many playing copies; the target's catalogue supplies the
  // discrete step, while the saved graph records only the musician's mode.
  func withRecipeParameterMode(node:String,parameter:UInt32,chosen:Bool=false,inherit:Bool=true,_ action:@escaping(Bool)->Void) {
    guard let graph=graphID else{return}
    let document=projectionDocument,context=viewContext,capturedRevision=revision
    let finish:([String:Any])->Void={[weak self] value in
      guard let self,self.projectionDocument==document,self.viewContext==context,self.graphID==graph,self.revision==capturedRevision else{return}
      guard value["writable"] as? Bool==true else{self.status.stringValue="This parameter is read-only";return}
      let lo=value["min"] as? Double ?? 0,hi=value["max"] as? Double ?? 1,step=value["step"] as? Double ?? 0
      guard lo.isFinite,hi.isFinite,hi>lo,(hi-lo).isFinite else{self.status.stringValue="This parameter has no usable value range";return}
      let peer=(self.definition?["modulation"] as? [[String:Any]] ?? []).contains{$0["target"] as? String==node && ($0["parameter"] as? NSNumber)?.uint32Value==parameter && $0["enabled"] as? Bool != false && $0["quantized"] as? Bool==true}
      let commit:(Bool)->Void={[weak self] mode in guard let self,self.projectionDocument==document,self.viewContext==context,self.graphID==graph,self.revision==capturedRevision else{return};action(mode)}
      if value["canSlide"] as? Bool==false {self.withSongQuantization(value,alreadyChosen:chosen || (inherit && peer),commit)}
      else {
        let mode=chosen || (inherit && peer)
        guard !mode || (step.isFinite && step>0 && step<=hi-lo)else{self.status.stringValue="This parameter has no supported quantization step";return}
        commit(mode)
      }
    }
    if let parameter=(portCatalogs[node]?["parameters"] as? [[String:Any]])?.first(where:{($0["id"] as? NSNumber)?.uint32Value==parameter}){finish(parameter);return}
    requestGraph("graph.plugin.get",["graph":graph,"node":node],document:document){[weak self] response in
      guard let self,self.graphID==graph,self.viewContext==context,self.revision==capturedRevision,self.projectionDocument==document else{return}
      guard let catalog=(response["result"] as? [String:Any])?["data"] as? [String:Any],let value=(catalog["parameters"] as? [[String:Any]])?.first(where:{($0["id"] as? NSNumber)?.uint32Value==parameter})else{self.status.stringValue="Parameter unavailable; reload this effect's controls";return}
      self.portCatalogs[node]=catalog;finish(value)
    }
  }
}
