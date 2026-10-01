    } else if([method isEqual:@"graph.provenance.get"]) {
      allowed(@[@"plugin",@"parameter",@"pattern",@"offset",@"limit"]);
      const auto plugin=std::string(string(p[@"plugin"],256).UTF8String);const auto parameter=uint32_t(integer(p[@"parameter"],0,UINT32_MAX));
      std::optional<uint16_t> pattern;if(p[@"pattern"])pattern=uint16_t(integer(p[@"pattern"],0,UINT16_MAX));
      const auto offset=uint32_t(integer(p[@"offset"]?:@0,0,100000)),limit=uint32_t(integer(p[@"limit"]?:@128,1,256));
      ParameterProvenanceRecording recording;
      const auto slot=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &v){return v.instanceID==plugin;})-_plugins.begin();
      if(slot<_plugins.size())for(const auto &v:_automation)if(v.slot==slot&&v.id==parameter){if(!recording.count)recording.firstFrame=v.frame;else recording.firstFrame=std::min(recording.firstFrame,v.frame);recording.lastFrame=std::max(recording.lastFrame,v.frame);++recording.count;}
      ParameterProvenancePage page;
      try{page=parameterProvenance(_document->native(),song,plugin,parameter,pattern,recording,offset,limit);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      NSMutableArray *sources=[NSMutableArray array];
      for(const auto &s:page.sources){
        NSString *kind=s.kind==ParameterProvenanceKind::Envelope?@"envelope":s.kind==ParameterProvenanceKind::PatternCommands?@"pattern-commands":@"recorded";
        NSMutableDictionary *v=[@{@"key":@(s.key.c_str()),@"kind":kind,@"plugin":@(s.plugin.c_str()),@"parameter":@(s.parameter),@"enabled":@(s.enabled),@"count":@(s.count),@"position":@(s.position),@"endPosition":@(s.endPosition),@"omittedCommands":@(s.omittedCommands),@"omittedOrders":@(s.omittedOrders)} mutableCopy];
        if(s.patternIndex!=UINT32_MAX){v[@"pattern"]=@(s.patternIndex);v[@"patternID"]=nativeID(s.pattern);}
        if(s.lane)v[@"id"]=nativeID(s.lane);
        if(s.track){v[@"track"]=nativeID(s.track);v[@"channel"]=@(s.channel);v[@"column"]=@(s.column);v[@"binding"]=@(s.binding);}
        if(s.kind==ParameterProvenanceKind::Recorded){v[@"firstFrame"]=@(s.firstFrame);v[@"lastFrame"]=@(s.lastFrame);v[@"sampleRate"]=@48000;}
        NSMutableArray *commands=[NSMutableArray array],*orders=[NSMutableArray array];
        for(const auto &c:s.commands)[commands addObject:@{@"kind":c.kind==PatternCommandKind::ParameterSlide?@"pattern-slide":@"pattern-set",@"position":@(c.position),@"duration":@(c.duration),@"value":@(c.value)}];
        for(const auto &[sequence,order]:s.orders)[orders addObject:@{@"sequence":@(sequence),@"order":@(order)}];v[@"commands"]=commands;v[@"orders"]=orders;[sources addObject:v];
      }
      result=@{@"sources":sources,@"total":@(page.total),@"offset":@(page.offset),@"unitsPerRow":@65536,@"sampleRate":@48000,@"rule":@"These are existing base-value controls, not additive modulation. Enabled envelopes, pattern parameter commands and recorded automation are mutually exclusive. Song modulation adds to the chosen baseline and clamps once."};
