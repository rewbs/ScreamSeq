// Native Core Audio adapter around the shared recipe plan and gesture owner.
// Each successful drag is transient until its ordinary graph.plugin.set commit.
- (void)retireRecipeParameterPreview {
  if(_recipePreview.active())_recipePreview.cancel(_recipePreview.active()->key,[](const auto &){return true;});
  _recipePreviewCatalog.clear();_recipePreviewGraph=_recipePreviewNode=0;
}
- (void)cancelRecipeParameterPreview {
  if(!_recipePreview.active())return;
  const auto &entry=*_recipePreview.active();
  if(!entry.audible||entry.hostEpoch!=_audio->pluginEpoch()){[self retireRecipeParameterPreview];return;}
  bool restored=false;
  try{restored=_audio->restoreGraphParameterPreview(_document->native());}
  catch(const std::exception &e){throw Automation::Error(-32002,(std::string("Recipe preview restoration is busy; preview retained: ")+e.what()).c_str());}
  if(!restored)throw Automation::Error(-32002,"Recipe preview restoration queue is busy; preview retained");
  [self retireRecipeParameterPreview];
}
- (void)reconcileRecipeParameterPreview {
  if(_parameterPreviewCommitting||!_recipePreview.active())return;
  const auto &e=*_recipePreview.active();
  if(e.revision!=self.automationRevision.UTF8String||e.hostEpoch!=_audio->pluginEpoch()||(e.audible&&!_audio->active()))
    [self cancelRecipeParameterPreview];
}
- (NSDictionary *)recipeParameterPreviewMethod:(NSString *)method params:(NSDictionary *)p {
  using namespace Automation;
  using Owner=ParameterPreviewOwner;
  const auto describe=[&]()->NSDictionary *{
    if(!_recipePreview.active())return @{@"active":@NO};
    const auto &e=*_recipePreview.active();
    return @{@"active":@YES,@"gesture":@(e.key.token.c_str()),@"graph":nativeID(_recipePreviewGraph),
      @"node":nativeID(_recipePreviewNode),@"parameter":@(e.key.parameter),
      @"revision":@(e.revision.c_str()),@"value":@(e.value),@"audible":@(e.audible)};
  };
  const auto stale=[&]{if(!_recipePreview.active())return false;const auto &e=*_recipePreview.active();
    return e.revision!=self.automationRevision.UTF8String||e.hostEpoch!=_audio->pluginEpoch()||(e.audible&&!_audio->active());};
  if([method isEqual:@"graph.plugin.preview.get"]){keys(p,@[]);[self reconcileRecipeParameterPreview];return describe();}
  const bool update=[method isEqual:@"graph.plugin.preview.set"],commit=[method isEqual:@"graph.plugin.preview.commit"],
    cancel=[method isEqual:@"graph.plugin.preview.cancel"];
  require(update||commit||cancel,"Unknown recipe parameter preview method");
  keys(p,cancel?@[@"expectedRevision",@"graph",@"node",@"parameter",@"gesture"]:@[@"expectedRevision",@"graph",@"node",@"parameter",@"gesture",@"value"]);
  const auto graph=decodeNativeID(p[@"graph"]),node=decodeNativeID(p[@"node"]);
  const Owner::Key key{string(p[@"gesture"],80).UTF8String,"recipe:n"+std::to_string(graph)+":n"+std::to_string(node),
    uint32_t(integer(p[@"parameter"],0,UINT32_MAX))};
  try{Owner::validateKey(key);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
  if(cancel){require(!_recipePreview.active()||_recipePreview.active()->key==key,"Cancellation belongs to another recipe preview");[self cancelRecipeParameterPreview];return @{@"active":@NO};}
  require(!_parameterPreview.active(),"A rack parameter preview is retained; cancel it first");
  if(stale()){[self cancelRecipeParameterPreview];throw Error(-32001,"Playback or song changed; earlier recipe preview cancelled");}
  const auto revision=std::string(self.automationRevision.UTF8String);const auto epoch=_audio->pluginEpoch();
  const bool audible=_audio->active();
  Owner::Entry next;
  try{next=_recipePreview.prepare(key,revision,epoch,0,audible);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
  auto catalog=_recipePreviewCatalog;
  if(!_recipePreview.active()){
    NSError *problem=nil;
    const auto response=[self dispatchAutomationMethod:@"graph.plugin.get" params:@{@"graph":p[@"graph"],@"node":p[@"node"]} error:&problem];
    if(!response)throw Error(int(problem.code),problem.localizedDescription.UTF8String);
    for(NSDictionary *v in response[@"data"][@"parameters"]){PluginParameter entry;
      entry.id=[v[@"id"] unsignedIntValue];entry.min=[v[@"min"] floatValue];entry.max=[v[@"max"] floatValue];
      entry.value=[v[@"manualValue"] floatValue];entry.writable=[v[@"writable"] boolValue];catalog.push_back(std::move(entry));}
  }
  const std::array<std::pair<uint32_t,double>,1> requested{{{key.parameter,
    number(p[@"value"],-double(std::numeric_limits<float>::max()),double(std::numeric_limits<float>::max()))}}};
  try{next.value=prepareParameterEdits(catalog,requested).at(0).second;}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
  if(revision!=self.automationRevision.UTF8String||epoch!=_audio->pluginEpoch()||audible!=_audio->active())
    throw Error(-32001,"Song or playback changed during recipe parameter preparation");
  if(commit){
    try{_recipePreview.require(key,revision);}catch(const std::invalid_argument &e){throw Error(-32001,e.what());}
    _parameterPreviewCommitting=YES;
    struct Guard{BOOL &flag;~Guard(){flag=NO;}}guard{_parameterPreviewCommitting};
    NSError *problem=nil;
    const auto reply=[self dispatchAutomationMethod:@"graph.plugin.set"
      params:@{@"expectedRevision":p[@"expectedRevision"],@"graph":p[@"graph"],@"node":p[@"node"],
        @"parameters":@[@{@"id":@(key.parameter),@"value":@(next.value)}]} error:&problem];
    if(!reply)throw Error(int(problem.code),problem.localizedDescription.UTF8String);
    if([reply[@"changed"] boolValue])[self retireRecipeParameterPreview];else [self cancelRecipeParameterPreview];
    return @{@"active":@NO,@"wouldChange":reply[@"changed"],@"dryRun":@NO};
  }
  std::unique_ptr<GraphControlPlan> plan;
  if(audible){
    try{plan=_audio->prepareGraphParameterPreview(_document->native(),graph,node,key.parameter,next.value,catalog);}
    catch(const std::exception &e){throw Error(-32002,(std::string("Recipe preview preparation is busy; earlier value retained: ")+e.what()).c_str());}
    if(!plan)throw Error(-32002,"Recipe preview needs a prepared playback graph");
  }
  if(!_recipePreview.publish(std::move(next),[&]{
      if(revision!=self.automationRevision.UTF8String||epoch!=_audio->pluginEpoch()||audible!=_audio->active())return false;
      return !audible||_audio->publishGraphControls(std::move(plan));
    }))
    throw Error(-32002,"Recipe preview queue is busy; earlier value retained");
  _recipePreviewGraph=graph;_recipePreviewNode=node;_recipePreviewCatalog.swap(catalog);
  return describe();
}
