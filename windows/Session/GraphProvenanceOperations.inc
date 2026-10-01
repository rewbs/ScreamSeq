    if(method=="graph.provenance.get") {
      keys(p,{"plugin","parameter","pattern","offset","limit"});const auto plugin=text(field(p,"plugin"),256);const auto parameter=uint32_t(integer(field(p,"parameter"),0,UINT32_MAX));
      std::optional<uint16_t> pattern;if(p.contains("pattern"))pattern=uint16_t(integer(p.at("pattern"),0,UINT16_MAX));
      const auto offset=uint32_t(integer(p.value("offset",Json(0)),0,100000)),limit=uint32_t(integer(p.value("limit",Json(128)),1,256));
      const auto key=std::pair{plugin,parameter};const auto cached=host_.cachedRecordings.find(key);
      const auto recording=host_.recording?host_.recording(plugin,parameter):cached==host_.cachedRecordings.end()?ParameterProvenanceRecording{}:cached->second;
      const auto page=parameterProvenance(document_.native(),song,plugin,parameter,pattern,recording,offset,limit);
      Json sources=Json::array();
      for(const auto &s:page.sources) {
        const auto kind=s.kind==ParameterProvenanceKind::Envelope?"envelope":s.kind==ParameterProvenanceKind::PatternCommands?"pattern-commands":"recorded";
        Json v={{"key",s.key},{"kind",kind},{"plugin",s.plugin},{"parameter",s.parameter},{"enabled",s.enabled},{"count",s.count},{"position",s.position},{"endPosition",s.endPosition},{"omittedCommands",s.omittedCommands},{"omittedOrders",s.omittedOrders}};
        if(s.patternIndex!=UINT32_MAX){v["pattern"]=s.patternIndex;v["patternID"]=id(s.pattern);}
        if(s.lane)v["id"]=id(s.lane);
        if(s.track){v["track"]=id(s.track);v["channel"]=s.channel;v["column"]=s.column;v["binding"]=s.binding;}
        if(s.kind==ParameterProvenanceKind::Recorded){v["firstFrame"]=s.firstFrame;v["lastFrame"]=s.lastFrame;v["sampleRate"]=48000;}
        v["commands"]=Json::array();for(const auto &c:s.commands)v["commands"].push_back({{"kind",c.kind==PatternCommandKind::ParameterSlide?"pattern-slide":"pattern-set"},{"position",c.position},{"duration",c.duration},{"value",c.value}});
        v["orders"]=Json::array();for(const auto &[sequence,order]:s.orders)v["orders"].push_back({{"sequence",sequence},{"order",order}});sources.push_back(std::move(v));
      }
      return {{"sources",sources},{"total",page.total},{"offset",page.offset},{"unitsPerRow",65536},{"sampleRate",48000},{"rule","These are existing base-value controls, not additive modulation. Enabled envelopes, pattern parameter commands and recorded automation are mutually exclusive. Song modulation adds to the chosen baseline and clamps once."}};
    }
