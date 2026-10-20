// Shared transient ownership with native Core Audio publication. Saved plugin
// states/manual deltas and unified history remain in their existing owners.
- (void)reconcileParameterPreview {
  if(_parameterPreviewCommitting||!_parameterPreview.active())return;
  const auto &entry=*_parameterPreview.active();
  if(entry.revision!=self.automationRevision.UTF8String||entry.hostEpoch!=_audio->pluginEpoch()||(entry.audible&&!_audio->active()))[self cancelParameterPreview];
}
- (void)refreshParameterPreview {
  // Native document edits can bypass the API adapter. Refresh retires only a
  // stale preview, never a current drag. A full queue keeps its owner for retry.
  try{[self reconcileParameterPreview];}catch(const std::exception &e){_pluginWarning=e.what();}
}
- (void)cancelParameterPreview {
  if(!_parameterPreview.active())return;
  const auto key=_parameterPreview.active()->key;
  if(!_parameterPreview.cancel(key,[&](const auto &entry){
    if(!entry.audible||entry.hostEpoch!=_audio->pluginEpoch())return true;
    const auto found=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &p){return p.instanceID==key.plugin;});
    return found==_plugins.end()||_audio->cancelPluginParameterPreview(size_t(found-_plugins.begin()),key.parameter);
  }))throw Automation::Error(-32002,"Parameter preview cancellation is busy; preview retained");
}
- (NSDictionary *)parameterPreviewMethod:(NSString *)method params:(NSDictionary *)p {
  using namespace Automation;
  const auto describe=[&]()->NSDictionary *{
    if(!_parameterPreview.active())return @{@"active":@NO};
    const auto &e=*_parameterPreview.active();return @{@"active":@YES,@"gesture":@(e.key.token.c_str()),@"plugin":@(e.key.plugin.c_str()),@"parameter":@(e.key.parameter),@"revision":@(e.revision.c_str()),@"value":@(e.value),@"audible":@(e.audible)};
  };
  if([method isEqual:@"plugin.parameters.preview.get"]){keys(p,@[]);[self reconcileParameterPreview];return describe();}
  const bool update=[method isEqual:@"plugin.parameters.preview.set"],commit=[method isEqual:@"plugin.parameters.preview.commit"],cancel=[method isEqual:@"plugin.parameters.preview.cancel"];
  require(update||commit||cancel,"Unknown parameter preview method");
  keys(p,cancel?@[@"expectedRevision",@"plugin",@"parameter",@"gesture"]:@[@"expectedRevision",@"plugin",@"parameter",@"gesture",@"value"]);
  const ParameterPreviewOwner::Key key{string(p[@"gesture"],80).UTF8String,string(p[@"plugin"],128).UTF8String,uint32_t(integer(p[@"parameter"],0,UINT32_MAX))};
  try{ParameterPreviewOwner::validateKey(key);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
  if(cancel){require(!_parameterPreview.active()||_parameterPreview.active()->key==key,"Cancellation belongs to another preview");[self cancelParameterPreview];return @{@"active":@NO};}
  const auto found=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &v){return v.instanceID==key.plugin;});
  require(found!=_plugins.end(),"Plugin instance no longer exists");const auto slot=uint32_t(found-_plugins.begin());
  const auto revision=std::string(self.automationRevision.UTF8String);
  const auto catalog=_audio->pluginParameters(slot);
  const std::array<std::pair<uint32_t,double>,1> requested{{{key.parameter,number(p[@"value"],-double(std::numeric_limits<float>::max()),double(std::numeric_limits<float>::max()))}};
  std::vector<std::pair<uint32_t,float>> edits;
  try{edits=prepareParameterEdits(catalog,requested);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
  const auto value=edits.at(0).second;
  if(_parameterPreview.active()&&(_parameterPreview.active()->revision!=revision||_parameterPreview.active()->hostEpoch!=_audio->pluginEpoch()||(_parameterPreview.active()->audible&&!_audio->active()))){
    [self cancelParameterPreview];throw Error(-32001,"Playback or song changed; earlier parameter preview cancelled");
  }
  if(commit){
    try{_parameterPreview.require(key,revision);}catch(const std::invalid_argument &e){throw Error(-32001,e.what());}
    _parameterPreviewCommitting=YES;
    struct Guard{BOOL &flag;~Guard(){flag=NO;}}guard{_parameterPreviewCommitting};
    const BOOL previousGesture=_parameterGesture;_parameterGesture=NO;_parameterGestureSequence=0;
    struct GestureGuard{BOOL &flag;BOOL previous;uint64_t &sequence;~GestureGuard(){flag=previous;sequence=0;}}gestureGuard{_parameterGesture,previousGesture,_parameterGestureSequence};
    NSError *problem=nil;
    const auto reply=[self dispatchAutomationMethod:@"plugin.parameters.set" params:@{@"expectedRevision":p[@"expectedRevision"],@"plugin":p[@"plugin"],@"values":@[@{@"id":@(key.parameter),@"value":@(value)}]} error:&problem];
    if(!reply)throw Error(int(problem.code),problem.localizedDescription.UTF8String);
    [self cancelParameterPreview];return @{@"active":@NO,@"wouldChange":reply[@"changed"],@"dryRun":@NO};
  }
  ParameterPreviewOwner::Entry next;
  try{next=_parameterPreview.prepare(key,revision,_audio->pluginEpoch(),value,_audio->active());}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
  const std::array<ParameterChange,1> changes{{{slot,key.parameter,value,0}}};const bool audible=next.audible;
  if(!_parameterPreview.publish(std::move(next),[&]{return !audible||_audio->previewPluginParameters(changes);}))throw Error(-32002,"Parameter preview queue is busy; earlier value retained");
  return describe();
}
