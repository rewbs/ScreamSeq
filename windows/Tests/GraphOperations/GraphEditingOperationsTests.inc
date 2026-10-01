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
}
