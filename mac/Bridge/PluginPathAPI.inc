    } else if(pluginLocation) {
      try {
        const bool graph=[method hasPrefix:@"graph."],scan=[method hasSuffix:@".scan"];
        const bool reconnect=[method hasSuffix:@".set"];
        NSArray *targetKeys=graph?@[@"graph",@"node"]:@[@"plugin"];
        allowed(reconnect?[targetKeys arrayByAddingObjectsFromArray:@[@"path",@"expectedModuleSHA256",@"dryRun"]]:scan?[targetKeys arrayByAddingObject:@"path"]:targetKeys);
        PluginState state;size_t slot=0;NativeSong next=_document->native();
        EffectSnapshot::Location location;
        NSMutableDictionary *target=[NSMutableDictionary dictionary];
        if(graph) {
          location.graph=decodeNativeID(p[@"graph"]);location.node=decodeNativeID(p[@"node"]);
          const auto &recipe=locationRecipe(next,location.graph,location.node);
          state.descriptor={recipe.type,recipe.subtype,recipe.manufacturer,recipe.name,recipe.format,recipe.path,recipe.classID,false};
          state.state=recipe.state;state.audioLayout=recipe.audioLayout;state.auxiliaryInputs=recipe.inputs;state.auxiliaryOutputs=recipe.outputs;
          location.path=recipe.path;
          if(const auto hint=_pluginPathHints.graph.find({location.graph,location.node});hint!=_pluginPathHints.graph.end())location.hint=hint->second;
          target[@"graph"]=p[@"graph"];target[@"node"]=p[@"node"];
        } else {
          slot=instanceSlot();state=_plugins[slot];location.plugin=state.instanceID;
          if(const auto hint=_pluginPathHints.rack.find(location.plugin);hint!=_pluginPathHints.rack.end())location.hint=hint->second;
          target[@"plugin"]=p[@"plugin"];
        }
        require(state.descriptor.format=="VST3","Only VST3 bundle locations can be reconnected; Audio Units use their registered component identity");
        auto stored=state.descriptor;
        if(location.hint&&location.hint->applies(stored.format,stored.path,stored.classID))stored.path=location.hint->path;
        NSString *classID=pathString(stored.classID);
        if(scan) {
          _pluginLocations->scan(string(p[@"path"],8192),classID,state.descriptor.instrument,[&](NSString *path)->NSArray<NSDictionary *> *{
            NSData *bytes=runScanner(@[@"--list-vst3",path]);
            return [NSJSONSerialization JSONObjectWithData:bytes options:0 error:nil];
          });
        }
        if(!reconnect) {
          BOOL verified=NO;NSString *reason=@"";
          try{_pluginLocations->verify(pathString(stored.path),classID,state.descriptor.instrument,nil);verified=YES;}
          catch(const std::exception &e){reason=@(e.what());}
          target[@"descriptor"]=descriptorDictionary(stored);target[@"moduleVerified"]=@(verified);target[@"reason"]=reason;
          target[@"candidates"]=_pluginLocations->candidates(classID,state.descriptor.instrument);result=target;
        } else {
          const bool dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;
          NSString *hash=string(p[@"expectedModuleSHA256"],64);
          NSString *resolved=_pluginLocations->verify(string(p[@"path"],8192),classID,state.descriptor.instrument,hash);
          const std::string path=resolved.UTF8String;const bool changed=stored.path!=path;
          target[@"path"]=resolved;target[@"moduleSHA256"]=hash;target[@"wouldChange"]=@(changed);target[@"dryRun"]=@(dry);target[@"reconnected"]=@(changed&&!dry);result=target;
          if(changed&&!dry) {
            // The chosen, hash-verified scan permits this process to probe the
            // module. Project hints never grant this permission by themselves.
            require(PluginTrust::trust(resolved),"Chosen VST3 bundle is no longer available");
            auto states=_plugins;auto manual=_manualParameters;auto hints=_pluginPathHints;
            if(graph){locationRecipe(next,location.graph,location.node).path=path;hints.graph.erase({location.graph,location.node});}
            else {states[slot].descriptor.path=path;hints.rack.erase(location.plugin);}
            if(!graph)commitManualParameterState(states,manual,[&](const PluginState &candidate){return std::make_unique<NativePlugin>(candidate,_audio->sampleRate());});
            auto candidate=graph?state:states[slot];candidate.descriptor.path=path;
            {
              NativePlugin probe(candidate,_audio->sampleRate(),false);
              if(graph){require(!probe.isInstrument(),"Graph nodes require effect plugins");for(const auto &[id,value]:locationRecipe(next,location.graph,location.node).parameters)
                require(probe.parameter(id,float(value)),"Reconnected plugin rejected a saved graph parameter");}
            }
            // Recheck after the vendor probe, before any history or transport
            // change. The scan's descriptor and the selected bytes stay coupled.
            require([_pluginLocations->verify(resolved,classID,state.descriptor.instrument,hash) isEqual:resolved],"VST3 module changed during validation");
            next.validate(song);[self synchronizeHistory];
            EffectSnapshot beforeLocation{_plugins,_automation,_manualParameters};beforeLocation.location=location;
            _effectUndo.reserve(_effectUndo.size()+1);
            locationCommitStarted=true;_audio->stop();
            if(graph) {
              _document->restoreNative(std::move(next));
              beforeLocation.sequence=_document->externalHistoryEdit();_knownHistorySequence=beforeLocation.sequence;_parameterGestureSequence=0;
              _effectUndo.push_back(std::move(beforeLocation));_effectRedo.clear();trimEffectHistory(_effectUndo);++_pluginRevision;
              if(_graphEditorGraph==location.graph&&_graphEditorNode==location.node){_graphEditorPlugin.reset();_graphEditorID=nil;}
            } else {
              [self applyPluginGraph:states automation:_automation];
              _effectUndo.back().location=std::move(beforeLocation.location);_manualParameters.clear();
            }
            std::swap(_pluginPathHints,hints);PluginTrust::persist(resolved);
          }
        }
      } catch(const std::invalid_argument &e) {throw Error(-32602,e.what());}
