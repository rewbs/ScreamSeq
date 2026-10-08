Json presentation(const SignalPresentation &p){
  Json regions=Json::array(),cables=Json::array();
  for(const auto &r:p.regions)regions.push_back({{"id",r.id},{"scope",r.scope},{"kind",r.comment?"comment":"frame"},{"title",r.title},{"text",r.text},{"x",r.x},{"y",r.y},{"width",r.width},{"height",r.height},{"color",r.color},{"collapsed",r.collapsed},{"nodes",r.nodes}});
  for(const auto &c:p.cables)cables.push_back({{"source",c.source},{"target",c.target},{"output",c.output},{"input",c.input},{"modulation",c.modulation},{"points",c.points}});
  return {{"regions",regions},{"cables",cables},{"collapsedNodes",p.collapsedNodes}};
}
SignalPresentation decodePresentation(const Json &p){
  // Project readers tolerate future dictionary keys, like every other native metadata section.
  auto keys=[](const Json &v,std::initializer_list<const char *>){need(v.is_object(),"Graph presentation needs an object");};
  keys(p,{"regions","cables","collapsedNodes"});SignalPresentation out;
  for(const auto &r:items(p,"regions",128,true)){keys(r,{"id","scope","kind","title","text","x","y","width","height","color","collapsed","nodes"});SignalVisualRegion v;v.id=text(field(r,"id"),128);v.scope=text(optional(r,"scope",""),512);auto kind=text(field(r,"kind"),16);need(kind=="frame"||kind=="comment","Unknown graph annotation kind");v.comment=kind=="comment";v.title=text(optional(r,"title",""),256);v.text=text(optional(r,"text",""),4096);v.x=number(field(r,"x"),-100000,100000);v.y=number(field(r,"y"),-100000,100000);v.width=number(optional(r,"width",320),120,100000);v.height=number(optional(r,"height",180),60,100000);v.color=uint32_t(integer(optional(r,"color",0x658b82),0,0xffffff));v.collapsed=boolean(optional(r,"collapsed",false));for(const auto &key:items(r,"nodes",8192,true))v.nodes.push_back(text(key,512));out.regions.push_back(std::move(v));}
  for(const auto &c:items(p,"cables",2048,true)){keys(c,{"source","target","output","input","modulation","points"});SignalCableGeometry v;v.source=text(field(c,"source"),512);v.target=text(field(c,"target"),512);v.output=uint32_t(integer(optional(c,"output",0),0,63));v.input=uint32_t(integer(optional(c,"input",0),0,UINT32_MAX));v.modulation=boolean(optional(c,"modulation",false));for(const auto &p:items(c,"points",64)){need(p.is_array()&&p.size()==2,"A reroute point needs x and y");v.points.push_back({number(p[0],-100000,100000),number(p[1],-100000,100000)});}out.cables.push_back(std::move(v));}
  for(const auto &key:items(p,"collapsedNodes",8192,true))out.collapsedNodes.push_back(text(key,512));
  out.validate();return out;
}
