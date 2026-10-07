      // Song controls keep rack instance/parameter identities and add to the
      // existing automation baseline. They do not convert processors to recipes.
      if([method hasPrefix:@"graph.song.source."]||[method hasPrefix:@"graph.song.modulation."]) {
        auto source=[&](id raw)->SignalSongSource & {
          const auto id=decodeNativeID(raw);auto s=std::find_if(graph.songSources.begin(),graph.songSources.end(),[&](const auto &s){return s.node.id==id;});
          require(s!=graph.songSources.end(),"Song modulation source does not exist");return *s;
        };
        auto target=[&](id rawPlugin,id rawParameter,bool quantized) {
          const auto plugin=std::string(string(rawPlugin,256).UTF8String);const auto parameter=uint32_t(integer(rawParameter,0,UINT32_MAX));
          auto instance=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &v){return v.instanceID==plugin;});
          require(instance!=_plugins.end(),"Modulation target plugin does not exist");
          const auto parameters=_audio->pluginParameters(size_t(instance-_plugins.begin()));
          auto value=std::find_if(parameters.begin(),parameters.end(),[&](const auto &v){return v.id==parameter;});
          require(value!=parameters.end(),"Modulation target parameter is unavailable");
          require(value->writable,"Read-only parameter");
          require(value->continuous||quantized,"Stepped parameter requires explicit quantized mode");
          const double range=double(value->max)-value->min;
          require(std::isfinite(value->min)&&std::isfinite(value->max)&&std::isfinite(range)&&range>0,"Modulation target parameter has no usable value range");
          const double step=double(value->step)/range;
          require(!quantized||(std::isfinite(step)&&step>0&&step<=1),"Quantized modulation requires a parameter with discrete steps");
          return std::pair{plugin,parameter};
        };
        auto validateScope=[&](const SignalSongSource &s) {
          // Read the implicit routing projection without enabling an explicit
          // mixer merely to reference a channel or the reserved master ID.
          auto exists=[&](uint64_t id) {
            if(next.mixer.active())return std::any_of(next.mixer.buses.begin(),next.mixer.buses.end(),[&](const auto &b){return b.id==id;});
            return id==next.masterID||std::any_of(next.tracks.begin(),next.tracks.end(),[&](const auto &t){return t.second.id==id;});
          };
          require(!s.audioBus||exists(s.audioBus),"Follower input channel or bus does not exist");
          require(!s.audioStage||exists(s.audioStage),"Follower graph stage does not exist");
          require(!s.noteTarget||exists(s.noteTarget),"Note source channel or bus does not exist");
          if(!s.audioPlugin.empty())require(std::any_of(_plugins.begin(),_plugins.end(),[&](const auto &v){return v.instanceID==s.audioPlugin;}),"Follower input plugin does not exist");
        };
        if([method isEqual:@"graph.song.source.add"]) {
          allowed(@[@"source",@"connect",@"parent",@"dryRun"]);auto spec=[object(p[@"source"]) mutableCopy];
          require(!spec[@"id"],"Source identities are allocated by the document");nodeID=next.makeEntity().id;spec[@"id"]=nativeID(nodeID);
          auto s=decodeSongSource(spec);require(s.node.kind>=SignalNodeKind::LFO,"Choose a modulation source kind");validateScope(s);
          graph.songSources.push_back(std::move(s));
          if(p[@"parent"])assignSongSourceToGroup(next,nodeID,decodeNativeID(p[@"parent"]));
          if(p[@"connect"]) {auto c=object(p[@"connect"]);keys(c,@[@"plugin",@"parameter",@"quantized"]);const bool quantized=c[@"quantized"]?boolean(c[@"quantized"]):false;
            auto [plugin,parameter]=target(c[@"plugin"],c[@"parameter"],quantized);graph.songModulation.push_back({nodeID,std::move(plugin),parameter,0,0,true,quantized});}
        } else if([method isEqual:@"graph.song.source.update"]) {
          allowed(@[@"node",@"source",@"dryRun"]);auto &s=source(p[@"node"]);nodeID=s.node.id;auto patch=object(p[@"source"]);
          require(!patch[@"id"]&&!patch[@"kind"]&&!patch[@"envelopes"],"Source identity and kind cannot change; use graph.automation.set for pattern curves");
          auto spec=[encodeSongSource(s) mutableCopy];[spec addEntriesFromDictionary:patch];auto updated=decodeSongSource(spec);validateScope(updated);s=std::move(updated);
        } else if([method isEqual:@"graph.song.source.remove"]) {
          allowed(@[@"nodes",@"dryRun"]);std::set<uint64_t> removed;
          for(id raw:array(p[@"nodes"],64))require(removed.insert(source(raw).node.id).second,"Duplicate source identity");
          next.removeSongSources(std::vector<uint64_t>(removed.begin(),removed.end()));
        } else if([method isEqual:@"graph.song.modulation.set"]) {
          allowed(@[@"source",@"plugin",@"parameter",@"minimum",@"maximum",@"enabled",@"quantized",@"replace",@"dryRun"]);nodeID=source(p[@"source"]).node.id;
          const auto plugin=std::string(string(p[@"plugin"],256).UTF8String);const auto parameter=uint32_t(integer(p[@"parameter"],0,UINT32_MAX));
          auto previous=std::find_if(graph.songModulation.begin(),graph.songModulation.end(),[&](const auto &e){return e.source==nodeID&&e.plugin==plugin&&e.parameter==parameter;});
          if(p[@"replace"]) {auto c=object(p[@"replace"]);keys(c,@[@"source",@"plugin",@"parameter"]);
            const auto oldSource=decodeNativeID(c[@"source"]);const auto oldPlugin=std::string(string(c[@"plugin"],256).UTF8String);const auto oldParameter=uint32_t(integer(c[@"parameter"],0,UINT32_MAX));
            auto old=std::find_if(graph.songModulation.begin(),graph.songModulation.end(),[&](const auto &e){return e.source==oldSource&&e.plugin==oldPlugin&&e.parameter==oldParameter;});
            require(old!=graph.songModulation.end(),"Modulation connection no longer exists");require(previous==graph.songModulation.end()||previous==old,"Destination already has this modulation source");previous=old;}
          SignalSongModulation edge=previous==graph.songModulation.end()?SignalSongModulation{nodeID,plugin,parameter}:*previous;
          edge.source=nodeID;edge.plugin=plugin;edge.parameter=parameter;
          if(p[@"minimum"])edge.minimum=number(p[@"minimum"],-1,1);if(p[@"maximum"])edge.maximum=number(p[@"maximum"],-1,1);
          if(p[@"enabled"])edge.enabled=boolean(p[@"enabled"]);if(p[@"quantized"])edge.quantized=boolean(p[@"quantized"]);
          target(p[@"plugin"],p[@"parameter"],edge.quantized);
          if(previous==graph.songModulation.end())graph.songModulation.push_back(std::move(edge));else {
            retargetSignalCableGeometry(graph.presentation,{"source:n"+std::to_string(previous->source),"plugin:"+previous->plugin,0,previous->parameter,true},{"source:n"+std::to_string(edge.source),"plugin:"+edge.plugin,0,edge.parameter,true});
            *previous=std::move(edge);
          }
        } else if([method isEqual:@"graph.song.modulation.remove"]) {
          allowed(@[@"connections",@"dryRun"]);std::set<size_t> removed;
          for(id raw:array(p[@"connections"],256)) {auto c=object(raw);keys(c,@[@"source",@"plugin",@"parameter"]);const auto id=decodeNativeID(c[@"source"]);
            const auto plugin=std::string(string(c[@"plugin"],256).UTF8String);const auto parameter=uint32_t(integer(c[@"parameter"],0,UINT32_MAX));
            auto found=std::find_if(graph.songModulation.begin(),graph.songModulation.end(),[&](const auto &e){return e.source==id&&e.plugin==plugin&&e.parameter==parameter;});
            require(found!=graph.songModulation.end(),"Modulation connection no longer exists");require(removed.insert(size_t(found-graph.songModulation.begin())).second,"Duplicate modulation connection");}
          for(auto it=removed.rbegin();it!=removed.rend();++it){const auto &e=graph.songModulation[*it];std::erase_if(graph.presentation.cables,[&](const auto &c){return c.source=="source:n"+std::to_string(e.source)&&c.target=="plugin:"+e.plugin&&c.input==e.parameter&&c.modulation;});graph.songModulation.erase(graph.songModulation.begin()+*it);}
        } else throw Error(-32601,"Unknown song modulation method");
      } else
