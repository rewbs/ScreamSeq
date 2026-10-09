      if([method isEqual:@"graph.group.bypass"]) {
        allowed(@[@"graph",@"group",@"bypass",@"dryRoutes",@"dryRun"]);require(p[@"graph"]!=nil,"Specify a graph or null for a song group");groupID=decodeNativeID(p[@"group"]);const bool bypass=boolean(p[@"bypass"]);
        try {
          if(p[@"graph"]==NSNull.null){
            auto g=std::find_if(graph.groups.begin(),graph.groups.end(),[&](const auto &v){return v.id==groupID;});require(g!=graph.groups.end(),"Song processing group no longer exists");g->bypass=bypass;if(p[@"dryRoutes"])g->dryRoutes=decodeSignalSongGroupDryRoutes(p[@"dryRoutes"]);
            auto projection=next;if(!projection.mixer.active())projection.ensureMixer();std::vector<std::string> rack;for(const auto &plugin:_plugins)if(!plugin.descriptor.instrument&&plugin.descriptor.type!=kAudioUnitType_MusicDevice)rack.push_back(plugin.instanceID);
            resolvedSongGroupDryRoutes(graph,projection.mixer,rack,groupID,bypass);
          }else{
            auto &d=definition(p[@"graph"]);affected=d.id;auto g=std::find_if(d.groups.begin(),d.groups.end(),[&](const auto &v){return v.id==groupID;});require(g!=d.groups.end(),"Processing group no longer exists");g->bypass=bypass;if(p[@"dryRoutes"])g->dryRoutes=decodeSignalGroupDryRoutes(p[@"dryRoutes"]);compileSignal(d);
          }
        }catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      } else
