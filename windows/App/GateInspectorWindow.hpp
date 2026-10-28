#pragma once
#include "NativeToolWindow.hpp"
#include "ScratchEditorState.hpp"
#include "PatternFields.hpp"
#include "editor/GateSignal.hpp"

namespace ScreamSeq {
class GateInspectorWindow final:public NativeToolWindow {
  using Json=Api::Json;
public:
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
  using Capture=std::function<std::optional<ScratchPatternTarget>()>;
private:
  enum:int{target=6101,plugin,parameter,riseShape,fallShape,linked,apply,stop,stopAll,reload,done,statusLabel,heading,shapeLabel,
    fieldBase=6200,sliderBase=6250,labelBase=6300};
  inline static constexpr std::array<const char *,9> numeric{"floor","ceiling","period","phase","width","rise","fall","pitchRange","offset"};
  inline static constexpr std::array<const wchar_t *,9> labels{L"Floor",L"Ceiling",L"Period / beats",L"Phase / beats",L"Width / %",L"Rise / beats",L"Fall / beats",L"Bend range / st",L"Onset / beats"};
  inline static constexpr std::array<const char *,4> targets{"volume","pitch","pan","parameter"};
  inline static constexpr std::array<const char *,5> shapes{"direct","linear","ease-in","ease-out","ease-in-out"};
  Request request_;Context context_;Capture capture_;std::function<Json()> rack_;
  std::optional<ScratchPatternTarget> captured_;
  Json fields_=Json::object(),commands_=Json::array(),bindings_=Json::array(),plugins_=Json::array(),parameters_=Json::array();
  bool setting_=false,dirty_=false,pending_=false;uint64_t generation_=0;
  std::array<bool,9> valid_{};
  int selected(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  void choose(int id,int index){SendMessageW(controls_.at(id),CB_SETCURSEL,index,0);}
  void choices(int id,const std::vector<std::wstring> &items){SendMessageW(controls_.at(id),CB_RESETCONTENT,0,0);for(const auto &s:items)SendMessageW(controls_.at(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s.c_str()));choose(id,items.empty()?-1:0);}
  double value(const char *key)const{return fields_.value(key,0.0);}
  void status(const std::wstring &s){status_=s;set(statusLabel,s);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what())+L" · Draft retained");}
  void current()const{if(!captured_||context_()!=std::pair(captured_->document,captured_->revision))throw std::runtime_error("Song changed; Use cursor reloads this draft");}
  double scale(size_t i)const{return i==4||((i==0||i==1)&&fields_.value("target",std::string())!="pitch")?100:1;}
  std::pair<double,double> range(size_t i)const{
    const auto t=fields_.value("target",std::string("volume"));const auto period=std::clamp(value("period"),1./65536,256.);
    if(i<2)return {t=="pitch"?-96:t=="pan"?-1:0,t=="pitch"?96:t=="volume"?4:1};
    if(i==2)return {1./64,16};if(i==3)return {-period,period};if(i==4)return {0,1};if(i==7)return {1,96};if(i==8)return {0,(1.-1./65536)/(captured_?captured_->rowsPerBeat:4)};return {0,period*2};
  }
  void sliders(){for(size_t i=0;i<numeric.size();++i){const auto [low,high]=range(i);const double position=i==2?std::log(std::max(low,value(numeric[i]))/low)/std::log(high/low):(value(numeric[i])-low)/(high-low);SendMessageW(controls_.at(sliderBase+int(i)),TBM_SETPOS,TRUE,LPARAM(std::lround(std::clamp(position,0.,1.)*10000)));}}
  void showFields(bool reset=false){setting_=true;for(size_t i=0;i<numeric.size();++i)if(reset||valid_[i]){set(fieldBase+int(i),value(numeric[i])*scale(i));valid_[i]=true;}sliders();
    choose(target,int(std::find(targets.begin(),targets.end(),fields_.value("target",std::string("volume")))-targets.begin()));
    for(auto [id,key]:{std::pair{riseShape,"riseShape"},std::pair{fallShape,"fallShape"}})choose(id,int(std::find(shapes.begin(),shapes.end(),fields_.value(key,std::string("linear")))-shapes.begin()));
    set(linked,fields_.value("linked",true)?L"Rise / fall linked":L"Rise / fall independent");setting_=false;layout();requestPaint();
  }
  void loadParameters(int desired=-1){
    const int slot=selected(plugin);parameters_=Json::array();choices(parameter,{});if(slot<0||size_t(slot)>=plugins_.size())return;current();
    auto result=request_("plugin.parameters.get",{{"plugin",plugins_[slot].at("instanceID")}});current();
    std::vector<std::wstring> names;int chosen=0;for(const auto &p:result)if(p.value("writable",false)){if(p.at("id")==desired)chosen=int(names.size());parameters_.push_back(p);names.push_back(wide(p.at("name")));}choices(parameter,names);choose(parameter,names.empty()?-1:chosen);
  }
  void load(){if(pending_)return;auto candidate=capture_();if(!candidate)throw std::runtime_error("Choose an editable pattern FX cell");const auto revision=std::pair(candidate->document,candidate->revision);const auto generation=generation_;pending_=true;
    Json data;try{data=request_("pattern.effects.get",{{"pattern",candidate->pattern}});if(context_()!=revision||generation_!=generation)throw std::runtime_error("Song changed while reading the cell");}catch(...){pending_=false;throw;}pending_=false;
    captured_=candidate;commands_=data.at("commands");bindings_=data.at("bindings");plugins_=rack_();fields_=Json::object();const auto &descriptor=Tracker::nativePatternCommand(Tracker::NativePatternOp::Gate);
    for(const auto &p:descriptor.parameters){if(p.type==Tracker::NativePatternFieldType::Choice)fields_[p.key]=p.choices[size_t(p.initial)];else if(p.type==Tracker::NativePatternFieldType::Boolean)fields_[p.key]=p.initial!=0;else fields_[p.key]=p.initial;}
    const auto command=candidate->command.is_object()?candidate->command:Json::object();if(command.value("native",std::string())=="gate")fields_.update(command.at("parameters"));fields_["offset"]=command.value("offset",0.0)/65536/candidate->rowsPerBeat;
    std::vector<std::wstring> names;for(const auto &p:plugins_)names.push_back(wide(p.at("name")));choices(plugin,names);
    int desired=-1;for(const auto &b:bindings_)if(b.at("id")==fields_.at("binding")){for(size_t i=0;i<plugins_.size();++i)if(plugins_[i].at("instanceID")==b.at("plugin"))choose(plugin,int(i));desired=b.at("parameter").get<int>();}
    showFields(true);if(fields_.at("target")=="parameter")loadParameters(desired);dirty_=false;++generation_;
    status(L"P"+std::to_wstring(candidate->pattern)+L" · row "+std::to_wstring(candidate->row)+L" · channel "+std::to_wstring(candidate->channel+1)+L" · FX "+std::to_wstring(candidate->column+1)+L" · Apply saves one Undo");
  }
  void changed(size_t i,double v){fields_[numeric[i]]=v;if(fields_.value("linked",true)&&(i==5||i==6)){fields_[numeric[i==5?6:5]]=v;setting_=true;if(valid_[i==5?6:5])set(fieldBase+int(i==5?6:5),v);setting_=false;}dirty_=true;++generation_;sliders();requestPaint();}
  void write(bool stopping,bool all=false){
    if(pending_)return;current();if(std::find(valid_.begin(),valid_.end(),false)!=valid_.end())throw std::runtime_error("Correct the invalid numeric field first");
    auto fields=fields_;const auto onset=value("offset")*captured_->rowsPerBeat*65536;fields.erase("offset");if(onset<0||onset>65535)throw std::runtime_error("Onset must stay within this row");
    Json binding=nullptr;if(fields.at("target")=="parameter"&&!all){const auto p=selected(parameter),slot=selected(plugin);if(p<0||slot<0)throw std::runtime_error("Choose a plugin and parameter");
      unsigned free=1;for(;free<=255;++free)if(std::none_of(bindings_.begin(),bindings_.end(),[&](const auto &b){return b.at("id")==free;}))break;
      for(const auto &b:bindings_)if(b.at("plugin")==plugins_[slot].at("instanceID")&&b.at("parameter")==parameters_[p].at("id")){free=b.at("id").get<unsigned>();break;}
      if(free>255)throw std::runtime_error("All parameter bindings are in use");fields["binding"]=free;binding={{"id",free},{"plugin",plugins_[slot].at("instanceID")},{"parameter",parameters_[p].at("id")},{"name",parameters_[p].at("name")}};
    }else fields["binding"]=0;
    if(stopping)fields={{"target",all?Json("all"):fields.at("target")},{"binding",all?Json(0):fields.at("binding")}};
    auto next=commands_;std::erase_if(next.get_ref<Json::array_t &>(),[&](const auto &c){return c.at("channel")==captured_->channel&&c.at("column")==captured_->column&&c.at("position").template get<unsigned>()/65536==captured_->row;});for(auto &c:next)c.erase("track");
    next.push_back({{"kind","native"},{"native",stopping?"gate-stop":"gate"},{"parameters",fields},{"position",captured_->row*65536+unsigned(std::llround(onset))},{"channel",captured_->channel},{"column",captured_->column}});
    Json args={{"pattern",captured_->pattern},{"commands",next},{"expectedRevision",captured_->revision}};if(!binding.is_null())args["bindings"]=Json::array({binding});const auto sent=generation_;const auto document=captured_->document;pending_=true;
    try{request_("pattern.effects.set",args);if(context_().first!=document)throw std::runtime_error("The captured song is no longer open");captured_->revision=context_().second;commands_=next;if(!binding.is_null()){std::erase_if(bindings_.get_ref<Json::array_t &>(),[&](const auto &b){return b.at("id")==binding.at("id");});bindings_.push_back(binding);}if(sent==generation_)dirty_=false;pending_=false;status(stopping?L"Stop gate saved · Undo restores the previous cell":L"Gate saved · Undo restores the previous cell");}catch(...){pending_=false;throw;}
  }
  bool controlScroll(UINT message,WPARAM,HWND control)override{if(message!=WM_HSCROLL||!control)return false;const auto id=GetDlgCtrlID(control);if(id<sliderBase||id>=sliderBase+9)return false;slider(id);return true;}
  void slider(int id){if(setting_||id<sliderBase||id>=sliderBase+9)return;const auto i=size_t(id-sliderBase);const auto [low,high]=range(i);const double p=SendMessageW(controls_.at(id),TBM_GETPOS,0,0)/10000.;double v=i==2?low*std::pow(high/low,p):low+(high-low)*p;if(i==7)v=std::round(v);valid_[i]=true;setting_=true;set(fieldBase+int(i),v*scale(i));setting_=false;changed(i,v);}
  void action(int id,unsigned notification)override{
    if(setting_)return;if(id>=fieldBase&&id<fieldBase+9){if(notification!=EN_CHANGE)return;const auto i=size_t(id-fieldBase);dirty_=true;++generation_;try{auto text=field(id);const auto slash=text.find(L'/');size_t used=0;double v=std::stod(text,&used);if(slash!=std::wstring::npos){if(used!=slash)throw std::runtime_error("Invalid fraction");size_t end=0;const auto denominator=std::stod(text.substr(slash+1),&end);if(end!=text.size()-slash-1||denominator==0)throw std::runtime_error("Invalid fraction");v/=denominator;}else if(used!=text.size())throw std::runtime_error("Invalid number");if(!std::isfinite(v))throw std::runtime_error("Invalid number");valid_[i]=true;changed(i,v/scale(i));}catch(...){valid_[i]=false;status(L"Enter a finite number or beat fraction");}return;}
    if(id==reload){load();return;}if(id==done){hide();return;}if(id==apply){write(false);return;}if(id==stop||id==stopAll){write(true,id==stopAll);return;}
    if(id==linked){fields_["linked"]=!fields_.value("linked",true);if(fields_["linked"].get<bool>()){fields_["fall"]=fields_["rise"];fields_["fallShape"]=fields_["riseShape"];}showFields();dirty_=true;++generation_;return;}
    if(notification!=CBN_SELCHANGE)return;dirty_=true;++generation_;
    if(id==target){fields_["target"]=targets[size_t(selected(target))];fields_["floor"]=selected(target)==2?-1:0;fields_["ceiling"]=selected(target)==1?12:1;if(selected(target)==1)fields_["pitchRange"]=12;showFields();if(selected(target)==3)loadParameters();}
    else if(id==plugin)loadParameters();else if(id==riseShape||id==fallShape){fields_[id==riseShape?"riseShape":"fallShape"]=shapes[size_t(selected(id))];if(fields_.value("linked",true))fields_[id==riseShape?"fallShape":"riseShape"]=fields_[id==riseShape?"riseShape":"fallShape"];showFields();}
  }
  void layout()override{const auto [w,h]=size();place(heading,16,10,w-32,24);place(target,16,40,w-32,28);const bool parameterTarget=fields_.value("target",std::string())=="parameter";place(plugin,16,76,(w-40)/2,26,parameterTarget);place(parameter,24+(w-40)/2,76,(w-40)/2,26,parameterTarget);
    for(int i=0;i<9;++i){place(labelBase+i,16,290+32*i,115,24);place(sliderBase+i,132,286+32*i,std::max(20.f,w-246),28);place(fieldBase+i,w-106,286+32*i,90,26);}
    place(riseShape,16,580,(w-40)/2,26);place(fallShape,24+(w-40)/2,580,(w-40)/2,26);place(linked,16,614,w-32,26);place(shapeLabel,16,644,w-32,32);place(apply,16,682,100,28);place(stop,124,682,126,28);place(stopAll,258,682,w-274,28);place(reload,16,718,126,28);place(done,w-106,718,90,28);place(statusLabel,16,752,w-32,std::max(36.f,h-758));}
  void paint(RenderSurface &s)override{const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);if(fields_.empty()||std::find(valid_.begin(),valid_.end(),false)!=valid_.end()||value("period")<=0||value("floor")>value("ceiling"))return;
    auto shape=[&](const char *key){return Tracker::GateShape(std::find(shapes.begin(),shapes.end(),fields_.value(key,std::string("linear")))-shapes.begin());};Tracker::GateSignal gate{value("floor"),value("ceiling"),value("period"),value("phase"),value("width"),value("rise"),value("fall"),shape("riseShape"),shape("fallShape")};
    const float left=52,width=w-68,top=120,height=130;const double low=gate.floor==gate.ceiling?gate.floor-.5:gate.floor,high=gate.floor==gate.ceiling?gate.ceiling+.5:gate.ceiling;
    for(int i=0;i<=4;++i){const float x=left+width*i/4;s.line(x,top,x,top+height,0x354757);s.uiText(wide(std::to_string(gate.period*i/2)),x-12,top+height+5,72,0x8fa8ba);}s.uiText(wide(std::to_string(gate.ceiling)),4,top,48,0x8fa8ba);s.uiText(wide(std::to_string(gate.floor)),4,top+height-18,48,0x8fa8ba);
    double previous=gate.value(0);for(int i=1;i<=1024;++i){const auto v=gate.value(gate.period*2*i/1024);s.line(left+width*(i-1)/1024,top+height-float((previous-low)/(high-low))*height,left+width*i/1024,top+height-float((v-low)/(high-low))*height,0x72dcc6,1.5f);previous=v;}
    const auto rise=gate.riseShape==Tracker::GateShape::Direct?0:std::min(gate.rise,gate.period*gate.width),fall=gate.fallShape==Tracker::GateShape::Direct?0:std::min(gate.fall,gate.period*(1-gate.width));set(shapeLabel,L"Effective rise "+wide(std::to_string(rise))+L" b · fall "+wide(std::to_string(fall))+L" b");
  }
