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
  enum:int {parameter=3950,reload=3955,close=3958,heading=3959,statusLabel=3961,previous,next,applyAll,rowBase=4000,stride=10,pageSize=10};
  enum Part:int {nameLabel,slider,value,choices,toggle,apply,rangeLabel,less,more};
  struct Row {int index=-1;PluginParameterField field;bool dirty=false;};
  std::array<Row,pageSize> rows_;
  static int control(int row,Part part){return rowBase+row*stride+part;}
  bool dirty()const{return std::any_of(rows_.begin(),rows_.end(),[](const auto &r){return r.dirty;});}
  int pages()const{return std::max(1,int((parameters_.size()+pageSize-1)/pageSize));}
  Request request_;Context context_;NativeWriteCompletion::Write write_;std::function<bool()> available_;
  std::string plugin_,document_,revision_;Json parameters_=Json::array(),report_,submitted_;
  int page_=0,sliderRow_=-1;uint64_t generation_=0;
  bool setting_=false,pending_=false,needsReload_=false;HWND pendingFocus_{};
  NativeWriteCompletion completion_;
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(document_,revision_,plugin_,generation_,dirty(),pending_,completion_.retained()&&!pending_);
  }
  void status(const std::wstring &text){status_=text;set(statusLabel,text);}
  void error(const std::exception &e)override{status(wide(e.what())+(completion_.retained()?L" / Review result before another edit":L""));layout();}
  void requireReady()const{if(pending_||completion_.retained()||needsReload_)throw std::runtime_error("Review or reload the previous parameter result before editing");}
  void presentPage(){
    setting_=true;
    for(int i=0;i<pageSize;++i){auto &r=rows_[i];r={};const auto index=page_*pageSize+i;if(size_t(index)>=parameters_.size())continue;
      r.index=index;r.field=PluginParameterField::read(parameters_[size_t(index)]);const auto &f=r.field;
      set(control(i,nameLabel),wide(f.name));set(control(i,value),f.valid?wide(Json(f.value).dump()):L"");
      const auto picker=controls_.at(control(i,choices));NativeInputGate::present(picker,CB_RESETCONTENT,0,0);
      for(const auto &name:f.choices){const auto label=wide(name);NativeInputGate::present(picker,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
      if(f.valid&&f.kind!=PluginParameterField::Kind::Number)NativeInputGate::present(picker,CB_SETCURSEL,WPARAM(f.value),0);
      set(control(i,toggle),f.valid&&f.kind==PluginParameterField::Kind::Toggle?wide(f.choices[size_t(f.value)]):L"Unavailable");
      set(control(i,rangeLabel),f.valid?wide(std::string(f.writable?"":"Read only · ")+Json(f.minimum).dump()+" … "+Json(f.maximum).dump()+" "+f.unit):L"Unavailable metadata");
      const auto position=f.valid&&f.maximum>f.minimum?int(std::round((f.value-f.minimum)/(f.maximum-f.minimum)*10000)):0;
      NativeInputGate::present(controls_.at(control(i,slider)),TBM_SETPOS,TRUE,position);
      for(auto part:{slider,value,choices,toggle}){const auto label=wide(f.name+(part==slider?" slider":" manual value"));NativeAccessibility::name(controls_.at(control(i,part)),label.c_str());}
    }
    NativeInputGate::present(controls_.at(parameter),CB_SETCURSEL,page_,0);setting_=false;layout();
  }
  Json readParameters(){
    auto data=request_("plugin.parameters.get",{{"plugin",plugin_}});
    if(!data.is_array()||data.size()>4096)throw std::runtime_error("Invalid plugin parameter catalogue");
    std::set<uint32_t> ids;for(const auto &p:data){(void)p.at("name").get<std::string>();const auto &id=p.at("id");if(!id.is_number_integer()||id.get<double>()<0||id.get<double>()>UINT32_MAX||!ids.insert(id.get<uint32_t>()).second)throw std::runtime_error("Invalid or duplicate plugin parameter identity");}
    return data;
  }
  void load(bool completing=false){
    cancelAutomaticEdit();
    if(!completing&&completion_.retained())throw std::runtime_error("Review the parameter result first");
    const auto context=context_();if(context.first!=document_)throw std::runtime_error("This parameter editor belongs to another song");
    const auto generation=generation_;pending_=true;layout();
    try{
      auto data=readParameters();if(context_()!=context||generation_!=generation)throw std::runtime_error("Song or parameter draft changed during read / fields retained");
      parameters_=std::move(data);page_=std::min(page_,pages()-1);revision_=context.second;needsReload_=false;sliderRow_=-1;++generation_;
      setting_=true;NativeInputGate::present(controls_.at(parameter),CB_RESETCONTENT,0,0);
      for(int i=0;i<pages();++i){const auto text=L"Page "+std::to_wstring(i+1)+L" / "+std::to_wstring(pages())+L"  ·  "+std::to_wstring(parameters_.empty()?0:i*pageSize+1)+L"–"+std::to_wstring(std::min(parameters_.size(),size_t((i+1)*pageSize)));NativeInputGate::present(controls_.at(parameter),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
      setting_=false;presentPage();status(L"Changes save automatically; sliders save on release. Up to 10 parameters per page.");
    }catch(...){setting_=pending_=false;layout();throw;}pending_=false;layout();
  }
  void finishResult(){
    const auto result=completion_.returned();if(!result)throw std::runtime_error("Parameter result is uncertain / use Review result");
    const auto report=Json{{"outcome","returned"},{"submission",submitted_},{"result",result->result}};
    if(context_()==std::pair(result->document,result->revision)&&generation_==completion_.generation())load(true);
    report_=report;completion_.finish();status(dirty()?L"Earlier edit completed / newer fields retained":L"Parameter applied / one Undo restores it");
  }
  void queueTypedEdit(){queueAutomaticEdit([this]{
    if(pending_||(available_&&!available_())){queueTypedEdit();return;}
    if(sliderRow_<0)commit();
  });}
  void commit(){
    cancelAutomaticEdit();requireReady();if(!dirty())return;
    if(context_()!=std::pair(document_,revision_))throw std::runtime_error("Song changed / parameter text retained. Reload before applying");
    Json values=Json::array(),raw=Json::array();
    for(int i=0;i<pageSize;++i)if(rows_[i].dirty){const auto &r=rows_[i];const auto &f=r.field;if(!f.editable()||r.index<0)throw std::runtime_error("This parameter is read-only or unavailable");
      const auto v=number(control(i,value));if(v<f.minimum||v>f.maximum||(f.kind!=PluginParameterField::Kind::Number&&std::floor(v)!=v))throw std::runtime_error("Enter a value in the displayed range for "+f.name);
      const auto id=parameters_[size_t(r.index)].at("id");values.push_back({{"id",id},{"value",v}});raw.push_back({{"id",id},{"value",utf8(field(control(i,value)))}});
    }
    submitted_={{"plugin",plugin_},{"expectedRevision",revision_},{"values",std::move(values)}};
    const auto generation=generation_;pending_=true;layout();
    try{completion_.submit(write_,"plugin.parameters.set",submitted_,document_,generation,raw);pending_=false;finishResult();}
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
    if(setting_)return;if(id==close){hide();return;}
    const int row=(id-rowBase)/stride,part=(id-rowBase)%stride;
    const bool rowControl=id>=rowBase&&row<pageSize&&rows_[row].index>=0;
    if(rowControl&&part==value&&notification==EN_CHANGE){rows_[row].dirty=true;++generation_;queueTypedEdit();return;}
    if(pending_)return;
    if(id==reload&&notification==BN_CLICKED){if(completion_.retained())review();else load();return;}requireReady();
    if(id==parameter&&notification==CBN_SELCHANGE){changePage(int(SendMessageW(controls_.at(parameter),CB_GETCURSEL,0,0)));return;}
    if(notification==BN_CLICKED&&(id==previous||id==next)){changePage(page_+(id==next?1:-1));return;}
    if(notification==BN_CLICKED&&(id==applyAll||(rowControl&&part==apply))){commit();return;}
    if(!rowControl)return;auto &r=rows_[row];const auto &f=r.field;
    if((part==choices&&notification==CBN_SELCHANGE)||((part==toggle||part==less||part==more)&&notification==BN_CLICKED)){
      if(dirty()){NativeInputGate::present(controls_.at(control(row,choices)),CB_SETCURSEL,WPARAM(f.value),0);throw std::runtime_error("Apply typed values before using switches or steps");}
      if(!f.editable())throw std::runtime_error("Parameter is read only");double candidate=f.value;
      if(part==choices){const auto i=SendMessageW(controls_.at(id),CB_GETCURSEL,0,0);if(i<0||size_t(i)>=f.choices.size())throw std::runtime_error("Choose an available parameter value");candidate=double(i);}
      else if(part==toggle)candidate=f.value==0?1:0;
      else{const auto step=f.step>0?f.step:(f.maximum-f.minimum)/100;candidate=std::clamp(candidate+(part==more?step:-step),f.minimum,f.maximum);}
      setting_=true;set(control(row,value),Json(candidate));setting_=false;r.dirty=true;++generation_;commit();
    }
  }
  bool controlScroll(UINT message,WPARAM event,HWND child)override{
    const int id=GetDlgCtrlID(child),row=(id-rowBase)/stride;
    if(message!=WM_HSCROLL||id<rowBase||row>=pageSize||(id-rowBase)%stride!=slider)return false;
    if(setting_||pending_)return true;requireReady();auto &r=rows_[row];const auto &f=r.field;
    if(!f.editable()||f.kind!=PluginParameterField::Kind::Number||f.maximum<=f.minimum)return true;
    if(dirty()&&sliderRow_!=row){NativeInputGate::present(child,TBM_SETPOS,TRUE,int(std::round((f.value-f.minimum)/(f.maximum-f.minimum)*10000)));throw std::runtime_error("Apply typed values before moving a slider");}
    const auto code=LOWORD(event);if(code==TB_ENDTRACK){if(sliderRow_==row){sliderRow_=-1;commit();}return true;}
    const auto position=std::clamp(LONG(SendMessageW(child,TBM_GETPOS,0,0)),0L,10000L);
    auto candidate=f.minimum+(f.maximum-f.minimum)*position/10000.;
    if(code==TB_LINEUP||code==TB_LINEDOWN){const auto step=f.step>0?f.step:(f.maximum-f.minimum)/100;candidate=std::clamp(f.value+(code==TB_LINEUP?-step:step),f.minimum,f.maximum);}
    if(f.step>0)candidate=std::clamp(f.minimum+std::round((candidate-f.minimum)/f.step)*f.step,f.minimum,f.maximum);
    sliderRow_=row;setting_=true;set(control(row,value),Json(candidate));setting_=false;r.dirty=true;++generation_;
    if(code==TB_THUMBTRACK||code==TB_THUMBPOSITION)status(L"Release the slider to apply this value / Escape cancels");
    else{sliderRow_=-1;commit();}return true;
  }
  void controlCaptureChanged(HWND child)override{if(sliderRow_>=0&&child==controls_.at(control(sliderRow_,slider)))SetTimer(window_,8,1,nullptr);}
  bool key(WPARAM key,bool ctrl,bool)override{
    if(key==VK_ESCAPE){if(sliderRow_>=0){sliderRow_=-1;ReleaseCapture();++generation_;presentPage();status(L"Slider cancelled / plugin unchanged");}else hide();return true;}
    if(ctrl&&key=='R'){action(reload,BN_CLICKED);return true;}
    if(ctrl&&(key==VK_NEXT||key==VK_PRIOR)){requireReady();changePage(page_+(key==VK_NEXT?1:-1));return true;}
    if(key==VK_RETURN){const auto id=GetDlgCtrlID(GetFocus());if(id>=rowBase&&(id-rowBase)%stride==value){commit();return true;}wchar_t type[24]{};GetClassNameW(GetFocus(),type,24);if(!_wcsicmp(type,L"Button")){action(id,BN_CLICKED);return true;}}return false;
  }
  void timer(UINT_PTR id)override{
    if(id==8){KillTimer(window_,8);if(sliderRow_>=0&&!GetCapture()&&!pending_){sliderRow_=-1;commit();}return;}
    if(id!=7||!visible()||pending_||dirty()||completion_.retained()||needsReload_||(available_&&!available_())||context_()==std::pair(document_,revision_))return;
    try{load();}catch(const Api::ApiError &e){if(e.code!=-32002){needsReload_=true;throw;}}catch(...){needsReload_=true;throw;}
  }
  void layout()override{
    if(!ready_)return;const auto focus=GetFocus();if(pending_&&!pendingFocus_&&owns(focus))pendingFocus_=focus;
    const auto [w,h]=size();place(heading,12,10,w-24,24);place(previous,12,42,64,26);place(parameter,84,42,w-244,260);place(next,w-152,42,64,26);place(close,w-80,42,68,26);
    const bool ready=!pending_&&!completion_.retained()&&!needsReload_;const float rowHeight=std::max(48.f,std::min(60.f,(h-168)/pageSize));
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
  PluginParametersWindow(HWND owner,std::string plugin,std::string name,Request request,Context context,NativeWriteCompletion::Write write,std::function<bool()> available):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),write_(std::move(write)),available_(std::move(available)),plugin_(std::move(plugin)){
    const auto captured=context_();document_=captured.first;revision_=captured.second;minimumClientWidth_=620;minimumClientHeight_=650;
    const auto title=wide(name)+L" — Parameters";create(L"ScreamSeq.PluginParameters",title.c_str(),720,740);combo(parameter);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{reload,L"Reload values"},{close,L"Close"},{previous,L"< Prev"},{next,L"Next >"},{applyAll,L"Retry values"}})button(id,text);
    label(heading,wide(name).c_str());label(statusLabel,L"");
    for(int i=0;i<pageSize;++i){label(control(i,nameLabel),L"");label(control(i,rangeLabel),L"");edit(control(i,value),L"",64);combo(control(i,choices));button(control(i,toggle),L"Off");button(control(i,apply),L"Set");button(control(i,less),L"−");button(control(i,more),L"+");
      const auto track=add(control(i,slider),TRACKBAR_CLASSW,L"Parameter",TBS_HORZ|TBS_NOTICKS);NativeInputGate::present(track,TBM_SETRANGE,TRUE,MAKELPARAM(0,10000));NativeInputGate::present(track,TBM_SETLINESIZE,0,100);NativeInputGate::present(track,TBM_SETPAGESIZE,0,1000);}
    finish();load();
  }
  void show(){NativeToolWindow::show();SetTimer(window_,7,250,nullptr);SetFocus(controls_.at(parameter));}
  void hide()override{KillTimer(window_,7);NativeToolWindow::hide();}
  Json snapshot()const{Json rows=Json::array();for(int i=0;i<pageSize;++i)if(rows_[i].index>=0)rows.push_back({{"index",rows_[i].index},{"id",parameters_[size_t(rows_[i].index)].at("id")},{"value",utf8(field(control(i,value)))},{"dirty",rows_[i].dirty},{"valueControl",control(i,value)},{"sliderControl",control(i,slider)}});
    return {{"visible",visible()},{"plugin",plugin_},{"document",document_},{"revision",revision_},{"parameters",parameters_},{"selected",parameters_.empty()?-1:page_*pageSize},{"page",page_},{"pages",pages()},{"rows",rows},{"dirty",dirty()},{"pending",pending_},{"completion",completion_.snapshot()},{"report",report_},{"needsReload",needsReload_},{"status",utf8(status_)}};}
};
}
