#pragma once
#include "GraphCanvas.hpp"
#include <unordered_map>
namespace ScreamSeq {
// A retained projection of the shared graph/mixer schema. Geometry is shared by
// painting and hit tests; no project access or processor work occurs in either.
struct SongRoutingCanvas {
  using Json=Api::Json;using Point=GraphCanvas::Point;
  struct Node {std::string id,bus,plugin,graph,instrument,kind;unsigned instrumentIndex=0;std::wstring name,detail;float x{},y{};WorkspaceRect rect;};
  struct Edge {std::string source,target;std::wstring label;Json action;unsigned output{},input{};bool enabled=true;std::array<Point,33> points;WorkspaceRect bounds;};
  WorkspaceRect viewport;float zoom=1,panX=16,panY=16;
  std::vector<Node> nodes;std::vector<Edge> edges;std::unordered_map<std::string,size_t> index;
  Point screen(float x,float y)const{return {viewport.x+panX+x*zoom,viewport.y+panY+y*zoom};}
  Point world(float x,float y)const{return {(x-viewport.x-panX)/zoom,(y-viewport.y-panY)/zoom};}
  Node *find(const std::string &id){auto i=index.find(id);return i==index.end()?nullptr:&nodes[i->second];}
  const Node *find(const std::string &id)const{auto i=index.find(id);return i==index.end()?nullptr:&nodes[i->second];}
  void geometry(){
    for(auto &n:nodes){auto p=screen(n.x,n.y);n.rect={p.x,p.y,184*zoom,68*zoom};}
    for(auto &e:edges){const auto a=find(e.source),b=find(e.target);if(!a||!b)continue;const auto &r=a->rect,&s=b->rect;e.points=GraphCanvas::curve({r.x+r.w,r.y+r.h*.5f},{s.x,s.y+s.h*.5f});float l=FLT_MAX,t=FLT_MAX,rr=-FLT_MAX,bb=-FLT_MAX;for(auto p:e.points){l=std::min(l,p.x);t=std::min(t,p.y);rr=std::max(rr,p.x);bb=std::max(bb,p.y);}e.bounds={l-7,t-7,rr-l+14,bb-t+14};}
  }
  void build(const Json &data,const std::string &filter,const std::string &selected){
    nodes.clear();edges.clear();index.clear();const auto &mixer=data.at("mixer"),&buses=mixer.at("buses"),&plugins=data.at("plugins");
    std::unordered_map<std::string,const Json*> plugin,definition;std::set<std::string> assigned,visible;
    for(const auto &p:plugins)plugin[p.at("id")]=&p;for(const auto &d:data.at("library"))definition[d.at("id")]=&d;
    for(const auto &b:buses){visible.insert(b.at("id"));for(const auto &p:b.at("inserts"))assigned.insert(p);}
    const auto inserts=[&](const Json &b){auto list=b.at("inserts");if(b.at("kind")=="master")for(const auto &p:plugins)if(!p.value("isInstrument",false)&&!assigned.contains(p.at("id")))list.push_back(p.at("id"));return list;};
    if(!filter.empty()){
      visible={filter};bool changed=true;while(changed){const auto count=visible.size();
        for(const auto &b:buses)if(visible.contains(b.at("id"))){if(b.at("output")!="")visible.insert(b.at("output"));for(const auto &s:b.at("sends"))visible.insert(s.at("target"));}
        for(const auto &r:data.at("inputs"))if(visible.contains(r.at("target")))visible.insert(r.at("source"));
        for(const auto &r:data.at("outputs"))if(visible.contains(r.at("source")))visible.insert(r.at("target"));
        for(const auto &b:buses)if(visible.contains(b.at("id"))){const auto list=inserts(b);for(const auto &s:mixer.at("sidechains"))if(std::find(list.begin(),list.end(),s.at("plugin"))!=list.end())visible.insert(s.at("source"));}
        for(const auto &b:buses)if(visible.contains(b.at("id"))&&b.at("kind")!="track"&&(b.at("kind")!="master"||b.at("id")==filter)){
          for(const auto &source:buses){if(source.at("output")==b.at("id"))visible.insert(source.at("id"));for(const auto &s:source.at("sends"))if(s.at("target")==b.at("id"))visible.insert(source.at("id"));}
          for(const auto &r:data.at("outputs"))if(r.at("target")==b.at("id"))visible.insert(r.at("source"));
          for(const auto &r:mixer.at("instruments"))if(r.at("target")==b.at("id"))for(const auto &source:buses){const auto list=inserts(source);if(std::find(list.begin(),list.end(),r.at("plugin"))!=list.end())visible.insert(source.at("id"));}
        }changed=count!=visible.size();
      }
    }
    std::map<std::string,Point> saved;for(const auto &p:data.at("layout"))saved[p.at("node")]={p.at("x").get<float>(),p.at("y").get<float>()};
    auto add=[&](std::string id,std::string name,std::string detail,float x,float y,std::string kind,std::string bus={},std::string p={},std::string graph={},std::string instrument={},unsigned instrumentIndex=0){
      if(index.contains(id))return;Node n;n.id=id;n.bus=bus;n.plugin=p;n.graph=graph;n.instrument=instrument;n.instrumentIndex=instrumentIndex;n.kind=kind;n.name=GraphCanvas::wide(name);n.detail=GraphCanvas::wide(detail);n.x=x;n.y=y;if(auto pos=saved.find(id);pos!=saved.end()){n.x=pos->second.x;n.y=pos->second.y;}index[id]=nodes.size();nodes.push_back(std::move(n));};
    auto edge=[&](const std::string &a,const std::string &b,std::string label,Json action=Json::object(),unsigned out=0,unsigned in=0,bool enabled=true){if(find(a)&&find(b))edges.push_back({a,b,GraphCanvas::wide(label),std::move(action),out,in,enabled});};
    auto graphName=[&](const std::string &g){const auto d=definition.find(g);return d==definition.end()?std::string("Unavailable subgraph"):std::to_string(d->second->at("number").get<unsigned>())+" · "+d->second->at("name").get<std::string>();};
    std::unordered_map<std::string,std::string> last,graphStage;float y=28;
    for(const auto &b:buses){const auto id=b.at("id").get<std::string>();if(!visible.contains(id))continue;const auto kind=b.at("kind").get<std::string>();
      add(id,b.at("name"),kind=="master"?"Master output":kind+((b.at("output")=="")?" · disconnected":" input"),28,y,kind,id);std::string previous=id;float x=264;
      auto addGraph=[&](const std::string &g,const std::string &role){const auto key="graph:"+id+":"+role+":"+g;add(key,graphName(g),role+" · independent copy",x,y,"graph",id,{},g);edge(previous,key,role=="Ordinary"?"":"When active");previous=key;graphStage[id]=key;x+=236;};
      for(const auto &role:{std::string("row"),std::string("start")}){std::set<std::string> seen;for(const auto &c:data.at("commands"))if(c.at("target")==id&&c.at("kind")==role&&seen.insert(c.at("graph")).second)addGraph(c.at("graph"),role=="row"?"Row":"Persistent");}
      for(const auto &a:data.at("assignments"))if(a.at("target")==id)addGraph(a.at("graph"),"Ordinary");
      for(const auto &p:inserts(b)){const auto pid=p.get<std::string>(),key="plugin:"+pid;const auto found=plugin.find(pid);const auto name=found==plugin.end()?"Unavailable effect":found->second->at("name").get<std::string>();
        const auto detail=found==plugin.end()?"Retained insert":found->second->value("bypass",false)?"Bypassed":assigned.contains(pid)?"Effect insert":"Master insert · default";
        add(key,name,detail,x,y,"plugin",id,pid);edge(previous,key,"");previous=key;x+=236;}
      last[id]=previous;y+=118;
    }
    for(const auto &b:buses){const auto id=b.at("id").get<std::string>();if(!last.contains(id))continue;edge(last.at(id),b.at("output"),"Output",{{"kind","output"},{"source",id}});
      for(size_t i=0;i<b.at("sends").size();++i){const auto &s=b.at("sends")[i];edge(last.at(id),s.at("target"),std::string(s.value("preFader",false)?"Pre send ":"Send ")+std::to_string(s.at("gainDB").get<double>())+" dB",{{"kind","send"},{"source",id},{"index",i}},0,0,s.value("enabled",true));}}
    std::string master;for(const auto &b:buses)if(b.at("kind")=="master")master=b.at("id");
    for(const auto &p:plugins)if(p.value("isInstrument",false)){const auto id=p.at("id").get<std::string>(),key="plugin:"+id;Json routes=Json::array();for(const auto &r:mixer.at("instruments"))if(r.at("plugin")==id)routes.push_back(r);
      if(std::none_of(routes.begin(),routes.end(),[](const auto &r){return r.at("output")==0;}))routes.push_back({{"target",master},{"output",0}});
      if(!filter.empty()&&std::none_of(routes.begin(),routes.end(),[&](const auto &r){return visible.contains(r.at("target"));}))continue;
      std::string detail="Instrument";for(const auto &i:p.at("instruments"))detail+=" · I"+std::to_string(i.get<unsigned>());if(p.value("bypass",false))detail+=" · bypassed";
      add(key,p.at("name"),detail,28,y,"instrument",{},id);y+=118;
      for(const auto &r:routes)edge(key,r.at("target"),"Out "+std::to_string(r.at("output").get<unsigned>()),{{"kind","plugin-output"},{"plugin",id},{"output",r.at("output")}},r.at("output"));
    }
    for(size_t i=0;i<data.at("inputs").size();++i){const auto &r=data.at("inputs")[i];const auto from=last.find(r.at("source"));if(from!=last.end())edge(from->second,r.at("target"),"Graph in "+std::to_string(r.at("input").get<unsigned>()),{{"kind","graph-input"},{"index",i}},0,r.at("input"));}
    for(size_t i=0;i<data.at("outputs").size();++i){const auto &r=data.at("outputs")[i];const auto source=r.at("source").get<std::string>();edge(graphStage.contains(source)?graphStage.at(source):source,r.at("target"),"Graph out "+std::to_string(r.at("output").get<unsigned>()),{{"kind","graph-output"},{"index",i}},r.at("output"));}
    for(size_t i=0;i<mixer.at("sidechains").size();++i){const auto &r=mixer.at("sidechains")[i];const auto from=last.find(r.at("source"));if(from!=last.end())edge(from->second,"plugin:"+r.at("plugin").get<std::string>(),"Sidechain "+std::to_string(r.at("input").get<unsigned>()),{{"kind","plugin-input"},{"index",i}},0,r.at("input"),r.value("enabled",true));}
    for(const auto &r:mixer.at("instruments")){const auto p=plugin.find(r.at("plugin"));if(p!=plugin.end()&&!p->second->value("isInstrument",false))edge("plugin:"+r.at("plugin").get<std::string>(),r.at("target"),"Aux "+std::to_string(r.at("output").get<unsigned>()),{{"kind","plugin-output"},{"plugin",r.at("plugin")},{"output",r.at("output")}},r.at("output"));}
    // A collapsed instrument graph represents independent per-channel copies;
    // selecting its instrument expands those copies for direct inspection.
    for(const auto &i:data.at("instruments"))if(!i.at("plugin").get<bool>()){
      const auto id=i.at("id").get<std::string>(),key="instrument:"+id;const Json *assignment=nullptr;for(const auto &a:data.at("instrumentAssignments"))if(a.at("target")==id)assignment=&a;
      const auto number=i.at("index").get<unsigned>();add(key,"I"+std::to_string(number)+" · "+i.at("name").get<std::string>(),"Sample voices · note's channel",28,y,"sample",{},{},{},id,number);
      if(assignment){const auto g=assignment->at("graph").get<std::string>();const bool expanded=selected==key;
        if(!expanded){const auto copy="instrument-graph:"+id+":all";add(copy,graphName(g),"Independent copies · select instrument",264,y,"graph",{},{},g,id,number);edge(key,copy,"Per channel");for(const auto &b:buses)if(b.at("kind")=="track")edge(copy,b.at("id"),"Before channel");}
        else for(const auto &b:buses)if(b.at("kind")=="track"&&visible.contains(b.at("id"))){const auto bus=b.at("id").get<std::string>(),copy="instrument-graph:"+id+":"+bus;add(copy,graphName(g),"I"+std::to_string(number)+" → "+b.at("name").get<std::string>(),264,y,"graph",{},{},g,id,number);edge(key,copy,"Independent copy");edge(copy,bus,"Before channel");y+=100;}
      }y+=118;
    }geometry();
  }
  void fit(){if(nodes.empty())return;float l=FLT_MAX,t=FLT_MAX,r=-FLT_MAX,b=-FLT_MAX;for(const auto &n:nodes){l=std::min(l,n.x);t=std::min(t,n.y);r=std::max(r,n.x+184);b=std::max(b,n.y+68);}zoom=std::clamp(std::min((viewport.w-32)/(r-l),(viewport.h-32)/(b-t)),.15f,1.5f);panX=(viewport.w-(r-l)*zoom)/2-l*zoom;panY=(viewport.h-(b-t)*zoom)/2-t*zoom;geometry();}
  int nodeAt(float x,float y)const{for(size_t i=nodes.size();i>0;--i)if(nodes[i-1].rect.contains(x,y))return int(i-1);return -1;}
  int edgeAt(float x,float y)const{float best=7;int found=-1;for(size_t i=0;i<edges.size();++i)if(edges[i].bounds.contains(x,y))for(size_t j=1;j<edges[i].points.size();++j){auto d=GraphCanvas::distance({x,y},edges[i].points[j-1],edges[i].points[j]);if(d<best){best=d;found=int(i);}}return found;}
  Json snapshot()const{Json ns=Json::array(),es=Json::array();for(const auto &n:nodes)ns.push_back({{"id",n.id},{"bus",n.bus},{"plugin",n.plugin},{"graph",n.graph},{"instrument",n.instrument},{"x",n.x},{"y",n.y},{"rect",{n.rect.x,n.rect.y,n.rect.w,n.rect.h}}});for(const auto &e:edges)es.push_back({{"source",e.source},{"target",e.target},{"action",e.action},{"enabled",e.enabled},{"midpoint",{e.points[16].x,e.points[16].y}}});return {{"nodes",ns},{"edges",es},{"viewport",{viewport.x,viewport.y,viewport.w,viewport.h}},{"zoom",zoom}};}
};
}
