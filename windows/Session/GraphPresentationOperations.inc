    if(method=="graph.presentation.set") {
      keys(p,{"graph","presentation","positions","dryRun"});const bool root=!p.contains("graph")||p.at("graph").is_null();
      const auto graphID=root?0:identity(p.at("graph"));if(graphID){definition(next,p.at("graph"));affected=graphID;}
      presentationKeys(field(p,"presentation"));
      patchModel(next,[&](Json &metadata){if(root)metadata["signalGraph"]["presentation"]=field(p,"presentation");else encodedDefinition(metadata,graphID)["presentation"]=field(p,"presentation");});
      std::set<std::string> positioned;
      const auto positions=p.value("positions",Json::array());
      for(const auto &v:array(positions,8192)){
        keys(v,{"node","x","y"});const auto key=text(field(v,"node"),512);require(positioned.insert(key).second,"A presentation gesture cannot position a node twice");
        const auto x=number(field(v,"x"),0,100000),y=number(field(v,"y"),0,100000);
        if(graphID){auto &d=definition(next,p.at("graph"));const auto id=identity(field(v,"node"));auto n=std::find_if(d.nodes.begin(),d.nodes.end(),[&](const auto &n){return n.id==id;});if(n!=d.nodes.end()){n->x=x;n->y=y;}else{require(std::any_of(d.groups.begin(),d.groups.end(),[&](const auto &g){return g.id==id;}),"Presentation node no longer exists");moveSignalGroup(d,id,x,y);}}
        else {const auto group=std::find_if(next.signal.groups.begin(),next.signal.groups.end(),[&](const auto &g){return key==id(g.id);});if(group!=next.signal.groups.end())moveSongSignalGroup(next.signal,group->id,x,y);else next.signal.layout[key]={x,y};}
      }
    } else
