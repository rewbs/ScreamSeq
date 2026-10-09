void graphEditingOperations() {
  Fixture f;const auto graph=f.api.invoke("graph.create",Json::object()).at("graph");
  const auto source=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","lfo"}}).at("node");
  const auto plugin=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",{{"format","Built-in"},{"classID","resonance.gainer.v1"}}},{"insertEdge",0}}).at("node");
  auto before=f.doc->native();const auto revision=f.doc->revision;
  f.api.invoke("graph.source.mute",{{"graph",graph},{"node",source},{"muted",true},{"dryRun",true}});CHECK(f.doc->native()==before&&f.doc->revision==revision);
  f.api.invoke("graph.source.mute",{{"graph",graph},{"node",source},{"muted",true}});CHECK(f.doc->native().signal.library[0].nodes[2].muted);
  auto muted=f.doc->native();const auto mutedRevision=f.doc->revision;f.api.invoke("graph.source.mute",{{"graph",graph},{"node",source},{"muted",true}});CHECK(f.doc->revision==mutedRevision);
  rejected(f,"graph.source.mute",{{"graph",graph},{"node",plugin},{"muted",true}});
  f.doc->undo();CHECK(f.doc->native()==before);f.doc->redo();CHECK(f.doc->native()==muted);
  const auto target="n"+std::to_string(f.doc->native().tracks.at(0).id);f.api.invoke("graph.assign",{{"graph",graph},{"target",target}});before=f.doc->native();
  const auto independent=f.api.invoke("graph.makeIndependent",{{"graph",graph},{"target",target},{"scope","channel"}}).at("graph");CHECK(independent!=graph&&f.api.invoke("graph.get",Json::object()).at("assignments")[0].at("graph")==independent);
  f.doc->undo();before.nextID=f.doc->native().nextID;CHECK(f.doc->native()==before);f.doc->redo();
  const auto fragment=f.api.invoke("graph.selection.copy",{{"graph",graph},{"nodes",{source,plugin}}}).at("fragment");CHECK(fragment.at("audio").empty()&&fragment.at("nodes").size()==4);
  before=f.doc->native();f.api.invoke("graph.selection.paste",{{"graph",graph},{"fragment",fragment},{"x",300},{"y",200}});CHECK(f.doc->native().signal.library[0].nodes.size()==6);
  f.doc->undo();before.nextID=f.doc->native().nextID;CHECK(f.doc->native()==before);
  const auto cut=f.api.invoke("graph.selection.cut",{{"graph",graph},{"nodes",{source,plugin}}});CHECK(cut.at("fragment")==fragment&&f.doc->native().signal.library[0].nodes.size()==2);
  f.doc->undo();CHECK(f.doc->native()==before);
  const auto encoded=ScreamSeq::Project::encodeNativeMetadata(f.doc->native());CHECK(ScreamSeq::Project::decodeNativeMetadata(encoded)==f.doc->native());
  const auto duplicate=f.api.invoke("graph.selection.duplicate",{{"graph",graph},{"nodes",{source,plugin}}});CHECK(duplicate.at("wouldChange")==true&&f.doc->native().signal.library[0].nodes.size()==6);
}

void groupDryMapDetachment() {
  Fixture f;
  const auto graph=f.api.invoke("graph.create",Json::object()).at("graph");
  const Json plugin={{"format","Built-in"},{"classID","resonance.gainer.v1"}};
  const auto a=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",plugin}}).at("node");
  const auto b=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",plugin}}).at("node");
  auto d=definition(f);const auto input=d.at("nodes")[0].at("id"),output=d.at("nodes")[1].at("id");
  auto edge=[](const Json &source,const Json &target,unsigned out,unsigned in,double gain) {
    return Json{{"source",source},{"target",target},{"output",out},{"input",in},{"gain",gain}};
  };
  d["audio"]=Json::array({edge(input,a,0,0,.5),edge(input,b,1,0,.2),
    edge(a,b,0,0,.7),edge(a,output,0,0,.3),edge(b,output,0,1,.8)});
  f.api.invoke("graph.update",{{"definition",d}});
  const auto group=f.api.invoke("graph.group.create",{{"graph",graph},{"nodes",Json::array({a,b})}}).at("group");
  auto mapping=[&](const Json &node,unsigned port) {
    return Json{{"input",{{"source",input},{"target",node},{"output",port},{"input",0}}},
      {"output",{{"node",node},{"port",0}}}};
  };
  f.api.invoke("graph.group.bypass",{{"graph",graph},{"group",group},{"bypass",true},
    {"dryRoutes",Json::array({mapping(a,0),mapping(b,1)})}});
  const auto before=f.doc->native();const auto revision=f.doc->revision;const auto history=f.doc->historyBytes();
  const Json detach={{"graph",graph},{"nodes",Json::array({a,b})},{"heal",{{"incoming",0},{"outgoing",3}}}};
  auto preview=detach;preview["dryRun"]=true;
  CHECK(f.api.invoke("graph.nodes.detach",preview).at("wouldChange")==true);
  CHECK(f.doc->native()==before&&f.doc->revision==revision&&f.doc->historyBytes()==history);
  CHECK(f.api.invoke("graph.nodes.detach",detach).at("wouldChange")==true);
  const auto after=f.doc->native();const auto &old=before.signal.library[0];const auto &changed=after.signal.library[0];
  CHECK(changed.nodes==old.nodes&&changed.groups.size()==1&&changed.groups[0].bypass);
  CHECK(changed.groups[0].nodes==old.groups[0].nodes);
  CHECK(changed.groups[0].dryRoutes==std::vector<SignalGroupDryRoute>{old.groups[0].dryRoutes[1]});
  CHECK(changed.audio.size()==4&&changed.audio[0]==old.audio[1]&&changed.audio[1]==old.audio[2]&&changed.audio[2]==old.audio[4]);
  const auto &healed=changed.audio[3];
  CHECK(healed.source==old.audio[0].source&&healed.target==old.audio[3].target&&healed.output==0&&healed.input==0);
  CHECK(std::abs(healed.gain-.15)<1e-12);
  f.doc->undo();CHECK(f.doc->native()==before); // One Undo restores both cables and both explicit maps.
  f.doc->redo();CHECK(f.doc->native()==after);
  CHECK(ScreamSeq::Project::decodeNativeMetadata(ScreamSeq::Project::encodeNativeMetadata(after))==after);
}

