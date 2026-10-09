    } else if ([method hasPrefix:@"graph.plugin."]) {
      const bool close=[method isEqual:@"graph.plugin.editor.close"];
      if(close){allowed(@[@"editor"]);require([string(p[@"editor"],100) isEqual:_graphEditorID],"Graph plugin editor has changed");_graphEditorPlugin.reset();_graphEditorID=nil;result=@{};}
      else {
        const auto graphID=decodeNativeID(p[@"graph"]),nodeID=decodeNativeID(p[@"node"]);
        NativeSong next=_document->native();auto definition=std::find_if(next.signal.library.begin(),next.signal.library.end(),[&](const auto &d){return d.id==graphID;});
        require(definition!=next.signal.library.end(),"Subgraph no longer exists");auto node=std::find_if(definition->nodes.begin(),definition->nodes.end(),[&](const auto &n){return n.id==nodeID;});
        require(node!=definition->nodes.end()&&node->kind==SignalNodeKind::Plugin,"Select a graph plugin node");
                auto make=[&](const GraphPluginRecipe &r){{const auto hint=_pluginPathHints.graph.find({graphID,nodeID});const auto issue=pluginLocationIssue({r.type,r.subtype,r.manufacturer,r.name,r.format,r.path,r.classID,false},hint==_pluginPathHints.graph.end()?nullptr:&hint->second,_pluginInventory->cached());if(!issue.empty())throw std::runtime_error(issue);}PluginState state;state.descriptor={r.type,r.subtype,r.manufacturer,r.name,r.format,r.path,r.classID,false};state.state=r.state;state.audioLayout=r.audioLayout;state.auxiliaryInputs=r.inputs;state.auxiliaryOutputs=r.outputs;
          auto plugin=std::make_unique<NativePlugin>(state,_audio->sampleRate(),false);require(!plugin->isInstrument(),"Graph nodes require effect plugins");const auto catalog=plugin->parameters();for(const auto &[id,value]:r.parameters){const auto parameter=std::find_if(catalog.begin(),catalog.end(),[&](const auto &p){return p.id==id;});require(parameter!=catalog.end()&&parameter->writable&&value>=parameter->min&&value<=parameter->max&&plugin->parameter(id,float(value)),"Graph parameter baseline is unavailable or outside its range");}return plugin;};
        auto baseline=[&](const PluginParameter &v){return float(signalManualParameterValue(*definition,nodeID,v.id,v.min,v.max,v.value));};
        auto metadata=[&](NativePlugin &plugin){NSMutableArray *parameters=[NSMutableArray array];for(const auto &v:plugin.parameters()){NSMutableArray *choices=[NSMutableArray array];for(const auto &choice:v.choices)[choices addObject:@(choice.c_str())];[parameters addObject:@{@"id":@(v.id),@"name":@(v.name.c_str()),@"min":@(v.min),@"max":@(v.max),@"value":@(baseline(v)),@"manualValue":@(baseline(v)),@"effectiveValue":NSNull.null,@"valueRole":@"manual-template",@"writable":@(v.writable),@"canSlide":@(v.continuous),@"choices":choices,@"unitLabel":@(v.unitLabel.c_str()),@"step":@(v.step),@"displayScale":v.logarithmic?@"logarithmic":@"linear"}];}
          return @{@"parameters":parameters,@"buses":pluginBusDictionaries(plugin.buses()),@"latency":@(plugin.latency()),@"tail":@(plugin.tail()),@"bypass":@(node->plugin.bypass),@"audioLayout":@(pluginAudioLayoutSignature(plugin.buses()).c_str())};};
        bool commit=false,dry=false;NSString *gesture=nil;uint32_t gestureParameter=0;
        if([method isEqual:@"graph.plugin.get"]){allowed(@[@"graph",@"node"]);auto plugin=make(node->plugin);result=metadata(*plugin);}
        else if([method isEqual:@"graph.plugin.preset.save"]){
          allowed(@[@"graph",@"node",@"path",@"name",@"overwrite",@"dryRun"]);auto plugin=make(node->plugin);
          for(const auto &parameter:plugin->parameters()){const auto value=baseline(parameter);if(value!=parameter.value)require(parameter.writable&&plugin->parameter(parameter.id,value),"Plugin rejected the configured manual baseline");}
          const auto state=plugin->state();result=PluginPreset::write(string(p[@"path"],8192),descriptorDictionary(state.descriptor),[NSData dataWithBytes:state.state.data() length:state.state.size()],string(p[@"name"],200),p[@"overwrite"]?boolean(p[@"overwrite"]):false,p[@"dryRun"]?boolean(p[@"dryRun"]):false,@(state.audioLayout.c_str()));
        } else if([method isEqual:@"graph.plugin.preset.load"]){
          allowed(@[@"graph",@"node",@"path",@"expectedPresetRevision",@"dryRun"]);dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;
          auto preset=PluginPreset::read(string(p[@"path"],8192));if(![string(p[@"expectedPresetRevision"],80) isEqual:preset[@"presetRevision"]])throw Error(-32001,"Preset changed; inspect it again before loading");
          const auto &r=node->plugin;PluginDescriptor descriptor{r.type,r.subtype,r.manufacturer,r.name,r.format,r.path,r.classID,false};require(PluginPreset::matches(preset[@"plugin"],descriptorDictionary(descriptor)),"Preset belongs to a different plugin type");
          auto replacement=r;const auto savedLayout=string(preset[@"audioLayout"]?:@"",8192).UTF8String;require(replacement.audioLayout.empty()||!savedLayout[0]||replacement.audioLayout==savedLayout,"Preset audio layout differs from the connected plugin; review routing before loading");if(replacement.audioLayout.empty())replacement.audioLayout=savedLayout;NSData *state=preset[@"state"];const auto *bytes=static_cast<const std::byte *>(state.bytes);replacement.state.clear();if(state.length)replacement.state.assign(bytes,bytes+state.length);replacement.parameters.clear();
          auto plugin=make(replacement);captureSignalParameterBases(*definition,nodeID,plugin->parameters());const auto captured=plugin->state();replacement.state=captured.state;replacement.audioLayout=captured.audioLayout;node->plugin=std::move(replacement);commit=true;
          result=@{@"preset":PluginPreset::summary(preset),@"loaded":@(!dry),@"dryRun":@(dry)};
        } else if([method isEqual:@"graph.plugin.bypass"]){
          allowed(@[@"graph",@"node",@"bypass",@"dryRun"]);dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;
          node->plugin.bypass=boolean(p[@"bypass"]);result=@{@"bypass":@(node->plugin.bypass)};commit=true;
        } else if([method isEqual:@"graph.plugin.set"]){
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
          allowed(@[@"graph",@"node"]);auto plugin=make(node->plugin);for(const auto &parameter:plugin->parameters()){const auto value=baseline(parameter);if(value!=parameter.value)require(parameter.writable&&plugin->parameter(parameter.id,value),"Plugin rejected the configured manual baseline");}plugin->showEditor();_graphEditorPlugin=std::move(plugin);_graphEditorRecipe=node->plugin;_graphEditorGraph=graphID;_graphEditorNode=nodeID;_graphEditorDocument=_automationDocumentID;_graphEditorID=NSUUID.UUID.UUIDString;
          result=@{@"editor":_graphEditorID,@"message":@"Edit this library recipe, then Apply plugin settings. All uses update together during playback when ports and latency remain unchanged."};
        } else if([method isEqual:@"graph.plugin.editor.commit"]){
          allowed(@[@"graph",@"node",@"editor",@"dryRun"]);dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;
          require(_graphEditorPlugin&&[string(p[@"editor"],100) isEqual:_graphEditorID]&&[_graphEditorDocument isEqual:_automationDocumentID]&&_graphEditorGraph==graphID&&_graphEditorNode==nodeID,"Graph plugin editor has changed");
          require(node->plugin==_graphEditorRecipe,"Plugin recipe changed; reopen its interface before applying settings");
          captureSignalParameterBases(*definition,nodeID,_graphEditorPlugin->parameters());const auto captured=_graphEditorPlugin->state();node->plugin.state=captured.state;node->plugin.audioLayout=captured.audioLayout;node->plugin.parameters.clear();commit=true;result=metadata(*_graphEditorPlugin);
        } else throw Error(-32601,"Unknown graph plugin method");
        if(commit){try{next.validate(song);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}if(!dry&&next!=_document->native()){
          const bool controls=next.mixer==_document->native().mixer;
          auto plan=controls?_audio->prepareGraphControls(next):nullptr;
          auto routing=!plan?_audio->prepareMixerRouting(next):nullptr;
          if(!plan && !routing){if(_audio->active())throw Error(-32002,"This plugin recipe or port edit cannot yet be prepared during playback; stop playback and retry");_audio->stop();}
          const bool merge=gesture&&[gesture isEqual:_graphParameterGesture]&&[_automationDocumentID isEqual:_graphParameterGestureDocument]&&graphID==_graphParameterGestureGraph&&nodeID==_graphParameterGestureNode&&gestureParameter==_graphParameterGestureID;
          _document->annotate([&](NativeSong &n){n=next;},[&]{if(plan)require(_audio->publishGraphControls(std::move(plan)),"Graph parameter queue is busy; retry the edit");if(routing&&!_audio->publishMixerRouting(routing))throw Error(-32002,"Routing publication is busy; retry the edit");},merge?_graphParameterGestureSequence:0);
          _graphParameterGesture=gesture;_graphParameterGestureDocument=_automationDocumentID;_graphParameterGestureGraph=graphID;_graphParameterGestureNode=nodeID;_graphParameterGestureID=gestureParameter;_graphParameterGestureSequence=_document->historySequence();
        }if(!dry&&[method isEqual:@"graph.plugin.editor.commit"])_graphEditorRecipe=node->plugin;}
      }
