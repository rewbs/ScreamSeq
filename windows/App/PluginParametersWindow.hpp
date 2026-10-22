#pragma once
#include "NativeToolWindow.hpp"
#include "NativeWriteCompletion.hpp"
#include "PluginParameterField.hpp"
#include "../Audio/HostClock.hpp"
#include <set>

namespace ScreamSeq {
// Independent, modeless editor for one stable rack or graph recipe identity. Graph navigation
// cannot retarget its text, and document replacement retires it via the registry.
class PluginParametersWindow final : public NativeToolWindow {
  using Json=Api::Json;
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
  enum:int {parameter=3950,reload=3955,close=3958,heading=3959,statusLabel=3961,previous,next,applyAll,search,rowBase=4000,stride=10,pageSize=10};
  enum Part:int {nameLabel,slider,value,choices,toggle,apply,rangeLabel,less,more};
  struct Row {int index=-1;PluginParameterField field;bool dirty=false;};
  std::array<Row,pageSize> rows_;
  static int control(int row,Part part){return rowBase+row*stride+part;}
  bool dirty()const{return std::any_of(rows_.begin(),rows_.end(),[](const auto &r){return r.dirty;});}
  std::vector<size_t> filtered_;
  std::wstring appliedSearch_;
  int pages()const{return std::max(1,int((filtered_.size()+pageSize-1)/pageSize));}
  Request request_;Context context_;NativeWriteCompletion::Write write_;std::function<bool()> available_;
  std::function<bool()> recording_;
  std::function<void(uint32_t,double,uint64_t)> capture_;
  bool recording()const{return recording_&&recording_();}
  void recordControl(int row,double candidate,uint64_t timestamp,bool typed=false) {
    auto &r=rows_.at(size_t(row));const auto &f=r.field;
    if(!capture_||!f.editable()||!std::isfinite(candidate)||candidate<f.minimum||candidate>f.maximum||
        (f.kind!=PluginParameterField::Kind::Number&&std::floor(candidate)!=candidate))
      throw std::runtime_error("Enter a writable parameter value in the displayed range");
    const auto id=parameters_.at(size_t(r.index)).at("id").get<uint32_t>();
    r.dirty=true;
    capture_(id,candidate,timestamp); // Enqueue only; never pump or await here.
    setting_=true;struct ResetSetting{bool &value;~ResetSetting(){value=false;}}resetSetting{setting_};
    // The retained event keeps its original double value and timestamp. The
    // host consumes a float scalar; display that scalar without its binary
    // promotion tail, and never rewrite a musician's typed text.
    if(!typed){const auto scalar=std::abs(candidate)<=std::numeric_limits<float>::max()?double(static_cast<float>(candidate)):candidate;
      set(control(row,value),wide(PluginParameterField::editableText(
        std::isfinite(scalar)&&scalar>=f.minimum&&scalar<=f.maximum?scalar:candidate,f.minimum,f.maximum)));}
    if(f.kind==PluginParameterField::Kind::Toggle)set(control(row,toggle),wide(f.choices.at(size_t(candidate))));
    else if(f.kind==PluginParameterField::Kind::Choice)NativeInputGate::present(controls_.at(control(row,choices)),CB_SETCURSEL,WPARAM(candidate));
    else if(f.maximum>f.minimum)NativeInputGate::present(controls_.at(control(row,slider)),TBM_SETPOS,TRUE,LPARAM(std::lround((candidate-f.minimum)/(f.maximum-f.minimum)*10000)));
    setting_=false;
    parameters_[size_t(r.index)]["value"]=candidate;parameters_[size_t(r.index)]["manualValue"]=candidate;
    r.field.value=candidate;r.dirty=false;++generation_;
    status(L"Recording original parameter input / Stop the take to review");
  }
  std::string plugin_,graph_,node_,document_,revision_;Json parameters_=Json::array(),report_,submitted_;
  Json target()const{return graph_.empty()?Json{{"plugin",plugin_}}:Json{{"graph",graph_},{"node",node_}};}
  const char *writeMethod()const{return graph_.empty()?"plugin.parameters.set":"graph.plugin.set";}
  int page_=0,sliderRow_=-1;bool sliderEndPending_=false,sliderCancelPending_=false;uint64_t generation_=0;
  std::string previewToken_;uint32_t previewParameter_=0;
  bool setting_=false,pending_=false,needsReload_=false,takeRefreshPending_=false;HWND pendingFocus_{},navigationFocus_{};
  NativeWriteCompletion completion_;
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(document_,revision_,graph_.empty()?plugin_:target().dump(),generation_,dirty(),pending_,completion_.retained()&&!pending_);
  }
  void status(const std::wstring &text){status_=text;set(statusLabel,text);}
  void error(const std::exception &e)override{status(wide(e.what())+(completion_.retained()?L" / Review result before another edit":L""));layout();}
  void requireReady()const{if(pending_||completion_.retained()||needsReload_||takeRefreshPending_)throw std::runtime_error("Review or reload the previous parameter result before editing");}
  std::string previewMethod(const char *operation)const{return std::string(graph_.empty()?"plugin.parameters.preview.":"graph.plugin.preview.")+operation;}
  Json previewIdentity(const std::string &revision)const{
    auto p=target();p["expectedRevision"]=revision;p["parameter"]=previewParameter_;p["gesture"]=previewToken_;return p;
  }
  bool ownsPreview(const Json &owner)const{
    if(!owner.at("active").get<bool>()||owner.at("gesture")!=previewToken_||owner.at("parameter")!=previewParameter_)return false;
    return graph_.empty()?owner.at("plugin")==plugin_:owner.at("graph")==graph_&&owner.at("node")==node_;
  }
  void cancelPreview(){
    if(previewToken_.empty())return;
    if(pending_)throw std::runtime_error("Wait for the current preview request before cancelling");
    const auto current=context_();
    if(current.first!=document_)throw std::runtime_error("Original preview song is unavailable; review before closing");
    pending_=true;
    try{
      const auto owner=request_(previewMethod("get"),Json::object());
      if(context_()!=current)throw std::runtime_error("Song changed while checking preview ownership / retry Reload");
      if(ownsPreview(owner))request_(previewMethod("cancel"),previewIdentity(current.second));
    }
    catch(...){pending_=false;throw;}
    pending_=false;
    previewToken_.clear();
  }
  void previewSlider(int row,double value){
    if(context_()!=std::pair(document_,revision_))throw std::runtime_error("Song changed; slider draft retained. Reload before editing");
    if(previewToken_.empty()){
      previewToken_="slider:"+std::to_string(reinterpret_cast<uintptr_t>(window_))+":"+std::to_string(generation_);
      previewParameter_=parameters_.at(size_t(rows_[row].index)).at("id").get<uint32_t>();
    }
    pending_=true;
    try{
      auto p=previewIdentity(revision_);p["value"]=value;
      const auto result=request_(previewMethod("set"),p);
      pending_=false;status(result.at("audible").get<bool>()?L"Auditioning value / release saves once / Escape restores":L"Release saves this value / Escape restores");
    }catch(...){
      // A rejected/uncertain preview must not turn release into an ordinary
      // durable write. Keep input gated while reconciling its transient owner.
      pending_=true;needsReload_=true;const auto failure=std::current_exception();
      try{const auto observed=request_(previewMethod("get"),Json::object());
        if(!ownsPreview(observed))previewToken_.clear();
      }catch(...){} // Unknown preview ownership remains explicit until Reload.
      pending_=false;
      std::rethrow_exception(failure);
    }
  }
  double sliderValue(int row)const{
    const auto &f=rows_[row].field;
    const auto position=std::clamp(LONG(SendMessageW(controls_.at(control(row,slider)),TBM_GETPOS,0,0)),0L,10000L);
    auto value=f.minimum+(f.maximum-f.minimum)*position/10000.;
    if(f.step>0)value=std::clamp(f.minimum+std::round((value-f.minimum)/f.step)*f.step,f.minimum,f.maximum);
    return value;
  }
  void cancelSlider(){
    cancelPreview();sliderRow_=-1;sliderEndPending_=sliderCancelPending_=false;ReleaseCapture();++generation_;presentPage();
    status(needsReload_?L"Slider cancelled / Reload values before editing":L"Slider cancelled / saved parameter restored");
  }
  void finishSlider(){
    if(sliderCancelPending_){cancelSlider();return;}
    if(sliderRow_<0)return;requireReady();const auto row=sliderRow_;
    // The worker wait may pump the final thumb position/release. Read the
    // control again so a deferred release commits the actual final position.
    setting_=true;set(control(row,value),Json(sliderValue(row)));setting_=false;
    rows_[row].dirty=true;++generation_;sliderRow_=-1;sliderEndPending_=false;commit();
  }
  void presentPage(){
    setting_=true;
    for(int i=0;i<pageSize;++i){auto &r=rows_[i];r={};const auto filteredPosition=size_t(page_*pageSize+i);if(filteredPosition>=filtered_.size())continue;const auto index=int(filtered_[filteredPosition]);
      r.index=index;r.field=PluginParameterField::read(parameters_[size_t(index)]);const auto &f=r.field;
      set(control(i,nameLabel),wide(f.name));set(control(i,value),f.valid?wide(PluginParameterField::editableText(f.value,f.minimum,f.maximum)):L"");
      const auto picker=controls_.at(control(i,choices));NativeInputGate::present(picker,CB_RESETCONTENT,0,0);
      for(const auto &name:f.choices){const auto label=wide(name);NativeInputGate::present(picker,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
      if(f.valid&&f.kind!=PluginParameterField::Kind::Number)NativeInputGate::present(picker,CB_SETCURSEL,WPARAM(f.value),0);
      set(control(i,toggle),f.valid&&f.kind==PluginParameterField::Kind::Toggle?wide(f.choices[size_t(f.value)]):L"Unavailable");
      set(control(i,rangeLabel),f.valid?wide(std::string(f.writable?"":"Read only · ")+PluginParameterField::text(f.minimum)+" … "+PluginParameterField::text(f.maximum)+" "+f.unit):L"Unavailable metadata");
      const auto position=f.valid&&f.maximum>f.minimum?int(std::round((f.value-f.minimum)/(f.maximum-f.minimum)*10000)):0;
      NativeInputGate::present(controls_.at(control(i,slider)),TBM_SETPOS,TRUE,position);
      for(auto part:{slider,value,choices,toggle}){const auto label=wide(f.name+(part==slider?" slider":" manual value"));NativeAccessibility::name(controls_.at(control(i,part)),label.c_str());}
      for(auto part:{less,more}){const auto label=wide(std::string(part==less?"Decrease ":"Increase ")+f.name);accessibleName(control(i,part),label.c_str());}
    }
    NativeInputGate::present(controls_.at(parameter),CB_SETCURSEL,page_,0);setting_=false;layout();
  }
  void filterParameters(bool resetPage){
    const auto query=field(search);std::vector<size_t> next;next.reserve(parameters_.size());
    for(size_t i=0;i<parameters_.size();++i){const auto name=wide(parameters_[i].at("name").get<std::string>());
      if(query.empty()||FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,FIND_FROMSTART|NORM_IGNORECASE|NORM_IGNORENONSPACE,
          name.data(),int(name.size()),query.data(),int(query.size()),nullptr,nullptr,nullptr,0)>=0)next.push_back(i);
    }
    // Typing another character often leaves the same ten visible matches.
    // Keep their native controls intact instead of resetting sliders/pickers.
    if(resetPage&&page_==0&&next==filtered_){appliedSearch_=query;return;}
    filtered_=std::move(next);appliedSearch_=query;page_=resetPage?0:std::min(page_,pages()-1);
    setting_=true;NativeInputGate::present(controls_.at(parameter),CB_RESETCONTENT,0,0);
    for(int i=0;i<pages();++i){const auto text=filtered_.empty()?L"No matching parameters":
      L"Page "+std::to_wstring(i+1)+L" / "+std::to_wstring(pages())+L"  ·  "+std::to_wstring(i*pageSize+1)+L"–"+std::to_wstring(std::min(filtered_.size(),size_t((i+1)*pageSize)));
      NativeInputGate::present(controls_.at(parameter),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
    setting_=false;presentPage();
  }
  Json readParameters(){
    auto data=request_(graph_.empty()?"plugin.parameters.get":"graph.plugin.get",target());
    if(!graph_.empty())data=data.at("parameters").get<Json>();
    if(!data.is_array()||data.size()>4096)throw std::runtime_error("Invalid plugin parameter catalogue");
    std::set<uint32_t> ids;for(const auto &p:data){(void)p.at("name").get<std::string>();const auto &id=p.at("id");if(!id.is_number_integer()||id.get<double>()<0||id.get<double>()>UINT32_MAX||!ids.insert(id.get<uint32_t>()).second)throw std::runtime_error("Invalid or duplicate plugin parameter identity");}
    return data;
  }
  void load(bool completing=false){
    cancelAutomaticEdit();
    if(!completing&&completion_.retained())throw std::runtime_error("Review the parameter result first");
    if(!completing)cancelPreview();
    const auto context=context_();if(context.first!=document_)throw std::runtime_error("This parameter editor belongs to another song");
    const auto generation=generation_;pending_=true;layout();
    try{
      auto data=readParameters();if(context_()!=context||generation_!=generation)throw std::runtime_error("Song or parameter draft changed during read / fields retained");
      parameters_=std::move(data);revision_=context.second;needsReload_=takeRefreshPending_=false;sliderRow_=-1;sliderEndPending_=sliderCancelPending_=false;++generation_;
      filterParameters(false);status(L"Changes save automatically; sliders save on release. Ctrl+F finds a parameter.");
    }catch(...){setting_=pending_=false;layout();throw;}pending_=false;layout();
  }
  void finishResult(){
    const auto result=completion_.returned();if(!result)throw std::runtime_error("Parameter result is uncertain / use Review result");
    const auto report=Json{{"outcome","returned"},{"submission",submitted_},{"result",result->result}};
    previewToken_.clear();
    if(context_()==std::pair(result->document,result->revision)&&generation_==completion_.generation())load(true);
    report_=report;completion_.finish();status(dirty()?L"Earlier edit completed / newer fields retained":L"Parameter applied / one Undo restores it");
  }
  void queueTypedEdit(){queueAutomaticEdit([this]{
    if(pending_||(available_&&!available_())){queueTypedEdit();return;}
    if(sliderRow_<0)commit();
  });}
  void commit(){
    cancelAutomaticEdit();requireReady();if(!dirty())return;
    // Valid recording input was already submitted at its original event time.
    // A retained partial/failed edit must never become a later manual write.
    if(recording())throw std::runtime_error("Correct the retained value while recording; it cannot be retried with a new timestamp");
    if(context_()!=std::pair(document_,revision_))throw std::runtime_error("Song changed / parameter text retained. Reload before applying");
    Json values=Json::array(),raw=Json::array();
    for(int i=0;i<pageSize;++i)if(rows_[i].dirty){const auto &r=rows_[i];const auto &f=r.field;if(!f.editable()||r.index<0)throw std::runtime_error("This parameter is read-only or unavailable");
      const auto v=number(control(i,value));if(v<f.minimum||v>f.maximum||(f.kind!=PluginParameterField::Kind::Number&&std::floor(v)!=v))throw std::runtime_error("Enter a value in the displayed range for "+f.name);
      const auto id=parameters_[size_t(r.index)].at("id");values.push_back({{"id",id},{"value",v}});raw.push_back({{"id",id},{"value",utf8(field(control(i,value)))}});
    }
    const bool preview=!previewToken_.empty();
    if(preview&&values.size()!=1)throw std::runtime_error("Finish the captured slider before editing other parameters");
    submitted_=target();submitted_["expectedRevision"]=revision_;
    if(preview){submitted_["gesture"]=previewToken_;submitted_["parameter"]=previewParameter_;submitted_["value"]=values[0].at("value");}
    else submitted_[graph_.empty()?"values":"parameters"]=std::move(values);
    const auto generation=generation_;pending_=true;layout();
    try{completion_.submit(write_,preview?previewMethod("commit"):writeMethod(),submitted_,document_,generation,raw);pending_=false;finishResult();}
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
  void changePage(int page){
    NativeInputGate::present(controls_.at(parameter),CB_SETCURSEL,page_,0);
    if(dirty()){flushAutomaticEdit();if(dirty())throw std::runtime_error("Correct or discard the unsaved values before changing pages");}
    if(page<0||page>=pages())return;page_=page;++generation_;presentPage();
  }
  void action(int id,unsigned notification)override{
    const auto inputTime=hostTime100ns();
    if(setting_)return;if(id==close){hide();return;}
    const int row=(id-rowBase)/stride,part=(id-rowBase)%stride;
    const bool rowControl=id>=rowBase&&row<pageSize&&rows_[row].index>=0;
    if(rowControl&&part==value&&notification==EN_CHANGE){
      rows_[row].dirty=true;++generation_;
      if(recording()) {cancelAutomaticEdit();recordControl(row,number(id),inputTime,true);}
      else queueTypedEdit();return;
    }
    if(pending_)return;
    if(id==reload&&notification==BN_CLICKED){if(recording())throw std::runtime_error("Stop and finish the parameter take before reloading its controls");if(completion_.retained())review();else load();return;}requireReady();
    if(id==search&&notification==EN_CHANGE){
      if(dirty()){flushAutomaticEdit();if(dirty())throw std::runtime_error("Correct or reload unsaved values before searching / current page retained");}
      ++generation_;filterParameters(true);status(std::to_wstring(filtered_.size())+L" of "+std::to_wstring(parameters_.size())+L" parameters / Ctrl+Page Up or Down changes page");return;
    }
    if(id==parameter&&notification==CBN_SELCHANGE){changePage(int(SendMessageW(controls_.at(parameter),CB_GETCURSEL,0,0)));return;}
    if(notification==BN_CLICKED&&(id==previous||id==next)){changePage(page_+(id==next?1:-1));return;}
    if(notification==BN_CLICKED&&(id==applyAll||(rowControl&&part==apply))){commit();return;}
    if(!rowControl)return;auto &r=rows_[row];const auto &f=r.field;
    if((part==choices&&notification==CBN_SELCHANGE)||((part==toggle||part==less||part==more)&&notification==BN_CLICKED)){
      if(dirty()){NativeInputGate::present(controls_.at(control(row,choices)),CB_SETCURSEL,WPARAM(f.value),0);throw std::runtime_error("Correct or discard unsaved values before using switches or steps");}
      if(!f.editable())throw std::runtime_error("Parameter is read only");double candidate=f.value;
      if(part==choices){const auto i=SendMessageW(controls_.at(id),CB_GETCURSEL,0,0);if(i<0||size_t(i)>=f.choices.size())throw std::runtime_error("Choose an available parameter value");candidate=double(i);}
      else if(part==toggle)candidate=f.value==0?1:0;
      else{const auto step=f.step>0?f.step:(f.maximum-f.minimum)/100;candidate=std::clamp(candidate+(part==more?step:-step),f.minimum,f.maximum);}
      if(recording()){recordControl(row,candidate,inputTime);return;}
      setting_=true;set(control(row,value),Json(candidate));setting_=false;r.dirty=true;++generation_;commit();
    }
  }
  bool controlScroll(UINT message,WPARAM event,HWND child)override{
    const auto inputTime=hostTime100ns();
    const int id=GetDlgCtrlID(child),row=(id-rowBase)/stride;
    if(message!=WM_HSCROLL||id<rowBase||row>=pageSize||(id-rowBase)%stride!=slider)return false;
    const auto code=LOWORD(event);
    if(setting_)return true;
    if(recording()) {
      if(code==TB_ENDTRACK)return true;
      if(dirty())throw std::runtime_error("Correct the retained parameter text before moving a slider");
      auto candidate=sliderValue(row);const auto &f=rows_[row].field;
      if(code==TB_LINEUP||code==TB_LINEDOWN){const auto step=f.step>0?f.step:(f.maximum-f.minimum)/100;candidate=std::clamp(f.value+(code==TB_LINEUP?-step:step),f.minimum,f.maximum);}
      recordControl(row,candidate,inputTime);return true;
    }
    if(pending_){if(code==TB_ENDTRACK&&sliderRow_==row){sliderEndPending_=true;SetTimer(window_,8,1,nullptr);}return true;}
    requireReady();auto &r=rows_[row];const auto &f=r.field;
    if(!f.editable()||f.kind!=PluginParameterField::Kind::Number||f.maximum<=f.minimum)return true;
    if(dirty()&&sliderRow_!=row){NativeInputGate::present(child,TBM_SETPOS,TRUE,int(std::round((f.value-f.minimum)/(f.maximum-f.minimum)*10000)));throw std::runtime_error("Correct or discard unsaved values before moving a slider");}
    if(code==TB_ENDTRACK){if(sliderRow_==row)finishSlider();return true;}
    auto candidate=sliderValue(row);
    if(code==TB_LINEUP||code==TB_LINEDOWN){const auto step=f.step>0?f.step:(f.maximum-f.minimum)/100;candidate=std::clamp(f.value+(code==TB_LINEUP?-step:step),f.minimum,f.maximum);}
    if(f.step>0)candidate=std::clamp(f.minimum+std::round((candidate-f.minimum)/f.step)*f.step,f.minimum,f.maximum);
    sliderRow_=row;setting_=true;set(control(row,value),Json(candidate));setting_=false;r.dirty=true;++generation_;
    if(code==TB_THUMBTRACK||code==TB_THUMBPOSITION){previewSlider(row,candidate);}
    else{sliderRow_=-1;commit();}return true;
  }
  void controlCaptureChanged(HWND child)override{if(sliderRow_>=0&&child==controls_.at(control(sliderRow_,slider)))SetTimer(window_,8,1,nullptr);}
  bool key(WPARAM key,bool ctrl,bool)override{
    if(key==VK_ESCAPE){if(pending_&&sliderRow_>=0){sliderCancelPending_=true;SetTimer(window_,8,1,nullptr);return true;}if(completion_.retained()){hide();return true;}if(sliderRow_>=0||!previewToken_.empty())cancelSlider();else hide();return true;}
    if(ctrl&&key=='R'){action(reload,BN_CLICKED);return true;}
    if(ctrl&&key=='F'){if(IsWindowEnabled(controls_.at(search))){SetFocus(controls_.at(search));NativeInputGate::present(controls_.at(search),EM_SETSEL,0,-1);}return true;}
    if(ctrl&&(key==VK_NEXT||key==VK_PRIOR)){requireReady();changePage(page_+(key==VK_NEXT?1:-1));return true;}
    if(key==VK_RETURN){const auto id=GetDlgCtrlID(GetFocus());if(id>=rowBase&&(id-rowBase)%stride==value){commit();return true;}wchar_t type[24]{};GetClassNameW(GetFocus(),type,24);if(!_wcsicmp(type,L"Button")){action(id,BN_CLICKED);return true;}}return false;
  }
  void timer(UINT_PTR id)override{
    if(id==8){if(pending_)return;KillTimer(window_,8);if(sliderCancelPending_)cancelSlider();else if(sliderRow_>=0&&(sliderEndPending_||!GetCapture()))finishSlider();return;}
    if(id!=7||(!visible()&&!takeRefreshPending_)||pending_||dirty()||completion_.retained()||needsReload_||(available_&&!available_())||(!takeRefreshPending_&&context_()==std::pair(document_,revision_)))return;
    try{load();}catch(const Api::ApiError &e){if(e.code!=-32002){needsReload_=true;throw;}}catch(...){needsReload_=true;throw;}
    if(!visible()&&!takeRefreshPending_)KillTimer(window_,7);
  }
  void layout()override{
    if(!ready_)return;const auto focus=GetFocus();if(pending_&&!pendingFocus_&&owns(focus))pendingFocus_=focus;
    const auto [w,h]=size();place(heading,12,10,w/2-24,24);place(search,w/2,8,w/2-12,26);place(previous,12,42,64,26);place(parameter,84,42,w-244,260);place(next,w-152,42,64,26);place(close,w-80,42,68,26);
    const bool ready=!pending_&&!completion_.retained()&&!needsReload_&&!takeRefreshPending_;const float rowHeight=std::max(48.f,std::min(60.f,(h-168)/float(pageSize)));
    EnableWindow(controls_.at(search),ready);
    for(int i=0;i<pageSize;++i){const auto &r=rows_[i];const auto &f=r.field;const bool visible=r.index>=0;const bool numeric=f.kind==PluginParameterField::Kind::Number;const float y=82+i*rowHeight;
      place(control(i,nameLabel),12,y,178,19,visible);place(control(i,rangeLabel),12,y+20,178,20,visible);
      place(control(i,slider),196,y+4,w-418,30,visible&&numeric);
      place(control(i,value),w-216,y+4,132,26,visible&&numeric);place(control(i,less),w-76,y+4,28,26,visible&&numeric);place(control(i,more),w-40,y+4,28,26,visible&&numeric);place(control(i,apply),w-50,y+4,38,26,false);
      place(control(i,choices),196,y+4,w-208,260,visible&&f.kind==PluginParameterField::Kind::Choice);place(control(i,toggle),196,y+4,w-208,26,visible&&f.kind==PluginParameterField::Kind::Toggle);
      for(auto part:{slider,value,choices,toggle,apply,less,more})EnableWindow(controls_.at(control(i,part)),ready&&visible&&f.editable()&&(part!=slider||f.maximum>f.minimum));
    }
    place(statusLabel,12,h-82,w-24,38);place(reload,12,h-36,134,26);place(applyAll,w-150,h-36,138,26,dirty()&&!pending_&&automaticEditFailed_);
    EnableWindow(controls_.at(previous),ready&&page_>0);EnableWindow(controls_.at(next),ready&&page_+1<pages());EnableWindow(controls_.at(parameter),ready);EnableWindow(controls_.at(applyAll),ready&&dirty());EnableWindow(controls_.at(reload),!pending_);set(reload,completion_.retained()?L"Review result":L"Reload values");
    if(!pending_&&pendingFocus_&&IsWindowEnabled(pendingFocus_)){const auto old=std::exchange(pendingFocus_,nullptr);if((!GetFocus()||GetFocus()==window_)&&IsWindowEnabled(old)&&IsWindowVisible(old)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(old);}
  }
public:
  bool retainedDraft()const{return dirty()||pending_||completion_.retained()||needsReload_||takeRefreshPending_||sliderRow_>=0||!previewToken_.empty();}
  void takeFinished(){
    if(!graph_.empty())return;
    // Discard can preserve the song revision. Still re-read the accepted
    // baseline before allowing another edit, without losing a partial number.
    takeRefreshPending_=true;cancelAutomaticEdit();
    // A closed editor must not leave an otherwise finished take blocking the
    // next Record forever. Schedule one guarded read even while hidden; dirty
    // or uncertain fields retain their existing protection and are not reset.
    if(!dirty()&&!pending_&&!completion_.retained()&&!needsReload_)SetTimer(window_,7,250,nullptr);
    status(dirty()?L"Take finished / unsaved text retained. Reload values to discard that text.":L"Take finished / refreshing accepted parameter values");
    // Keep the focused HWND enabled until the guarded reload begins. Its
    // normal pending/readback path already preserves and restores field focus.
  }
  void recording(std::function<bool()> active,std::function<void(uint32_t,double,uint64_t)> capture) {
    recording_=std::move(active);capture_=std::move(capture);
  }
  PluginParametersWindow(HWND owner,std::string plugin,std::string name,Request request,Context context,NativeWriteCompletion::Write write,std::function<bool()> available,std::string graph={},std::string node={}):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),write_(std::move(write)),available_(std::move(available)),plugin_(std::move(plugin)),graph_(std::move(graph)),node_(std::move(node)){
    if(graph_.empty()!=node_.empty()||plugin_.empty()==graph_.empty())throw std::invalid_argument("Parameter editor requires one rack identity or graph/node pair");
    const auto captured=context_();document_=captured.first;revision_=captured.second;minimumClientWidth_=620;minimumClientHeight_=650;
    const auto title=wide(name)+L" — Parameters";create(L"ScreamSeq.PluginParameters",title.c_str(),720,740);combo(parameter);edit(search,L"",256);
    SendMessageW(controls_.at(search),EM_SETCUEBANNER,FALSE,reinterpret_cast<LPARAM>(L"Find a parameter (Ctrl+F)"));
    accessibleName(search,L"Find plugin parameter");accessibleName(parameter,L"Parameter page");
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{reload,L"Reload values"},{close,L"Close"},{previous,L"< Prev"},{next,L"Next >"},{applyAll,L"Retry values"}})button(id,text);
    label(heading,wide(name).c_str());label(statusLabel,L"");
    for(int i=0;i<pageSize;++i){label(control(i,nameLabel),L"");label(control(i,rangeLabel),L"");edit(control(i,value),L"",64);combo(control(i,choices));button(control(i,toggle),L"Off");button(control(i,apply),L"Set");button(control(i,less),L"−");button(control(i,more),L"+");
      const auto track=add(control(i,slider),TRACKBAR_CLASSW,L"Parameter",TBS_HORZ|TBS_NOTICKS);NativeInputGate::present(track,TBM_SETRANGE,TRUE,MAKELPARAM(0,10000));NativeInputGate::present(track,TBM_SETLINESIZE,0,100);NativeInputGate::present(track,TBM_SETPAGESIZE,0,1000);}
    // Dialog navigation follows the visible page/header and row order. Hidden
    // variants are skipped by USER32; never rebuild controls while typing.
    const auto order=[&](int id){check(SetWindowPos(controls_.at(id),HWND_BOTTOM,0,0,0,0,
      SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE),"Order plugin parameter controls");};
    for(int id:{search,previous,parameter,next,close})order(id);
    for(int row=0;row<pageSize;++row)for(auto part:{slider,value,choices,toggle,less,more})order(control(row,part));
    order(reload);order(applyAll);
    focusPresentationChanged([this]{const auto focus=GetFocus();if(focus!=window_&&owns(focus))navigationFocus_=focus;});
    finish();load();
  }
  void show(){const auto focus=navigationFocus_;NativeToolWindow::show();SetTimer(window_,7,250,nullptr);
    SetFocus(focus&&owns(focus)&&IsWindowVisible(focus)&&IsWindowEnabled(focus)?focus:controls_.at(parameter));}
  void hide()override{if(sliderRow_>=0||!previewToken_.empty()){cancelPreview();sliderRow_=-1;sliderEndPending_=sliderCancelPending_=false;if(!completion_.retained()&&!needsReload_){++generation_;presentPage();}}
    KillTimer(window_,7);KillTimer(window_,8);NativeToolWindow::hide();
    if(takeRefreshPending_&&!dirty()&&!pending_&&!completion_.retained()&&!needsReload_)SetTimer(window_,7,250,nullptr);}
  Json snapshot()const{Json rows=Json::array();for(int i=0;i<pageSize;++i)if(rows_[i].index>=0)rows.push_back({{"index",rows_[i].index},{"id",parameters_[size_t(rows_[i].index)].at("id")},{"value",utf8(field(control(i,value)))},{"dirty",rows_[i].dirty},{"valueControl",control(i,value)},{"sliderControl",control(i,slider)}});
    return {{"visible",visible()},{"plugin",plugin_},{"graph",graph_},{"node",node_},{"document",document_},{"revision",revision_},{"parameters",parameters_},{"selected",filtered_.empty()?-1:int(filtered_.at(size_t(page_*pageSize)))},{"page",page_},{"pages",pages()},{"rows",rows},{"search",utf8(field(search))},{"appliedSearch",utf8(appliedSearch_)},{"matches",filtered_.size()},{"dirty",dirty()},{"pending",pending_},{"completion",completion_.snapshot()},{"report",report_},{"needsReload",needsReload_},{"status",utf8(status_)}};}
};
}