public:
  GateInspectorWindow(HWND parent,Request request,Context context,Capture capture,std::function<Json()> rack):NativeToolWindow(parent),request_(std::move(request)),context_(std::move(context)),capture_(std::move(capture)),rack_(std::move(rack)){
    minimumWidth_=450;minimumHeight_=830;create(L"ScreamSeq.GateInspector",L"Advanced gate",520,860);for(auto id:{target,plugin,parameter,riseShape,fallShape})combo(id);choices(target,{L"Volume multiplier",L"Pitch offset / semitones",L"Absolute pan",L"Plugin parameter"});for(auto id:{riseShape,fallShape})choices(id,{L"Direct",L"Linear",L"Ease in",L"Ease out",L"Ease in/out"});
    for(int i=0;i<9;++i){label(labelBase+i,labels[size_t(i)]);edit(fieldBase+i,L"0",48);add(sliderBase+i,TRACKBAR_CLASSW,labels[size_t(i)],TBS_HORZ|TBS_NOTICKS);SendMessageW(controls_.at(sliderBase+i),TBM_SETRANGE,TRUE,MAKELPARAM(0,10000));}
    label(heading,L"Advanced gate · two cycles · time in beats");label(shapeLabel,L"");label(statusLabel,L"");button(linked,L"Link rise / fall");button(apply,L"Apply gate");button(stop,L"Write stop");button(stopAll,L"Stop all");button(reload,L"Use cursor");button(done,L"Close");finish();clampToOwnerWorkArea();
  }
  void open(){const bool retained=visible()||dirty_;show();if(!retained)load();}
  bool retainedDraft()const{return dirty_||pending_;}
  Json snapshot()const{return {{"visible",visible()},{"retainedDraft",dirty_},{"pending",pending_},{"generation",generation_},{"revision",captured_?captured_->revision:""},{"parameters",fields_}};}
};
}
