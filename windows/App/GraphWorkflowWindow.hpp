#pragma once
#include "NativeToolWindow.hpp"
#include <array>
#include <set>
#include <sstream>
#include <tuple>

namespace ScreamSeq {
// Additional graph operations share the document APIs with the canvas. This
// window owns raw command fields, never a second editable graph definition.
class GraphWorkflowWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Callbacks {
    std::function<Json()> context; // documentId, revision, busy, fieldDraft, graph, node
    std::function<Json(const std::string &,const Json &)> request; // result.data
    std::function<void(const Json &,const std::string &)> curve;
    std::function<void(const std::string &,uint32_t)> activity;
    std::function<void(const Json &)> provenance;
  };
  enum:int {page=10000,graph,reload,nodes,close,check,title,statusText,
    group=10100,parent,name,x,y,groupCreate,groupUpdate,groupRemove,groupExport,
    processor=10200,edge,pluginParameter,pluginValue,addEffect,insert,detach,heal,bypass,setParameter,
    source=10300,sourceKind,sourceName,rate,phase,attack,release,controller,amount,audioBus,audioPlugin,output,noteTarget,noteInstrument,preFader,sourceAdd,sourceUpdate,sourceRemove,sourceCurve,
    connection=10400,modSource,modPlugin,modParameter,minimum,maximum,enabled,quantized,modSet,modRemove,inspectActivity,inspectSources,
    region=10500,regionKind,regionTitle,regionText,regionX,regionY,regionWidth,regionHeight,regionColor,regionScope,regionCollapsed,regionAdd,regionSet,regionRemove,collapseNodes,
    cable=10600,rerouteX,rerouteY,reroutePoint,rerouteAdd,rerouteMove,rerouteRemove,rerouteClear,
    port=10700,listenGain,spectrum,signalRefresh,scopeStart,scopeStop,listenStart,listenStop,clearClip,
    provenanceList=10800,provenanceRefresh,provenanceNext,provenanceOpen,details=10900};
private:
  struct Field {int id,page;std::wstring label;bool combo=false;};
  struct Action {int id,page;std::wstring label;};
  Callbacks callbacks_;
  std::vector<Field> fields_;
  std::vector<Action> actions_;
  std::map<int,std::vector<Json>> choices_;
  std::map<int,LRESULT> acceptedSelection_,pendingSelections_;
  std::vector<int> pendingNodes_,acceptedNodes_;
  Json captured_=Json::object(),catalog_=Json::object(),signals_=Json::object(),scope_=Json::object(),provenance_=Json::object();
  Json graph_=nullptr;
  unsigned page_=0,provenanceOffset_=0;
  uint64_t generation_=0;
  bool setting_=false,pending_=false,dirty_=false,spectrum_=false,dryRun_=false;
  HWND pendingFocus_{};
  std::string message_,error_,observedRevision_;bool observedDraft_=false;
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(captured_.value("documentId",std::string()),captured_.value("revision",std::string()),
      graph_.dump(),generation_,dirty_,pending_);
  }
  static constexpr std::array<const wchar_t *,8> pages_{L"Processing groups",L"Processors and patching",L"Song control sources",L"Parameter modulation",L"Frames and comments",L"Cable reroutes",L"Signal scope and listening",L"Existing parameter sources"};
  static void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
  bool current()const {const auto c=callbacks_.context();return !captured_.empty()&&c.at("documentId")==captured_.at("documentId")&&c.at("revision")==captured_.at("revision");}
  bool available()const {const auto c=callbacks_.context();return !pending_&&!c.value("busy",false);}
  void requireWrite()const {require(available(),"Document is busy; no graph edit was queued");require(current(),"Song changed / raw graph fields retained. Reload before applying.");require(!callbacks_.context().value("fieldDraft",false),"Apply or reload the main graph draft before editing graph structure here.");}
  const Json &definition()const {if(graph_.is_null())return catalog_;for(const auto &d:catalog_.at("library"))if(d.at("id")==graph_)return d;throw std::runtime_error("Captured graph no longer exists");}
  Json choice(int id,bool required=true)const {const auto i=SendMessageW(controls_.at(id),CB_GETCURSEL,0,0);const auto it=choices_.find(id);if(it==choices_.end()||i<0||size_t(i)>=it->second.size()){require(!required,"Choose an available target");return nullptr;}return it->second[size_t(i)];}
  std::string chosen(int id)const {const auto c=choice(id);require(c.is_string()&&!c.get_ref<const std::string &>().empty(),"Choose an available target");return c.get<std::string>();}
  std::string text(int id)const{return utf8(field(id));}
  double finite(int id,double low,double high)const {const auto s=field(id);size_t used=0;double n=std::stod(s,&used);require(used==s.size()&&std::isfinite(n)&&n>=low&&n<=high,"Enter a finite number in the labelled range");return n;}
  uint32_t integer(int id,uint32_t high=UINT32_MAX)const {const auto n=finite(id,0,high);require(n==std::floor(n),"Enter a whole number");return uint32_t(n);}
  bool boolean(int id)const{return choice(id).get<bool>();}
  Json selectedNodes()const {Json out=Json::array();const auto &values=choices_.at(nodes);for(size_t i=0;i<values.size();++i)if(SendMessageW(controls_.at(nodes),LB_GETSEL,i,0)>0)out.push_back(values[i]);return out;}
  void fill(int id,const std::vector<std::pair<std::wstring,Json>> &values,Json selected=nullptr) {
    const bool list=id==nodes||id==provenanceList;auto h=controls_.at(id);SendMessageW(h,list?LB_RESETCONTENT:CB_RESETCONTENT,0,0);auto &out=choices_[id];out.clear();
    int select=-1;for(const auto &[label,value]:values){const auto at=SendMessageW(h,list?LB_ADDSTRING:CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));require(at>=0,"Cannot fill graph choices");out.push_back(value);if(value==selected)select=int(out.size()-1);}
    if(list){if(select>=0)ScreamSeq::NativeInputGate::present(h,LB_SETSEL,TRUE,select);if(id==nodes){acceptedNodes_.clear();if(select>=0)acceptedNodes_.push_back(select);}}else {const auto index=select>=0?select:values.empty()?-1:0;ScreamSeq::NativeInputGate::present(h,CB_SETCURSEL,index,0);acceptedSelection_[id]=index;}
  }
  void boolField(int id,unsigned p,const wchar_t *label,bool initial) {combo(id,p,label);fill(id,{{L"Yes",true},{L"No",false}},initial);}
  void editField(int id,unsigned p,const wchar_t *label,const wchar_t *initial=L"",int limit=256){edit(id,initial,limit);fields_.push_back({id,int(p),label});}
  void combo(int id,unsigned p,const wchar_t *label){add(id,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS);fields_.push_back({id,int(p),label,true});}
  void actionButton(int id,unsigned p,const wchar_t *label){button(id,label);actions_.push_back({id,int(p),label});}
  void rememberPendingSelections(){pendingSelections_.clear();for(auto [id,h]:controls_){wchar_t kind[20]{};GetClassNameW(h,kind,20);if(_wcsicmp(kind,L"ComboBox")==0)pendingSelections_[id]=SendMessageW(h,CB_GETCURSEL,0,0);}pendingNodes_.clear();for(int i=0;i<SendMessageW(controls_.at(nodes),LB_GETCOUNT,0,0);++i)if(SendMessageW(controls_.at(nodes),LB_GETSEL,i,0)>0)pendingNodes_.push_back(i);}
  void restorePendingSelection(int id){if(const auto i=pendingSelections_.find(id);i!=pendingSelections_.end())ScreamSeq::NativeInputGate::present(controls_.at(id),CB_SETCURSEL,i->second,0);if(id==nodes){ScreamSeq::NativeInputGate::present(controls_.at(nodes),LB_SETSEL,FALSE,-1);for(int i:pendingNodes_)ScreamSeq::NativeInputGate::present(controls_.at(nodes),LB_SETSEL,TRUE,i);}}
  static bool rawChoice(int id){return id==parent||id==sourceKind||id==audioBus||id==audioPlugin||id==noteTarget||id==noteInstrument||id==preFader||id==modSource||id==modParameter||id==enabled||id==quantized||id==regionKind||id==regionScope||id==regionCollapsed;}
  Json call(const std::string &method,Json params,bool write) {
    if(write)requireWrite();else require(available(),"Document is busy");
    const auto before=callbacks_.context();const auto generation=generation_;rememberPendingSelections();pending_=true;error_.clear();layout();
    if(write)params["expectedRevision"]=captured_.at("revision");
    try {
      auto result=callbacks_.request(method,params);pending_=false;layout();
      const auto after=callbacks_.context();require(after.at("documentId")==before.at("documentId"),"Document changed during the graph operation; fields retained");
      if(!write)require(after.at("revision")==before.at("revision"),"Song changed while reading graph data; reload before continuing");
      if(write&&!params.value("dryRun",false)){if(generation_==generation){captured_["revision"]=after.at("revision");dirty_=false;message_="Graph edit completed. Undo restores the previous state.";}else message_="Graph edit completed; newer raw fields retained. Reload before applying again.";}
      return result;
    }catch(...){pending_=false;layout();throw;}
  }
  void load(bool adoptSelection=false) {
    require(available(),"Document is busy");const auto before=callbacks_.context();const auto generation=generation_;
    auto data=call("graph.get",{{"includeState",false},{"includeImplicitMixer",true}},false);
    require(generation==generation_,"Newer raw fields retained; reload again when ready");
    for(const auto *key:{"library","plugins","songSources","songModulation","groups"})require(data.contains(key)&&data[key].is_array(),"Incomplete graph catalogue");
    if(adoptSelection)graph_=before.value("graph",Json(nullptr));
    if(graph_.is_string()&&graph_.get_ref<const std::string &>().empty())graph_=nullptr;
    if(!graph_.is_null()&&std::none_of(data["library"].begin(),data["library"].end(),[&](const auto &d){return d.at("id")==graph_;}))graph_=nullptr;
    catalog_=std::move(data);captured_=before;dirty_=false;error_.clear();message_="Current saved graph loaded. Actions make one guarded Undo step.";
    setting_=true;try{populate();}catch(...){setting_=false;throw;}setting_=false;++generation_;layout();
  }
  void populate();
  void loadSelectedFields(int id);
  void execute(int id,bool dryRun=false);
  void refreshSignals();
  void refreshProvenance();
  Json presentation()const {auto p=definition().value("presentation",Json::object());for(const auto *k:{"regions","cables","collapsedNodes"})if(!p.contains(k))p[k]=Json::array();return p;}
  void writePresentation(Json value,bool dryRun) {call("graph.presentation.set",{{"graph",graph_},{"presentation",std::move(value)},{"dryRun",dryRun}},true);}
  void refreshAfterWrite(bool dryRun){if(dryRun){message_="Preview validated. No song edit or Undo step.";return;}if(dirty_)return;load(false);}
  void layout() override {
    const auto [w,h]=size();std::set<int> visible{page,graph,reload,close,check,title,statusText};
    if(page_<=1||page_==4)visible.insert(nodes);if(page_==6||page_==7)visible.insert(details);if(page_==7)visible.insert(provenanceList);
    for(const auto &f:fields_)if(f.page==int(page_))visible.insert(f.id);for(const auto &a:actions_)if(a.page==int(page_))visible.insert(a.id);
    for(auto [id,control]:controls_)if(!visible.contains(id)&&IsWindowVisible(control))ShowWindow(control,SW_HIDE);
    place(page,8,8,252,26);place(graph,268,8,w-382,26);place(reload,w-106,8,98,26);
    set(title,captured_.empty()?L"Reload to capture the current document":wide((graph_.is_null()?"Song graph":"Reusable graph "+graph_.get<std::string>())+(dirty_?" / raw fields retained":"")));
    place(title,8,42,w-16,36);
    const bool members=page_<=1||page_==4;place(nodes,8,90,206,std::max(100.f,h-188),members);
    float left=members?226.f:8.f,width=w-left-8;unsigned at=0;
    for(const auto &f:fields_)if(f.page==int(page_)){const float cw=(width-12)/2,x=left+(at%2)*(cw+12),y=90+float(at/2)*46;place(f.id,x,y+18,cw,f.combo?240:25);++at;}
    unsigned button=0;const auto count=std::count_if(actions_.begin(),actions_.end(),[&](const auto &a){return a.page==int(page_);});
    const float buttonW=(w-16-6*float(std::max<ptrdiff_t>(0,count-1)))/std::max<ptrdiff_t>(1,count);
    for(const auto &a:actions_)if(a.page==int(page_)){place(a.id,8+float(button++)*(buttonW+6),h-88,buttonW,26);EnableWindow(controls_.at(a.id),!pending_);}
    place(check,w-228,h-50,132,26);place(close,w-88,h-50,80,26);place(statusText,8,h-51,w-244,44);
    const auto c=callbacks_.context();const auto status=!error_.empty()?error_:!captured_.empty()&&!current()?"Song changed. Reload to use the latest graph; your fields are retained.":c.value("fieldDraft",false)?"Main graph has a local draft. Apply or reload it before structural changes.":message_;
    set(statusText,wide(status));
    if(page_==6)place(details,8,198,w-16,70);else if(page_==7){place(provenanceList,8,145,w-16,std::max(90.f,h-330));place(details,8,h-175,w-16,72);}
    EnableWindow(controls_.at(reload),!pending_);EnableWindow(controls_.at(graph),!pending_);EnableWindow(controls_.at(page),!pending_);
    const auto focused=GetFocus();if(pending_&&focused&&owns(focused)&&!IsWindowEnabled(focused)){if(!pendingFocus_)pendingFocus_=focused;SetFocus(nullptr);}if(!pending_&&pendingFocus_){const auto old=std::exchange(pendingFocus_,nullptr);if(!GetFocus()&&IsWindowVisible(old)&&IsWindowEnabled(old)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(old);}
    requestPaint();
  }
  void paint(RenderSurface &s) override {
    const auto [w,h]=size();s.fill(0,0,w,h,0x161f29);const bool members=page_<=1||page_==4;const float left=members?226.f:8.f,width=w-left-8;unsigned at=0;
    if(members)s.uiText(L"Select nodes (Ctrl / Shift adds)",8,72,206,0x9eafbf);
    for(const auto &f:fields_)if(f.page==int(page_)){const float cw=(width-12)/2;s.uiText(f.label,left+(at%2)*(cw+12),90+float(at/2)*46,cw,0x9eafbf);++at;}
    if(page_==6){const float top=280,bottom=h-104;const float height=std::max(1.f,bottom-top);s.fill(8,top,w-16,height,0x101922);s.line(8,top+height/2,w-8,top+height/2,0x384956);
      if(scope_.value("fresh",false)){if(spectrum_){const auto bins=scope_.value("spectrum",Json::array());for(size_t i=0;i<bins.size();++i){const float value=std::clamp(20.f*std::log10(std::max(1e-6f,bins[i].get<float>())),-120.f,0.f);s.line(8+float(i)*(w-16)/std::max<size_t>(1,bins.size()),bottom,8+float(i)*(w-16)/std::max<size_t>(1,bins.size()),bottom-height*(value+120)/120,0x70c9b9);}}
      else{const auto wave=scope_.value("waveform",Json::array());for(size_t i=0;i<wave.size();++i){const float x=8+float(i)*(w-16)/std::max<size_t>(1,wave.size());s.line(x,top+height*(.5f-.45f*wave[i].at("maximum")[0].get<float>()),x,top+height*(.5f-.45f*wave[i].at("minimum")[0].get<float>()),0x70c9b9);}}}
      else s.uiText(L"No fresh prepared signal. Play the song, then refresh the port list.",18,top+12,w-36,0x9eafbf);
    }
  }
  void action(int id,unsigned notification) override;
  bool key(WPARAM key,bool ctrl,bool shift) override {if(key==VK_ESCAPE){hide();return true;}if(key==VK_RETURN&&GetFocus()&&GetDlgCtrlID(GetFocus())>=groupCreate){wchar_t klass[16]{};GetClassNameW(GetFocus(),klass,16);if(_wcsicmp(klass,L"Button")==0){action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}}return false;}
  void timer(UINT_PTR id)override {if(id!=3||!visible()||page_!=6||pending_||!available())return;try{scope_=call("graph.scope.get",{{"spectrum",spectrum_}},false);requestPaint();}catch(const std::exception &e){error_=e.what();KillTimer(window_,3);layout();}}
  void error(const std::exception &e)override{error_=e.what();layout();}
public:
  GraphWorkflowWindow(HWND owner,Callbacks callbacks);
  void open(){if(captured_.empty())load(true);show();if(page_==6)SetTimer(window_,3,120,nullptr);}
  void hide()override{KillTimer(window_,3);NativeToolWindow::hide();}
  void update(){if(visible()&&!pending_){const auto c=callbacks_.context();if(c.value("revision",std::string())!=observedRevision_||c.value("fieldDraft",false)!=observedDraft_){observedRevision_=c.value("revision",std::string());observedDraft_=c.value("fieldDraft",false);layout();}}}
  Json snapshot()const;
  void openSongSource(std::string source,std::string plugin,uint32_t parameter);
};
}
#include "GraphWorkflowModel.inc"
#include "GraphWorkflowActions.inc"
