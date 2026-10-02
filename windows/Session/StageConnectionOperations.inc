    if(method=="graph.audio.connection.set") {
      keys(p,{"source","target","output","input","gainDB","enabled","replace","dryRun"});ensureMixer();
      auto value=stageCable(p);std::optional<SignalStageConnection> old;
      if(p.contains("replace")){keys(p.at("replace"),{"source","target","output","input"});old=stageCable(p.at("replace"));}
      const auto &key=old?*old:value;
      const auto previous=std::find_if(graph.stageConnections.begin(),graph.stageConnections.end(),[&](const auto &r){return r.source==key.source&&r.target==key.target&&r.output==key.output&&r.input==key.input;});
      if(previous!=graph.stageConnections.end()){if(!p.contains("gainDB"))value.gainDB=previous->gainDB;if(!p.contains("enabled"))value.enabled=previous->enabled;}
      const auto rack=host_.rack?host_.rack():host_.cachedRack;
      std::map<std::string,std::vector<PluginAudioBus>> catalogues;
      for(const auto &r:rack)catalogues[r.id]=host_.audioBuses?host_.audioBuses(r.id):host_.cachedAudioBuses.contains(r.id)?host_.cachedAudioBuses.at(r.id):std::vector<PluginAudioBus>{};
      for(bool input:{false,true}) {
        const auto &e=input?value.target:value.source;
        if(e.stage){bus(id(e.stage));continue;}
        const auto found=std::find_if(rack.begin(),rack.end(),[&](const auto &r){return r.id==e.plugin;});
        require(found!=rack.end(),"Plugin endpoint no longer exists");
        const bool instrument=found->descriptor.value("isInstrument",false);
        const auto &ports=catalogues.at(e.plugin);const auto port=input?value.input:value.output;
        require(std::any_of(ports.begin(),ports.end(),[&](const auto &b){return b.input==input&&b.index==port&&b.supported;}),"Choose an available physical plugin audio port");
        if(!instrument)rootDetachedMixerPlugin(next.mixer,e.plugin,[&]{return allocate(next);});
      }
      setSignalStageConnection(graph,value,old?&*old:nullptr);
      // Compile the complete projected DAG on dry runs too. An absent physical
      // catalogue is unavailable, never an invented stereo plugin port.
      std::vector<MixerProcessorInfo> processors;std::vector<uint64_t> tracks;
      for(const auto &[index,track]:next.tracks)tracks.push_back(track.id);
      for(const auto &r:rack) {
        uint64_t inputs=0,outputs=0;uint32_t count=1;
        for(const auto &b:catalogues.at(r.id))if(b.supported){require(b.index<64,"Invalid host audio port index");if(b.input)inputs|=uint64_t(1)<<b.index;else{outputs|=uint64_t(1)<<b.index;count=std::max(count,b.index+1);}}
        const bool instrument=r.descriptor.value("isInstrument",false);processors.push_back({r.id,0,0,instrument,false,count,outputs,inputs,0,instrument});
      }
      for(const auto &b:next.mixer.buses) {
        const auto in=signalStagePorts(graph,b.id,true),out=signalStagePorts(graph,b.id,false);
        if(in.empty()&&out.empty()&&std::none_of(graph.assignments.begin(),graph.assignments.end(),[&](const auto &a){return a.target==b.id;})&&std::none_of(graph.commands.begin(),graph.commands.end(),[&](const auto &c){return c.target==b.id&&(c.kind==SignalCommandKind::Row||c.kind==SignalCommandKind::Start);}))continue;
        uint64_t inputs=0,outputs=1;uint32_t count=1;for(auto p:in)inputs|=uint64_t(1)<<p;for(auto p:out){outputs|=uint64_t(1)<<p;count=std::max(count,p+1);}
        processors.push_back({signalBusIdentity(b.id),0,0,false,false,count,outputs,inputs});
      }
      compileMixer(signalRoutingGraph(next.mixer,graph),tracks,processors,uint32_t(std::clamp(host_.sampleRate,8000.,384000.)));
    } else
