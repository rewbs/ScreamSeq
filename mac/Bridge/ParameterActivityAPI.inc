    } else if ([method hasPrefix:@"parameter.activity."]) {
      auto *activity=_audio->parameterActivity();
      const auto processor=[&]() -> const ParameterProcessor & {
        require(activity!=nullptr,"No prepared processors; add a plugin or start playback");
        std::string key=string(p[@"target"],300).UTF8String;
        auto found=std::find_if(activity->processors.begin(),activity->processors.end(),[&](const auto &v){return std::string(_automationDocumentID.UTF8String)+"/"+v.key==key;});
        require(found!=activity->processors.end(),"Processor copy is unavailable; start playback and refresh the processor list");return *found;
      };
      auto processorObject=[&](const ParameterProcessor &v) -> NSDictionary * {
        return @{@"key":[NSString stringWithFormat:@"%@/%s",_automationDocumentID,v.key.c_str()],@"name":@(v.name.c_str()),@"plugin":@(v.plugin.c_str()),@"graph":nativeID(v.graph),@"node":nativeID(v.node),@"target":nativeID(v.target),@"instrument":nativeID(v.instrument),@"channel":@(v.channel),@"role":@(v.role),@"bypass":@(v.bypass),@"parameterCount":@(v.parameters.size())};
      };
      if([method isEqual:@"parameter.activity.targets"]) {
        allowed(@[]);NSMutableArray *targets=[NSMutableArray array];if(activity)for(const auto &v:activity->processors)[targets addObject:processorObject(v)];
        result=@{@"targets":targets,@"active":@(_audio->active()),@"engine":activity?@(activity->identity):@0};
      } else if([method isEqual:@"parameter.activity.parameters"]) {
        allowed(@[@"target"]);const auto &target=processor();NSMutableArray *parameters=[NSMutableArray array];
        for(const auto &v:target.parameters)[parameters addObject:@{@"id":@(v.id),@"name":@(v.name.c_str()),@"min":@(v.min),@"max":@(v.max),@"value":@(v.value),@"unitLabel":@(v.unitLabel.c_str())}];
        result=@{@"target":processorObject(target),@"parameters":parameters};
      } else if([method isEqual:@"parameter.activity.watch"]) {
        allowed(@[@"target",@"parameter",@"clear"]);const auto &target=processor();const auto id=uint32_t(integer(p[@"parameter"],0,UINT32_MAX));
        const auto found=std::find_if(target.parameters.begin(),target.parameters.end(),[&](const auto &v){return v.id==id;});require(found!=target.parameters.end(),"Parameter no longer exists");
        _audio->watchParameterActivity(target.key,id,found->value,p[@"clear"]?boolean(p[@"clear"]):false);
        result=@{@"token":[NSString stringWithFormat:@"%llu:%llu",activity->identity,activity->generation()],@"target":processorObject(target),@"parameter":@(id)};
      } else if([method isEqual:@"parameter.activity.get"]) {
        allowed(@[@"after",@"limit"]);const auto after=integer(p[@"after"]?:@0,0,9007199254740991ULL),limit=integer(p[@"limit"]?:@2048,1,8192);
        NSMutableArray *points=[NSMutableArray array];uint64_t cursor=after,oldest=0,latest=0;
        static NSArray *origins=@[@"baseline",@"manual",@"plugin-editor",@"recorded",@"envelope",@"pattern-set",@"pattern-slide",@"graph",@"reset",@"graph-source"];
        if(activity) {const auto &history=activity->history();if(!history.empty()){oldest=history.front().sequence;latest=history.back().sequence;}
          for(const auto &v:history)if(v.sequence>after){if(points.count>=limit)break;cursor=v.sequence;const auto &s=v.source;
            [points addObject:@{@"sequence":@(v.sequence),@"frame":@(v.frame),@"seconds":@(v.frame/_audio->sampleRate()),@"value":@(v.value),@"minimum":@(v.minimum),@"maximum":@(v.maximum),@"pattern":@(v.pattern),@"order":@(v.order),@"position":@(v.position),@"audible":@(v.audible),
              @"source":@{@"kind":origins[uint8_t(s.kind)],@"id":nativeID(s.id),@"pattern":@(s.pattern),@"position":@(s.position),@"channel":@(s.channel),@"column":@(s.column),@"binding":@(s.binding)}}];
          }
        }
        result=@{@"token":activity?[NSString stringWithFormat:@"%llu:%llu",activity->identity,activity->generation()]:@"none",@"target":activity&&activity->processor()?processorObject(*activity->processor()):(id)NSNull.null,@"parameter":activity?@(activity->parameter()):@0,
          @"points":points,@"cursor":@(cursor),@"oldest":@(oldest),@"latest":@(latest),@"dropped":activity?@(activity->dropped()):@0,@"active":@(_audio->active()),@"sampleRate":@(_audio->sampleRate()),@"resolutionSeconds":@.001};
      } else if([method isEqual:@"parameter.activity.sources"]) {
        allowed(@[@"target",@"parameter"]);const auto &target=processor();auto id=uint32_t(integer(p[@"parameter"],0,UINT32_MAX));
        require(std::any_of(target.parameters.begin(),target.parameters.end(),[&](const auto &v){return v.id==id;}),"Parameter no longer exists");
        const auto &native=_document->native();NSMutableArray *sources=[NSMutableArray array];size_t omitted=0;
        auto add=[&](NSDictionary *v){if(sources.count<512)[sources addObject:v];else ++omitted;};
        auto patternIndex=[&](uint64_t id){for(const auto &[index,p]:native.patterns)if(p.id==id)return uint32_t(index);return UINT32_MAX;};
        add(@{@"kind":@"baseline",@"title":@"Saved / manual parameter value",@"enabled":@YES,@"plugin":@(target.plugin.c_str()),@"graph":nativeID(target.graph),@"node":nativeID(target.node)});
        if(!target.plugin.empty()) {
          for(const auto &lane:native.automation)if(lane.plugin==target.plugin&&lane.parameter==id)
            add(@{@"kind":@"envelope",@"title":[NSString stringWithFormat:@"Pattern %u envelope · %lu points",patternIndex(lane.pattern),lane.points.size()],@"id":nativeID(lane.id),@"pattern":@(patternIndex(lane.pattern)),@"enabled":@(lane.enabled),@"plugin":@(target.plugin.c_str()),@"parameter":@(id)});
          for(const auto &c:native.performance.commands){auto b=native.performance.bindings.find(c.binding);if(b==native.performance.bindings.end()||b->second.plugin!=target.plugin||b->second.parameter!=id)continue;
            uint16_t channel=UINT16_MAX;for(const auto &[index,t]:native.tracks)if(t.id==c.track)channel=index;
            add(@{@"kind":c.kind==PatternCommandKind::ParameterSlide?@"pattern-slide":@"pattern-set",@"title":[NSString stringWithFormat:@"%@ · Pattern %u · row %.3f · channel %u · FX %u",c.kind==PatternCommandKind::ParameterSlide?@"PL":@"PS",patternIndex(c.pattern),double(c.position)/65536,channel+1,c.column+1],@"pattern":@(patternIndex(c.pattern)),@"position":@(c.position),@"channel":@(channel),@"column":@(c.column),@"binding":@(c.binding),@"enabled":@YES});
          }
          const auto slot=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &v){return v.instanceID==target.plugin;})-_plugins.begin();
          const auto count=std::count_if(_automation.begin(),_automation.end(),[&](const auto &v){return v.slot==slot&&v.id==id;});
          add(@{@"kind":@"recorded",@"title":[NSString stringWithFormat:@"Recorded automation · %lu timed points",size_t(count)],@"count":@(count),@"enabled":@(count>0),@"plugin":@(target.plugin.c_str()),@"parameter":@(id)});
          bool base=false;
          for(const auto &m:native.signal.songModulation)if(m.plugin==target.plugin&&m.parameter==id) {
            const auto source=std::find_if(native.signal.songSources.begin(),native.signal.songSources.end(),[&](const auto &v){return v.node.id==m.source;});
            if(source==native.signal.songSources.end())continue;
            if(!base&&m.enabled){
              add(@{@"kind":@"graph-source",@"scope":@"song",@"id":nativeID(0),@"title":@"Unmodulated base · manual or automated value",@"enabled":@YES,@"graph":NSNull.null,@"plugin":@(target.plugin.c_str()),@"parameter":@(id)});base=true;
            }
            add(@{@"kind":@"graph-source",@"scope":@"song",@"id":nativeID(m.source),@"title":[NSString stringWithFormat:@"%@ · contribution %.4f … %.4f%@",@(source->node.name.c_str()),m.minimum,m.maximum,m.quantized?@" · discrete values":@""],@"enabled":@(m.enabled),@"graph":NSNull.null,@"node":nativeID(m.source),@"plugin":@(m.plugin.c_str()),@"parameter":@(m.parameter),@"minimum":@(m.minimum),@"maximum":@(m.maximum),@"quantized":@(m.quantized),@"sourceKind":@(int(source->node.kind))});
          }
        } else for(const auto &g:native.signal.library)if(g.id==target.graph) {
          bool base=false;for(size_t index=0;index<g.modulation.size();++index){const auto &m=g.modulation[index];if(m.target!=target.node||m.parameter!=id)continue;
            auto n=std::find_if(g.nodes.begin(),g.nodes.end(),[&](const auto &n){return n.id==m.source;});
            if(!base&&m.enabled){add(@{@"kind":@"graph-source",@"id":nativeID(0),@"title":[NSString stringWithFormat:@"Modulation base · %.4f",m.base],@"enabled":@YES,@"graph":nativeID(g.id),@"node":nativeID(m.target),@"connection":@(index),@"value":@(m.base)});base=true;}
            add(@{@"kind":@"graph-source",@"id":nativeID(m.source),@"title":[NSString stringWithFormat:@"%@ · contribution %.4f … %.4f",@(n->name.c_str()),m.minimum,m.maximum],@"enabled":@(m.enabled),@"graph":nativeID(g.id),@"node":nativeID(m.source),@"connection":@(index),@"sourceKind":@(int(n->kind))});
          }
          for(const auto &c:native.signal.commands)if(c.target==target.target&&(c.graph==target.graph||c.kind==SignalCommandKind::Clear))
            add(@{@"kind":@"graph-command",@"title":[NSString stringWithFormat:@"Graph activity / amount / wet · Pattern %u · row %.3f",patternIndex(c.pattern),double(c.position)/65536],@"pattern":@(patternIndex(c.pattern)),@"position":@(c.position),@"target":nativeID(c.target),@"column":@(c.column),@"graph":nativeID(g.id),@"enabled":@YES});
        }
        result=@{@"sources":sources,@"omitted":@(omitted),@"rule":target.graph?@"Each graph copy has independent state. Modulation adds contributions to a base, then clamps to 0…1. Inactive copies still process silence; wet and activity affect audibility, not the parameter value.":@"Enabled envelopes, parameter pattern commands and recorded automation are mutually exclusive base controls. Song modulation adds its contributions to that base, then clamps to 0…1 and rounds discrete targets. Manual edits may be overwritten by base automation. Plugin-internal modulation is not exposed."};
      } else throw Error(-32601,"Unknown parameter activity method");
    } else if([method isEqual:@"automation.recorded.get"] || [method isEqual:@"automation.recorded.edit"]) {
      const auto slot=instanceSlot();auto id=uint32_t(integer(p[@"parameter"],0,UINT32_MAX));
      if([method isEqual:@"automation.recorded.get"]) {
        allowed(@[@"plugin",@"parameter",@"offset",@"limit"]);const auto offset=integer(p[@"offset"]?:@0,0,100000),limit=integer(p[@"limit"]?:@512,1,4096);
        size_t total=0;NSMutableArray *points=[NSMutableArray array];for(const auto &v:_automation)if(v.slot==slot&&v.id==id){if(total>=offset&&points.count<limit)[points addObject:@{@"frame":@(v.frame),@"value":@(v.value)}];++total;}
        result=@{@"points":points,@"total":@(total),@"offset":@(offset),@"sampleRate":@48000};
      } else {
        allowed(@[@"plugin",@"parameter",@"frame",@"newFrame",@"value",@"remove",@"dryRun"]);const bool dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;const auto frame=integer(p[@"frame"],0,uint64_t(48000)*604800),newFrame=integer(p[@"newFrame"]?:p[@"frame"],0,uint64_t(48000)*604800);const bool remove=p[@"remove"]?boolean(p[@"remove"]):false;
        require(remove?!p[@"value"]:p[@"value"]!=nil,"Supply value to add/update, or remove=true");require(!remove||!p[@"newFrame"],"Cannot move a removed point");
        auto parameters=_audio->pluginParameters(slot);auto param=std::find_if(parameters.begin(),parameters.end(),[&](const auto &v){return v.id==id;});require(param!=parameters.end()&&param->writable,"Parameter is unavailable or read-only");
        if(!remove){require(std::none_of(_document->native().automation.begin(),_document->native().automation.end(),[&](const auto &v){return v.enabled&&v.plugin==_plugins[slot].instanceID&&v.parameter==id;}),"Disable this parameter's envelopes before adding recorded automation");require(!_document->native().performance.controls(_plugins[slot].instanceID,id),"Remove this parameter's pattern commands before adding recorded automation");}
        const float value=remove?0:float(number(p[@"value"],param->min,param->max));auto next=_automation;
        if(frame!=newFrame)require(std::none_of(next.begin(),next.end(),[&](const auto &v){return v.slot==slot&&v.id==id&&v.frame==newFrame;}),"A recorded point already exists at the new time");
        std::erase_if(next,[&](const auto &v){return v.slot==slot&&v.id==id&&v.frame==frame;});if(!remove)next.push_back({uint32_t(slot),id,value,newFrame});
        require(next.size()<=100000,"Automation exceeds 100000 points");std::stable_sort(next.begin(),next.end(),[](const auto &a,const auto &b){return a.frame<b.frame;});
        const bool changed=next.size()!=_automation.size()||!std::equal(next.begin(),next.end(),_automation.begin(),[](const auto &a,const auto &b){return a.slot==b.slot&&a.id==b.id&&a.frame==b.frame&&a.value==b.value;});
        if(changed&&!dry){[self commitManualParameters];[self applyPluginGraph:_plugins automation:next];}result=@{@"points":@(next.size()),@"wouldChange":@(changed),@"dryRun":@(dry)};
      }
