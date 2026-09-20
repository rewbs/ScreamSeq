    } else if ([method isEqual:@"pattern.performance.get"]) {
      allowed(@[@"pattern"]);
      const auto index = uint16_t(integer(p[@"pattern"],0,UINT16_MAX));
      require(song.Patterns.IsValidPat(index),"Pattern does not exist");
      const auto &native = _document->native();
      const auto pattern = native.patterns.at(index).id;
      const auto &performance = native.performance;
      NSMutableArray *columns = [NSMutableArray array], *bindings = [NSMutableArray array], *commands = [NSMutableArray array];
      for (const auto &[channel,track] : native.tracks) {
        const auto found = performance.columns.find(track.id);
        [columns addObject:@{@"channel":@(channel),@"track":nativeID(track.id),@"count":@(found==performance.columns.end()?0:found->second)}];
      }
      for (const auto &[id,binding] : performance.bindings) {
        NSMutableDictionary *item = [@{@"id":@(id),@"plugin":@(binding.plugin.c_str()),@"parameter":@(binding.parameter),
          @"name":@(binding.name.c_str()),@"resolved":@NO,@"canSlide":@NO} mutableCopy];
        for (size_t slot=0;slot<_plugins.size();++slot) if (_plugins[slot].instanceID==binding.plugin) {
          const auto parameters = _audio->pluginParameters(slot);
          const auto parameter = std::find_if(parameters.begin(),parameters.end(),[&](const auto &v){return v.id==binding.parameter;});
          item[@"slot"]=@(slot);
          if (parameter!=parameters.end()) {
            item[@"resolved"]=@(parameter->writable&&parameter->max>parameter->min);item[@"canSlide"]=@(parameter->continuous);
            item[@"parameterName"]=@(parameter->name.c_str());item[@"minimum"]=@(parameter->min);item[@"maximum"]=@(parameter->max);
          }
          break;
        }
        [bindings addObject:item];
      }
      for (const auto &command : performance.commands) if (command.pattern==pattern) {
        const auto track = std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &v){return v.second.id==command.track;});
        [commands addObject:@{@"channel":@(track->first),@"track":nativeID(command.track),@"position":@(command.position),
          @"duration":@(command.duration),@"column":@(command.column),@"kind":patternCommandName(command.kind),
          @"binding":@(command.binding),@"value":@(command.value),@"pitchRange":@(command.pitchRange)}];
      }
      result=@{@"pattern":@(index),@"patternID":nativeID(pattern),@"rows":@(song.Patterns[index].GetNumRows()),
        @"unitsPerRow":@(performanceUnitsPerRow),@"columns":columns,@"bindings":bindings,@"commands":commands};
    } else if ([method isEqual:@"pattern.performance.set"]) {
      allowed(@[@"pattern",@"columns",@"bindings",@"removeBindings",@"commands",@"dryRun"]);
      const auto index = uint16_t(integer(p[@"pattern"],0,UINT16_MAX));
      require(song.Patterns.IsValidPat(index),"Pattern does not exist");
      const bool dry = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      auto next = _document->native();
      auto &performance = next.performance;
      const auto pattern = next.patterns.at(index).id;
      const uint32_t end = uint32_t(song.Patterns[index].GetNumRows())*performanceUnitsPerRow;
      std::set<uint16_t> changedBindings;
      if (p[@"columns"]) for (id raw : array(p[@"columns"],127)) {
        const auto item=object(raw);keys(item,@[@"channel",@"count"]);
        const auto channel=uint16_t(integer(item[@"channel"],0,song.GetNumChannels()-1));
        require(changedBindings.insert(channel).second,"Duplicate effect-column configuration");
        const auto count=uint8_t(integer(item[@"count"],0,maximumEffectColumns));
        const auto track=next.tracks.at(channel).id;
        if(count)performance.columns[track]=count;else performance.columns.erase(track);
      }
      changedBindings.clear();
      if (p[@"removeBindings"]) for(id raw:array(p[@"removeBindings"],255)) {
        const auto id=uint16_t(integer(raw,1,255));
        require(changedBindings.insert(id).second,"Duplicate binding edit");
        require(performance.bindings.erase(id),"Parameter binding does not exist");
      }
      if (p[@"bindings"]) for(id raw:array(p[@"bindings"],255)) {
        const auto item=object(raw);keys(item,@[@"id",@"plugin",@"parameter",@"name"]);
        const auto id=uint16_t(integer(item[@"id"],1,255));
        require(changedBindings.insert(id).second,"Duplicate binding edit");
        ParameterBinding binding{string(item[@"plugin"],128).UTF8String,uint32_t(integer(item[@"parameter"],0,UINT32_MAX)),
          string(item[@"name"]?:@"",256).UTF8String};
        const auto found=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &v){return v.instanceID==binding.plugin;});
        require(found!=_plugins.end(),"Binding plugin instance does not exist");
        const auto parameters=_audio->pluginParameters(size_t(found-_plugins.begin()));
        require(std::any_of(parameters.begin(),parameters.end(),[&](const auto &v){return v.id==binding.parameter&&v.writable&&v.max>v.min;}),
          "Binding parameter does not exist or has no writable value range");
        performance.bindings[id]=std::move(binding);
      }
      if(p[@"commands"]) {
        std::erase_if(performance.commands,[&](const auto &command){return command.pattern==pattern;});
        for(id raw:array(p[@"commands"],maximumPatternCommands)) {
          const auto item=object(raw);keys(item,@[@"channel",@"position",@"duration",@"column",@"kind",@"binding",@"value",@"pitchRange"]);
          const auto channel=uint16_t(integer(item[@"channel"],0,song.GetNumChannels()-1));
          const auto kind=decodePatternCommandName(item[@"kind"]);
          const bool pitch=kind==PatternCommandKind::PitchSet||kind==PatternCommandKind::PitchSlide;
          const auto position=uint32_t(integer(item[@"position"],0,end-1));
          performance.commands.push_back({pattern,next.tracks.at(channel).id,position,uint32_t(integer(item[@"duration"]?:@0,0,end-position)),
            uint8_t(integer(item[@"column"],0,maximumEffectColumns-1)),kind,uint16_t(integer(item[@"binding"]?:@0,pitch?0:1,pitch?0:255)),number(item[@"value"],pitch?-96:0,pitch?96:1),
            uint8_t(integer(item[@"pitchRange"]?:@2,1,96))});
        }
      }
      try {next.validate(song);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      // A preview never instantiates a plugin or touches transport.
      std::map<std::string,std::set<uint32_t>> targets;
      std::map<size_t,std::vector<PluginParameter>> parameterCache;
      for(const auto &command:performance.commands) {
        if(command.kind==PatternCommandKind::PitchSet||command.kind==PatternCommandKind::PitchSlide)continue;
        const auto &binding=performance.bindings.at(command.binding);
        auto found=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &v){return v.instanceID==binding.plugin;});
        const bool edited=changedBindings.contains(command.binding)||std::find(_document->native().performance.commands.begin(),_document->native().performance.commands.end(),command)==_document->native().performance.commands.end();
        if(found==_plugins.end()){require(!edited,"Command binding is unresolved; repair the binding first");continue;}
        const auto slot=size_t(found-_plugins.begin());
        if(!parameterCache.contains(slot))parameterCache[slot]=_audio->pluginParameters(slot);
        const auto &parameters=parameterCache.at(slot);
        const auto parameter=std::find_if(parameters.begin(),parameters.end(),[&](const auto &v){return v.id==binding.parameter&&v.writable&&v.max>v.min;});
        if(parameter==parameters.end()){require(!edited,"Command parameter is unresolved; repair the binding first");continue;}
        require(command.kind!=PatternCommandKind::ParameterSlide||parameter->continuous,"This parameter is discrete and cannot slide");
        require(std::none_of(next.automation.begin(),next.automation.end(),[&](const auto &lane){return lane.enabled&&lane.plugin==binding.plugin&&lane.parameter==binding.parameter;}),
          "Disable the envelope for this parameter before using pattern commands");
        require(std::none_of(_automation.begin(),_automation.end(),[&](const auto &event){return event.slot==slot&&event.id==binding.parameter;}),
          "Remove absolute automation for this parameter before using pattern commands");
        targets[binding.plugin].insert(binding.parameter);
        require(targets[binding.plugin].size()<=64,"Use at most 64 pattern-command parameters per plugin");
      }
      const bool changed=next!=_document->native();
      if(changed&&!dry){_audio->stop();_document->annotate([&](NativeSong &native){native=next;});}
      result=@{@"dryRun":@(dry),@"wouldChange":@(changed),@"commands":@(performance.commands.size()),@"bindings":@(performance.bindings.size())};
