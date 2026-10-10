#pragma once
#include "NativeToolWindow.hpp"
#include "NativeWriteCompletion.hpp"

namespace ScreamSeq {
// Retains an instrument identity independently of the pattern cursor and rack
// selection. Assignment never edits the instrument's sample keymap.
class InstrumentPluginWindow final : public NativeToolWindow {
  using Json=Api::Json;
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
  enum : int {plugin=4801,channel,apply,reload,browse,rack,close,
    heading=4850,pluginLabel,channelLabel,explanation,statusLabel};
  Request request_;Context context_;NativeWriteCompletion::Write write_;
  std::function<void(bool)> navigate_;
  std::string document_,identity_,revision_,selected_;
  unsigned index_=0,midi_=1;Json plugins_=Json::array();
  bool dirty_=false,pending_=false,setting_=false;uint64_t generation_=0;
  NativeWriteCompletion completion_;
  void require(bool ok,const char *message)const{if(!ok)throw std::runtime_error(message);}
  void status(std::wstring text){status_=std::move(text);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  bool current()const{return context_()==std::pair(document_,revision_);}
  void load(){
    require(!pending_,"Wait for the assignment operation");const auto before=context_();
    require(before.first==document_,"The captured instrument belongs to another song");
    const auto token=generation_;const auto data=request_("document.get",Json::object());
    require(context_()==before&&token==generation_,"Song changed while reading / selection retained");
    const auto &instruments=data.at("instruments");
    const auto found=std::find_if(instruments.begin(),instruments.end(),[&](const auto &i){return i.at("id")==identity_;});
    require(found!=instruments.end(),"The captured instrument was removed");
    const auto slot=found->at("index").template get<unsigned>();
    Json choices=Json::array();std::string selected;unsigned midi=1;
    for(const auto &p:data.at("nativePlugins"))if(p.value("isInstrument",false)){
      choices.push_back(p);
      if(p.value("instrumentAssignments",Json::array()).empty()&&p.value("instrument",0u)==slot)selected=p.at("instanceID");
      for(const auto &a:p.value("instrumentAssignments",Json::array()))if(a.at("instrument")==slot){selected=p.at("instanceID");midi=a.at("channel");}
    }
    index_=slot;plugins_=std::move(choices);selected_=std::move(selected);midi_=midi;revision_=before.second;
    setting_=true;NativeInputGate::present(controls_.at(plugin),CB_RESETCONTENT,0,0);
    NativeInputGate::present(controls_.at(plugin),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"None — samples only"));
    int chosen=0;for(size_t i=0;i<plugins_.size();++i){const auto &p=plugins_[i];auto text=wide(p.at("name").get<std::string>()+"  / "+p.at("instanceID").get<std::string>());
      NativeInputGate::present(controls_.at(plugin),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(p.at("instanceID")==selected_)chosen=int(i)+1;}
    NativeInputGate::present(controls_.at(plugin),CB_SETCURSEL,chosen,0);NativeInputGate::present(controls_.at(channel),CB_SETCURSEL,midi_-1,0);setting_=false;
    set(heading,L"Instrument "+std::to_wstring(index_)+L" · "+wide(found->at("name").template get<std::string>()));
    dirty_=false;++generation_;completion_.finish();layout();
    status(L"Choose a plugin and Apply. Add plugin opens the library; return here to assign it.");
  }
  void commit(){
    require(!pending_&&!completion_.retained(),"Reload / review the previous assignment before applying again");
    require(current(),"Song changed / Reload before applying the captured assignment");
    const auto token=generation_;pending_=true;layout();
    try{
      completion_.submit(write_,"instrument.plugin.set",{{"instrument",index_},{"plugin",selected_},{"channel",midi_},{"expectedRevision",revision_}},document_,token,{{"instrument",identity_},{"plugin",selected_},{"channel",midi_}});
      pending_=false;const auto returned=completion_.returned();
      require(returned&&context_()==std::pair(document_,returned->revision)&&token==generation_,"Assignment result retained / Reload to inspect the current song");
      load();status(L"Plugin assignment saved / sample mapping preserved / Undo restores the previous assignment");
    }catch(...){pending_=false;layout();throw;}
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;if(id==close&&notification==BN_CLICKED){hide();return;}if(pending_)return;
    if(id==reload&&notification==BN_CLICKED){request_("synchronizeView",Json::object());load();return;}
    require(!completion_.retained(),"Reload / review the previous result first");
    if(notification==CBN_SELCHANGE&&(id==plugin||id==channel)){
      const auto choice=int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));if(choice<0)return;
      if(id==plugin){require(size_t(choice)<=plugins_.size(),"Choose an available instrument plugin");selected_=choice?plugins_[size_t(choice-1)].at("instanceID").get<std::string>():std::string{};}
      else midi_=unsigned(choice+1);
      dirty_=true;++generation_;status(L"Assignment draft / Apply saves / Reload discards");layout();return;
    }
    if(notification!=BN_CLICKED)return;
    if(id==apply)commit();else if(id==browse||id==rack){require(bool(navigate_),"Plugin navigation is unavailable");navigate_(id==browse);}
  }
  void timer(UINT_PTR id)override{
    if(id!=1||!visible()||pending_||dirty_||completion_.retained()||current()||context_().first!=document_)return;
    // This reads the adopted UI snapshot only; no worker polling or focus moves.
    try{load();}catch(const std::exception &e){KillTimer(window_,1);error(e);}
  }
  bool key(WPARAM k,bool ctrl,bool)override{
    if(k==VK_ESCAPE){hide();return true;}if(ctrl&&k==VK_RETURN){commit();return true;}
    if(ctrl&&k=='R'){action(reload,BN_CLICKED);return true;}
    if(k==VK_RETURN){wchar_t type[32]{};GetClassNameW(GetFocus(),type,32);if(!_wcsicmp(type,L"Button")){action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}}return false;
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();place(heading,16,12,w-32,24);
    place(pluginLabel,16,48,w-156,20);place(channelLabel,w-124,48,108,20);
    place(plugin,16,72,w-156,240);place(channel,w-124,72,108,260);
    place(browse,16,112,142,28);place(rack,166,112,142,28);
    place(explanation,16,154,w-32,66);place(apply,16,h-76,100,28);place(reload,124,h-76,158,28);place(close,w-116,h-76,100,28);place(statusLabel,16,h-40,w-32,34);
    for(int id:{plugin,channel,apply,browse,rack})EnableWindow(controls_.at(id),!pending_&&!completion_.retained());
    EnableWindow(controls_.at(channel),!pending_&&!completion_.retained()&&!selected_.empty());EnableWindow(controls_.at(reload),!pending_);
    set(reload,completion_.retained()?L"Reload / review result":L"Reload / discard");
  }
public:
  InstrumentPluginWindow(HWND owner,std::string document,std::string instrument,Request request,Context context,NativeWriteCompletion::Write write,std::function<void(bool)> navigate)
    :NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),write_(std::move(write)),navigate_(std::move(navigate)),document_(std::move(document)),identity_(std::move(instrument)){
    minimumClientWidth_=460;minimumClientHeight_=310;create(L"ScreamSeq.InstrumentPlugin",L"Instrument plugin",640,360,true);
    combo(plugin);combo(channel);for(unsigned i=1;i<=16;++i){auto text=std::to_wstring(i);NativeInputGate::present(controls_.at(channel),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{apply,L"Apply"},{reload,L"Reload / discard"},{browse,L"Add plugin…"},{rack,L"Plugin rack…"},{close,L"Close"}})button(id,text);
    label(heading,L"Instrument plugin");label(pluginLabel,L"Instrument plugin in this song");label(channelLabel,L"MIDI channel");label(statusLabel,L"");
    label(explanation,L"The plugin and mapped samples play together. Samples use the instrument keymap and track routing; plugin audio uses its output routes. Choose None to keep only the samples.");finish();load();
  }
  void show(){NativeToolWindow::show();SetTimer(window_,1,250,nullptr);}
  void hide()override{KillTimer(window_,1);NativeToolWindow::hide();}
  bool matches(const std::string &document,const std::string &identity)const{return document_==document&&identity_==identity;}
  bool retainedDraft()const{return pending_||dirty_||completion_.retained();}
  std::optional<Tracker::DocumentDraft> documentDraft()const override{return describeDraft(document_,revision_,identity_,generation_,dirty_||completion_.retained(),pending_,completion_.retained());}
  Json snapshot()const{return {{"visible",visible()},{"document",document_},{"instrument",identity_},{"index",index_},{"revision",revision_},{"plugin",selected_},{"channel",midi_},{"dirty",dirty_},{"pending",pending_},{"stale",!current()},{"completion",completion_.snapshot()},{"plugins",plugins_},{"status",utf8(status_)}};}
};
}
