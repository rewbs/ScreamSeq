import AppKit

// Edits the reusable recipe. The custom interface owns a private draft until
// Apply is pressed, so its controls never race the audio callback's clones.
final class GraphPluginControls: NSView {
  let parameter=NSPopUpButton(),value=NSTextField(string:""),inputs=NSTextField(string:""),outputs=NSTextField(string:"")
  let message=Theme.label("Load controls to inspect this effect",size:10,color:Theme.muted)
  var onRequest:((String,[String:Any],@escaping ([String:Any])->Void)->Void)?
  var onChanged:(()->Void)?,onParameter:((UInt32)->Void)?
  var onCatalog:((String,String,[String:Any])->Void)?
  var currentRevision:(()->String)?
  private var graph="",node="",revision="",parameters=[[String:Any]](),pending=false
  private var editor:String?,editorGraph="",editorNode=""
  override init(frame:NSRect){
    super.init(frame:frame)
    parameter.target=self;parameter.action = #selector(selectParameter)
    parameter.setAccessibilityLabel("Graph plugin parameter");value.setAccessibilityLabel("Graph plugin parameter value")
    inputs.placeholderString="e.g. 1, 2";outputs.placeholderString="e.g. 1, 2";inputs.setAccessibilityLabel("Enabled auxiliary input buses");outputs.setAccessibilityLabel("Enabled auxiliary output buses")
    value.fixed(width:80)
    let content=stack(.vertical,[
      stack(.horizontal,[ActionButton("Load controls"){[weak self] in self?.load()},ActionButton("Custom interface"){[weak self] in self?.openEditor()}]),parameter,
      stack(.horizontal,[value,ActionButton("Set value"){[weak self] in self?.setValue()},ActionButton("Modulate"){[weak self] in self?.useParameter()}]),
      stack(.horizontal,[Theme.label("Aux in",size:11),inputs]),stack(.horizontal,[Theme.label("Aux out",size:11),outputs]),
      ActionButton("Apply ports"){[weak self] in self?.setPorts()},
      stack(.horizontal,[ActionButton("Apply plugin settings"){[weak self] in self?.commitEditor()},ActionButton("Close interface"){[weak self] in self?.closeEditor()}]),message
    ],spacing:5);content.stretchAcrossAxis();content.fill(self)
  }
  required init?(coder:NSCoder){fatalError()}
  func context(graph:String?,node:String?){let g=graph ?? "",n=node ?? "";guard g != self.graph || n != self.node else{return};self.graph=g;self.node=n;parameters=[];parameter.removeAllItems();value.stringValue="";inputs.stringValue="";outputs.stringValue="";revision="";message.stringValue="Load controls to inspect this effect"}
  private func request(_ method:String,_ extra:[String:Any]=[:],write:Bool=false,done:@escaping([String:Any])->Void){
    guard !pending,!graph.isEmpty,!node.isEmpty,let onRequest else{return};pending=true;let g=graph,n=node
    var params=extra;params["graph"]=g;params["node"]=n;if write{params["expectedRevision"]=revision.isEmpty ? currentRevision?() ?? "" : revision}
    onRequest(method,params){[weak self] response in guard let self else{return};self.pending=false
      // Retain the close/commit token even if the musician selected another
      // node while the custom interface was being created.
      if method=="graph.plugin.editor.open",let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any],let token=data["editor"] as? String {self.editor=token;self.editorGraph=g;self.editorNode=n}
      guard self.graph==g,self.node==n else{return}
      guard let result=response["result"] as? [String:Any],let data=result["data"] as? [String:Any] else {self.message.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Plugin operation failed";return}
      self.revision=result["revision"] as? String ?? "";done(data)
    }
  }
  private func populate(_ data:[String:Any]){
    onCatalog?(graph,node,data)
    parameters=data["parameters"] as? [[String:Any]] ?? [];parameter.removeAllItems();parameter.addItems(withTitles:parameters.map{$0["name"] as? String ?? "Parameter"});selectParameter()
    let buses=data["buses"] as? [[String:Any]] ?? []
    for (field,direction) in [(inputs,"input"),(outputs,"output")]{field.stringValue=buses.filter{$0["direction"] as? String==direction && $0["active"] as? Bool==true && ($0["index"] as? Int ?? 0)>0}.compactMap{$0["index"] as? Int}.map(String.init).joined(separator:", ")}
    message.stringValue=buses.filter{($0["index"] as? Int ?? 0)>0}.map{"\($0["direction"] as? String ?? "") \($0["index"] as? Int ?? 0): \($0["name"] as? String ?? "")"}.joined(separator:" · ")
    if message.stringValue.isEmpty{message.stringValue="Settings update this library recipe and all its future playback copies."}
  }
  func load(){request("graph.plugin.get"){[weak self] data in self?.populate(data)}}
  @objc private func selectParameter(){guard parameters.indices.contains(parameter.indexOfSelectedItem)else{return};let p=parameters[parameter.indexOfSelectedItem];value.stringValue="\(p["value"] ?? 0)";value.isEnabled=p["writable"] as? Bool ?? false;value.toolTip="\(p["min"] ?? 0)…\(p["max"] ?? 1) \(p["unitLabel"] ?? "")"}
  private func useParameter(){guard parameters.indices.contains(parameter.indexOfSelectedItem),let id=(parameters[parameter.indexOfSelectedItem]["id"] as? NSNumber)?.uint32Value else{return};onParameter?(id)}
  private func setValue(){guard parameters.indices.contains(parameter.indexOfSelectedItem),let id=parameters[parameter.indexOfSelectedItem]["id"],let amount=Double(value.stringValue),amount.isFinite else{return};request("graph.plugin.set",["parameters":[["id":id,"value":amount]]],write:true){[weak self] data in self?.populate(data);self?.onChanged?()}}
  private func ports(_ field:NSTextField)->[Int]?{if field.stringValue.trimmingCharacters(in:.whitespaces).isEmpty{return []};let values=field.stringValue.split(separator:",").map{$0.trimmingCharacters(in:.whitespaces)};let numbers=values.compactMap(Int.init);return numbers.count==values.count ? numbers : nil}
  private func setPorts(){guard let i=ports(inputs),let o=ports(outputs)else{message.stringValue="Use comma-separated auxiliary bus numbers";return};request("graph.plugin.set",["inputs":i,"outputs":o],write:true){[weak self] data in self?.populate(data);self?.onChanged?()}}
  func openEditor(){revision=currentRevision?() ?? revision;request("graph.plugin.editor.open",write:true){[weak self] data in guard let self else{return};self.editor=data["editor"] as? String;self.editorGraph=self.graph;self.editorNode=self.node;self.message.stringValue=data["message"] as? String ?? "Apply plugin settings when finished"}}
  private func commitEditor(){guard let editor,graph==editorGraph,node==editorNode else{message.stringValue="Open this effect's custom interface first";return};revision=currentRevision?() ?? revision;request("graph.plugin.editor.commit",["editor":editor],write:true){[weak self] data in self?.populate(data);self?.onChanged?()}}
  private func closeEditor(){guard !pending,let editor,let onRequest else{return};pending=true;onRequest("graph.plugin.editor.close",["editor":editor,"expectedRevision":currentRevision?() ?? revision]){[weak self] response in guard let self else{return};self.pending=false;if response["result"] != nil{self.editor=nil;self.message.stringValue="Interface closed"}else{self.message.stringValue=(response["error"] as? [String:Any])?["message"] as? String ?? "Could not close interface"}}}
}
