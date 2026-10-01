import AppKit

extension InterfaceTests {
  static func graphParameterBaselineChecks() throws {
    let rack=GraphRackControls(frame:NSRect(x:0,y:0,width:340,height:400))
    var requests=[(String,[String:Any],([String:Any])->Void)]()
    rack.onRequest={requests.append(($0,$1,$2))}
    var p:[String:Any]=["id":7,"name":"Cutoff","min":20.0,"max":20000.0,"value":1079.0,"manualValue":80.0,"effectiveValue":1079.0,"writable":true]
    func reply(_ revision:String){requests.removeFirst().2(["result":["revision":revision,"data":[p]]])}
    func descendants<T:NSView>(_ view:NSView,_ type:T.Type)->[T]{(view as? T).map{[$0]} ?? view.subviews.flatMap{descendants($0,type)}}
    rack.context(["id":"filter"]);reply("song:1")
    rack.table.frame=NSRect(x:0,y:0,width:320,height:200)
    guard let first=rack.table.view(atColumn:0,row:0,makeIfNecessary:true),let slider=descendants(first,ParameterSlider.self).first,let field=descendants(first,ParameterValueField.self).first,let effective=descendants(first,NSTextField.self).first(where:{$0.identifier?.rawValue=="parameter-effective-7"})else{throw InterfaceFailure(message:"Missing baseline parameter row")}
    let host=NSWindow(contentRect:NSRect(x:0,y:0,width:340,height:400),styleMask:[.borderless],backing:.buffered,defer:false);host.contentView=rack
    let text=NSTextView()
    try require(field.returnFocus === rack.table && field.control(field,textView:text,doCommandBy:#selector(NSResponder.insertNewline(_:))) && host.firstResponder === rack.table,"Return commits a graph parameter and leaves text-only Undo focus for global song history")
    let standalone=ParameterValueField()
    try require(!standalone.control(standalone,textView:text,doCommandBy:#selector(NSResponder.insertNewline(_:))),"Other parameter fields retain their established text editing behavior unless they opt in")
    try require(field.stringValue=="80" && abs(slider.doubleValue-(80-20)/19980)<1e-8 && effective.stringValue=="Effective 1079","Editable control uses manual base while a separate label shows effective host snapshot")
    rack.set(7,value:80)
    try require(requests.isEmpty,"Setting the unchanged manual base is a no-op even when modulation changes effective output")
    rack.context(["id":"filter"],revision:"song:2");p["effectiveValue"]=3000.0;p["value"]=3000.0;reply("song:2")
    try require(rack.table.view(atColumn:0,row:0,makeIfNecessary:true) === first && effective.stringValue=="Effective 3000" && field.stringValue=="80","Effective-only refresh retains native controls and updates only the snapshot readout")
    rack.context(["id":"filter"],revision:"song:3");reply("song:3")
    try require(rack.table.view(atColumn:0,row:0,makeIfNecessary:true) === first,"Unrelated document revisions with identical catalogue do not recreate parameter controls")
    rack.context(["id":"filter"],revision:"song:4");slider.gesture?(true);p["manualValue"]=120.0;reply("song:4")
    try require(rack.table.view(atColumn:0,row:0,makeIfNecessary:true) === first && field.stringValue=="80","An in-flight catalogue reply cannot replace a control during its drag")
    slider.gesture?(false)
    try require(requests.count==1 && requests.first?.0=="plugin.parameters.get","Ending the drag requests fresh metadata instead of applying its retired snapshot")
    reply("song:4")
    guard let changed=rack.table.view(atColumn:0,row:0,makeIfNecessary:true)else{throw InterfaceFailure(message:"Missing refreshed row")}
    try require(changed !== first && descendants(changed,ParameterValueField.self).first?.stringValue=="120","External manual baseline changes rebuild stale control captures after editing ends")
    let editedField=descendants(changed,ParameterValueField.self).first!
    editedField.commit?("140");requests.removeFirst().2(["result":["revision":"song:4-edit","data":[:]]])
    try require(editedField.stringValue=="140" && GraphRackControls.manualValue(rack.values[0])==140,"A direct field edit displays and confirms its manual scalar")
    rack.context(["id":"filter"],revision:"song:4-undo");p["manualValue"]=120.0;p["effectiveValue"]=147.5;reply("song:4-undo")
    let undone=rack.table.view(atColumn:0,row:0,makeIfNecessary:true)!
    let undoneField=descendants(undone,ParameterValueField.self).first!
    try require(undone !== changed && undoneField.stringValue=="120","Undo to the pre-edit baseline refreshes the locally edited field even while effective telemetry also changes")
    rack.context(["id":"filter"],revision:"song:4-redo");p["manualValue"]=140.0;reply("song:4-redo")
    let redone=rack.table.view(atColumn:0,row:0,makeIfNecessary:true)!
    let redoneField=descendants(redone,ParameterValueField.self).first!
    try require(redoneField.stringValue=="140","Redo refreshes the manual control and its captured value")
    redoneField.commit?("150");requests.removeFirst().2(["error":["message":"Queue full"]]);reply("song:4-redo")
    let rolledBack=rack.table.view(atColumn:0,row:0,makeIfNecessary:true)!
    try require(descendants(rolledBack,ParameterValueField.self).first?.stringValue=="140" && rack.message.stringValue=="Queue full","A rejected local edit restores the confirmed scalar rather than taking the effective-only refresh path")
    p["effectiveValue"]=3000.0
    rack.context(["id":"filter"],revision:"song:5");p["name"]="Resonance";reply("song:5")
    let renamed=rack.table.view(atColumn:0,row:0,makeIfNecessary:true)!
    try require(renamed !== changed,"Actual parameter metadata changes replace the row and its actions")
    rack.set(7,value:160);rack.set(7,value:180)
    let write=requests.removeFirst();write.2(["result":["revision":"song:6","data":[:]]])
    try require((requests.first?.1["values"] as? [[String:Any]])?.first?["value"] as? Double==180,"Manual writes queued during a pending transaction preserve the latest musician value")
    requests.removeFirst().2(["result":["revision":"song:7","data":[:]]])
    try require(GraphRackControls.manualValue(rack.values[0])==180 && rack.values[0]["effectiveValue"] as? Double==3000,"Queued manual edit updates the editable base without inventing a new effective sample")
    rack.context(["id":"another"]);p["manualValue"]=200.0;reply("song:8")
    try require(GraphRackControls.manualValue(rack.values[0])==200,"Changing stable processor identity loads its own manual catalogue")
    let recipe=GraphPluginControls(frame:.zero)
    var recipeRequests=[(String,[String:Any],([String:Any])->Void)]()
    recipe.onRequest={recipeRequests.append(($0,$1,$2))}
    recipe.context(graph:"g",node:"effect")
    recipeRequests.removeFirst().2(["result":["revision":"recipe:1","data":["parameters":[],"buses":[],"bypass":true]]])
    let bypass=recipe.parametersView.enabled
    try require(!bypass.isHidden && bypass.state == .on && bypass.toolTip?.contains("every use")==true,"Recipe inspector exposes persisted host bypass and its shared scope")
    recipe.load();let retiredRecipeRead=recipeRequests.removeFirst()
    bypass.performClick(nil)
    let bypassWrite=recipeRequests.removeFirst()
    try require(bypassWrite.0=="graph.plugin.bypass" && bypassWrite.1["graph"] as? String=="g" && bypassWrite.1["node"] as? String=="effect" && bypassWrite.1["bypass"] as? Bool==false && bypassWrite.1["parameters"]==nil && !bypass.isEnabled,"Recipe checkbox issues one dedicated revision-guarded host bypass, never a vendor parameter edit")
    bypassWrite.2(["result":["revision":"recipe:2","data":["bypass":false]]])
    retiredRecipeRead.2(["result":["revision":"recipe:1","data":["parameters":[],"buses":[],"bypass":true]]])
    try require(bypass.state == .off && bypass.isEnabled,"A late pre-edit recipe read cannot revert a confirmed bypass state")
    bypass.performClick(nil);recipeRequests.removeFirst().2(["error":["message":"Publication busy"]])
    try require(bypass.state == .off && recipe.parametersView.message.stringValue=="Publication busy","Rejected bypass restores the confirmed flag and explains the rejection")
    recipe.context(graph:"g",node:"effect",revision:"recipe:undo")
    recipeRequests.removeFirst().2(["result":["revision":"recipe:undo","data":["parameters":[],"buses":[],"bypass":true]]])
    try require(bypass.state == .on,"Same-node Undo refreshes persisted recipe bypass")
    let mode=SignalGraphEditor(frame:.zero)
    mode.connectionKind.selectItem(withTitle:"Modulation")
    mode.picker(mode.destination,[("Mode","plugin:effect")],select:"plugin:effect")
    mode.parameter.stringValue="9";mode.connectionQuantized.state = .on
    mode.portCatalogs["plugin:effect"]=["parameters":[["id":9,"canSlide":false,"step":1.0]]]
    mode.updateQuantizationControl()
    try require(!mode.connectionQuantized.isEnabled && mode.connectionQuantized.title.contains("required") && mode.connectionQuantized.toolTip?.contains("stepped parameter")==true,"An already chosen mandatory discrete mode is disabled with a visible explanation")
    mode.graphID="recipe";mode.picker(mode.destination,[("Recipe mode","effect")],select:"effect");mode.portCatalogs["effect"]=mode.portCatalogs["plugin:effect"]
    mode.updateQuantizationControl()
    try require(!mode.connectionQuantized.isEnabled,"Recipe and song modulation explain the same required discrete mode")
    mode.connectionQuantized.state = .off;mode.updateQuantizationControl()
    try require(mode.connectionQuantized.isEnabled && mode.connectionQuantized.state == .off,"A new discrete connection still requires the musician's explicit choice")
    mode.connectionQuantized.state = .on;mode.portCatalogs["effect"]=["parameters":[["id":9,"canSlide":true,"step":0.25]]];mode.updateQuantizationControl()
    try require(mode.connectionQuantized.isEnabled && !mode.connectionQuantized.title.contains("required"),"Changing to a continuous target restores optional quantization editing")
  }
}

extension InterfaceTests {
  static func graphParameterRangeChecks() throws {
    let range=GraphParameterRange(source:"lfo",target:"effect",parameter:7,name:"Slow LFO",minimum:-0.1,maximum:0.3,enabled:true)
    let strip=GraphParameterRangeView(range);strip.frame=NSRect(x:0,y:0,width:300,height:22)
    var writes=[(Double,Double)](),gestures=[Bool](),edits=0
    strip.onCommit={writes.append(($0,$1))};strip.onGesture={gestures.append($0)};strip.onEdit={edits+=1}
    strip.begin(at:0.3,handle:1);strip.drag(to:0.7);strip.drag(to:0.8)
    try require(writes.isEmpty && strip.range.maximum==0.8,"Modulation range drag previews locally without generating per-move Undo transactions")
    strip.finish()
    try require(writes.count==1 && writes[0].0 == -0.1 && writes[0].1==0.8 && gestures==[true,false],"Releasing an endpoint emits exactly one source-specific range transaction")
    strip.begin(at:-0.1,handle:0);strip.drag(to:0.5);strip.finish(cancelled:true)
    try require(writes.count==1 && strip.range==range,"Esc cancels a range draft and leaves the configured range intact")
    strip.begin(at:0,handle:2);strip.drag(to:2);strip.finish()
    try require(abs(writes.last!.0-0.6)<1e-8 && writes.last!.1==1,"Translating a source range preserves its width and clamps at the normalized boundary")
    strip.begin(at:0.3,handle:1);strip.drag(to:-0.5);strip.finish()
    try require(writes.last!.0 == -0.1 && writes.last!.1 == -0.5,"Independent endpoints preserve reversed source mappings")
    let editor=SignalGraphEditor(frame:.zero)
    editor.showExternalFailure("Parameter queue is busy; retry Undo");editor.rebuild()
    try require(editor.status.stringValue.contains("retry Undo"),"A global history rejection stays visible in the floating graph after passive refresh")
    editor.historyDidComplete();editor.rebuild()
    try require(!editor.status.stringValue.contains("retry Undo"),"A successful history retry clears the graph's prior rejection")
    editor.update(["plugins":[["id":"effect","name":"Filter","isInstrument":false]],"songSources":[["id":"n31","name":"Slow LFO","kind":"lfo"],["id":"n32","name":"Random","kind":"random"]],"songModulation":[["source":"n31","plugin":"effect","parameter":7,"minimum":-0.1,"maximum":0.3,"enabled":true],["source":"n32","plugin":"effect","parameter":7,"minimum":0.0,"maximum":0.2,"enabled":false]],"mixer":["buses":[["id":"n1","name":"Master","kind":"master","inserts":["effect"]]]]])
    let song=editor.data
    editor.onRequest={m,_,done in if m=="graph.get"{done(["result":["revision":"song:1","data":song]])}}
    editor.load();editor.selectedID="plugin:effect";editor.canvas.selected=editor.selectedID
    var requests=[(String,[String:Any])]()
    var mutationReply:(([String:Any])->Void)?
    editor.onRequest={m,p,reply in requests.append((m,p));if m=="graph.song.modulation.set"{mutationReply=reply}};editor.inspect()
    let a=GraphParameterRange(source:"n31",target:"effect",parameter:7,name:"Slow LFO",minimum:-0.1,maximum:0.3,enabled:true)
    let commit=editor.rackControls.onRangeCommit
    commit?(a,-0.2,0.4)
    try require(requests.last?.0=="graph.song.modulation.set" && requests.last?.1["source"] as? String=="n31" && requests.last?.1["plugin"] as? String=="effect" && requests.last?.1["parameter"] as? UInt32==7 && requests.last?.1["minimum"] as? Double == -0.2 && requests.last?.1["enabled"]==nil,"Range commit targets only the exact source edge and retains its other settings")
    mutationReply?(["error":["message":"Test rejection"]])
    editor.onRequest={m,p,done in requests.append((m,p));if m=="graph.get"{done(["result":["revision":"song:2","data":song]])}}
    editor.load();let count=requests.filter{$0.0=="graph.song.modulation.set"}.count;commit?(a,-0.4,0.5)
    try require(requests.filter{$0.0=="graph.song.modulation.set"}.count==count && editor.status.stringValue.contains("cancelled"),"A range gesture captured before a graph revision cannot overwrite newer work")
    let recipe=SignalGraphEditor(frame:.zero)
    recipe.update(["library":[["id":"n10","name":"Recipe","nodes":[["id":"n20","name":"Filter","kind":"plugin"],["id":"n21","name":"LFO","kind":"lfo"],["id":"n22","name":"Other","kind":"random"]],"modulation":[["source":"n21","target":"n20","parameter":7,"minimum":0.0,"maximum":0.2,"base":0.3,"enabled":true],["source":"n22","target":"n20","parameter":7,"minimum":-0.1,"maximum":0.1,"base":0.3,"enabled":true]]]]])
    recipe.graphID="n10";recipe.selectedID="n20";recipe.rebuild();recipe.inspect()
    var payload:[String:Any]=[:];recipe.onRequest={m,p,_ in if m=="graph.update"{payload=p}}
    recipe.pluginControls.parametersView.onRangeCommit?(.init(source:"n21",target:"n20",parameter:7,name:"LFO",minimum:0,maximum:0.2,enabled:true),-0.3,0.4)
    let edges=(payload["definition"] as? [String:Any])?["modulation"] as? [[String:Any]] ?? []
    try require(edges.count==2 && edges[0]["minimum"] as? Double == -0.3 && edges[0]["maximum"] as? Double==0.4 && edges[0]["base"] as? Double==0.3 && edges[1]["minimum"] as? Double == -0.1,"Recipe range edits preserve the common base and every other contributing source")
  }
}
