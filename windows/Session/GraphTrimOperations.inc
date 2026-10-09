    if(method=="graph.trim.get") {
      keys(p,{"graph","node"});auto copy=document_.native();const auto &raw=field(p,"graph");const auto graph=raw.is_null()?0:identity(raw);std::vector<AudioTrimOwnerInfo> rack;
      for(const auto &r:host_.rack?host_.rack():host_.cachedRack){
        AudioTrimOwnerInfo owner;owner.node="plugin:"+r.id;owner.instrument=r.descriptor.value("isInstrument",false);
        const auto buses=host_.audioBuses?host_.audioBuses(r.id):host_.cachedAudioBuses.contains(r.id)?host_.cachedAudioBuses.at(r.id):std::vector<PluginAudioBus>{};
        for(const auto &b:buses)if(b.supported)owner.ports.push_back({audioTrimPort(!b.input,b.index),b.name,!b.input});rack.push_back(std::move(owner));
      }
      auto access=graphTrimAccess(copy,graph,text(field(p,"node"),256),rack);Json ports=Json::array(),sources=Json::array();
      for(const auto &v:access.ports){
        std::string linked;for(const auto &[a,b]:access.trims->links){if(a==v.key)linked=b;if(b==v.key)linked=a;}
        Json modulation=Json::array();if(access.trims->modulation.contains(v.key))for(const auto &e:access.trims->modulation.at(v.key))modulation.push_back({{"source",id(e.source)},{"minimumDB",e.minimumDB},{"maximumDB",e.maximumDB}});
        ports.push_back({{"key",v.key},{"name",v.name},{"output",v.output},{"gainDB",access.trims->gain(v.key)},{"linkTo",linked},{"modulation",modulation}});
      }
      for(const auto &n:graphTrimSources(copy,graph))sources.push_back({{"id",id(n.id)},{"name",n.name}});
      return {{"graph",raw},{"node",p.at("node")},{"ports",ports},{"sources",sources},{"minimumDB",-48},{"maximumDB",48}};
    }
