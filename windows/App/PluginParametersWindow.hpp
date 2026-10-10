#pragma once
#include "NativeToolWindow.hpp"
#include "NativeWriteCompletion.hpp"
#include "PluginParameterField.hpp"
#include <set>

namespace ScreamSeq {
// Independent, modeless editor for one stable rack identity. Graph navigation
// cannot retarget its text, and document replacement retires it via the registry.
class PluginParametersWindow final : public NativeToolWindow {
  using Json=Api::Json;
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
  enum:int {parameter=3950,value,choices,toggle,apply,reload,less,more,close,heading,rangeLabel,statusLabel};
  Request request_;Context context_;NativeWriteCompletion::Write write_;std::function<bool()> available_;
  std::string plugin_,document_,revision_;Json parameters_=Json::array(),report_,submitted_;
  PluginParameterField field_;int selected_=-1;uint64_t generation_=0;
  bool setting_=false,pending_=false,dirty_=false,needsReload_=false;HWND pendingFocus_{};
  NativeWriteCompletion completion_;
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(document_,revision_,Json::array({plugin_,selected_>=0?parameters_[size_t(selected_)].at("id"):Json()}).dump(),generation_,dirty_,pending_,completion_.retained()&&!pending_);
  }
  void status(const std::wstring &text){status_=text;set(statusLabel,text);}
  void error(const std::exception &e)override{status(wide(e.what())+(completion_.retained()?L" / Review result before another edit":L""));layout();}
  void requireReady()const{if(pending_||completion_.retained()||needsReload_)throw std::runtime_error("Review or reload the previous parameter result before editing");}
  void presentValue(){
    setting_=true;field_=selected_>=0?PluginParameterField::read(parameters_[size_t(selected_)]):PluginParameterField{};
    set(value,field_.valid?wide(Json(field_.value).dump()):L"");
    NativeInputGate::present(controls_.at(choices),CB_RESETCONTENT,0,0);
    for(const auto &name:field_.choices){const auto label=wide(name);NativeInputGate::present(controls_.at(choices),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
    if(field_.valid&&field_.kind!=PluginParameterField::Kind::Number)NativeInputGate::present(controls_.at(choices),CB_SETCURSEL,WPARAM(field_.value),0);
    set(toggle,field_.valid&&field_.kind==PluginParameterField::Kind::Toggle?wide(field_.name+": "+field_.choices[size_t(field_.value)]):L"Off");
    set(rangeLabel,field_.valid?wide(std::string(field_.writable?"Manual value: ":"Read only: ")+Json(field_.minimum).dump()+" to "+Json(field_.maximum).dump()+" "+field_.unit):L"No editable parameter metadata");
    const auto name=wide(field_.name)+L" manual value";for(int id:{value,choices})NativeAccessibility::name(controls_.at(id),name.c_str());
    setting_=false;layout();
  }
  Json readParameters(){
    auto data=request_("plugin.parameters.get",{{"plugin",plugin_}});
    if(!data.is_array()||data.size()>4096)throw std::runtime_error("Invalid plugin parameter catalogue");
    std::set<uint32_t> ids;for(const auto &p:data){(void)p.at("name").get<std::string>();const auto &id=p.at("id");if(!id.is_number_integer()||id.get<double>()<0||id.get<double>()>UINT32_MAX||!ids.insert(id.get<uint32_t>()).second)throw std::runtime_error("Invalid or duplicate plugin parameter identity");}
    return data;
  }
  void load(bool completing=false){
    if(!completing&&completion_.retained())throw std::runtime_error("Review the parameter result first");
    const auto context=context_();if(context.first!=document_)throw std::runtime_error("This parameter editor belongs to another song");
    const auto generation=generation_;const auto identity=selected_>=0?parameters_[size_t(selected_)].at("id"):Json();pending_=true;layout();
    try{
      auto data=readParameters();if(context_()!=context||generation_!=generation)throw std::runtime_error("Song or parameter draft changed during read / fields retained");
      int selected=data.empty()?-1:0;for(size_t i=0;i<data.size();++i)if(data[i].at("id")==identity)selected=int(i);
      parameters_=std::move(data);selected_=selected;revision_=context.second;dirty_=needsReload_=false;++generation_;
      setting_=true;NativeInputGate::present(controls_.at(parameter),CB_RESETCONTENT,0,0);
      for(const auto &p:parameters_){auto text=wide(p.at("name").get<std::string>());NativeInputGate::present(controls_.at(parameter),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
      NativeInputGate::present(controls_.at(parameter),CB_SETCURSEL,selected_,0);setting_=false;presentValue();
      status(L"Enter applies a value. This editor stays on its plugin while you edit the graph.");
    }catch(...){setting_=pending_=false;layout();throw;}pending_=false;layout();
  }
  void finishResult(){
    const auto result=completion_.returned();if(!result)throw std::runtime_error("Parameter result is uncertain / use Review result");
    const auto report=Json{{"outcome","returned"},{"submission",submitted_},{"result",result->result}};
    if(context_()==std::pair(result->document,result->revision)&&generation_==completion_.generation())load(true);
    report_=report;completion_.finish();status(dirty_?L"Earlier edit completed / newer fields retained":L"Parameter applied / one Undo restores it");
  }
  void commit(){
    requireReady();if(!field_.editable()||selected_<0)throw std::runtime_error("This parameter is read-only or unavailable");
    if(context_()!=std::pair(document_,revision_))throw std::runtime_error("Song changed / parameter text retained. Reload before applying");
    const auto numberValue=number(value);if(numberValue<field_.minimum||numberValue>field_.maximum||(field_.kind!=PluginParameterField::Kind::Number&&std::floor(numberValue)!=numberValue))throw std::runtime_error("Enter a value in the displayed parameter range");
    submitted_={{"plugin",plugin_},{"expectedRevision",revision_},{"values",Json::array({{{"id",parameters_[size_t(selected_)].at("id")},{"value",numberValue}}})}};
    const auto generation=generation_;pending_=true;layout();
    try{completion_.submit(write_,"plugin.parameters.set",submitted_,document_,generation,{{"value",utf8(field(value))}});pending_=false;finishResult();}
    catch(...){pending_=false;layout();throw;}layout();
  }
  void review(){
    if(pending_)return;pending_=true;layout();
    try{
      request_("synchronizeView",Json::object());pending_=false;
      if(completion_.returned())finishResult();
      else{
        const auto context=context_();const auto generation=generation_;if(context.first!=document_)throw std::runtime_error("Original plugin song is unavailable / result retained");
        pending_=true;auto observed=readParameters();if(context_()!=context||generation_!=generation)throw std::runtime_error("Song or fields changed during Review / result retained");
        report_={{"outcome","unverified"},{"submission",submitted_},{"observed",std::move(observed)}};needsReload_=true;completion_.finish();status(L"Current values inspected / original outcome unverified. Reload before editing.");
      }
    }catch(...){pending_=false;layout();throw;}pending_=false;layout();
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;if(id==close){hide();return;}
    if(id==value&&notification==EN_CHANGE){dirty_=true;++generation_;return;}
    if(pending_)return;
    if(id==reload&&notification==BN_CLICKED){if(completion_.retained())review();else load();return;}
    requireReady();
    if(id==parameter&&notification==CBN_SELCHANGE){
      const auto chosen=SendMessageW(controls_.at(parameter),CB_GETCURSEL,0,0);
      if(dirty_){NativeInputGate::present(controls_.at(parameter),CB_SETCURSEL,selected_,0);throw std::runtime_error("Apply or reload this parameter's text before choosing another parameter");}
      if(chosen>=0&&size_t(chosen)<parameters_.size()){selected_=int(chosen);++generation_;presentValue();}return;
    }
    if((id==choices&&notification==CBN_SELCHANGE)||(id==toggle&&notification==BN_CLICKED)||((id==less||id==more)&&notification==BN_CLICKED)){
      if(dirty_){NativeInputGate::present(controls_.at(choices),CB_SETCURSEL,WPARAM(field_.value),0);throw std::runtime_error("Apply the typed value before using parameter steps");}
      if(!field_.editable())throw std::runtime_error("Parameter is read only");
      double next=field_.value;
      if(id==choices){const auto i=SendMessageW(controls_.at(choices),CB_GETCURSEL,0,0);if(i<0||size_t(i)>=field_.choices.size())throw std::runtime_error("Choose an available parameter value");next=double(i);}
      else if(id==toggle)next=field_.value==0?1:0;
      else{const auto step=field_.step>0?field_.step:(field_.maximum-field_.minimum)/100;next=std::clamp(next+(id==more?step:-step),field_.minimum,field_.maximum);}
      setting_=true;set(value,Json(next));setting_=false;dirty_=true;++generation_;commit();return;
    }
    if(id==apply&&notification==BN_CLICKED)commit();
  }
  bool key(WPARAM key,bool ctrl,bool)override{
    if(key==VK_ESCAPE){hide();return true;}
    if(ctrl&&key=='R'){action(reload,BN_CLICKED);return true;}
    if(key==VK_RETURN){if(GetFocus()==controls_.at(value)){commit();return true;}wchar_t type[24]{};GetClassNameW(GetFocus(),type,24);if(!_wcsicmp(type,L"Button")){action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}}return false;
  }
  void timer(UINT_PTR id)override{
    if(id!=7||!visible()||pending_||dirty_||completion_.retained()||needsReload_||(available_&&!available_())||context_()==std::pair(document_,revision_))return;
    try{load();}catch(const Api::ApiError &e){if(e.code!=-32002)throw;}
  }
  void layout()override{
    if(!ready_)return;const auto focus=GetFocus();if(pending_&&!pendingFocus_&&owns(focus))pendingFocus_=focus;
    const auto [w,h]=size();place(heading,12,12,w-24,28);place(parameter,12,52,w-24,260);
    const bool numeric=field_.kind==PluginParameterField::Kind::Number;
    place(value,12,96,w-176,27,numeric);place(choices,12,96,w-24,240,field_.kind==PluginParameterField::Kind::Choice);place(toggle,12,96,w-24,27,field_.kind==PluginParameterField::Kind::Toggle);
    place(less,w-156,96,34,27,numeric);place(more,w-118,96,34,27,numeric);place(apply,w-80,96,68,27,numeric);
    place(rangeLabel,12,134,w-24,42);place(statusLabel,12,184,w-24,std::max(44.f,h-232));place(reload,12,h-38,134,26);place(close,w-88,h-38,76,26);
    const bool ready=!pending_&&!completion_.retained()&&!needsReload_;for(int id:{value,choices,toggle,apply,less,more})EnableWindow(controls_.at(id),ready&&field_.editable());
    EnableWindow(controls_.at(parameter),ready);EnableWindow(controls_.at(reload),!pending_);set(reload,completion_.retained()?L"Review result":L"Reload values");
    if(!pending_&&pendingFocus_&&IsWindowEnabled(pendingFocus_)){const auto old=std::exchange(pendingFocus_,nullptr);if((!GetFocus()||GetFocus()==window_)&&IsWindowEnabled(old)&&IsWindowVisible(old)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(old);}
  }
public:
  PluginParametersWindow(HWND owner,std::string plugin,std::string name,Request request,Context context,NativeWriteCompletion::Write write,std::function<bool()> available):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),write_(std::move(write)),available_(std::move(available)),plugin_(std::move(plugin)){
    const auto captured=context_();document_=captured.first;revision_=captured.second;minimumWidth_=480;minimumHeight_=310;
    const auto title=wide(name)+L" — Parameters";create(L"ScreamSeq.PluginParameters",title.c_str(),540,330);combo(parameter);combo(choices);edit(value,L"",64);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{toggle,L"Off"},{apply,L"Apply"},{reload,L"Reload values"},{less,L"−"},{more,L"+"},{close,L"Close"}})button(id,text);
    label(heading,wide(name).c_str());label(rangeLabel,L"");label(statusLabel,L"");finish();load();
  }
  void show(){NativeToolWindow::show();SetTimer(window_,7,250,nullptr);SetFocus(controls_.at(parameter));}
  void hide()override{KillTimer(window_,7);NativeToolWindow::hide();}
  Json snapshot()const{return {{"visible",visible()},{"plugin",plugin_},{"document",document_},{"revision",revision_},{"parameters",parameters_},{"selected",selected_},{"value",utf8(field(value))},{"dirty",dirty_},{"pending",pending_},{"completion",completion_.snapshot()},{"report",report_},{"needsReload",needsReload_}};}
};
}
