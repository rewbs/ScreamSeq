    } else if([method isEqual:@"graph.group.boundary"]) {
      allowed(@[@"graph",@"group"]);require(p[@"graph"]!=nil,"Specify a graph or null for a song group");const auto gid=decodeNativeID(p[@"group"]);
      NSMutableArray *inputs=[NSMutableArray array],*outputs=[NSMutableArray array];NSArray *routes=@[];bool needsMapping=false,bypassed=false;
      try {
        if(p[@"graph"]==NSNull.null){
          auto projection=_document->native();if(!projection.mixer.active())projection.ensureMixer();std::vector<std::string> rack;
          for(const auto &plugin:_plugins)if(!plugin.descriptor.instrument&&plugin.descriptor.type!=kAudioUnitType_MusicDevice)rack.push_back(plugin.instanceID);
          const auto boundary=signalSongGroupBoundary(projection.signal,projection.mixer,rack,gid);
          for(const auto &v:boundary.inputs)[inputs addObject:encodeSignalRouteIdentity(v)];for(const auto &v:boundary.outputs)[outputs addObject:encodeSignalRouteIdentity(v)];
          const auto &group=*std::find_if(projection.signal.groups.begin(),projection.signal.groups.end(),[&](const auto &g){return g.id==gid;});bypassed=group.bypass;
          try{routes=encodeSignalSongGroupDryRoutes(resolvedSongGroupDryRoutes(projection.signal,projection.mixer,rack,gid));}catch(const std::invalid_argument &){needsMapping=true;}
        }else{
          const auto id=decodeNativeID(p[@"graph"]);const auto &library=_document->native().signal.library;const auto d=std::find_if(library.begin(),library.end(),[&](const auto &v){return v.id==id;});require(d!=library.end(),"Subgraph no longer exists");
          const auto boundary=signalGroupBoundary(*d,gid);for(const auto &v:boundary.inputs)[inputs addObject:encodeSignalGroupInput(v)];for(const auto &v:boundary.outputs)[outputs addObject:@{@"node":nativeID(v.node),@"port":@(v.port)}];
          bypassed=std::find_if(d->groups.begin(),d->groups.end(),[&](const auto &g){return g.id==gid;})->bypass;
          try{routes=encodeSignalGroupDryRoutes(resolvedSignalGroupDryRoutes(*d,gid));}catch(const std::invalid_argument &){needsMapping=true;}
        }
      }catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      result=@{@"graph":p[@"graph"],@"group":p[@"group"],@"bypass":@(bypassed),@"inputs":inputs,@"outputs":outputs,@"dryRoutes":routes,@"needsMapping":@(needsMapping)};
