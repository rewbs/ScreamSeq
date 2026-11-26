    } else if ([method isEqual:@"pattern.activity.get"]) {
      allowed(@[@"pattern",@"order",@"row",@"channel"]);
      std::vector<ActivityPlugin> plugins;
      for(const auto &p:_plugins){ActivityPlugin plugin{p.instanceID,p.descriptor.name,p.descriptor.instrument,p.bypass};for(const auto &a:pluginAssignments(p))plugin.assignments.push_back(uint16_t(a.instrument));plugins.push_back(std::move(plugin));}
      PatternActivityCatalog activity;
      try{activity=patternActivityCatalog(*_document,uint16_t(integer(p[@"pattern"],0,65535)),uint32_t(integer(p[@"order"],0,UINT32_MAX)),uint16_t(integer(p[@"row"],0,65535)),uint16_t(integer(p[@"channel"],0,126)),plugins);}
      catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      NSMutableArray *items=[NSMutableArray array];for(const auto &i:activity.items)[items addObject:@{@"category":@(i.category.c_str()),@"kind":@(i.kind.c_str()),@"label":@(i.label.c_str()),@"plugin":@(i.plugin.c_str()),@"envelope":@(i.envelope.c_str()),@"graph":i.graph?nativeID(i.graph):(id)NSNull.null,@"node":i.node?nativeID(i.node):(id)NSNull.null,@"slot":@(i.slot),@"pattern":@(i.pattern),@"row":@(i.row),@"channel":@(i.channel),@"column":@(i.column),@"parameter":@(i.parameter)}];
      result=@{@"reachable":@(activity.playback.reachable),@"complete":@(activity.playback.complete),@"limitation":@(activity.playback.limitation.c_str()),@"items":items,@"semantics":@"First visit from sequence start; samples observed throughout row at <=32-frame intervals, held plugin notes and routed effects, enabled pattern/instrument envelopes. Vendor-private tails and modulation are not measured."};
