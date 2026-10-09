#pragma once
#include "NativeToolWindow.hpp"
namespace ScreamSeq {
class GraphTrimsWindow final:public NativeToolWindow {
  using Json=Api::Json;
public:
  struct Context {std::string document,revision;};
private:
  using Request=std::function<Json(const std::string &,const Json &)>;
  enum {owner=4201,port,gain,link,source,minimum,maximum,apply,reload,close,heading=4300,gainLabel,linkLabel,sourceLabel,rangeLabel,help,statusLabel};
  Request request_;std::function<Context()> context_;Context captured_;Json owners_=Json::array(),data_=Json::object();std::vector<std::string> links_,sources_;int owner_=0,port_=0;bool setting_=false,dirty_=false;
  int selected(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  void select(int id,int value){SendMessageW(controls_.at(id),CB_SETCURSEL,value,0);}
  void addChoice(int id,const std::string &name){auto text=wide(name);SendMessageW(controls_.at(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
  void clear(int id){SendMessageW(controls_.at(id),CB_RESETCONTENT,0,0);}
  void status(std::wstring message){status_=std::move(message);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  void current()const{auto now=context_();if(now.document!=captured_.document||now.revision!=captured_.revision)throw std::runtime_error("Song changed / Reload before applying this trim draft");}
  void showPort(){setting_=true;const auto &ports=data_.at("ports");if(port_<0||size_t(port_)>=ports.size()){setting_=false;return;}const auto &p=ports[port_];set(gain,p.at("gainDB"));clear(link);links_={""};addChoice(link,"Independent");int chosen=0;for(const auto &other:ports)if(other.at("output")!=p.at("output")){links_.push_back(other.at("key"));addChoice(link,other.at("name"));if(other.at("key")==p.at("linkTo"))chosen=int(links_.size()-1);}select(link,chosen);
    clear(source);sources_={""};addChoice(source,"None");chosen=0;const auto edges=p.value("modulation",Json::array());for(const auto &s:data_.at("sources")){sources_.push_back(s.at("id"));addChoice(source,s.at("name"));if(!edges.empty()&&s.at("id")==edges[0].at("source"))chosen=int(sources_.size()-1);}select(source,chosen);set(minimum,edges.empty()?Json(0):edges[0].at("minimumDB"));set(maximum,edges.empty()?Json(6):edges[0].at("maximumDB"));for(auto id:{source,minimum,maximum})EnableWindow(controls_.at(id),edges.size()<=1);dirty_=false;setting_=false;
  }
  void loadOwner(){current();auto target=owners_.at(owner_);data_=request_("graph.trim.get",{{"graph",target.at("graph")},{"node",target.at("node")}});current();setting_=true;clear(port);for(const auto &p:data_.at("ports"))addChoice(port,std::string(p.at("output").get<bool>()?"Out / ":"In / ")+p.at("name").get<std::string>());port_=0;select(port,0);setting_=false;showPort();status(L"Trims are in dB / Apply creates one document Undo");}
  void load(){captured_=context_();auto graph=request_("graph.get",{{"includeState",false},{"includeImplicitMixer",true}});current();owners_=Json::array();auto add=[&](Json graph,const std::string &node,const std::string &name){owners_.push_back({{"graph",graph},{"node",node},{"name",name}});};
    for(const auto &b:graph.at("mixer").at("buses")){add(nullptr,b.at("id"),b.at("name").get<std::string>()+" / bus");bool staged=false;for(const auto &a:graph.at("assignments"))staged|=a.at("target")==b.at("id");for(const auto &c:graph.at("commands"))staged|=c.at("target")==b.at("id")&&(c.at("kind")=="row"||c.at("kind")=="start");if(staged)add(nullptr,"stage:"+b.at("id").get<std::string>(),b.at("name").get<std::string>()+" / graph stage");}
    for(const auto &p:graph.value("plugins",Json::array()))add(nullptr,"plugin:"+p.at("id").get<std::string>(),p.value("name",p.at("id").get<std::string>()));
    for(const auto &source:graph.value("songSources",Json::array()))if(source.at("kind")=="follower")add(nullptr,"source:"+source.at("id").get<std::string>(),source.at("name"));
    for(const auto &g:graph.value("groups",Json::array()))add(nullptr,g.at("id"),g.at("name"));
    for(const auto &g:graph.at("library")){for(const auto &n:g.at("nodes"))if(n.at("kind")=="input"||n.at("kind")=="output"||n.at("kind")=="plugin"||n.at("kind")=="follower")add(g.at("id"),n.at("id"),g.at("name").get<std::string>()+" / "+n.at("name").get<std::string>());for(const auto &n:g.at("groups"))add(g.at("id"),n.at("id"),g.at("name").get<std::string>()+" / "+n.at("name").get<std::string>());}
    setting_=true;clear(owner);for(const auto &o:owners_)addChoice(owner,o.at("name"));owner_=0;select(owner,0);setting_=false;if(!owners_.empty())loadOwner();else status(L"Add an audio processor or enable the mixer first");dirty_=false;
  }
  void action(int id,unsigned notification)override{if(setting_)return;if(id==close){hide();return;}if(id==reload){load();return;}
    if(notification==CBN_SELCHANGE&&(id==owner||id==port)){if(dirty_){select(id,id==owner?owner_:port_);throw std::runtime_error("Apply or Reload this draft before changing ports");}if(id==owner){owner_=selected(owner);loadOwner();}else{port_=selected(port);showPort();}return;}
    if(id==apply){current();if(owners_.empty()||data_.at("ports").empty())return;const auto &p=data_.at("ports").at(port_);const auto a=number(gain),lo=number(minimum),hi=number(maximum);if(std::abs(a)>48||std::abs(lo)>96||std::abs(hi)>96)throw std::runtime_error("Trim range is -48 to +48 dB; source contributions are -96 to +96 dB");Json args{{"graph",owners_[owner_].at("graph")},{"node",owners_[owner_].at("node")},{"port",p.at("key")},{"gainDB",a},{"expectedRevision",captured_.revision}};const auto linked=links_.at(selected(link));args["linkTo"]=linked.empty()?Json(nullptr):Json(linked);if(p.at("modulation").size()<=1){auto s=sources_.at(selected(source));args["modulation"]=s.empty()?Json::array():Json::array({{{"source",s},{"minimumDB",lo},{"maximumDB",hi}}});}request_("graph.trim.set",args);captured_=context_();loadOwner();return;}
    if(notification==EN_CHANGE||notification==CBN_SELCHANGE){dirty_=true;status(L"Trim draft / Apply saves; Reload discards");}
  }
  void layout()override{auto [w,h]=size();place(heading,18,18,w-36,25);place(owner,18,52,w-36,28);place(port,18,92,w-36,28);place(gainLabel,18,139,150,24);place(gain,185,135,100,28);place(linkLabel,18,181,150,24);place(link,185,177,w-203,28);place(sourceLabel,18,223,150,24);place(source,185,219,w-203,28);place(rangeLabel,18,265,150,24);place(minimum,185,261,90,28);place(maximum,285,261,90,28);place(apply,18,308,130,28);place(reload,160,308,130,28);place(help,18,358,w-36,60);place(statusLabel,18,h-82,w-154,62);place(close,w-124,h-50,106,28);}
  void paint(RenderSurface &s)override{auto [w,h]=size();s.fill(0,0,w,h,0x18222d);}
public:
  GraphTrimsWindow(HWND owner,Request request,std::function<Context()> context):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)){minimumWidth_=580;minimumHeight_=540;create(L"ScreamSeq.GraphTrims",L"Audio port trims",660,580);for(auto id:{GraphTrimsWindow::owner,port,link,source})combo(id);for(auto id:{gain,minimum,maximum})edit(id,L"0",32);button(apply,L"Apply");button(reload,L"Reload");button(close,L"Close");for(auto [id,labelText]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"Audio port trims"},{gainLabel,L"Trim / dB"},{linkLabel,L"Inverse link"},{sourceLabel,L"Modulation source"},{rangeLabel,L"Source range / dB"},{help,L"Input trim changes drive; output trim adjusts level. Linked trims move in opposite directions. Loudness may still change."},{statusLabel,L""}})label(id,labelText);finish();}
  void open(){const bool retain=visible()||dirty_;show();if(!retain)load();}
};
}
