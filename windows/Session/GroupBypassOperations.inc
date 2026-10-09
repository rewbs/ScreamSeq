    if(method=="graph.group.boundary") {
      keys(p,{"graph","group"});const auto gid=identity(field(p,"group"));const auto &rawGraph=field(p,"graph");
      Json inputs=Json::array(),outputs=Json::array(),routes=Json::array();bool needsMapping=false,bypassed=false;
      if(rawGraph.is_null()){
        auto projection=document_.native();if(!projection.mixer.active())projection.ensureMixer();std::vector<std::string> rack;
        for(const auto &r:host_.rack?host_.rack():host_.cachedRack)if(!r.descriptor.value("isInstrument",false))rack.push_back(r.id);
        const auto boundary=signalSongGroupBoundary(projection.signal,projection.mixer,rack,gid);
        auto encode=[](const SignalRouteIdentity &r)->Json{return {{"kind",r.kind},{"source",r.source},{"target",r.target},{"plugin",r.plugin},{"tap",r.tap},{"input",r.input},{"output",r.output}};};
        for(const auto &v:boundary.inputs)inputs.push_back(encode(v));for(const auto &v:boundary.outputs)outputs.push_back(encode(v));
        bypassed=std::find_if(projection.signal.groups.begin(),projection.signal.groups.end(),[&](const auto &g){return g.id==gid;})->bypass;
        try{for(const auto &r:resolvedSongGroupDryRoutes(projection.signal,projection.mixer,rack,gid))routes.push_back({{"input",r.input.kind.empty()?Json(""):encode(r.input)},{"output",encode(r.output)}});}catch(const std::invalid_argument &){needsMapping=true;}
      }else{
        const auto graphID=identity(rawGraph);const auto &library=document_.native().signal.library;const auto d=std::find_if(library.begin(),library.end(),[&](const auto &v){return v.id==graphID;});require(d!=library.end(),"Subgraph no longer exists");
        const auto boundary=signalGroupBoundary(*d,gid);auto encodeInput=[](const SignalGroupInput &v)->Json{return {{"source",id(v.source)},{"target",id(v.target)},{"output",v.output},{"input",v.input}};};auto encodeOutput=[](const SignalGroupOutput &v)->Json{return {{"node",id(v.node)},{"port",v.port}};};
        for(const auto &v:boundary.inputs)inputs.push_back(encodeInput(v));for(const auto &v:boundary.outputs)outputs.push_back(encodeOutput(v));
        bypassed=std::find_if(d->groups.begin(),d->groups.end(),[&](const auto &g){return g.id==gid;})->bypass;
        try{for(const auto &r:resolvedSignalGroupDryRoutes(*d,gid))routes.push_back({{"input",r.input.source?encodeInput(r.input):Json("")},{"output",encodeOutput(r.output)}});}catch(const std::invalid_argument &){needsMapping=true;}
      }
      return {{"graph",rawGraph},{"group",p.at("group")},{"bypass",bypassed},{"inputs",inputs},{"outputs",outputs},{"dryRoutes",routes},{"needsMapping",needsMapping}};
    }
