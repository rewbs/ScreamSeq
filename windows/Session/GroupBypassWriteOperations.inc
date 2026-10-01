    if(method=="graph.group.bypass") {
      keys(p,{"graph","group","bypass","dryRoutes","dryRun"});const auto &rawGraph=field(p,"graph");groupID=identity(field(p,"group"));const bool bypass=boolean(field(p,"bypass"));
      if(p.contains("dryRoutes"))for(const auto &r:array(p.at("dryRoutes"),256)){
        keys(r,{"input","output"});const auto &input=field(r,"input"),&output=field(r,"output");
        if(rawGraph.is_null()){if(!input.is_null()&&input!="")keys(input,{"kind","source","target","plugin","tap","input","output"});keys(output,{"kind","source","target","plugin","tap","input","output"});}
        else {if(!input.is_null()&&input!="")keys(input,{"source","target","output","input"});keys(output,{"node","port"});}
      }
      if(rawGraph.is_null()){
        require(std::any_of(graph.groups.begin(),graph.groups.end(),[&](const auto &g){return g.id==groupID;}),"Song processing group no longer exists");
        patchModel(next,[&](Json &m){for(auto &g:m["signalGraph"]["groups"])if(g["id"]==id(groupID)){g["bypass"]=bypass;if(p.contains("dryRoutes"))g["dryRoutes"]=p.at("dryRoutes");}});
        auto projection=next;if(!projection.mixer.active())projection.ensureMixer();std::vector<std::string> rack;for(const auto &r:host_.rack?host_.rack():host_.cachedRack)if(!r.descriptor.value("isInstrument",false))rack.push_back(r.id);
        resolvedSongGroupDryRoutes(next.signal,projection.mixer,rack,groupID,bypass);
      }else{
        affected=definition(next,rawGraph).id;require(std::any_of(definition(next,rawGraph).groups.begin(),definition(next,rawGraph).groups.end(),[&](const auto &g){return g.id==groupID;}),"Processing group no longer exists");
        patchModel(next,[&](Json &m){for(auto &g:encodedDefinition(m,affected)["groups"])if(g["id"]==id(groupID)){g["bypass"]=bypass;if(p.contains("dryRoutes"))g["dryRoutes"]=p.at("dryRoutes");}});
      }
    } else