void groupBypassOperations() {
  Fixture f;const auto graph=f.api.invoke("graph.create",Json::object()).at("graph");
  const auto node=f.api.invoke("graph.node.add",{{"graph",graph},{"kind","plugin"},{"plugin",{{"format","Built-in"},{"classID","resonance.gainer.v1"}}},{"insertEdge",0}}).at("node");
  const auto group=f.api.invoke("graph.group.create",{{"graph",graph},{"nodes",{node}}}).at("group");
  const auto boundary=f.api.invoke("graph.group.boundary",{{"graph",graph},{"group",group}});CHECK(boundary["needsMapping"]==false&&boundary["inputs"].size()==1&&boundary["outputs"].size()==1);
  auto before=f.doc->native();const auto revision=f.doc->revision;
  f.api.invoke("graph.group.bypass",{{"graph",graph},{"group",group},{"bypass",true},{"dryRun",true}});CHECK(f.doc->native()==before&&f.doc->revision==revision);
  f.api.invoke("graph.group.bypass",{{"graph",graph},{"group",group},{"bypass",true},{"dryRoutes",boundary["dryRoutes"]}});const auto bypassed=f.doc->native();CHECK(bypassed.signal.library[0].groups[0].bypass);
  const auto r=f.doc->revision;f.api.invoke("graph.group.bypass",{{"graph",graph},{"group",group},{"bypass",true}});CHECK(f.doc->revision==r);
  f.doc->undo();CHECK(f.doc->native()==before);f.doc->redo();CHECK(f.doc->native()==bypassed);
  CHECK(ScreamSeq::Project::decodeNativeMetadata(ScreamSeq::Project::encodeNativeMetadata(bypassed))==bypassed);
  auto invalid=boundary["dryRoutes"];invalid[0]["output"]["node"]="n99999";rejected(f,"graph.group.bypass",{{"graph",graph},{"group",group},{"bypass",true},{"dryRoutes",invalid}});
  f.api.invoke("graph.clone",{{"graph",graph}});const auto &copy=f.doc->native().signal.library.back();CHECK(copy.groups[0].bypass&&copy.groups[0].dryRoutes[0].output.node!=bypassed.signal.library[0].groups[0].dryRoutes[0].output.node);
  f.api.invoke("graph.group.export",{{"graph",graph},{"group",group}});CHECK(f.doc->native().signal.library.back().groups[0].bypass);
  const auto source=f.api.invoke("graph.song.source.add",{{"source",{{"kind","lfo"},{"x",25},{"y",35}}}}).at("node");
  const auto rootGroup=f.api.invoke("graph.song.group.create",{{"nodes",{"source:"+source.get<std::string>()}},{"name","Control"}}).at("group");
  CHECK(f.doc->native().signal.groups.back().nodes==std::vector<std::string>{"source:"+source.get<std::string>()});
  f.api.invoke("graph.song.group.update",{{"group",rootGroup},{"x",100},{"y",200}});CHECK((f.doc->native().signal.layout.at("source:"+source.get<std::string>())==std::array<double,2>{100,200}));
  auto grouped=f.doc->native();CHECK(ScreamSeq::Project::decodeNativeMetadata(ScreamSeq::Project::encodeNativeMetadata(grouped))==grouped);
  rejected(f,"graph.song.group.create",{{"nodes",{"source:n999999"}}});
  f.api.invoke("graph.song.source.remove",{{"nodes",{source}}});CHECK(f.doc->native().signal.groups.empty());
  f.doc->undo();CHECK(f.doc->native()==grouped);f.doc->redo();CHECK(f.doc->native().signal.groups.empty());
}
