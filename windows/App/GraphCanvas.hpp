#pragma once
#include "WorkspaceState.hpp"
#include <set>
#include <cfloat>
namespace ScreamSeq {
// Geometry is retained in screen DIPs. Drawing and all hit tests consume these
// same sockets/Bezier samples; no document or plugin access occurs in drawing.
struct GraphCanvas {
  struct Point {float x{},y{};};
  struct Socket {std::string node;bool output{},modulation{};uint32_t port{};Point at;};
  struct Node {std::string id,kind;std::wstring name;WorkspaceRect rect;};
  struct Wire {bool modulation{};size_t index{};std::array<Point,33> points;WorkspaceRect bounds;};
  WorkspaceRect viewport;float zoom=1,panX=0,panY=0;
  std::vector<Node> nodes;std::vector<Socket> sockets;std::vector<Wire> wires;
  static std::wstring wide(const std::string &s){const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),r.data(),n);return r;}
  Point screen(float x,float y) const {return {viewport.x+panX+x*zoom,viewport.y+panY+y*zoom};}
  Point world(float x,float y) const {return {(x-viewport.x-panX)/zoom,(y-viewport.y-panY)/zoom};}
  static std::array<Point,33> curve(Point a,Point b){std::array<Point,33> out;const auto dx=std::max(50.0f,std::abs(b.x-a.x)*.45f);for(size_t i=0;i<out.size();++i){const float t=float(i)/32,u=1-t;out[i]={u*u*u*a.x+3*u*u*t*(a.x+dx)+3*u*t*t*(b.x-dx)+t*t*t*b.x,u*u*u*a.y+3*u*u*t*a.y+3*u*t*t*b.y+t*t*t*b.y};}return out;}
  void rebuild(const Api::Json &definition,const std::string &exposedNode={},uint32_t exposedParameter=0) {
    nodes.clear();sockets.clear();wires.clear();if(!definition.contains("nodes"))return;
    for(const auto &n:definition.at("nodes")){
      const auto id=n.at("id").get<std::string>(),kind=n.at("kind").get<std::string>();std::set<uint32_t> inputs,outputs,modulation;
      if(kind=="plugin"||kind=="output"||kind=="follower")inputs.insert(0);if(kind=="plugin"||kind=="input")outputs.insert(0);
      if(kind=="plugin"){for(const auto &p:n.at("plugin").value("inputs",Api::Json::array()))inputs.insert(p.get<uint32_t>());for(const auto &p:n.at("plugin").value("outputs",Api::Json::array()))outputs.insert(p.get<uint32_t>());if(id==exposedNode)modulation.insert(exposedParameter);}
      for(const auto &e:definition.at("audio")){if(kind=="input"&&e.at("source")==id)outputs.insert(e.value("output",0u));if(kind=="output"&&e.at("target")==id)inputs.insert(e.value("input",0u));}
      for(const auto &m:definition.at("modulation"))if(m.at("target")==id)modulation.insert(m.at("parameter").get<uint32_t>());
      const bool source=kind!="input"&&kind!="output"&&kind!="plugin";
      const auto pos=screen(n.at("x").get<float>(),n.at("y").get<float>());const float h=48+18*float(std::max({inputs.size()+modulation.size(),outputs.size()+size_t(source),size_t(1)}));
      nodes.push_back({id,kind,wide(n.at("name").get<std::string>()),{pos.x,pos.y,156*zoom,h*zoom}});
      size_t i=0;for(auto port:inputs)sockets.push_back({id,false,false,port,{pos.x,pos.y+(48+18*float(i++))*zoom}});
      for(auto port:modulation)sockets.push_back({id,false,true,port,{pos.x,pos.y+(48+18*float(i++))*zoom}});
      i=0;for(auto port:outputs)sockets.push_back({id,true,false,port,{pos.x+156*zoom,pos.y+(48+18*float(i++))*zoom}});
      if(source)sockets.push_back({id,true,true,0,{pos.x+156*zoom,pos.y+48*zoom}});
    }
    auto socket=[&](const Api::Json &id,bool out,bool mod,uint32_t port)->const Socket *{for(const auto &s:sockets)if(s.node==id.get_ref<const std::string &>()&&s.output==out&&s.modulation==mod&&s.port==port)return &s;return nullptr;};
    for(bool mod:{false,true}){const auto &edges=definition.at(mod?"modulation":"audio");for(size_t i=0;i<edges.size();++i){const auto &e=edges[i];auto a=socket(e.at("source"),true,mod,mod?0:e.value("output",0u)),b=socket(e.at("target"),false,mod,mod?e.at("parameter").get<uint32_t>():e.value("input",0u));if(!a||!b)continue;Wire w;w.modulation=mod;w.index=i;w.points=curve(a->at,b->at);float left=w.points[0].x,right=left,top=w.points[0].y,bottom=top;for(auto p:w.points){left=std::min(left,p.x);right=std::max(right,p.x);top=std::min(top,p.y);bottom=std::max(bottom,p.y);}w.bounds={left-7,top-7,right-left+14,bottom-top+14};wires.push_back(std::move(w));}}
  }
  int socketAt(float x,float y) const {for(size_t i=0;i<sockets.size();++i){const auto &p=sockets[i].at;if(std::hypot(x-p.x,y-p.y)<=7)return int(i);}return -1;}
  int nodeAt(float x,float y) const {for(size_t i=nodes.size();i>0;--i)if(nodes[i-1].rect.contains(x,y))return int(i-1);return -1;}
  static float distance(Point p,Point a,Point b){const auto dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;const auto t=length?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length,0.0f,1.0f):0;return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);}
  int wireAt(float x,float y) const {float best=7;int selected=-1;for(size_t i=0;i<wires.size();++i)if(wires[i].bounds.contains(x,y))for(size_t j=1;j<wires[i].points.size();++j){auto d=distance({x,y},wires[i].points[j-1],wires[i].points[j]);if(d<best){best=d;selected=int(i);}}return selected;}
  void fit(const Api::Json &definition){if(!definition.contains("nodes")||definition.at("nodes").empty())return;zoom=1;panX=panY=0;rebuild(definition);float left=FLT_MAX,top=FLT_MAX,right=-FLT_MAX,bottom=-FLT_MAX;for(const auto &n:nodes){left=std::min(left,n.rect.x-viewport.x);top=std::min(top,n.rect.y-viewport.y);right=std::max(right,n.rect.x+n.rect.w-viewport.x);bottom=std::max(bottom,n.rect.y+n.rect.h-viewport.y);}zoom=std::clamp(std::min((viewport.w-36)/std::max(1.0f,right-left),(viewport.h-36)/std::max(1.0f,bottom-top)),.25f,1.5f);panX=(viewport.w-(right-left)*zoom)/2-left*zoom;panY=(viewport.h-(bottom-top)*zoom)/2-top*zoom;}
};
}
