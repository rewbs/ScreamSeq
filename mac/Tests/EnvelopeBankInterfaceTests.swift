import AppKit
extension InterfaceTests {
  static func envelopeBankChecks() throws {
    FormulaCatalog.symbols=[["name":"sin","insert":"sin(tau*beats)","category":"Function","description":"Sine"],["name":"start","insert":"start","category":"Value","description":"Start value"]]
    try require(FormulaCatalog.completions("si",range:NSRange(location:0,length:2))==["sin(tau*beats)"],"Formula completion offers a compilable function snippet")
    try require(FormulaCatalog.completions("abc",range:NSRange(location:4,length:1)).isEmpty,"Completion rejects obsolete editor ranges")
    let point:[String:Any]=["position":0,"value":0.5,"curve":"scripted","formula":"start"]
    var formulaReplies=[([String:Any])->Void](),uses=[String]()
    let request:EnvelopeRequest={method,params,reply in if method=="automation.formula.preview"{formulaReplies.append(reply)}}
    let workbench=FormulaWorkbench(source:"start",title:"Formula test",points:[point],selected:0,rows:64,rowsPerBeat:4,request:request){uses.append($0);return false}
    workbench.window?.orderOut(nil)
    RunLoop.current.run(until:Date().addingTimeInterval(0.16))
    workbench.code.string="end";workbench.updatePreview()
    formulaReplies.first?(["result":["data":["values":[[0.0,0.5],[1.0,0.5]]]]])
    try require(workbench.validSource==nil,"Obsolete formula preview cannot authorize a newer draft")
    RunLoop.current.run(until:Date().addingTimeInterval(0.16))
    formulaReplies.last?(["result":["data":["values":[[0.0,0.5],[1.0,0.5]]]]]);workbench.apply()
    try require(uses==["end"] && workbench.code.string=="end" && workbench.status.stringValue.contains("changed"),"Rejected stale use keeps formula text recoverable")
    workbench.close()
    let shape:[String:Any]=["span":16384,"points":[point]]
    var calls=[(String,[String:Any])](),replies=[([String:Any])->Void](),validTarget=true,applied=0
    let bank=EnvelopeBankWindow(title:"Test",target:["kind":"volume","instrument":"n1"],shape:shape,revision:"r1",request:{m,p,r in calls.append((m,p));replies.append(r)},canReplace:{validTarget},applied:{applied+=1})
    bank.window?.orderOut(nil)
    try require((bank.window?.frame.width ?? 0)<=1100,"Bank help text wraps without forcing an oversized window")
    replies.removeFirst()(["result":["revision":"r1","data":["entries":[["id":"n2","name":"Master","shape":shape]],"linkedTemplate":"n2"]]])
    try require(bank.link.stringValue.contains("Master"),"Bank shows the linked song template")
    validTarget=false;let count=calls.count;bank.use(linked:true)
    try require(calls.count==count,"Changed parent draft cannot be overwritten by a bank use")
    validTarget=true;bank.use(linked:true)
    try require(calls.last?.0=="envelope.bank.apply" && calls.last?.1["expectedRevision"] as? String=="r1" && calls.last?.1["linked"] as? Bool==true,"Bank use pins target revision and explicit linkage mode")
    replies.removeLast()(["error":["message":"Song changed"]]);try require(applied==0,"Rejected bank application does not discard parent draft")
    bank.use(linked:false);replies.removeLast()(["result":["revision":"r2","data":[:]]]);try require(applied==1,"Successful independent use notifies original editor exactly once")
    bank.close();FormulaCatalog.symbols=[];FormulaCatalog.notes=""
    print("PASS envelope UI: completion snippets, stale formula previews, retained formulas, linked status, target guards and explicit copy/link requests")
  }
}
