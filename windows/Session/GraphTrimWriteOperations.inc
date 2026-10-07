    if(method=="graph.trim.set") {
      keys(p,{"graph","node","port","gainDB","linkTo","modulation","dryRun"});const auto &raw=field(p,"graph");require(p.contains("gainDB")||p.contains("linkTo")||p.contains("modulation"),"Supply a trim gain or link");affected=raw.is_null()?0:identity(raw);
      std::vector<AudioTrimOwnerInfo> rack;for(const auto &r:host_.rack?host_.rack():host_.cachedRack){AudioTrimOwnerInfo owner;owner.node="plugin:"+r.id;owner.instrument=r.descriptor.value("isInstrument",false);const auto buses=host_.audioBuses?host_.audioBuses(r.id):host_.cachedAudioBuses.contains(r.id)?host_.cachedAudioBuses.at(r.id):std::vector<PluginAudioBus>{};for(const auto &b:buses)if(b.supported)owner.ports.push_back({audioTrimPort(!b.input,b.index),b.name,!b.input});rack.push_back(std::move(owner));}
      auto access=graphTrimAccess(next,affected,text(field(p,"node"),256),rack);std::optional<double> gain;std::optional<std::string> link;
      if(p.contains("gainDB"))gain=number(p.at("gainDB"),-48,48);if(p.contains("linkTo"))link=p.at("linkTo").is_null()?std::string{}:text(p.at("linkTo"),2048);
      std::optional<std::vector<AudioTrimModulation>> modulation;if(p.contains("modulation"))modulation=decodeTrimEdges(p.at("modulation"));editGraphTrim(access,text(field(p,"port"),2048),gain,link,modulation);std::erase_if(graph.trims,[](const auto &v){return v.second.empty();});
      if(next.signal==document_.native().signal&&!document_.native().mixer.active())next.mixer=document_.native().mixer;
    } else
