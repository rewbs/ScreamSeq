#pragma once
#include "EnvelopeBankWindow.hpp"
#include "NativeContextMenu.hpp"
#include "SampleFileDialog.hpp"
#include "NativeWriteCompletion.hpp"
#include "editor/TrackerDocument.hpp"

namespace ScreamSeq {
class InstrumentEnvelopeWindow final : public NativeToolWindow {
  using Json=Api::Json;
public:
  struct Context {std::string document,revision;unsigned instrument=0,sample=0;Json instruments,samples;};
private:
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Audition=std::function<void(unsigned,const std::string &,const std::string &,const std::string &)>;Audition audition_;
  using SelectSound=std::function<void(unsigned,const std::string &)>;SelectSound selectSound_;
  enum : int {instrument=4401,kind,newInstrument,bank,enabled,sustain,loop,carry,filter,adsr,clear,
    node,tick,value,setPoint,addPoint,deletePoint,loopStart,loopEnd,sustainStart,sustainEnd,release,setMarkers,
    fit,zoomOut,zoomIn,panLeft,panRight,tool,rangeStart,rangeEnd,toolValue0,toolValue1,toolValue2,toolValue3,toolValue4,copyRange,previewTool,
    name,volume,pan,fade,nna,dct,dna,mapFrom,mapTo,mapSample,mapStage,apply,reload,fromCursor,close,audition,importFile,keymap,
    heading=4500,targetLabel,nodeLabel,tickLabel,valueLabel,loopStartLabel,loopEndLabel,sustainStartLabel,sustainEndLabel,releaseLabel,
    rangeLabel,toolLabel0,toolLabel1,toolLabel2,toolLabel3,toolLabel4,nameLabel,volumeLabel,panLabel,fadeLabel,nnaLabel,dctLabel,dnaLabel,mapLabel,statusLabel,keymapLabel,
    pageEnvelope=4600,pagePoints,pageTools,pageProperties,pageKeymap,shortOptions,
    contextAdd=4700,contextEdit,contextDelete,contextDock};
  static constexpr std::array<const char *,3> kinds={"volume","pan","pitch"};
  static constexpr std::array<const char *,9> operations={"flip-time","flip-values","shift","scale","ramp","sine","humanize","paste","insert"};
  static constexpr std::array<const char *,5> markerKeys={"loopStart","loopEnd","sustainPoint","sustainEnd","releaseNode"};
  Request request_;std::function<Context()> context_;
  Context captured_;std::string identity_;unsigned index_=0;int kind_=0,selected_=-1,tool_=0;
  Json envelope_=Json::object(),properties_=Json::object(),mapping_=Json::array(),clipboard_;
  bool setting_=false,pending_=false,envelopeDirty_=false,propertiesDirty_=false,mappingDirty_=false,mappingFields_=false,pointFields_=false,markerFields_=false,dragging_=false;
  int selectedKey_=-1,page_=0;bool compact_=false,canvasVisible_=true,toolFieldsDirty_=false,shortDock_=false,shortEnvelopeOptions_=false;
  HWND pendingFocus_{};bool contextOpen_=false;
  static constexpr std::array<const char *,5> pages={"envelope","points","tools","properties","keymap"};
  uint64_t generation_=0;Json dragBefore_;bool dragDirty_=false;int dragSelection_=-1;
  AutomationCanvas canvas_;std::vector<double> playbackTicks_;std::unique_ptr<EnvelopeBankWindow> bank_;
  void require(bool ok,const char *message)const{if(!ok)throw std::runtime_error(message);}
  bool draft()const{return envelopeDirty_||propertiesDirty_||mappingDirty_||mappingFields_||pointFields_||markerFields_;}
  bool current()const{const auto c=context_();return c.document==captured_.document&&c.revision==captured_.revision;}
  bool editable()const{return !identity_.empty()&&envelope_.value("editable",false);}
  void requireCurrent()const{require(current(),"Song changed / captured instrument retained; Reload before applying");}
  void status(std::wstring text){status_=std::move(text);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  int selection(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  void choose(int id,int index){ScreamSeq::NativeInputGate::present(controls_.at(id),CB_SETCURSEL,index,0);}
  int whole(int id,int low,int high)const{const auto n=number(id);require(n>=low&&n<=high&&n==std::floor(n),"Use a whole number within the indicated range");return int(n);}
  const Json &points()const{static const Json empty=Json::array();return envelope_.contains("points")?envelope_.at("points"):empty;}
  void rebuild(){Json nodes=Json::array(),values=Json::array();for(const auto &p:points()){nodes.push_back({{"position",p[0].get<unsigned>()*256},{"value",p[1].get<double>()/64}});values.push_back({p[0].get<unsigned>()*256,p[1].get<double>()/64});}canvas_.rebuild(nodes,values);requestPaint();}
  void fitCurve(){canvas_.start=0;canvas_.end=std::min(65536u,std::max(64u,points().empty()?64u:points().back()[0].get<unsigned>()+8))*256.0;canvas_.valueLow=0;canvas_.valueHigh=1;rebuild();}
  void changed(){envelopeDirty_=true;++generation_;rebuild();status(L"Instrument envelope draft / Apply saves one document Undo step");}
  void showPoint(){setting_=true;ScreamSeq::NativeInputGate::present(controls_.at(node),CB_RESETCONTENT,0,0);for(size_t i=0;i<points().size();++i){auto text=std::to_wstring(i)+L" · tick "+std::to_wstring(points()[i][0].get<unsigned>());ScreamSeq::NativeInputGate::present(controls_.at(node),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}choose(node,selected_);
    const auto p=selected_>=0&&size_t(selected_)<points().size()?points()[size_t(selected_)]:Json::array({0,32});set(tick,p[0]);set(value,p[1]);pointFields_=false;setting_=false;}
  void showMarkers(){setting_=true;for(int i=0;i<5;++i)set(loopStart+i,envelope_.value(markerKeys[size_t(i)],i==4?255:0));markerFields_=false;setting_=false;}
  void showProperties(){setting_=true;for(auto [id,key]:std::initializer_list<std::pair<int,const char *>>{{name,"name"},{volume,"volume"},{pan,"pan"},{fade,"fadeout"}})set(id,properties_.value(key,Json(id==name?Json(""):Json(0))));
    for(auto [id,key]:std::initializer_list<std::pair<int,const char *>>{{nna,"nna"},{dct,"dct"},{dna,"dna"}})choose(id,properties_.value(key,0));setting_=false;}
  void showKeymap(){
    const auto list=controls_.at(keymap);const auto top=SendMessageW(list,LB_GETTOPINDEX,0,0);SendMessageW(list,WM_SETREDRAW,FALSE,0);ScreamSeq::NativeInputGate::present(list,LB_RESETCONTENT,0,0);
    static constexpr const wchar_t *notes[]={L"C-",L"C#",L"D-",L"D#",L"E-",L"F-",L"F#",L"G-",L"G#",L"A-",L"A#",L"B-"};
    const auto saved=properties_.value("mapping",Json::array());
    for(size_t key=0;key<mapping_.size();++key){const auto slot=mapping_[key].get<unsigned>();std::wstring sample=slot?std::to_wstring(slot)+L" · unavailable sample":L"No sample";for(const auto &s:captured_.samples)if(s.at("index")==slot){sample=std::to_wstring(slot)+L" · "+wide(s.at("name").get<std::string>());break;}
      wchar_t prefix[48]{};swprintf_s(prefix,L"%s%03u  %s%u  ·  ",key<saved.size()&&mapping_[key]!=saved[key]?L"* ":L"  ",unsigned(key),notes[key%12],unsigned(key/12));const auto text=prefix+sample;ScreamSeq::NativeInputGate::present(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
    ScreamSeq::NativeInputGate::present(list,LB_SETCURSEL,selectedKey_,0);ScreamSeq::NativeInputGate::present(list,LB_SETTOPINDEX,top,0);SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);
    set(keymapLabel,mappingDirty_?L"Sample keymap · * staged changes":L"Sample keymap · select a note");
  }
  void showMapFields(){setting_=true;set(mapFrom,selectedKey_>=0?selectedKey_:0);set(mapTo,selectedKey_>=0?selectedKey_:119);unsigned slot=selectedKey_>=0&&size_t(selectedKey_)<mapping_.size()?mapping_[size_t(selectedKey_)].get<unsigned>():0;int choice=0;for(size_t i=0;i<captured_.samples.size();++i)if(captured_.samples[i].at("index")==slot)choice=int(i)+1;choose(mapSample,choice);mappingFields_=false;setting_=false;}
  void choices(){setting_=true;ScreamSeq::NativeInputGate::present(controls_.at(instrument),CB_RESETCONTENT,0,0);int position=0;for(const auto &i:captured_.instruments){auto text=std::to_wstring(i.at("index").get<unsigned>())+L" · "+wide(i.at("name").get<std::string>());ScreamSeq::NativeInputGate::present(controls_.at(instrument),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(i.at("id")==identity_)choose(instrument,position);++position;}choose(kind,kind_);
    const auto oldSample=selection(mapSample);ScreamSeq::NativeInputGate::present(controls_.at(mapSample),CB_RESETCONTENT,0,0);ScreamSeq::NativeInputGate::present(controls_.at(mapSample),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"No sample"));for(const auto &s:captured_.samples){auto text=std::to_wstring(s.at("index").get<unsigned>())+L" · "+wide(s.at("name").get<std::string>());ScreamSeq::NativeInputGate::present(controls_.at(mapSample),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}choose(mapSample,std::clamp(oldSample,0,int(captured_.samples.size())));setting_=false;}
  void load(bool follow,std::string wanted={},std::optional<int> wantedKind={},bool adoptingResult=false){
    require(!creationCompletion_.retained()||adoptingResult,"Review the instrument creation result before reloading");
    if(pending_)return;const auto c=context_();require(follow||c.document==captured_.document,"Document changed / use From cursor to capture the new song");
    const auto selectedKind=wantedKind.value_or(kind_);if(wanted.empty()&&!follow)wanted=identity_;
    auto chosen=std::find_if(c.instruments.begin(),c.instruments.end(),[&](const auto &i){return wanted.empty()?i.at("index")==c.instrument:i.at("id")==wanted;});
    if(chosen==c.instruments.end()){require(follow||wanted.empty(),"Captured instrument was removed / use From cursor");chosen=c.instruments.begin();}
    const auto token=generation_;pending_=true;layout();try{Json env=Json::object(),props=Json::object();std::string identity;unsigned index=0;
      if(chosen!=c.instruments.end()){identity=chosen->at("id");index=chosen->at("index");env=request_("instrument.envelope.get",{{"instrument",identity},{"envelope",kinds[size_t(selectedKind)]}});props=request_("instrument.get",{{"instrument",index}});}
      const auto after=context_();require(after.document==c.document&&after.revision==c.revision&&generation_==token,"Song or draft changed while loading / editor retained");
      const bool same=c.document==captured_.document&&identity_==identity&&kind_==selectedKind;captured_=c;identity_=identity;index_=index;kind_=selectedKind;envelope_=std::move(env);properties_=std::move(props);mapping_=properties_.value("mapping",Json::array());
      selected_=same&&!points().empty()?std::clamp(selected_,-1,int(points().size()-1)):-1;envelopeDirty_=propertiesDirty_=mappingDirty_=mappingFields_=pointFields_=markerFields_=false;if(!same)selectedKey_=-1;++generation_;playbackTicks_.clear();choices();showPoint();showMarkers();showProperties();showKeymap();showMapFields();
      if(!same){fitCurve();setting_=true;set(rangeStart,L"0");set(rangeEnd,points().empty()?49u:points().back()[0].get<unsigned>()+1);setting_=false;toolFieldsDirty_=false;}else rebuild();
      set(targetLabel,identity_.empty()?L"Create an instrument from the current sample":std::to_wstring(index_)+L" · "+wide(properties_.at("name").get<std::string>())+L" · "+wide(kinds[size_t(kind_)])+L" · "+std::to_wstring(envelope_.at("maxPoints").get<unsigned>())+L" points maximum");pending_=false;
      status(identity_.empty()?L"No instruments / New from sample preserves the song's existing sample assignments":editable()?L"Double-click to add / drag or use point fields / Apply saves the captured instrument":L"This envelope is unavailable in the current module format");
    }catch(...){pending_=false;layout();throw;}layout();
  }
  void setPointFields(){require(!markerFields_,"Set or discard pending marker fields first");require(editable(),"Choose an editable envelope");const int at=whole(tick,0,65535),amount=whole(value,0,64);auto next=points();
    if(selected_>=0){require(selected_!=0||at==0,"The first envelope point stays at tick zero");require((selected_==0||at>next[size_t(selected_-1)][0].get<int>())&&(size_t(selected_+1)==next.size()||at<next[size_t(selected_+1)][0].get<int>()),"Point ticks must stay strictly ordered");next[size_t(selected_)]=Json::array({at,amount});}
    else{require(next.size()<envelope_.at("maxPoints").get<size_t>(),"This envelope reached its format's point limit");require(!next.empty()||at==0,"Start the envelope at tick zero");auto found=std::lower_bound(next.begin(),next.end(),at,[](const auto &p,int t){return p[0].template get<int>()<t;});require(found==next.end()||(*found)[0]!=at,"Another point occupies this tick");selected_=int(found-next.begin());next.insert(found,Json::array({at,amount}));for(const auto *key:markerKeys)if(envelope_.value(key,0)!=255&&envelope_.value(key,0)>=selected_&&!points().empty())envelope_[key]=envelope_.at(key).get<unsigned>()+1;}
    if(next!=points()){envelope_["points"]=std::move(next);changed();}showPoint();showMarkers();layout();
  }
  void setMarkerFields(){require(!pointFields_,"Set or discard pending point fields first");require(editable(),"Choose an editable envelope");const int last=std::max(0,int(points().size())-1);std::array<int,5> markers{};for(int i=0;i<4;++i)markers[size_t(i)]=whole(loopStart+i,0,last);markers[4]=whole(release,0,255);require(markers[4]==255||markers[4]<int(points().size()),"Release node must exist, or use 255 for none");require(markers[0]<=markers[1]&&markers[2]<=markers[3],"Loop and sustain starts must precede their ends");
    auto next=envelope_;for(size_t i=0;i<5;++i)next[markerKeys[i]]=markers[i];if(next!=envelope_){envelope_=std::move(next);changed();}showMarkers();}
  void removePoint(){require(!pointFields_&&!markerFields_,"Set or discard pending point and marker fields first");if(selected_<0)return;auto next=points();next.erase(next.begin()+selected_);if(!next.empty())next[0][0]=0;const int last=std::max(0,int(next.size())-1);
    for(const auto *key:markerKeys){const int previous=envelope_.value(key,0);if(previous==255)continue;envelope_[key]=next.empty()&&std::string_view(key)=="releaseNode"?255:std::min(last,previous>selected_?previous-1:previous);}
    envelope_["points"]=next;if(next.empty()){envelope_["enabled"]=false;envelope_["loop"]=false;envelope_["sustain"]=false;}selected_=next.empty()?-1:std::min(selected_,last);changed();showPoint();showMarkers();}
  Json patch()const{Json values=Json::object();if(envelopeDirty_){values["envelope"]=kind_;for(const auto *key:{"points","enabled","sustain","loop","carry","loopStart","loopEnd","sustainPoint","sustainEnd","releaseNode"})values[key]=envelope_.at(key);if(kind_==2)values["filter"]=envelope_.at("filter");}
    if(propertiesDirty_){Json fields={{"name",utf8(field(name))},{"volume",whole(volume,0,64)},{"pan",whole(pan,0,256)},{"fadeout",whole(fade,0,32768)},{"nna",selection(nna)},{"dct",selection(dct)},{"dna",selection(dna)}};for(auto i=fields.begin();i!=fields.end();++i)if(i.value()!=properties_.at(i.key()))values[i.key()]=i.value();}
    if(mappingDirty_)values["mapping"]=mapping_;return values;}
  void commit(){requireCurrent();require(!identity_.empty(),"Choose an instrument");require(!pointFields_&&!markerFields_&&!mappingFields_,"Set or discard pending point, marker and key range fields before Apply");auto values=patch();const auto token=generation_;pending_=true;layout();try{request_("instrument.patch",{{"expectedRevision",captured_.revision},{"instrument",index_},{"values",values}});pending_=false;require(context_().document==captured_.document&&generation_==token,"Source changed during Apply / newer draft retained");load(false);status(L"Instrument saved / document Undo restores its settings, points and markers");}catch(...){pending_=false;layout();throw;}layout();}
  #include "InstrumentCreation.inc"
  uint32_t seedValue()const{const auto n=number(toolValue2);require(n>=0&&n<=UINT32_MAX&&n==std::floor(n),"Seed must be a whole number from 0 to 4294967295");return uint32_t(n);}
  Json toolSignature()const{return Json::array({tool_,field(rangeStart),field(rangeEnd),field(toolValue0),field(toolValue1),field(toolValue2),field(toolValue3),field(toolValue4)});}
  void transform(bool copy){requireCurrent();require(editable()&&!draft(),"Apply or Reload before copying or transforming saved envelope points");const auto first=whole(rangeStart,0,65535),last=(copy||tool_<7)?whole(rangeEnd,1,65536):first+1;require(first<last,"Choose a nonempty tick range");Json p={{"instrument",identity_},{"envelope",kinds[size_t(kind_)]},{"start",first}};
    if(copy)p["end"]=last;else{p["expectedRevision"]=captured_.revision;p["dryRun"]=true;p["operation"]=operations[size_t(tool_)];if(tool_<7)p["end"]=last;Json o=Json::object();if(tool_==2)o["amount"]=number(toolValue0);if(tool_==3)o={{"amount",number(toolValue0)},{"offset",number(toolValue1)}};if(tool_==4)o={{"from",number(toolValue0)},{"to",number(toolValue1)}};if(tool_==5)o={{"center",number(toolValue0)},{"amplitude",number(toolValue1)},{"cycles",number(toolValue2)},{"phase",number(toolValue3)},{"spacing",whole(toolValue4,1,65536)}};if(tool_==6)o={{"amount",number(toolValue0)},{"jitter",whole(toolValue1,0,65536)},{"seed",seedValue()}};if(tool_>=7){require(!clipboard_.is_null(),"Copy an instrument envelope range first");o={{"clip",clipboard_},{"repeats",whole(toolValue0,1,4096)}};}p["options"]=o;}
    const auto token=generation_;const auto signature=toolSignature();pending_=true;layout();try{const auto result=request_(copy?"instrument.envelope.copy":"instrument.envelope.transform",p);pending_=false;requireCurrent();require(token==generation_&&signature==toolSignature(),"Tool settings changed / preview discarded");if(copy){clipboard_=result;status(L"Instrument range copied privately / choose Paste or Insert paste");}else if(result.at("wouldChange").get<bool>()){envelope_=result.at("after");selected_=-1;changed();showPoint();showMarkers();fitCurve();status(L"Preview · "+std::to_wstring(result.at("clippedValues").get<unsigned>())+L" clipped · "+std::to_wstring(result.at("roundedValues").get<unsigned>())+L" rounded · "+std::to_wstring(result.at("reanchoredMarkers").get<unsigned>())+L" markers reattached / Apply saves");}else status(L"The tool leaves this envelope unchanged");toolFieldsDirty_=false;}catch(...){pending_=false;layout();throw;}layout();}
  void toolFields(){static const std::array<std::vector<std::pair<const wchar_t *,const wchar_t *>>,9> fields={{{},{},{{L"Shift / ticks",L"1"}},{{L"Multiply",L"1"},{L"Add / 0–64",L"0"}},{{L"From / 0–64",L"0"},{L"To / 0–64",L"64"}},{{L"Center",L"32"},{L"Amplitude",L"32"},{L"Cycles",L"1"},{L"Phase / degrees",L"0"},{L"Spacing / ticks",L"4"}},{{L"Value jitter",L"3"},{L"Tick jitter",L"0"},{L"Seed",L"0"}},{{L"Repeats",L"1"}},{{L"Repeats",L"1"}}}};setting_=true;for(int i=0;i<5;++i)if(size_t(i)<fields[size_t(tool_)].size()){set(toolLabel0+i,fields[size_t(tool_)][size_t(i)].first);set(toolValue0+i,fields[size_t(tool_)][size_t(i)].second);}setting_=false;layout();}
  void openBank(){if(bank_&&(bank_->visible()||bank_->retainedDraft())){bank_->show();return;}requireCurrent();require(editable()&&!pointFields_&&!markerFields_&&!propertiesDirty_&&!mappingDirty_&&!mappingFields_,"Set pending fields and apply instrument settings before opening the bank");
    const auto document=captured_.document,id=identity_;const auto kind=kind_;auto token=std::make_shared<uint64_t>(generation_);auto source=[this,document,id,kind,token]{return context_().document==document&&captured_.document==document&&identity_==id&&kind_==kind&&generation_==*token&&!pointFields_&&!markerFields_&&!propertiesDirty_&&!mappingDirty_&&!mappingFields_;};
    auto dispatch=[this,source,token](const std::string &method,const Json &p,const std::shared_ptr<NativeCallReceipt> &receipt){const bool owned=source(),old=pending_;if(owned)pending_=true;Api::CompletedCall completed;Json result;try{if(receipt){completed=creationWrite_(method,p,receipt);if(!receipt->read())receipt->publish(std::make_shared<const Api::CompletedCall>(completed));result=completed.result;}else result=request_(method,p);}catch(...){pending_=old;throw;}pending_=old;if(p.contains("expectedRevision")&&owned&&source()){captured_.revision=context_().revision;if(method=="envelope.bank.apply"||((method=="envelope.bank.save"||method=="envelope.bank.unlink")&&!draft())){const auto before=generation_;load(false);if(generation_==before+1&&!draft())*token=generation_;}}if(receipt)return completed;return Api::CompletedCall{method,captured_.document,context_().revision,std::move(result)};};
    auto request=[dispatch](const auto &method,const auto &params){return dispatch(method,params,{}).result;};
    auto write=[dispatch](const auto &method,const auto &params,const auto &receipt){return dispatch(method,params,receipt);};
    Json shape;const auto span=points().empty()?49u*256:points().back()[0].get<unsigned>()*256+1;if(!points().empty()){Json items=Json::array(),markers=Json::array();for(const auto &p:points())items.push_back({{"position",p[0].get<unsigned>()*256},{"value",p[1].get<double>()/64},{"curve","linear"}});for(const auto *key:markerKeys){const auto marker=envelope_.at(key).get<unsigned>();markers.push_back(marker==255?UINT32_MAX:points().at(marker)[0].get<unsigned>()*256);}unsigned flags=0;for(size_t i=0;i<5;++i)if(envelope_.at(std::array<const char *,5>{"enabled","loop","sustain","carry","filter"}[i]).get<bool>())flags|=1u<<i;shape={{"span",span},{"rowsPerBeat",4},{"points",items},{"instrument",true},{"flags",flags},{"markers",markers}};}
    bank_=std::make_unique<EnvelopeBankWindow>(window_,Json{{"kind",kinds[size_t(kind_)]},{"instrument",identity_}},shape,captured_.document,captured_.revision,L"Instrument "+std::to_wstring(index_)+L" · "+wide(kinds[size_t(kind_)]),std::move(request),[this]{const auto c=context_();return std::pair(c.document,c.revision);},std::move(source),std::move(write));bank_->show();}
  void selectPage(int page){if(page_==page)return;const auto previous=page_;page_=page;InvalidateRect(controls_.at(pageEnvelope+previous),nullptr,FALSE);InvalidateRect(controls_.at(pageEnvelope+page_),nullptr,FALSE);}
  bool contextMenu(HWND source,POINT screen)override{
    // Text and selectors keep their own native editing/navigation menus.
    for(auto child=source;child&&child!=window_;child=GetParent(child)){wchar_t type[32]{};GetClassNameW(child,type,32);if(!_wcsicmp(type,L"EDIT")||!_wcsicmp(type,L"COMBOBOX")||!_wcsicmp(type,L"LISTBOX"))return false;}
    if(contextOpen_||pending_||creationFrozen()||dragging_)return true;
    const bool keyboard=screen.x==-1&&screen.y==-1;POINT local=screen;
    const float scale=GetDpiForWindow(window_)/96.f;const auto r=canvas_.viewport;
    if(keyboard){
      const auto anchor=canvasVisible_?(selected_>=0&&size_t(selected_)<canvas_.handles.size()?canvas_.handles[size_t(selected_)]:AutomationCanvas::Point{r.x+r.w*.5f,r.y+r.h*.5f}):AutomationCanvas::Point{24,24};
      local={LONG(std::lround(anchor.x*scale)),LONG(std::lround(anchor.y*scale))};screen=local;ClientToScreen(window_,&screen);
    }else ScreenToClient(window_,&local);
    const float x=local.x/scale,y=local.y/scale;const WorkspaceRect hitBounds{r.x-7,r.y-7,r.w+14,r.h+14};
    const bool onCanvas=canvasVisible_&&hitBounds.contains(x,y);const int hit=onCanvas?canvas_.hit(x,y):selected_;
    const int at=points().empty()?0:int(std::clamp(std::round(canvas_.position(x)/256),0.,65535.));
    const int amount=int(std::clamp(std::lround(canvas_.value(y)*64),0l,64l));
    const bool fresh=current(),edit=fresh&&editable(),rawPoints=pointFields_||markerFields_,hasPoint=hit>=0&&size_t(hit)<points().size();
    const bool freeTick=std::none_of(points().begin(),points().end(),[&](const auto &p){return p[0]==at;});
    using Item=NativeContextMenu::Item;
    std::vector<Item> menu{{0,identity_.empty()?L"Instrument and envelopes":L"Instrument "+std::to_wstring(index_)+L" · "+wide(properties_.value("name",std::string{}))+L" · "+wide(kinds[size_t(kind_)])}};
    if(onCanvas||keyboard){
      menu.push_back({contextAdd,L"Add point here",edit&&!rawPoints&&onCanvas&&freeTick&&points().size()<envelope_.value("maxPoints",size_t(0))});
      menu.push_back({contextEdit,L"Edit point fields…",edit&&!rawPoints&&hasPoint});
      menu.push_back({contextDelete,L"Delete point",edit&&!rawPoints&&hasPoint});
    }
    menu.push_back({setPoint,L"Set point fields",edit&&pointFields_&&!markerFields_});
    menu.push_back({0,L""});menu.push_back({fit,L"Fit envelope\tHome",canvasVisible_&&!points().empty()});
    menu.push_back({pageTools,L"Envelope tools…",!identity_.empty()});
    menu.push_back({bank,L"Envelope bank…",edit&&!rawPoints&&!propertiesDirty_&&!mappingDirty_&&!mappingFields_});
    std::vector<Item> envelope;
    for(auto [id,title,key]:std::initializer_list<std::tuple<int,const wchar_t *,const char *>>{{enabled,L"Enabled","enabled"},{sustain,L"Sustain","sustain"},{loop,L"Loop","loop"},{carry,L"Carry","carry"},{filter,L"Filter","filter"}})
      envelope.push_back({id,title,edit&&(id!=filter||kind_==2)&&(!points().empty()||id==carry||id==filter),envelope_.value(key,false)});
    envelope.push_back({0,L""});envelope.push_back({adsr,L"ADSR preset",edit&&!rawPoints});envelope.push_back({clear,L"Clear points",edit&&!rawPoints&&!points().empty()});
    menu.push_back({0,L"Envelope",true,false,std::move(envelope)});
    menu.push_back({0,L"Instrument",true,false,{{pageProperties,L"Properties…"},{pageKeymap,L"Sample keymap…",!identity_.empty()},{0,L""},{newInstrument,L"New from sample",fresh&&!retainedDraft()},{importFile,L"Import instrument…",fresh&&!retainedDraft()}}});
    menu.push_back({0,L""});menu.push_back({apply,L"Apply instrument\tCtrl+Enter",fresh&&!identity_.empty()&&!rawPoints&&!mappingFields_});
    menu.push_back({reload,L"Reload\tCtrl+R"});menu.push_back({fromCursor,L"From cursor"});menu.push_back({audition,L"Audition…",fresh&&!identity_.empty()});
    if(hasWorkspaceDockAction()){menu.push_back({0,L""});menu.push_back({contextDock,docked()?L"Float editor":L"Dock beside tracker"});}
    menu.push_back({close,L"Hide editor"});
    const auto before=context_();const auto document=captured_.document,revision=captured_.revision,identity=identity_;
    const auto generation=generation_;const auto tools=toolSignature();const int kind=kind_,selected=selected_,page=page_,selectedKey=selectedKey_;const bool wasDocked=docked();
    releaseMusicalInput();contextOpen_=true;struct Guard{bool &value;~Guard(){value=false;}}guard{contextOpen_};
    const int command=NativeContextMenu::show(window_,screen,menu);if(!command)return true;
    const auto after=context_();require(visible()&&!pending_&&!dragging_&&before.document==after.document&&before.revision==after.revision&&document==captured_.document&&revision==captured_.revision&&identity==identity_&&kind==kind_&&generation==generation_&&tools==toolSignature()&&selected==selected_&&selectedKey==selectedKey_&&page==page_&&wasDocked==docked(),"Song, target or draft changed while the menu was open / action cancelled");
    if(command==contextDock){toggleWorkspaceDock();return true;}
    if(command==contextAdd){selected_=-1;setting_=true;set(tick,at);set(value,amount);setting_=false;setPointFields();SetFocus(window_);}
    else if(command==contextEdit||command==contextDelete){selected_=hit;showPoint();if(command==contextDelete){removePoint();SetFocus(window_);}else{if(compact_)selectPage(pagePoints-pageEnvelope);layout();SetFocus(controls_.at(tick));}}
    else if(command>=pageEnvelope&&command<=pageKeymap){selectPage(command-pageEnvelope);layout();SetFocus(controls_.at(compact_?command:command==pageTools?tool:command==pageKeymap?keymap:name));}
    else action(command,BN_CLICKED);
    layout();requestPaint();return true;
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;if(id==shortOptions&&notification==BN_CLICKED){if(dragging_)cancelDrag();shortEnvelopeOptions_=!shortEnvelopeOptions_;layout();requestPaint();return;}if(id>=pageEnvelope&&id<=pageKeymap&&notification==BN_CLICKED){if(dragging_)cancelDrag();selectPage(id-pageEnvelope);layout();requestPaint();return;}if(id==audition){require(current(),"Instrument changed / Reload before audition");audition_(index_,identity_,captured_.document,captured_.revision);return;}if(id==close){if(dragging_)cancelDrag();hide();return;}
    if(notification==EN_CHANGE){if(id==tick||id==value){pointFields_=true;++generation_;}else if(id>=loopStart&&id<=release){markerFields_=true;++generation_;}else if(id>=name&&id<=fade){propertiesDirty_=true;++generation_;}else if(id==mapFrom||id==mapTo){mappingFields_=true;++generation_;}else if(id==rangeStart||id==rangeEnd||(id>=toolValue0&&id<=toolValue4)){toolFieldsDirty_=true;++generation_;}return;}if(pending_||creationWorking_)return;
    if(creationCompletion_.retained()){if(id==reload||id==importFile||id==newInstrument)reviewCreation();else if(id==fromCursor&&creationNeedsAcknowledgement_)acknowledgeCreation();else throw std::runtime_error("Review the retained instrument result first");return;}
    if(id==keymap&&notification==LBN_SELCHANGE){if(mappingFields_){ScreamSeq::NativeInputGate::present(controls_.at(keymap),LB_SETCURSEL,selectedKey_,0);throw std::runtime_error("Stage or discard the key range fields before selecting another note");}selectedKey_=int(SendMessageW(controls_.at(keymap),LB_GETCURSEL,0,0));showMapFields();return;}
    if(notification==CBN_SELCHANGE){if(id==instrument||id==kind){if(draft()){choices();throw std::runtime_error("Apply or Reload the instrument draft before changing target");}try{if(id==instrument){const auto i=selection(id);if(i>=0)load(false,captured_.instruments.at(size_t(i)).at("id"));}else load(false,{},std::clamp(selection(kind),0,2));}catch(...){choices();throw;}return;}
      if(id==node){if(pointFields_){choose(node,selected_);throw std::runtime_error("Set or discard the point fields first");}selected_=selection(node);showPoint();requestPaint();return;}if(id==tool){tool_=std::clamp(selection(tool),0,8);toolFields();return;}if(id>=nna&&id<=dna){propertiesDirty_=true;++generation_;return;}if(id==mapSample){mappingFields_=true;++generation_;return;}}
    if(notification!=BN_CLICKED)return;
    if(id==reload||id==fromCursor){load(id==fromCursor);toolFieldsDirty_=false;}else if(id==apply)commit();else if(id==newInstrument)createInstrument();else if(id==importFile)importInstrument();else if(id==bank)openBank();else if(id==setPoint)setPointFields();else if(id==setMarkers)setMarkerFields();else if(id==deletePoint)removePoint();
    else if(id==addPoint){require(!pointFields_,"Set or discard the pending point first");selected_=-1;showPoint();if(compact_){selectPage(pagePoints-pageEnvelope);layout();}SetFocus(controls_.at(tick));}
    else if(id>=enabled&&id<=filter){require(editable(),"Choose an editable envelope");const auto *key=std::array<const char *,5>{"enabled","sustain","loop","carry","filter"}.at(size_t(id-enabled));require(id!=filter||kind_==2,"Filter mode belongs to the pitch envelope");require(!points().empty()||id==carry||id==filter,"Add points before enabling envelope playback or loops");envelope_[key]=!envelope_.at(key).get<bool>();changed();}
    else if(id==adsr||id==clear){require(editable()&&!pointFields_&&!markerFields_,"Set or discard pending point and marker fields first");envelope_["points"]=id==adsr?Json::array({Json::array({0,0}),Json::array({2,64}),Json::array({12,48}),Json::array({32,48}),Json::array({48,0})}):Json::array();envelope_["enabled"]=id==adsr;envelope_["sustain"]=false;envelope_["loop"]=false;for(const auto *key:markerKeys)envelope_[key]=std::string_view(key)=="releaseNode"?255:0;selected_=id==adsr?0:-1;changed();showPoint();showMarkers();fitCurve();}
    else if(id==copyRange||id==previewTool)transform(id==copyRange);
    else if(id==mapStage){require(!identity_.empty(),"Choose an instrument");const auto first=whole(mapFrom,0,127),last=whole(mapTo,0,127),selected=selection(mapSample);require(first<=last&&selected>=0,"Choose an ordered note range and sample");const auto sample=selected?captured_.samples.at(size_t(selected-1)).at("index").get<unsigned>():0u;auto next=mapping_;for(int n=first;n<=last;++n)next[size_t(n)]=sample;if(next!=mapping_){mapping_=std::move(next);mappingDirty_=true;++generation_;status(L"Keymap range staged / Apply saves it with the instrument");}mappingFields_=false;showKeymap();}
    else if(id==fit||id==zoomIn||id==zoomOut||id==panLeft||id==panRight){if(id==fit)fitCurve();else if(id==panLeft||id==panRight){canvas_.pan((canvas_.end-canvas_.start)*(id==panLeft?-.25:.25),65536);rebuild();}else{canvas_.zoom(id==zoomIn?2:.5,65536);rebuild();}}
  }
  void cancelDrag(){if(!dragging_)return;dragging_=false;envelope_=dragBefore_;envelopeDirty_=dragDirty_;selected_=dragSelection_;++generation_;showPoint();showMarkers();rebuild();ReleaseCapture();}
  void movePoint(int at,int amount){if(selected_<0)return;const auto &p=points();if(selected_==0)at=0;else{const int low=p[size_t(selected_-1)][0].get<int>()+1,high=size_t(selected_+1)<p.size()?p[size_t(selected_+1)][0].get<int>()-1:65535;at=low<=high?std::clamp(at,low,high):p[size_t(selected_)][0].get<int>();}Json point=Json::array({at,std::clamp(amount,0,64)});if(point!=p[size_t(selected_)]){envelope_["points"][size_t(selected_)]=point;changed();}showPoint();}
  void mouse(UINT message,float x,float y,WPARAM)override{
    if(message==WM_CAPTURECHANGED){cancelDrag();return;}if(message==WM_LBUTTONUP){dragging_=false;ReleaseCapture();return;}if(!canvasVisible_||pending_||creationFrozen()||pointFields_||markerFields_)return;
    const auto r=canvas_.viewport;const WorkspaceRect hitBounds{r.x-7,r.y-7,r.w+14,r.h+14};
    if(message==WM_LBUTTONDOWN&&hitBounds.contains(x,y)){SetFocus(window_);selected_=canvas_.hit(x,y);showPoint();if(selected_>=0&&editable()){dragBefore_=envelope_;dragDirty_=envelopeDirty_;dragSelection_=selected_;dragging_=true;SetCapture(window_);}requestPaint();return;}
    if(message==WM_LBUTTONDBLCLK&&canvas_.viewport.contains(x,y)&&editable()&&canvas_.hit(x,y)<0){selected_=-1;setting_=true;set(tick,points().empty()?0:int(std::clamp(std::round(canvas_.position(x)/256),0.,65535.)));set(value,int(std::lround(canvas_.value(y)*64)));setting_=false;setPointFields();return;}
    if(message==WM_MOUSEMOVE&&dragging_)movePoint(int(std::clamp(std::round(canvas_.position(x)/256),0.,65535.)),int(std::lround(canvas_.value(y)*64)));
  }
  bool wheel(UINT message,float x,float y,WPARAM w)override{if(!canvasVisible_||!canvas_.viewport.contains(x,y)||dragging_)return false;const auto delta=GET_WHEEL_DELTA_WPARAM(w)/120.;if(GET_KEYSTATE_WPARAM(w)&MK_CONTROL)canvas_.zoom(std::pow(1.25,delta),65536);else canvas_.pan(delta*(message==WM_MOUSEHWHEEL?1:-1)*(canvas_.end-canvas_.start)*.1,65536);rebuild();return true;}
  bool key(WPARAM k,bool ctrl,bool shift)override{if(k==VK_ESCAPE){if(dragging_)cancelDrag();else if(pointFields_||markerFields_||mappingFields_){++generation_;showPoint();showMarkers();showMapFields();}else hide();return true;}if(k==VK_F6){const auto entry=compact_?std::array<int,5>{kind,node,tool,name,keymap}.at(size_t(page_)):node;SetFocus(GetFocus()==window_?controls_.at(entry):window_);requestPaint();return true;}if(ctrl&&k==VK_RETURN){action(apply,BN_CLICKED);return true;}if(ctrl&&k=='R'){action(reload,BN_CLICKED);return true;}
    if(k==VK_RETURN){const auto id=GetDlgCtrlID(GetFocus());if(id==tick||id==value){action(setPoint,BN_CLICKED);return true;}if(id==mapFrom||id==mapTo||id==mapSample){action(mapStage,BN_CLICKED);return true;}if(id>=loopStart&&id<=release){action(setMarkers,BN_CLICKED);return true;}wchar_t cls[32]{};GetClassNameW(GetFocus(),cls,32);if(_wcsicmp(cls,L"Button")==0){action(id,BN_CLICKED);return true;}}
    if(GetFocus()!=window_||pending_||creationFrozen()||!canvasVisible_)return false;if(ctrl&&(k==VK_LEFT||k==VK_RIGHT)){action(k==VK_LEFT?panLeft:panRight,BN_CLICKED);return true;}if(ctrl)return false;if(k==VK_HOME){action(fit,BN_CLICKED);return true;}if(k==VK_OEM_PLUS||k==VK_ADD||k==VK_OEM_MINUS||k==VK_SUBTRACT){action(k==VK_OEM_PLUS||k==VK_ADD?zoomIn:zoomOut,BN_CLICKED);return true;}if(!editable()||pointFields_||markerFields_)return false;
    if(k==VK_DELETE){removePoint();return true;}if(k==VK_INSERT){action(addPoint,BN_CLICKED);return true;}if(k==VK_TAB&&!points().empty()){selected_=(selected_+(shift?-1:1)+int(points().size()))%int(points().size());showPoint();requestPaint();return true;}
    if(selected_>=0&&(k==VK_LEFT||k==VK_RIGHT||k==VK_UP||k==VK_DOWN)){const int amount=shift?4:1;movePoint(points()[size_t(selected_)][0].get<int>()+(k==VK_LEFT?-amount:k==VK_RIGHT?amount:0),points()[size_t(selected_)][1].get<int>()+(k==VK_UP?amount:k==VK_DOWN?-amount:0));return true;}return false;}
  void layoutShortDock(float w,float h){
    // Region chrome is outside this body. Retain every field HWND and draft;
    // the Options view is presentation state, never an envelope reload.
    std::vector<int> shown;auto at=[&](int id,float x,float y,float width,float height=26){shown.push_back(id);place(id,x,y,width,height);};
    const float left=12,width=w-24,gap=4,half=(width-gap)/2,third=(width-2*gap)/3;
    at(instrument,left,6,width-(page_==0?96:0),240);
    if(page_==0){at(shortOptions,w-104,6,92);set(shortOptions,shortEnvelopeOptions_?L"Curve":L"Options");}
    const float tab=(width-4*gap)/5;for(int i=0;i<5;++i)at(pageEnvelope+i,left+i*(tab+gap),36,tab,24);
    const float action=(width-4*gap)/5;int column=0;
    for(int id:{apply,reload,fromCursor,audition,close})at(id,left+(action+gap)*column++,h-48,action,26);
    at(statusLabel,left,h-20,width,18);
    canvasVisible_=page_==0&&!shortEnvelopeOptions_;
    if(page_==0&&!shortEnvelopeOptions_){
      const float kindWidth=width-64-82-72-3*gap;
      at(kind,left,64,kindWidth,220);at(enabled,left+kindWidth+gap,64,64,24);
      at(sustain,w-12-72-gap-82,64,82,24);at(loop,w-12-72,64,72,24);
      canvas_.viewport={42,104,w-54,std::max(100.f,h-200)};
      float x=left;for(auto [id,labelId,fieldWidth]:std::initializer_list<std::tuple<int,int,float>>{{node,nodeLabel,100.f},{tick,tickLabel,70.f},{value,valueLabel,70.f},{setPoint,0,88.f},{deletePoint,0,width-344.f}}){
        if(labelId)at(labelId,x,h-94,fieldWidth,14);at(id,x,h-80,fieldWidth,id==node?240:26);x+=fieldWidth+gap;
      }
    }else if(page_==0){
      at(targetLabel,left,66,width,18);at(kind,left,88,width,220);
      for(int i=0;i<3;++i)at(enabled+i,left+(third+gap)*i,120,third,24);
      at(carry,left,150,third,24);at(filter,left+third+gap,150,third,24);at(adsr,left+2*(third+gap),150,third,24);
      at(clear,left,180,half);at(bank,left+half+gap,180,half);
      const float nav=(width-4*gap)/5;int index=0;for(int id:{panLeft,zoomOut,fit,zoomIn,panRight})at(id,left+(nav+gap)*index++,214,nav,26);
    }else if(page_==1){
      for(auto [id,labelId,index]:std::initializer_list<std::tuple<int,int,int>>{{node,nodeLabel,0},{tick,tickLabel,1},{value,valueLabel,2}}){at(labelId,left+(third+gap)*index,66,third,16);at(id,left+(third+gap)*index,84,third,id==node?240:26);}
      at(setPoint,left,114,third);at(addPoint,left+third+gap,114,third);at(deletePoint,left+2*(third+gap),114,third);
      for(auto [id,labelId,index]:std::initializer_list<std::tuple<int,int,int>>{{loopStart,loopStartLabel,0},{loopEnd,loopEndLabel,1},{release,releaseLabel,2}}){at(labelId,left+(third+gap)*index,144,third,18);at(id,left+(third+gap)*index,162,third);}
      for(int i=0;i<2;++i){at(sustainStartLabel+i,left+(third+gap)*i,192,third,18);at(sustainStart+i,left+(third+gap)*i,210,third);}at(setMarkers,left+2*(third+gap),210,third);
    }else if(page_==2){
      at(rangeLabel,left,68,84,22);at(rangeStart,left+88,64,(width-92)/2);at(rangeEnd,left+92+(width-92)/2,64,(width-92)/2);
      at(tool,left,96,width,230);
      const int count=std::array<int,9>{0,0,1,2,2,5,3,1,1}.at(size_t(tool_));
      for(int i=0;i<count;++i){const float x=left+(third+gap)*(i%3),y=128+44.f*(i/3);at(toolLabel0+i,x,y,third,18);at(toolValue0+i,x,y+18,third);}
      at(copyRange,left,216,half);at(previewTool,left+half+gap,216,half);
    }else if(page_==3){
      at(newInstrument,left,66,half);at(importFile,left+half+gap,66,half);
      at(nameLabel,left,96,width,18);at(name,left,114,width);
      for(int i=0;i<3;++i){const float x=left+(third+gap)*i;at(volumeLabel+i,x,144,third,18);at(volume+i,x,162,third);at(nnaLabel+i,x,192,third,18);at(nna+i,x,210,third,230);}
    }else{
      at(keymapLabel,left,66,width,18);at(keymap,left,86,width,std::max(64.f,h-234));
      at(mapLabel,left,h-146,width,18);at(mapFrom,left,h-126,half);at(mapTo,left+half+gap,h-126,half);
      at(mapSample,left,h-94,width-142,230);at(mapStage,w-150,h-94,138);
    }
    for(const auto &[id,control]:controls_)if(std::find(shown.begin(),shown.end(),id)==shown.end())NativeControls::show(control,false);
    if(canvasVisible_)rebuild();
  }
  void layoutCompact(float w,float h){
    // Pages only change visibility and geometry. The original HWNDs, field text,
    // selections, keymap scroll and captured draft remain owned by this editor.
    std::vector<int> shown;auto at=[&](int id,float x,float y,float width,float height=26){shown.push_back(id);place(id,x,y,width,height);};
    const float left=12,width=w-24,gap=8,half=(width-gap)/2,third=(width-2*gap)/3,bottom=h-92;
    at(heading,left,10,width-74,22);at(close,w-72,8,60);at(instrument,left,42,width,260);at(targetLabel,left,74,width,20);
    const float tabWidth=(width-16)/5;for(int i=0;i<5;++i)at(pageEnvelope+i,left+(tabWidth+4)*i,102,tabWidth,28);
    at(apply,left,h-80,half,28);at(reload,left+half+gap,h-80,half,28);at(fromCursor,left,h-48,half,28);at(audition,left+half+gap,h-48,half,28);at(statusLabel,left,h-18,width,18);
    canvasVisible_=page_==0;
    if(page_==0){
      at(kind,left,140,width-140,210);at(bank,w-144,140,132);
      for(int i=0;i<3;++i)at(enabled+i,left+(third+gap)*i,174,third);
      at(carry,left,206,third);at(filter,left+third+gap,206,third);at(adsr,left+2*(third+gap),206,third);
      canvas_.viewport={42,258,w-54,std::max(56.f,bottom-306)};
      at(clear,left,bottom-28,104);float x=left+112;const float navWidth=(width-112-16)/5;
      for(int id:{panLeft,zoomOut,fit,zoomIn,panRight}){at(id,x,bottom-28,navWidth);x+=navWidth+4;}
    }else if(page_==1){
      for(auto [id,labelId,column]:std::initializer_list<std::tuple<int,int,int>>{{node,nodeLabel,0},{tick,tickLabel,1},{value,valueLabel,2}}){at(labelId,left+(third+gap)*column,140,third,18);at(id,left+(third+gap)*column,160,third,id==node?240:26);}
      at(setPoint,left,194,third);at(addPoint,left+third+gap,194,third);at(deletePoint,left+2*(third+gap),194,third);
      for(auto [id,labelId,column]:std::initializer_list<std::tuple<int,int,int>>{{loopStart,loopStartLabel,0},{loopEnd,loopEndLabel,1},{release,releaseLabel,2}}){at(labelId,left+(third+gap)*column,236,third,18);at(id,left+(third+gap)*column,256,third);}
      for(int i=0;i<2;++i){at(sustainStartLabel+i,left+(third+gap)*i,288,third,18);at(sustainStart+i,left+(third+gap)*i,308,third);}
      at(setMarkers,left+2*(third+gap),308,third);
    }else if(page_==2){
      at(rangeLabel,left,140,88,22);const float rangeWidth=(width-104)/2;at(rangeStart,left+96,140,rangeWidth);at(rangeEnd,left+104+rangeWidth,140,rangeWidth);
      at(tool,left,174,half,230);at(copyRange,left+half+gap,174,half);
      const int count=std::array<int,9>{0,0,1,2,2,5,3,1,1}.at(size_t(tool_));
      for(int i=0;i<count;++i){const float x=left+(half+gap)*(i%2),y=208+44.f*(i/2);at(toolLabel0+i,x,y,half,18);at(toolValue0+i,x,y+18,half);}
      at(previewTool,left,bottom-28,width);
    }else if(page_==3){
      at(newInstrument,left,140,half);at(importFile,left+half+gap,140,half);
      at(nameLabel,left,184,width,18);at(name,left,204,width);
      for(int i=0;i<3;++i){const float x=left+(third+gap)*i;at(volumeLabel+i,x,244,third,18);at(volume+i,x,264,third);at(nnaLabel+i,x,304,third,18);at(nna+i,x,324,third,230);}
    }else{
      at(keymapLabel,left,140,width,20);at(keymap,left,164,width,std::max(64.f,bottom-284));
      at(mapLabel,left,bottom-104,width,18);at(mapFrom,left,bottom-82,half);at(mapTo,left+half+gap,bottom-82,half);
      at(mapSample,left,bottom-48,width-142,230);at(mapStage,w-146,bottom-48,134);
    }
    for(const auto &[id,control]:controls_)if(std::find(shown.begin(),shown.end(),id)==shown.end())NativeControls::show(control,false);
    if(canvasVisible_)rebuild();
  }
  void layoutFull(float w,float h){
    canvasVisible_=true;for(int id=pageEnvelope;id<=shortOptions;++id)NativeControls::show(controls_.at(id),false);
    place(heading,18,14,w-36,24);place(instrument,18,48,280,260);place(kind,306,48,210,210);place(newInstrument,524,48,154,26);place(bank,686,48,146,26);place(importFile,840,48,158,26);place(targetLabel,18,123,w-36,24);
    float x=18;for(auto [id,width]:std::initializer_list<std::pair<int,int>>{{enabled,120},{sustain,104},{loop,94},{carry,94},{filter,118},{adsr,112},{clear,112}}){place(id,x,88,width,26);x+=width+8;}
    canvas_.viewport={42,174,w-364,std::max(100.f,h-568)};place(keymapLabel,w-302,150,284,22);place(keymap,w-302,174,284,canvas_.viewport.h);rebuild();const float y=h-376;place(nodeLabel,18,y-18,145,18);place(node,18,y,145,240);place(tickLabel,174,y-18,88,18);place(tick,174,y,86,26);place(valueLabel,268,y-18,90,18);place(value,268,y,86,26);place(setPoint,362,y,94,26);place(addPoint,464,y,84,26);place(deletePoint,556,y,96,26);x=w-230;for(auto [id,width]:std::initializer_list<std::pair<int,int>>{{panLeft,32},{zoomOut,32},{fit,42},{zoomIn,32},{panRight,32}}){place(id,x,y,width,26);x+=width+4;}
    const float my=h-324;for(int i=0;i<5;++i){place(loopStartLabel+i,18+106.f*i,my-18,102,18);place(loopStart+i,18+106.f*i,my,i==4?118:94,26);}place(setMarkers,574,my,126,26);
    const float ty=h-274;place(rangeLabel,18,ty,80,22);place(rangeStart,102,ty,84,26);place(rangeEnd,194,ty,84,26);place(tool,290,ty,178,230);place(copyRange,476,ty,116,26);place(previewTool,600,ty,124,26);
    const int count=std::array<int,9>{0,0,1,2,2,5,3,1,1}.at(size_t(tool_));for(int i=0;i<5;++i){place(toolLabel0+i,18+190.f*i,h-232,180,18,i<count);place(toolValue0+i,18+190.f*i,h-212,180,26,i<count);}
    place(nameLabel,18,h-176,220,18);place(name,18,h-158,220,26);x=246;for(auto [id,width]:std::initializer_list<std::pair<int,int>>{{volume,70},{pan,76},{fade,84},{nna,136},{dct,136},{dna,136}}){place(volumeLabel+(id-volume),x,h-176,width,18);place(id,x,h-158,width,id>=nna?230:26);x+=width+8;}
    place(mapLabel,18,h-113,202,18);place(mapFrom,18,h-92,76,26);place(mapTo,102,h-92,76,26);place(mapSample,186,h-92,276,230);place(mapStage,470,h-92,134,26);
    place(apply,18,h-52,148,28);place(reload,174,h-52,118,28);place(fromCursor,300,h-52,126,28);place(audition,434,h-52,126,28);place(close,w-118,h-52,100,28);place(statusLabel,18,h-22,w-36,20);
  }
  void layout()override{
    if(bank_)bank_->refreshSourceState();
    const auto focus=GetFocus();const auto [w,h]=size();const bool wasShort=shortDock_;compact_=w<1080||h<780;shortDock_=docked()&&h<500;
    if(shortDock_&&!wasShort){
      // The wide form exposes every section. Keep its focused field visible
      // when the same HWND first moves into a short, paged dock body.
      const auto id=GetDlgCtrlID(focus);
      if(focus==window_){selectPage(0);shortEnvelopeOptions_=false;}
      else if(focus&&IsChild(window_,focus)){
        if((id>=name&&id<=dna)||id==newInstrument||id==importFile)selectPage(3);
        else if((id>=mapFrom&&id<=mapStage)||id==keymap)selectPage(4);
        else if(id>=tool&&id<=previewTool)selectPage(2);
        else if((id>=loopStart&&id<=setMarkers)||id==addPoint)selectPage(1);
        else if((id>=carry&&id<=clear)||id==bank||(id>=fit&&id<=panRight)){selectPage(0);shortEnvelopeOptions_=true;}
        else if(id>=node&&id<=deletePoint){if(page_!=1&&(page_!=0||shortEnvelopeOptions_))selectPage(1);}
        else if(id==kind||(id>=enabled&&id<=loop))selectPage(0);
      }
    }
    if(wasShort&&!shortDock_&&compact_&&focus&&IsChild(window_,focus)){
      // The short Curve page has inline point fields. In a compact floating
      // window those same HWNDs live on Points; keep the focused draft visible.
      const auto id=GetDlgCtrlID(focus);if(id>=node&&id<=deletePoint)selectPage(1);
    }
    if(shortDock_)layoutShortDock(w,h);else if(compact_)layoutCompact(w,h);else layoutFull(w,h);
    if(focus&&IsChild(window_,focus)&&!IsWindowVisible(focus))SetFocus(controls_.at(compact_?pageEnvelope+page_:instrument));
    const bool inlinePoint=shortDock_&&page_==0&&!shortEnvelopeOptions_;
    set(apply,shortDock_?L"Apply":L"Apply instrument");set(fromCursor,shortDock_?L"Cursor":L"From cursor");set(audition,shortDock_?L"Audition":L"Audition…");
    set(nodeLabel,inlinePoint?L"Point":L"Selected node");set(tickLabel,inlinePoint?L"Tick":L"Tick / 0–65535");set(valueLabel,inlinePoint?L"Value":L"Value / 0–64");set(deletePoint,inlinePoint?L"Delete":L"Delete point");
    for(auto [id,title,key]:std::initializer_list<std::tuple<int,const wchar_t *,const char *>>{{enabled,L"Envelope","enabled"},{sustain,L"Sustain","sustain"},{loop,L"Loop","loop"},{carry,L"Carry","carry"},{filter,L"Filter","filter"}})set(id,std::wstring(inlinePoint&&id==enabled?L"Env":title)+(envelope_.value(key,false)?(inlinePoint?L" on":L" · on"):(inlinePoint?L" off":L" · off")));
    for(auto [id,control]:controls_)if(id>=instrument&&id<=keymap)EnableWindow(control,!pending_||id==close);for(int id=enabled;id<=previewTool;++id)EnableWindow(controls_.at(id),!pending_&&editable());for(int id=name;id<=apply;++id)EnableWindow(controls_.at(id),!pending_&&!identity_.empty());EnableWindow(controls_.at(keymap),!pending_&&!identity_.empty());EnableWindow(controls_.at(filter),!pending_&&editable()&&kind_==2);EnableWindow(controls_.at(bank),!pending_&&editable());
    if(creationFrozen()){
      for(auto [id,control]:controls_)if(id>=instrument&&id<=keymap)EnableWindow(control,id==close);
      EnableWindow(controls_.at(reload),!creationWorking_&&!pending_);
      EnableWindow(controls_.at(fromCursor),!creationWorking_&&!pending_&&creationNeedsAcknowledgement_);
    }
    set(reload,creationCompletion_.retained()?(shortDock_?L"Review":L"Review result"):L"Reload");
    if(creationCompletion_.retained())set(fromCursor,shortDock_?L"Accept":L"Use current song");
    // Native disabling clears focus during a document request. Restore only
    // that lost focus, never a newer choice made while the request was pending.
    if(pending_&&focus&&IsChild(window_,focus)&&!GetFocus())pendingFocus_=focus;
    if(!pending_&&!creationFrozen()&&pendingFocus_){const auto restore=pendingFocus_;pendingFocus_=nullptr;if(!GetFocus()&&IsWindowVisible(restore)&&IsWindowEnabled(restore)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(restore);}
  }
  void drawControl(const DRAWITEMSTRUCT &d)override{
    NativeToolWindow::drawControl(d);if(d.CtlType!=ODT_BUTTON||d.CtlID!=unsigned(pageEnvelope+page_))return;
    RECT r=d.rcItem;r.top=r.bottom-std::max(2,int(2*GetDpiForWindow(window_)/96));SetDCBrushColor(d.hDC,NativeControls::highContrast()?GetSysColor(COLOR_HIGHLIGHT):RGB(110,218,197));FillRect(d.hDC,&r,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  }
  void paint(RenderSurface &s)override{
    const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);if(compact_){if(!shortDock_)s.line(12,134,w-12,134,0x334757);s.line(12,h-(shortDock_?52:88),w-12,h-(shortDock_?52:88),0x334757);}if(!canvasVisible_)return;const auto r=canvas_.viewport;s.fill(r.x,r.y,r.w,r.h,0x101923);s.clip(r.x,r.y,r.w,r.h);
    for(int v=0;v<=64;v+=16){const auto y=canvas_.screen(0,v/64.).y;s.line(r.x,y,r.x+r.w,y,0x2a3947);}const auto step=std::max(256.,std::ceil((canvas_.end-canvas_.start)/4096)*256);for(double t=std::ceil(canvas_.start/step)*step;t<=canvas_.end;t+=step){const auto x=canvas_.screen(t,0).x;s.line(x,r.y,x,r.y+r.h,0x2a3947);}
    auto marker=[&](const char *key,uint32_t color){if(envelope_.empty())return;const auto index=envelope_.value(key,255u);if(index>=points().size())return;const auto x=canvas_.screen(points()[index][0].get<unsigned>()*256,0).x;s.line(x,r.y,x,r.y+r.h,color,1.5f);};
    if(envelope_.value("loop",false)){marker("loopStart",0xb9a46a);marker("loopEnd",0xb9a46a);}if(envelope_.value("sustain",false)){marker("sustainPoint",0x638dce);marker("sustainEnd",0x638dce);}marker("releaseNode",0xc787a4);
    for(size_t i=1;i<canvas_.curve.size();++i)s.line(canvas_.curve[i-1].x,canvas_.curve[i-1].y,canvas_.curve[i].x,canvas_.curve[i].y,envelope_.value("enabled",false)?0x6edac5:0x647c89,2);
    for(const auto tick:playbackTicks_){const auto x=canvas_.screen(tick*256,0).x;s.line(x,r.y,x,r.y+r.h,0xe4eff6,1.5f);}
    for(size_t i=0;i<canvas_.handles.size();++i){const auto p=canvas_.handles[i];s.fill(p.x-4,p.y-4,8,8,int(i)==selected_?0xffd08a:0x6edac5);}s.unclip();s.outline(r.x,r.y,r.w,r.h,GetFocus()==window_?0x6edac5:0x334757);for(int v=0;v<=64;v+=16)s.uiText(std::to_wstring(v),8,canvas_.screen(0,v/64.).y-7,30,0x93aabd);
    wchar_t text[180]{};swprintf_s(text,compact_?L"Ticks %.2f–%.2f":L"Ticks %.2f–%.2f · loop: gold · sustain: blue · release: rose · playback: white",canvas_.start/256,canvas_.end/256);s.uiText(text,r.x,r.y-(shortDock_?15:23),r.w,0x93aabd);
  }
public:
  InstrumentEnvelopeWindow(HWND owner,Request request,std::function<Context()> context,Audition auditionCallback,SelectSound selectSound)
    :InstrumentEnvelopeWindow(owner,request,context,std::move(auditionCallback),std::move(selectSound),
      [request,context](const auto &method,const auto &params){const auto before=context();auto result=request(method,params);return Api::CompletedCall{method,before.document,context().revision,std::move(result)};}){}
  InstrumentEnvelopeWindow(HWND owner,Request request,std::function<Context()> context,Audition auditionCallback,SelectSound selectSound,NativeWriteCompletion::Write write):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),audition_(std::move(auditionCallback)),selectSound_(std::move(selectSound)){
    creationWrite_=std::move(write);
    minimumClientWidth_=440;minimumClientHeight_=500;create(L"ScreamSeq.InstrumentEnvelope",L"Instrument & envelopes",1200,880,true);
    for(int id:{instrument,kind,node,tool,nna,dct,dna,mapSample})combo(id);add(keymap,L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);label(keymapLabel,L"Sample keymap · select a note");button(importFile,L"Import instrument…");for(int id:{tick,value,loopStart,loopEnd,sustainStart,sustainEnd,release,rangeStart,rangeEnd,toolValue0,toolValue1,toolValue2,toolValue3,toolValue4,name,volume,pan,fade,mapFrom,mapTo})edit(id,L"",id==name?200:32);
    auto items=[&](int id,std::initializer_list<const wchar_t *> names){for(auto name:names)ScreamSeq::NativeInputGate::present(controls_.at(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));choose(id,0);};items(kind,{L"Volume envelope",L"Pan envelope",L"Pitch / filter envelope"});items(tool,{L"Flip time",L"Flip values",L"Shift",L"Scale",L"Ramp",L"Sine",L"Humanize",L"Paste",L"Insert paste"});items(nna,{L"Cut",L"Continue",L"Note off",L"Fade"});items(dct,{L"Off",L"Note",L"Sample",L"Instrument",L"Plugin"});items(dna,{L"Cut",L"Note off",L"Fade"});
    for(auto [id,title]:std::initializer_list<std::pair<int,const wchar_t *>>{{newInstrument,L"New from sample"},{bank,L"Envelope bank…"},{enabled,L"Envelope"},{sustain,L"Sustain"},{loop,L"Loop"},{carry,L"Carry"},{filter,L"Filter"},{adsr,L"ADSR preset"},{clear,L"Clear points"},{setPoint,L"Set point"},{addPoint,L"Add point"},{deletePoint,L"Delete point"},{setMarkers,L"Set markers"},{fit,L"Fit"},{zoomOut,L"−"},{zoomIn,L"+"},{panLeft,L"‹"},{panRight,L"›"},{copyRange,L"Copy range"},{previewTool,L"Preview tool"},{mapStage,L"Stage key range"},{apply,L"Apply instrument"},{reload,L"Reload"},{fromCursor,L"From cursor"},{close,L"Close"},{audition,L"Audition…"}})button(id,title);
    for(auto [id,title]:std::initializer_list<std::pair<int,const wchar_t *>>{{pageEnvelope,L"Envelope"},{pagePoints,L"Points"},{pageTools,L"Tools"},{pageProperties,L"Instrument"},{pageKeymap,L"Keymap"}})button(id,title);
    button(shortOptions,L"Options");
    for(auto [id,title]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"Instrument and envelopes"},{targetLabel,L""},{nodeLabel,L"Selected node"},{tickLabel,L"Tick / 0–65535"},{valueLabel,L"Value / 0–64"},{loopStartLabel,L"Loop start"},{loopEndLabel,L"Loop end"},{sustainStartLabel,L"Sustain start"},{sustainEndLabel,L"Sustain end"},{releaseLabel,L"Release / 255: none"},{rangeLabel,L"Tick range"},{nameLabel,L"Name"},{volumeLabel,L"Volume / 64"},{panLabel,L"Pan / 256"},{fadeLabel,L"Fade / 32768"},{nnaLabel,L"New note action"},{dctLabel,L"Duplicate check"},{dnaLabel,L"Duplicate action"},{mapLabel,L"Keymap range / notes 0–127"},{statusLabel,L""}})label(id,title);for(int id=toolLabel0;id<=toolLabel4;++id)label(id,L"");
    for(int id:{targetLabel,statusLabel})SetWindowLongPtrW(controls_.at(id),GWL_STYLE,GetWindowLongPtrW(controls_.at(id),GWL_STYLE)|SS_ENDELLIPSIS|SS_NOPREFIX);
    set(mapFrom,L"0");set(mapTo,L"119");set(rangeStart,L"0");set(rangeEnd,L"49");finish();
  }
  void openAt(){const bool retain=visible()||retainedDraft();show();if(!retain)load(true);SetFocus(controls_.at(instrument));}
  void initializeHidden(){
    require(!(GetWindowLongPtrW(window_,GWL_STYLE)&WS_VISIBLE)&&!docked()&&captured_.document.empty()&&generation_==0&&!retainedDraft(),"Initialize only a fresh hidden instrument editor");
    load(true);
  }
  bool retainedDraft()const{return pending_||creationFrozen()||draft()||toolFieldsDirty_||dragging_||(bank_&&bank_->retainedDraft());}
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    if(creationFrozen())return describeDraft(creationTarget_.value("documentId",std::string()),creationTarget_.value("expectedRevision",std::string()),
      Json::array({creationTarget_,identity_,kind_}).dump(),generation_,draft()||toolFieldsDirty_||dragging_,pending_||creationWorking_,!pending_&&!creationWorking_&&creationCompletion_.retained());
    return describeDraft(captured_.document,captured_.revision,Json::array({identity_,kind_}).dump(),generation_,
      draft()||toolFieldsDirty_||dragging_,pending_);
  }
  bool followCursor(){
    if(retainedDraft())return false;const auto c=context_();auto chosen=std::find_if(c.instruments.begin(),c.instruments.end(),[&](const auto &i){return i.at("index")==c.instrument;});if(chosen==c.instruments.end())chosen=c.instruments.begin();
    const std::string wanted=chosen==c.instruments.end()?std::string{}:chosen->at("id").get<std::string>();
    if(c.document!=captured_.document||c.revision!=captured_.revision||wanted!=identity_)load(true);return true;
  }
  void importInstrument(std::function<std::vector<std::filesystem::path>()> choose={}){beginCreation(true,std::move(choose));}
  Json musicalTarget()const{return {{"document",captured_.document},{"revision",captured_.revision},{"sample",false},{"slot",index_},{"id",identity_}};}
  void playback(const std::string &document,const Json &instruments,const std::vector<Tracker::VoicePosition> &voices){if(!visible())return;std::vector<double> next;if(document==captured_.document&&std::any_of(instruments.begin(),instruments.end(),[&](const auto &i){return i.at("id")==identity_&&i.at("index")==index_;}))for(const auto &v:voices)if(v.instrument==index_)next.push_back(v.envelopeTicks[size_t(kind_)]);if(next!=playbackTicks_){playbackTicks_=std::move(next);requestPaint();}}
  Json snapshot()const{const auto r=canvas_.viewport;Json handles=Json::array(),bounds=Json::array();for(size_t i=0;i<canvas_.handles.size();++i)handles.push_back({{"index",i},{"x",canvas_.handles[i].x},{"y",canvas_.handles[i].y}});for(const auto &[id,control]:controls_)if(IsWindowVisible(control)){RECT rect{};GetWindowRect(control,&rect);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT *>(&rect),2);bounds.push_back({{"id",id},{"bounds",{rect.left,rect.top,rect.right,rect.bottom}}});}return {{"visible",visible()},{"compactLayout",compact_},{"shortDock",shortDock_},{"shortEnvelopeOptions",shortEnvelopeOptions_},{"generation",generation_},{"controlBounds",bounds},{"toolFieldDraft",toolFieldsDirty_},{"retainedDraft",retainedDraft()},{"creationCompletion",creationCompletion_.snapshot()},{"creationTarget",creationTarget_},{"creationReport",creationReport_},{"creationNeedsAcknowledgement",creationNeedsAcknowledgement_},{"page",pages[size_t(page_)]},{"canvasVisible",canvasVisible_},{"document",captured_.document},{"expectedRevision",captured_.revision},{"instrument",identity_},{"index",index_},{"kind",kinds[size_t(kind_)]},{"dirty",draft()},{"envelopeDirty",envelopeDirty_},{"fieldDraft",pointFields_||markerFields_||mappingFields_},{"mappingFields",mappingFields_},{"mappingDirty",mappingDirty_},{"selectedKey",selectedKey_},{"pending",pending_||creationWorking_},{"stale",!current()},{"envelope",envelope_},{"selectedPoint",selected_},{"mapping",mapping_},{"handles",handles},{"playbackTicks",playbackTicks_},{"canvas",{r.x,r.y,r.w,r.h}},{"start",canvas_.start/256},{"end",canvas_.end/256},{"status",utf8(status_)},{"envelopeBank",bank_?bank_->snapshot():Json{{"visible",false}}}};}
};
}
