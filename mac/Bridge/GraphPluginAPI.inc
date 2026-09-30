    } else if ([method hasPrefix:@"graph.plugin."]) {
      const bool close=[method isEqual:@"graph.plugin.editor.close"];
      if(close){allowed(@[@"editor"]);require([string(p[@"editor"],100) isEqual:_graphEditorID],"Graph plugin editor has changed");_graphEditorPlugin.reset();_graphEditorID=nil;result=@{};}
      else {
        const auto graphID=decodeNativeID(p[@"graph"]),nodeID=decodeNativeID(p[@"node"]);
        NativeSong next=_document->native();auto definition=std::find_if(next.signal.library.begin(),next.signal.library.end(),[&](const auto &d){return d.id==graphID;});
        require(definition!=next.signal.library.end(),"Subgraph no longer exists");auto node=std::find_if(definition->nodes.begin(),definition->nodes.end(),[&](const auto &n){return n.id==nodeID;});
        require(node!=definition->nodes.end()&&node->kind==SignalNodeKind::Plugin,"Select a graph plugin node");
        auto make=[&](const GraphPluginRecipe &r){PluginState state;state.descriptor={r.type,r.subtype,r.manufacturer,r.name,r.format,r.path,r.classID,false};state.state=r.state;state.auxiliaryInputs=r.inputs;state.auxiliaryOutputs=r.outputs;
          auto plugin=std::make_unique<NativePlugin>(state,_audio->sampleRate(),false);require(!plugin->isInstrument(),"Graph nodes require effect plugins");const auto catalog=plugin->parameters();for(const auto &[id,value]:r.parameters){const auto parameter=std::find_if(catalog.begin(),catalog.end(),[&](const auto &p){return p.id==id;});require(parameter!=catalog.end()&&parameter->writable&&value>=parameter->min&&value<=parameter->max&&plugin->parameter(id,float(value)),"Graph parameter baseline is unavailable or outside its range");}return plugin;};
        auto baseline=[&](const PluginParameter &v){for(const auto &edge:definition->modulation)if(edge.enabled&&edge.target==nodeID&&edge.parameter==v.id)return float(v.min+(v.max-v.min)*edge.base);return v.value;};
        auto metadata=[&](NativePlugin &plugin){NSMutableArray *parameters=[NSMutableArray array];for(const auto &v:plugin.parameters()){NSMutableArray *choices=[NSMutableArray array];for(const auto &choice:v.choices)[choices addObject:@(choice.c_str())];[parameters addObject:@{@"id":@(v.id),@"name":@(v.name.c_str()),@"min":@(v.min),@"max":@(v.max),@"value":@(baseline(v)),@"writable":@(v.writable),@"canSlide":@(v.continuous),@"choices":choices,@"unitLabel":@(v.unitLabel.c_str()),@"step":@(v.step),@"displayScale":v.logarithmic?@"logarithmic":@"linear"}];}
          return @{@"parameters":parameters,@"buses":pluginBusDictionaries(plugin.buses()),@"latency":@(plugin.latency()),@"tail":@(plugin.tail())};};
        bool commit=false,dry=false;NSString *gesture=nil;uint32_t gestureParameter=0;
        if([method isEqual:@"graph.plugin.get"]){allowed(@[@"graph",@"node"]);auto plugin=make(node->plugin);result=metadata(*plugin);}
        else if([method isEqual:@"graph.plugin.set"]){
          allowed(@[@"graph",@"node",@"parameters",@"inputs",@"outputs",@"dryRun",@"gesture"]);dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;
          if(p[@"gesture"]){gesture=string(p[@"gesture"],80);require(gesture.length>0&&!p[@"inputs"]&&!p[@"outputs"]&&array(p[@"parameters"],1024).count==1,"A parameter gesture requires exactly one parameter and no bus changes");gestureParameter=uint32_t(integer(object(array(p[@"parameters"],1024)[0])[@"id"],0,UINT32_MAX));}
          auto plugin=make(node->plugin);const auto buses=plugin->buses();const auto previousInputs=node->plugin.inputs,previousOutputs=node->plugin.outputs;
          auto ports=[&](id raw,bool input){std::vector<uint32_t> list;for(id value:array(raw,63)){auto port=uint32_t(integer(value,1,63));require(std::any_of(buses.begin(),buses.end(),[&](const auto &b){return b.index==port&&b.input==input&&b.supported;}),"Plugin port is unavailable or unsupported");require(std::find(list.begin(),list.end(),port)==list.end(),"Duplicate plugin port");list.push_back(port);}std::sort(list.begin(),list.end());return list;};
          if(p[@"inputs"])node->plugin.inputs=ports(p[@"inputs"],true);if(p[@"outputs"])node->plugin.outputs=ports(p[@"outputs"],false);
          if(previousInputs!=node->plugin.inputs||previousOutputs!=node->plugin.outputs)plugin=make(node->plugin);const auto parameters=plugin->parameters();std::set<uint32_t> seen;
          for(id raw:array(p[@"parameters"]?:@[],1024)){auto value=object(raw);keys(value,@[@"id",@"value"]);auto id=uint32_t(integer(value[@"id"],0,UINT32_MAX));auto found=std::find_if(parameters.begin(),parameters.end(),[&](const auto &v){return v.id==id;});require(found!=parameters.end()&&found->writable&&seen.insert(id).second,"Parameter is unavailable, read-only or repeated");auto amount=number(value[@"value"],found->min,found->max);require(plugin->parameter(id,float(amount)),"Plugin rejected parameter value");if(float(amount)!=baseline(*found))node->plugin.parameters[id]=float(amount);
            if(found->max>found->min)for(auto &m:definition->modulation)if(m.target==nodeID&&m.parameter==id)m.base=(float(amount)-found->min)/(found->max-found->min);}
          result=metadata(*plugin);commit=true;
        } else if([method isEqual:@"graph.plugin.editor.open"]){
          allowed(@[@"graph",@"node"]);auto plugin=make(node->plugin);plugin->showEditor();_graphEditorPlugin=std::move(plugin);_graphEditorRecipe=node->plugin;_graphEditorGraph=graphID;_graphEditorNode=nodeID;_graphEditorDocument=_automationDocumentID;_graphEditorID=NSUUID.UUID.UUIDString;
          result=@{@"editor":_graphEditorID,@"message":@"Edit this library recipe, then Apply plugin settings. Playback copies use it on the next start."};
        } else if([method isEqual:@"graph.plugin.editor.commit"]){
          allowed(@[@"graph",@"node",@"editor",@"dryRun"]);dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;
          require(_graphEditorPlugin&&[string(p[@"editor"],100) isEqual:_graphEditorID]&&[_graphEditorDocument isEqual:_automationDocumentID]&&_graphEditorGraph==graphID&&_graphEditorNode==nodeID,"Graph plugin editor has changed");
          require(node->plugin==_graphEditorRecipe,"Plugin recipe changed; reopen its interface before applying settings");
          node->plugin.state=_graphEditorPlugin->state().state;node->plugin.parameters.clear();commit=true;result=metadata(*_graphEditorPlugin);
        } else throw Error(-32601,"Unknown graph plugin method");
        if(commit){try{next.validate(song);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}if(!dry&&next!=_document->native()){
          const bool controls=next.mixer==_document->native().mixer&&sameSignalControlLayout(_document->native().signal,next.signal);
          auto plan=controls?_audio->prepareGraphControls(next):nullptr;
          if(!controls||(_audio->active()&&!plan))_audio->stop();
          const bool merge=gesture&&[gesture isEqual:_graphParameterGesture]&&[_automationDocumentID isEqual:_graphParameterGestureDocument]&&graphID==_graphParameterGestureGraph&&nodeID==_graphParameterGestureNode&&gestureParameter==_graphParameterGestureID;
          _document->annotate([&](NativeSong &n){n=next;},[&]{if(plan)require(_audio->publishGraphControls(std::move(plan)),"Graph parameter queue is busy; retry the edit");},merge?_graphParameterGestureSequence:0);
          _graphParameterGesture=gesture;_graphParameterGestureDocument=_automationDocumentID;_graphParameterGestureGraph=graphID;_graphParameterGestureNode=nodeID;_graphParameterGestureID=gestureParameter;_graphParameterGestureSequence=_document->historySequence();
        }if(!dry&&[method isEqual:@"graph.plugin.editor.commit"])_graphEditorRecipe=node->plugin;}
      }
