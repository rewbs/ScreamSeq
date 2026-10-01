#pragma once
#include "NativeToolWindow.hpp"

namespace ScreamSeq {
// A command draft owns its pattern identity/revision independently of the cursor.
// All writes replace only that pattern, merging untouched rows/buses/columns.
class GraphCommandsWindow final : public NativeToolWindow {
  using Json=Api::Json;
public:
  struct Cursor {std::string document,revision,target;unsigned pattern=0,row=0;};
private:
  using Request=std::function<Json(const std::string &,const Json &)>;
  enum : int {target=4001,lane,kind,graph,row,offset,amount,wet,tails,apply,remove,verify,reload,cursor,enableLane,enableMixer,open,close,loadCell,
    heading=4100,targetLabel,laneLabel,kindLabel,graphLabel,rowLabel,offsetLabel,amountLabel,wetLabel,help,statusLabel};
  Request request_;std::function<Cursor()> context_;std::function<void(const std::string &)> inspect_;
  Cursor captured_;std::string patternID_,bus_;unsigned rows_=0,column_=0;
  Json data_=Json::object();bool dirty_=false,pending_=false,setting_=false,tails_=false;
  uint64_t generation_=0;
  static constexpr std::array<const char *,6> kinds={"row","start","stop","clear","amount","wet"};
  int selection(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  void select(int id,int index){SendMessageW(controls_.at(id),CB_SETCURSEL,index,0);}
  void status(std::wstring text){status_=std::move(text);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  bool current()const{auto c=context_();return c.document==captured_.document&&c.revision==captured_.revision;}
  void requireCurrent()const{if(!current())throw std::runtime_error("Song changed / draft retained; Reload cell or From cursor explicitly refreshes");}
  unsigned rowValue()const{double value=number(row);if(value<0||value>=rows_||value!=std::floor(value))throw std::runtime_error("Choose a whole row inside the captured pattern");return unsigned(value);}
  Json buses()const{return data_.value("mixer",Json::object()).value("buses",Json::array());}
  std::string selectedGraph()const{auto index=selection(graph);const auto &library=data_.at("library");return index>=0&&size_t(index)<library.size()?library.at(index).at("id").get<std::string>():"";}
  unsigned count()const{for(const auto &l:data_.value("lanes",Json::array()))if(l.at("target")==bus_)return l.at("count");return 0;}
  void choices(){
    setting_=true;SendMessageW(controls_.at(target),CB_RESETCONTENT,0,0);int i=0,chosen=-1;
    for(const auto &b:buses()){auto name=wide(b.at("name").get<std::string>());SendMessageW(controls_.at(target),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(b.at("id")==bus_)chosen=i;++i;}
    select(target,chosen);SendMessageW(controls_.at(graph),CB_RESETCONTENT,0,0);
    for(const auto &g:data_.at("library")){auto name=std::to_wstring(g.at("number").get<unsigned>())+L" · "+wide(g.at("name").get<std::string>());SendMessageW(controls_.at(graph),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}
    select(graph,data_.at("library").empty()?-1:0);select(lane,int(column_));setting_=false;
  }
  void cell(){
    setting_=true;select(kind,0);set(row,std::to_wstring(captured_.row));set(offset,L"0");set(amount,L"100");set(wet,L"100");tails_=false;
    for(const auto &c:data_.at("commands"))if(c.at("pattern")==patternID_&&c.at("target")==bus_&&c.at("column")==column_&&c.at("position").get<uint32_t>()/65536==captured_.row){
      for(size_t i=0;i<kinds.size();++i)if(c.at("kind")==kinds[i])select(kind,int(i));
      for(size_t i=0;i<data_.at("library").size();++i)if(data_.at("library")[i].at("id")==c.at("graph"))select(graph,int(i));
      set(offset,Json(double(c.at("position").get<uint32_t>()%65536)*100/65536));set(amount,Json(c.at("amount").get<double>()*100));set(wet,Json(c.at("wet").get<double>()*100));tails_=c.at("tails");break;
    }
    set(heading,L"Pattern "+std::to_wstring(captured_.pattern)+L" · Graph commands / captured target");
    setting_=false;dirty_=false;++generation_;layout();
  }
  void load(bool fromCursor,std::string preferred={},unsigned preferredLane=0){
    if(pending_)return;const auto now=context_();auto chosen=fromCursor?now:captured_;
    if(!fromCursor&&now.document!=captured_.document)throw std::runtime_error("Document changed / use From cursor to capture the new song");
    auto selectedBus=fromCursor?(preferred.empty()?now.target:std::move(preferred)):bus_;const auto selectedColumn=fromCursor?std::min(7u,preferredLane):column_;
    pending_=true;layout();const auto token=generation_;
    try{auto data=request_("graph.get",{{"includeState",false}});const auto after=context_();
      if(after.document!=now.document||after.revision!=now.revision||token!=generation_)throw std::runtime_error("Song or draft changed while loading / draft retained");
      const auto &patterns=data.at("patterns");auto found=std::find_if(patterns.begin(),patterns.end(),[&](const auto &p){return fromCursor?p.at("index")==chosen.pattern:p.at("id")==patternID_;});
      if(found==patterns.end())throw std::runtime_error("Captured pattern is unavailable / use From cursor");
      const auto available=data.at("mixer").at("buses");
      if(std::none_of(available.begin(),available.end(),[&](const auto &b){return b.at("id")==selectedBus;})){
        if(!fromCursor&&!selectedBus.empty())throw std::runtime_error("Captured bus is unavailable / use From cursor");selectedBus=available.empty()?"":available.front().at("id").get<std::string>();}
      chosen.pattern=found->at("index");rows_=found->at("rows");chosen.row=std::min(chosen.row,rows_-1);patternID_=found->at("id");
      chosen.document=now.document;chosen.revision=now.revision;captured_=chosen;data_=std::move(data);bus_=std::move(selectedBus);column_=selectedColumn;
      choices();cell();pending_=false;status(bus_.empty()?L"Enable the mixer to add graph lanes":L"Ready / Apply uses document Undo; the pattern cursor stays independent");
    }catch(...){pending_=false;layout();throw;}layout();
  }
  Json parameters(bool removing,bool dry)const{
    if(bus_.empty())throw std::runtime_error("Enable the mixer and choose a channel or group");const auto destination=rowValue();
    Json events=Json::array();for(auto c:data_.at("commands"))if(c.at("pattern")==patternID_){
      if(c.at("target")==bus_&&c.at("column")==column_&&c.at("position").get<uint32_t>()/65536==destination)continue;c.erase("pattern");events.push_back(std::move(c));}
    if(!removing){const auto index=selection(kind);if(index<0||index>=int(kinds.size()))throw std::runtime_error("Choose a graph action");const auto graphID=selectedGraph();if(index!=3&&graphID.empty())throw std::runtime_error("Create or choose a reusable graph first");
      const double p=number(offset),a=number(amount),w=number(wet);if(p<0||p>=100||a<0||a>100||w<0||w>100)throw std::runtime_error("Offset must be 0 to below 100%; Amount and Wet must be 0 to 100%");
      events.push_back({{"target",bus_},{"graph",index==3?"":graphID},{"position",uint64_t(destination)*65536+std::min(65535LL,std::llround(p*65536/100))},{"column",column_},{"kind",kinds[size_t(index)]},{"amount",a/100},{"wet",w/100},{"tails",tails_}});}
    return {{"pattern",captured_.pattern},{"lanes",Json::array({{{"target",bus_},{"count",std::max(count(),column_+1)}}})},{"commands",events},{"dryRun",dry}};
  }
  void mutate(const std::string &method,Json p,bool dry=false){
    if(pending_)return;requireCurrent();const auto token=generation_;p["expectedRevision"]=captured_.revision;pending_=true;layout();
    try{request_(method,p);pending_=false;if(context_().document!=captured_.document||token!=generation_)throw std::runtime_error("Source changed during Apply / newer draft retained");
      if(dry){status(L"Valid / Verify left the song and document Undo unchanged");}
      else {if(method=="graph.commands.set"&&p.contains("commands"))captured_.row=rowValue();load(false);status(L"Saved / document Undo restores the previous commands");}
    }catch(...){pending_=false;layout();throw;}layout();
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;if(id==close){hide();return;}
    if(notification==EN_CHANGE&&(id==row||id==offset||id==amount||id==wet)){dirty_=true;++generation_;status(L"Command draft / Apply saves; Reload cell discards");return;}
    if(pending_)return;
    if(notification==CBN_SELCHANGE&&(id==target||id==lane)){
      if(dirty_){int index=0;for(const auto &b:buses()){if(b.at("id")==bus_)select(target,index);++index;}select(lane,int(column_));throw std::runtime_error("Apply or Reload the command draft before choosing a different cell");}
      if(id==target){auto index=selection(target);if(index>=0)bus_=buses().at(size_t(index)).at("id");}else column_=unsigned(std::max(0,selection(lane)));cell();return;
    }
    if(notification==CBN_SELCHANGE&&(id==kind||id==graph)){dirty_=true;++generation_;return;}
    if(notification!=BN_CLICKED)return;
    if(id==reload)load(false);else if(id==cursor)load(true);else if(id==loadCell){captured_.row=rowValue();load(false);}
    else if(id==apply||id==verify||id==remove)mutate("graph.commands.set",parameters(id==remove,id==verify),id==verify);
    else if(id==tails){tails_=!tails_;dirty_=true;++generation_;}
    else if(id==enableLane){if(dirty_)throw std::runtime_error("Apply or Reload the command draft first");if(bus_.empty())throw std::runtime_error("Enable the mixer first");mutate("graph.commands.set",{{"pattern",captured_.pattern},{"lanes",Json::array({{{"target",bus_},{"count",std::max(count(),column_+1)}}})}});}
    else if(id==enableMixer){if(dirty_)throw std::runtime_error("Apply or Reload the command draft first");mutate("mixer.enable",Json::object());}
    else if(id==open){requireCurrent();const auto id=selectedGraph();if(!id.empty())inspect_(id);}
  }
  bool key(WPARAM k,bool ctrl,bool)override{
    if(k==VK_ESCAPE){hide();return true;}if(ctrl&&k=='R'){action(reload,BN_CLICKED);return true;}if(ctrl&&k==VK_RETURN){action(apply,BN_CLICKED);return true;}
    if(k==VK_F6){SetFocus(GetFocus()==controls_.at(target)?controls_.at(row):controls_.at(target));return true;}
    if(k==VK_RETURN&&GetFocus()==controls_.at(row)){action(loadCell,BN_CLICKED);return true;}
    if(k==VK_RETURN){const auto id=GetDlgCtrlID(GetFocus());if(id>=tails&&id<=loadCell){action(id,BN_CLICKED);return true;}}
    return false;
  }
  void layout()override{
    const auto [w,h]=size();const float x=18,c=142,width=w-c-18;place(heading,x,16,w-36,24);
    const std::array<std::pair<int,int>,4> pairs={{{targetLabel,target},{laneLabel,lane},{kindLabel,kind},{graphLabel,graph}}};
    float y=54;for(auto [label,control]:pairs){place(label,x,y+5,118,22);place(control,c,y,width,220);y+=40;}
    place(rowLabel,x,220,118,22);place(row,c,216,100,26);place(loadCell,c+108,216,110,26);place(offsetLabel,c+230,220,84,22);place(offset,c+316,216,std::max(80.0f,width-316),26);
    place(amountLabel,x,260,118,22);place(amount,c,256,100,26);place(wetLabel,c+130,260,85,22);place(wet,c+216,256,100,26);place(tails,x,298,w-36,27);
    float bx=x;for(auto [id,width]:std::initializer_list<std::pair<int,float>>{{apply,138.f},{remove,100.f},{verify,100.f},{open,114.f}}){place(id,bx,342,width,28);bx+=width+8;}
    bx=x;for(auto [id,width]:std::initializer_list<std::pair<int,float>>{{enableLane,115.f},{enableMixer,118.f},{reload,118.f},{cursor,118.f}}){place(id,bx,381,width,28);bx+=width+8;}
    place(help,x,432,w-36,68);place(statusLabel,x,h-90,w-148,72);place(close,w-118,h-54,100,28);
    set(tails,tails_?L"Stopped effects keep their tails: on":L"Stopped effects keep their tails: off");
    for(auto [id,control]:controls_)if(id>=target&&id<=loadCell)EnableWindow(control,!pending_||id==close);
    for(int id:{lane,kind,graph,row,offset,amount,wet,tails,apply,remove,verify,enableLane,open,loadCell})EnableWindow(controls_.at(id),!pending_&&!bus_.empty());
    EnableWindow(controls_.at(graph),!pending_&&!bus_.empty()&&selection(kind)!=3);EnableWindow(controls_.at(open),!pending_&&!selectedGraph().empty());
  }
  void paint(RenderSurface &s)override{auto [w,h]=size();s.fill(0,0,w,h,0x18222d);s.line(18,420,w-18,420,0x334757);}
public:
  GraphCommandsWindow(HWND owner,Request request,std::function<Cursor()> context,std::function<void(const std::string &)> inspect):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),inspect_(std::move(inspect)){
    minimumWidth_=660;minimumHeight_=660;create(L"ScreamSeq.GraphCommands",L"Pattern graph commands",760,700);
    for(int id:{target,lane,kind,graph})combo(id);
    for(unsigned i=1;i<=8;++i){auto name=L"Graph lane "+std::to_wstring(i);SendMessageW(controls_.at(lane),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}
    for(auto name:{L"R · This row",L"S · Start / persistent",L"X · Stop named graph",L"CLR · Clear persistent graphs",L"A · Set Amount",L"W · Set Wet"})SendMessageW(controls_.at(kind),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
    for(int id:{row,offset,amount,wet})edit(id,L"0",32);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{{tails,L"Tails off"},{apply,L"Apply command"},{remove,L"Remove"},{verify,L"Verify"},{reload,L"Reload cell"},{cursor,L"From cursor"},{enableLane,L"Enable lane"},{enableMixer,L"Enable mixer"},{open,L"Open graph"},{close,L"Close"},{loadCell,L"Load row"}})button(id,name);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"Graph commands"},{targetLabel,L"Channel / group"},{laneLabel,L"Lane"},{kindLabel,L"Action"},{graphLabel,L"Reusable graph"},{rowLabel,L"Pattern row"},{offsetLabel,L"Offset %"},{amountLabel,L"Amount %"},{wetLabel,L"Wet %"},{help,L"R lasts one row; S stays active until X or CLR. Repeated S updates its existing chain.\nProcessing order: row → persistent → ordinary graph.\nCtrl+Enter applies · Ctrl+R reloads · F6 switches target/row · Close keeps drafts."},{statusLabel,L""}})label(id,name);
    data_["library"]=Json::array();finish();
  }
  void openAt(std::string target={},unsigned column=0){const bool retain=visible()||dirty_;show();if(!retain)load(true,std::move(target),column);SetFocus(controls_.at(row));}
  Json snapshot()const{return {{"visible",visible()},{"document",captured_.document},{"expectedRevision",captured_.revision},{"pattern",captured_.pattern},{"patternID",patternID_},{"row",captured_.row},{"target",bus_},{"column",column_},{"draft",dirty_},{"pending",pending_},{"stale",!current()},{"status",utf8(status_)}};}
};
}
