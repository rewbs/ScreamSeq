    if(method.starts_with("graph.song.source.")||method.starts_with("graph.song.modulation.")) {
      const auto rack=host_.rack?host_.rack():host_.cachedRack;
      auto pluginExists=[&](const std::string &wanted) {return std::any_of(rack.begin(),rack.end(),[&](const auto &r){return r.id==wanted;});};
      auto target=[&](const Json &rawPlugin,const Json &rawParameter,bool quantized) {
        const auto plugin=text(rawPlugin,256);const auto parameter=uint32_t(integer(rawParameter,0,UINT32_MAX));
        require(pluginExists(plugin),"Modulation target plugin does not exist");
        const auto cached=host_.cachedParameters.find(plugin);
        const auto parameters=host_.parameters?host_.parameters(plugin):cached==host_.cachedParameters.end()?std::vector<PluginParameter>{}:cached->second;
        const auto value=std::find_if(parameters.begin(),parameters.end(),[&](const auto &p){return p.id==parameter;});
        require(value!=parameters.end(),"Modulation target parameter is unavailable");
        require(value->writable,"Read-only parameter");
        require(value->continuous||quantized,"Stepped parameter requires explicit quantized mode");
        const double range=double(value->max)-value->min;
        require(std::isfinite(value->min)&&std::isfinite(value->max)&&std::isfinite(range)&&range>0,"Modulation target parameter has no usable value range");
        const double step=double(value->step)/range;
        require(!quantized||(std::isfinite(step)&&step>0&&step<=1),"Quantized modulation requires a parameter with discrete steps");
        return std::pair{plugin,parameter};
      };
      // Source scope is a read of the implicit routing projection, not a mixer edit.
      auto validateScope=[&](const Json &spec) {
        for(auto key:{"audioBus","audioStage","noteTarget"})if(spec.contains(key)&&text(spec.at(key),32)!="") {
          const auto wanted=identity(spec.at(key));
          const bool exists=next.mixer.active() ? std::any_of(next.mixer.buses.begin(),next.mixer.buses.end(),[&](const auto &b){return b.id==wanted;}) :
            wanted==next.masterID||std::any_of(next.tracks.begin(),next.tracks.end(),[&](const auto &t){return t.second.id==wanted;});
          require(exists,"Modulation source channel or bus does not exist");
        }
        if(spec.contains("audioPlugin")) {const auto plugin=text(spec.at("audioPlugin"),256);require(plugin.empty()||pluginExists(plugin),"Follower input plugin does not exist");}
      };
      if(method=="graph.song.source.add") {
        keys(p,{"source","connect","parent","dryRun"});auto spec=field(p,"source");strictSongSource(spec);
        require(!spec.contains("id"),"Source identities are allocated by the document");nodeID=allocate(next);spec["id"]=id(nodeID);
        validateScope(spec);patchModel(next,[&](Json &m){m["signalGraph"]["songSources"].push_back(spec);});
        require(songSource(next,id(nodeID)).node.kind>=SignalNodeKind::LFO,"Choose a modulation source kind");
        if(p.contains("connect")) {const auto &c=p.at("connect");keys(c,{"plugin","parameter","quantized"});const bool quantized=c.contains("quantized")?boolean(c.at("quantized")):false;
          auto [plugin,parameter]=target(field(c,"plugin"),field(c,"parameter"),quantized);graph.songModulation.push_back({nodeID,std::move(plugin),parameter,0,0,true,quantized});}
        if(p.contains("parent"))assignSongSourceToGroup(next,nodeID,identity(p.at("parent")));
      } else if(method=="graph.song.source.update") {
        keys(p,{"node","source","dryRun"});nodeID=songSource(next,field(p,"node")).node.id;auto patch=field(p,"source");strictSongSource(patch);
        require(!patch.contains("id")&&!patch.contains("kind")&&!patch.contains("envelopes"),"Source identity and kind cannot change; use graph.automation.set for pattern curves");
        auto metadata=Project::encodeNativeMetadata(next);auto spec=encodedNode(metadata,0,nodeID);spec.update(patch);validateScope(spec);
        patchModel(next,[&](Json &m){encodedNode(m,0,nodeID)=spec;});
      } else if(method=="graph.song.source.remove") {
        keys(p,{"nodes","dryRun"});std::set<uint64_t> removed;
        for(const auto &raw:array(field(p,"nodes"),64))require(removed.insert(songSource(next,raw).node.id).second,"Duplicate source identity");
        next.removeSongSources(std::vector<uint64_t>(removed.begin(),removed.end()));
      } else if(method=="graph.song.modulation.set") {
        keys(p,{"source","plugin","parameter","minimum","maximum","enabled","quantized","replace","dryRun"});nodeID=songSource(next,field(p,"source")).node.id;
        const auto plugin=text(field(p,"plugin"),256);const auto parameter=uint32_t(integer(field(p,"parameter"),0,UINT32_MAX));
        auto previous=std::find_if(graph.songModulation.begin(),graph.songModulation.end(),[&](const auto &e){return e.source==nodeID&&e.plugin==plugin&&e.parameter==parameter;});
        if(p.contains("replace")) {
          const auto &r=p.at("replace");keys(r,{"source","plugin","parameter"});const auto oldSource=identity(field(r,"source"));const auto oldPlugin=text(field(r,"plugin"),256);const auto oldParameter=uint32_t(integer(field(r,"parameter"),0,UINT32_MAX));
          const auto old=std::find_if(graph.songModulation.begin(),graph.songModulation.end(),[&](const auto &e){return e.source==oldSource&&e.plugin==oldPlugin&&e.parameter==oldParameter;});
          require(old!=graph.songModulation.end(),"Modulation connection no longer exists");
          require(previous==graph.songModulation.end()||previous==old,"A modulation connection already exists at this destination");previous=old;
        }
        auto edge=previous==graph.songModulation.end()?SignalSongModulation{nodeID,plugin,parameter}:*previous;
        edge.source=nodeID;edge.plugin=plugin;edge.parameter=parameter;
        if(p.contains("minimum"))edge.minimum=number(p.at("minimum"),-1,1);if(p.contains("maximum"))edge.maximum=number(p.at("maximum"),-1,1);
        if(p.contains("enabled"))edge.enabled=boolean(p.at("enabled"));if(p.contains("quantized"))edge.quantized=boolean(p.at("quantized"));
        target(p.at("plugin"),p.at("parameter"),edge.quantized);
        if(previous==graph.songModulation.end())graph.songModulation.push_back(std::move(edge));else {
            retargetSignalCableGeometry(graph.presentation,{"source:n"+std::to_string(previous->source),"plugin:"+previous->plugin,0,previous->parameter,true},{"source:n"+std::to_string(edge.source),"plugin:"+edge.plugin,0,edge.parameter,true});
            *previous=std::move(edge);
          }
      } else if(method=="graph.song.modulation.remove") {
        keys(p,{"connections","dryRun"});std::set<size_t> removed;
        for(const auto &c:array(field(p,"connections"),256)) {
          keys(c,{"source","plugin","parameter"});const auto source=identity(field(c,"source"));const auto plugin=text(field(c,"plugin"),256);const auto parameter=uint32_t(integer(field(c,"parameter"),0,UINT32_MAX));
          const auto found=std::find_if(graph.songModulation.begin(),graph.songModulation.end(),[&](const auto &e){return e.source==source&&e.plugin==plugin&&e.parameter==parameter;});
          require(found!=graph.songModulation.end(),"Modulation connection no longer exists");require(removed.insert(size_t(found-graph.songModulation.begin())).second,"Duplicate modulation connection");
        }
        for(auto i=removed.rbegin();i!=removed.rend();++i){const auto &e=graph.songModulation[*i];std::erase_if(graph.presentation.cables,[&](const auto &c){return c.source=="source:n"+std::to_string(e.source)&&c.target=="plugin:"+e.plugin&&c.input==e.parameter&&c.modulation;});graph.songModulation.erase(graph.songModulation.begin()+*i);}
      }
    } else
