      if([method isEqual:@"graph.presentation.set"]) {
        allowed(@[@"graph",@"presentation",@"positions",@"dryRun"]);
        const bool root=!p[@"graph"]||p[@"graph"]==NSNull.null;
        auto visual=decodeSignalPresentation(p[@"presentation"],true);
        SignalDefinition *d=root?nullptr:&definition(p[@"graph"]);if(d)affected=d->id;
        std::set<std::string> positioned;
        for(id raw:array(p[@"positions"]?:@[],8192)) {
          auto v=object(raw);keys(v,@[@"node",@"x",@"y"]);const auto key=std::string(string(v[@"node"],512).UTF8String);
          require(positioned.insert(key).second,"A presentation gesture cannot position a node twice");
          const auto x=number(v[@"x"],0,100000),y=number(v[@"y"],0,100000);
          if(d){const auto id=decodeNativeID(v[@"node"]);auto node=std::find_if(d->nodes.begin(),d->nodes.end(),[&](const auto &n){return n.id==id;});if(node!=d->nodes.end()){node->x=x;node->y=y;}else {require(std::any_of(d->groups.begin(),d->groups.end(),[&](const auto &g){return g.id==id;}),"Presentation node no longer exists");moveSignalGroup(*d,id,x,y);}}
          else {const auto group=std::find_if(graph.groups.begin(),graph.groups.end(),[&](const auto &g){return key=="n"+std::to_string(g.id);});if(group!=graph.groups.end())moveSongSignalGroup(graph,group->id,x,y);else graph.layout[key]={x,y};}
        }
        if(d)d->presentation=std::move(visual);else graph.presentation=std::move(visual);
      } else
