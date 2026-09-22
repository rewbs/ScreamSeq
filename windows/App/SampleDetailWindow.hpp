#pragma once
#include "NativeToolWindow.hpp"
#include "editor/TrackerDocument.hpp"

namespace ScreamSeq {
// Detailed sample work stays on the document worker. The window retains only
// a bounded waveform, presentation state, and an unapplied drawing gesture.
class SampleDetailWindow final : public NativeToolWindow {
  using Json=Api::Json;
public:
  struct Context {std::string document,revision;unsigned sample=0;Json samples;};
private:
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Audition=std::function<void(unsigned,const std::string &,const std::string &,const std::string &)>;Audition audition_;
  enum:int {sample=4801,fromCursor,reload,undo,redo,close,fit,zoomIn,zoomOut,zoomSelection,panLeft,panRight,channels,drawMode,
    rangeStart,rangeEnd,setRange,all,viewStart,viewEnd,setView,pointFrame,pointValue,stagePoint,applyDraw,discardDraw,interpolation,
    operation,fadeCurve,amount,exponent,previewProcess,applyProcess,crossLoop,crossMode,crossCurve,crossFrames,previewCross,applyCross,
    snapMode,snapDirection,snapSize,snapRange,normalLoop,sustainLoop,disableNormal,disableSustain,loopDirection,
    copy,cut,erase,pasteMode,paste,copyNew,audition,
    heading=4900,targetLabel,rangeLabel,viewLabel,pointLabel,processLabel,crossLabel,snapLabel,loopsLabel,clipboardLabel,statusLabel,helpLabel};
  struct Region {unsigned first=0,last=0,start=0,end=0,frames=0;};
  Request request_;std::function<Context(bool)> context_;Context captured_;std::string id_;unsigned slot_=0;
  Json info_=Json::object(),report_=Json::object();std::map<std::string,Region> regions_;Region region_;
  std::set<int> edited_;
  std::vector<float> peaks_;std::map<unsigned,double> stroke_,dragBefore_;
  std::vector<double> playbackFrames_;
  unsigned waveStart_=0,waveEnd_=0,anchor_=0,lastFrame_=0;double lastValue_=0;
  int channel_=0;unsigned bins_=0;uint64_t generation_=0;
  bool setting_=false,pending_=false,fields_=false,drawing_=false,dragging_=false,selecting_=false,priorPoint_=false;
  float layoutWidth_=-1,layoutHeight_=-1;UINT layoutDpi_=0;bool layoutPending_=false;
  Region selectionBefore_;WorkspaceRect canvas_;
  void require(bool condition,const char *why)const{if(!condition)throw std::runtime_error(why);}
  bool current()const{const auto c=context_(false);return c.document==captured_.document&&c.revision==captured_.revision;}
  bool draft()const{return fields_||!stroke_.empty()||dragging_;}
  void clearFields(std::initializer_list<int> ids){for(auto id:ids)edited_.erase(id);fields_=!edited_.empty();}
  void requireCurrent()const{require(current(),"Song changed / captured sample retained; Reload before applying");}
  void status(std::wstring value){status_=std::move(value);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  int choice(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  void choose(int id,int value){SendMessageW(controls_.at(id),CB_SETCURSEL,value,0);}
  const char *channelName()const{return channel_==1?"left":channel_==2?"right":"both";}
  unsigned integerField(int id,unsigned maximum)const{auto n=number(id);require(n>=0&&n<=maximum&&std::floor(n)==n,"Enter a whole frame number within the allowed range");return unsigned(n);}
  unsigned wantedBins()const{return std::min(region_.end-region_.start,unsigned(std::clamp(canvas_.w,1.f,4096.f)));}
  bool precise()const{return region_.end>region_.start&&region_.end-region_.start<=unsigned(std::clamp(canvas_.w,1.f,4096.f))&&waveStart_==region_.start&&waveEnd_==region_.end&&peaks_.size()==size_t(region_.end-region_.start)*2;}
  void remember(){if(!id_.empty())regions_[id_]=region_;}
  void clamp(){auto &r=region_;r.first=std::min(r.first,r.frames);r.last=std::clamp(r.last,r.first,r.frames);const auto span=std::min(r.frames,std::max(1u,r.end>r.start?r.end-r.start:1u));r.start=std::min(r.start,r.frames-span);r.end=r.start+span;}
  void syncFields(){setting_=true;set(rangeStart,region_.first);set(rangeEnd,region_.last);set(viewStart,region_.start);set(viewEnd,region_.end);setting_=false;remember();}
  void describeTarget(){set(targetLabel,wide(info_.at("name").get<std::string>())+L" · "+std::to_wstring(region_.frames)+L" frames · "+std::to_wstring(info_.at("channels").get<unsigned>())+L" channel(s) · "+std::to_wstring(info_.at("rate").get<unsigned>())+L" Hz");}
  void sampleChoices(){setting_=true;SendMessageW(controls_.at(sample),CB_RESETCONTENT,0,0);for(size_t i=0;i<captured_.samples.size();++i){const auto &v=captured_.samples[i];auto name=std::to_wstring(v.at("index").get<unsigned>())+L" · "+wide(v.at("name").get<std::string>());SendMessageW(controls_.at(sample),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(v.at("id")==id_)choose(sample,int(i));}choose(channels,channel_);setting_=false;}
  void pointFields(unsigned frame,double value){setting_=true;set(pointFrame,frame);set(pointValue,value);setting_=false;}
  void readWave(){
    requireCurrent();const auto token=generation_;const auto first=region_.start,last=region_.end,bins=wantedBins();
    if(first==last){peaks_.clear();waveStart_=waveEnd_=first;bins_=0;return;}
    const auto result=request_("sample.waveform.get",{{"sample",slot_},{"start",first},{"end",last},{"bins",bins},{"channels",channelName()}});
    requireCurrent();require(token==generation_,"Sample view changed while loading / retry the view");
    peaks_=result.at("peaks").get<std::vector<float>>();waveStart_=first;waveEnd_=last;bins_=bins;requestPaint();
  }
  template<class Function> void busy(Function work){if(pending_)return;pending_=true;layout();try{work();pending_=false;layout();}catch(...){pending_=false;layout();throw;}}
  void refreshWave(){busy([&]{readWave();});}
  void load(bool follow,unsigned requested=0){
    if(pending_)return;const auto c=context_(true);require(follow||c.document==captured_.document,"Document replaced / use From cursor to capture the new song");
    auto selected=std::find_if(c.samples.begin(),c.samples.end(),[&](const auto &v){return follow?v.at("index")==unsigned(requested?requested:c.sample):v.at("id")==id_;});
    require(selected!=c.samples.end(),"Captured sample is unavailable / choose From cursor");const auto index=selected->at("index").get<unsigned>();const auto identity=selected->at("id").get<std::string>();
    const auto token=generation_;busy([&]{const auto data=request_("sample.get",{{"sample",index}});const auto now=context_(true);require(now.document==c.document&&now.revision==c.revision&&token==generation_,"Sample changed while loading / captured draft retained");
      remember();if(c.document!=captured_.document)regions_.clear();const bool same=c.document==captured_.document&&identity==id_;captured_=c;slot_=index;id_=identity;info_=data;
      const unsigned frames=data.at("frames");if(regions_.contains(id_))region_=regions_.at(id_);else region_={0,frames,0,frames,frames};
      const auto previousFrames=region_.frames;region_.frames=frames;if(region_.end==previousFrames)region_.end=frames;if(region_.last==previousFrames)region_.last=frames;clamp();
      stroke_.clear();dragBefore_.clear();dragging_=selecting_=fields_=false;edited_.clear();++generation_;report_=Json::object();
      if(!same||channel_==2&&data.at("channels")==1)channel_=0;
      sampleChoices();syncFields();pointFields(region_.start,0);describeTarget();readWave();
    });status(L"Captured sample / F6: waveform or text focus / Z–M, Q–U: audition saved sound");
  }
  void applyRange(){const auto first=integerField(rangeStart,region_.frames),last=integerField(rangeEnd,region_.frames);require(first<=last,"Range end must follow its start");region_.first=first;region_.last=last;clearFields({rangeStart,rangeEnd});syncFields();}
  void viewport(unsigned first,unsigned last){require(stroke_.empty()&&!dragging_,"Apply or discard the drawing before changing its view");require(first<last&&last<=region_.frames,"Visible range must be nonempty and inside the sample");region_.start=first;region_.end=last;clearFields({viewStart,viewEnd});++generation_;syncFields();refreshWave();}
  void zoom(double factor,std::optional<double> center={}){if(!region_.frames)return;const double anchor=center.value_or(region_.first>=region_.start&&region_.first<region_.end?double(region_.first):(double(region_.start)+region_.end)*.5);const double ratio=std::clamp((anchor-region_.start)/std::max(1u,region_.end-region_.start),0.,1.);const auto span=unsigned(std::clamp(std::round((region_.end-region_.start)/factor),1.,double(region_.frames)));const auto first=unsigned(std::clamp(std::round(anchor-span*ratio),0.,double(region_.frames-span)));viewport(first,first+span);}
  void pan(int direction){const auto span=region_.end-region_.start;const auto first=unsigned(std::clamp(int64_t(region_.start)+direction*int64_t(std::max(1u,span/4)),int64_t(0),int64_t(region_.frames-span)));if(span)viewport(first,first+span);}
  Json rangeParams(){applyRange();return {{"sample",slot_},{"start",region_.first},{"end",region_.last}};}
  void resultStatus(const std::wstring &prefix){std::wstring text=prefix;if(report_.contains("changedFrames"))text+=L" · "+std::to_wstring(report_.at("changedFrames").get<uint64_t>())+L" audio frames change";if(report_.contains("loopBefore"))text+=L" · loop "+std::to_wstring(report_.at("loopBefore").at("frames").get<unsigned>())+L" → "+std::to_wstring(report_.at("loopAfter").at("frames").get<unsigned>())+L" frames";if(report_.value("clippedSamples",uint64_t(0)))text+=L" · "+std::to_wstring(report_.at("clippedSamples").get<uint64_t>())+L" clipped values";if(report_.contains("id"))text+=L" · new sample "+std::to_wstring(report_.at("sample").get<unsigned>());status(std::move(text));}
  void mutate(const std::string &method,Json params,bool dry=false,bool drawing=false){
    requireCurrent();require(drawing||stroke_.empty(),"Apply or discard the drawing first");params["expectedRevision"]=captured_.revision;const auto token=generation_;const auto before=captured_;
    busy([&]{auto result=request_(method,params);const auto now=context_(true);require(now.document==before.document,"Document changed / edit completed in its captured song");require(token==generation_,"Fields changed while applying / newer draft retained; Reload to inspect the result");
      report_=std::move(result);require(std::any_of(now.samples.begin(),now.samples.end(),[&](const auto &v){return v.at("index")==slot_&&v.at("id")==id_;}),"Captured sample was removed by history / use From cursor");captured_=now;if(!dry){fields_=false;edited_.clear();if(drawing)stroke_.clear();info_=request_("sample.get",{{"sample",slot_}});requireCurrent();const auto old=region_.frames;region_.frames=info_.at("frames");if(region_.end==old)region_.end=region_.frames;if(region_.last==old)region_.last=region_.frames;clamp();syncFields();sampleChoices();describeTarget();readWave();}
    });resultStatus(dry?L"Preview / no audio or history changed":method=="sample.clipboard.copy"?L"Copied to the private sample clipboard":L"Applied / document history updated only when audio or settings change");
  }
  void process(bool dry){static constexpr const char *names[]={"reverse","normalize","gain","fade-in","fade-out","invert","remove-dc","smooth","trim","silence","swap-channels","copy-left","copy-right","stereo-average"};const auto op=choice(operation);require(op>=0&&op<14,"Choose an operation");auto p=rangeParams();p["operation"]=names[op];p["channels"]=channelName();p["dryRun"]=dry;
    if(op==1)p["targetDB"]=number(amount);else if(op==2)p["gainDB"]=number(amount);else if(op==7)p["window"]=integerField(amount,255);
    if(op==3||op==4){static constexpr const char *curves[]={"linear","smooth","exponential","logarithmic"};p["curve"]=curves[choice(fadeCurve)];if(choice(fadeCurve)>=2)p["exponent"]=number(exponent);}mutate("sample.process",std::move(p),dry);
  }
  void crossfade(bool dry){auto p=Json{{"sample",slot_},{"loop",choice(crossLoop)==1?"sustain":"normal"},{"mode",choice(crossMode)==1?"overlap":"preserve"},{"curve",choice(crossCurve)==1?"equal-power":"linear"},{"frames",integerField(crossFrames,1048576)},{"dryRun",dry}};mutate("sample.crossfade",std::move(p),dry);}
  void loop(bool sustain,bool disable){auto p=rangeParams();const unsigned first=disable?info_.at(sustain?"sustainStart":"loopStart").get<unsigned>():region_.first,last=disable?info_.at(sustain?"sustainEnd":"loopEnd").get<unsigned>():region_.last;const auto direction=choice(loopDirection);
    mutate("sample.loops.set",{{"sample",slot_},{sustain?"sustain":"normal",{{"start",first},{"end",last},{"enabled",!disable},{"pingpong",!disable&&direction==1},{"reverse",!disable&&direction==2}}}});
  }
  void snap(){requireCurrent();applyRange();auto p=Json{{"sample",slot_},{"positions",Json::array({region_.first,region_.last})},{"mode",choice(snapMode)==1?"grid":"zero"},{"direction",choice(snapDirection)==1?"before":choice(snapDirection)==2?"after":"nearest"}};p[choice(snapMode)==1?"step":"radius"]=integerField(snapSize,choice(snapMode)==1?UINT32_MAX:65536);if(choice(snapMode)==0)p["channels"]=channelName();const auto token=generation_;busy([&]{auto result=request_("sample.snap.get",p);requireCurrent();require(token==generation_,"Selection changed while snapping");const auto &v=result.at("positions");region_.first=v[0].at("after");region_.last=v[1].at("after");syncFields();report_=std::move(result);});status(L"Selection snapped / audio unchanged");}
  void clipboard(int id){auto p=rangeParams();if(id==copy||id==copyNew)p["channels"]=channelName();if(id==paste){requireCurrent();const auto token=generation_;Json clip;busy([&]{clip=request_("sample.clipboard.get",Json::object());requireCurrent();require(token==generation_,"Paste selection changed");});require(clip.value("available",false),"The sample clipboard is empty");static constexpr const char *modes[]={"insert","overwrite","mix","replace"};const auto mode=choice(pasteMode);p={{"sample",slot_},{"at",region_.first},{"clipboardId",clip.at("clipboardId")},{"channels",channelName()},{"mode",modes[mode]}};if(mode==3)p["end"]=region_.last;}
    mutate(id==copy?"sample.clipboard.copy":id==cut?"sample.cut":id==erase?"sample.delete":id==copyNew?"sample.copyToNew":"sample.paste",std::move(p));
  }
  void stage(unsigned frame,double value){requireCurrent();require(frame<region_.frames&&std::isfinite(value)&&value>=-1&&value<=1,"Draw inside the sample with values from -1 to 1");require(stroke_.contains(frame)||stroke_.size()<4096,"Drawing is limited to 4096 points per Apply");stroke_[frame]=value;clearFields({pointFrame,pointValue});++generation_;pointFields(frame,value);status(L"Drawing draft / Apply drawing saves one Undo step");}
  void applyDrawing(){require(!stroke_.empty(),"Draw or stage a point first");Json points=Json::array();for(const auto &[frame,value]:stroke_)points.push_back({{"frame",frame},{"value",value}});mutate("sample.draw",{{"sample",slot_},{"points",points},{"channels",channelName()},{"interpolation",choice(interpolation)==1?"step":"linear"}},false,true);}
  unsigned frameAt(float x,bool insertion=true)const{const auto span=region_.end-region_.start;return std::min(insertion?region_.end:std::max(region_.start,region_.end-1),region_.start+unsigned(std::clamp(double((x-canvas_.x)/std::max(1.f,canvas_.w)),0.,1.)*span));}
  double valueAt(float y)const{return std::clamp(double((canvas_.y+canvas_.h*.5f-y)/std::max(1.f,canvas_.h*.44f)),-1.,1.);}
  float xAt(double frame)const{return canvas_.x+float((frame-region_.start)/std::max(1u,region_.end-region_.start))*canvas_.w;}
  void extend(float x,float y){const auto frame=frameAt(x,false);const auto value=valueAt(y);requireCurrent();size_t extra=0;const auto first=priorPoint_?std::min(frame,lastFrame_):frame,last=priorPoint_?std::max(frame,lastFrame_):frame;for(unsigned i=first;i<=last;++i)extra+=!stroke_.contains(i);require(stroke_.size()+extra<=4096,"Drawing is limited to 4096 points per Apply");if(priorPoint_&&frame!=lastFrame_)for(unsigned i=std::min(frame,lastFrame_);i<=std::max(frame,lastFrame_);++i)stroke_[i]=lastValue_+(value-lastValue_)*(double(i)-lastFrame_)/(double(frame)-lastFrame_);else stroke_[frame]=value;lastFrame_=frame;lastValue_=value;priorPoint_=true;++generation_;pointFields(frame,value);}
  void cancelGesture(){if(dragging_){stroke_=dragBefore_;dragging_=false;priorPoint_=false;}if(selecting_){region_=selectionBefore_;selecting_=false;syncFields();}if(GetCapture()==window_)ReleaseCapture();++generation_;}
  void mouse(UINT message,float x,float y,WPARAM flags)override{
    if(message==WM_CAPTURECHANGED){if(dragging_||selecting_)cancelGesture();return;}if(pending_)return;
    if(message==WM_LBUTTONDOWN&&canvas_.contains(x,y)){requireCurrent();SetFocus(window_);if(drawing_){require(precise(),"Zoom to one frame per pixel before drawing");dragBefore_=stroke_;priorPoint_=false;dragging_=true;extend(x,y);}else{require(stroke_.empty(),"Apply or discard the drawing before selecting");selectionBefore_=region_;anchor_=(flags&MK_SHIFT)?region_.first:frameAt(x);selecting_=true;region_.first=std::min(anchor_,frameAt(x));region_.last=std::max(anchor_,frameAt(x));syncFields();}SetCapture(window_);}
    else if(message==WM_MOUSEMOVE&&(dragging_||selecting_)){try{requireCurrent();if(dragging_)extend(x,y);else{region_.first=std::min(anchor_,frameAt(x));region_.last=std::max(anchor_,frameAt(x));syncFields();}}catch(...){cancelGesture();throw;}}
    else if(message==WM_LBUTTONUP&&(dragging_||selecting_)){try{requireCurrent();if(dragging_){extend(x,y);dragging_=false;priorPoint_=false;status(L"Drawing draft / Apply drawing saves one Undo step");}selecting_=false;ReleaseCapture();}catch(...){cancelGesture();throw;}}
  }
  bool wheel(UINT message,float x,float y,WPARAM w)override{if(!canvas_.contains(x,y)||pending_)return false;const auto delta=GET_WHEEL_DELTA_WPARAM(w);if(GET_KEYSTATE_WPARAM(w)&MK_CONTROL)zoom(std::pow(2.,double(delta)/WHEEL_DELTA),double(frameAt(x)));else if(delta)pan((message==WM_MOUSEHWHEEL?delta:-delta)>0?1:-1);return true;}
  bool key(WPARAM key,bool ctrl,bool shift)override{
    if(pending_)return false;if(key==VK_ESCAPE){cancelGesture();status(L"Gesture cancelled / staged drawing retained");return true;}
    if(ctrl&&key==VK_RETURN){applyDrawing();return true;}if(ctrl&&key=='R'){load(false);return true;}if(key==VK_F6){SetFocus(GetFocus()==window_?controls_.at(pointFrame):window_);return true;}
    if(GetFocus()!=window_)return false;
    if(ctrl&&(key=='Z'||key=='Y')){action(key=='Z'?undo:redo,BN_CLICKED);return true;}
    if(ctrl&&(key=='A'||key=='C'||key=='X'||key=='V')){action(key=='A'?all:key=='C'?copy:key=='X'?cut:paste,BN_CLICKED);return true;}
    if(key==VK_HOME){action(fit,BN_CLICKED);return true;}if(key==VK_ADD||key==VK_OEM_PLUS){zoom(2);return true;}if(key==VK_SUBTRACT||key==VK_OEM_MINUS){zoom(.5);return true;}
    if(key==VK_LEFT||key==VK_RIGHT){if(ctrl)pan(key==VK_LEFT?-1:1);else{const int64_t step=shift?1:std::max(1u,(region_.end-region_.start)/100);if(shift)region_.last=unsigned(std::clamp(int64_t(region_.last)+(key==VK_LEFT?-step:step),int64_t(region_.first),int64_t(region_.frames)));else region_.first=region_.last=unsigned(std::clamp(int64_t(region_.first)+(key==VK_LEFT?-step:step),int64_t(0),int64_t(region_.frames)));syncFields();}return true;}
    if(key==VK_DELETE){clipboard(erase);return true;}return false;
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;if(notification==EN_CHANGE){edited_.insert(id);fields_=true;++generation_;report_=Json::object();return;}if(pending_)return;
    if(notification==CBN_SELCHANGE){if(id==sample){const auto selected=choice(sample);require(selected>=0&&size_t(selected)<captured_.samples.size(),"Choose a sample");if(draft()||!current()){for(size_t i=0;i<captured_.samples.size();++i)if(captured_.samples[i].at("id")==id_)choose(sample,int(i));requireCurrent();throw std::runtime_error("Apply or reload the captured draft before changing sample");}load(true,captured_.samples[size_t(selected)].at("index"));return;}
      if(id==channels){const auto next=choice(channels);if(!stroke_.empty()||next==2&&info_.at("channels")==1){choose(channels,channel_);throw std::runtime_error(!stroke_.empty()?"Apply or discard the drawing before changing channels":"This sample has no right channel");}channel_=next;++generation_;refreshWave();return;}edited_.insert(id);fields_=true;++generation_;report_=Json::object();return;}
    if(notification!=BN_CLICKED)return;
    if(id==audition){requireCurrent();audition_(slot_,id_,captured_.document,captured_.revision);return;}
    if(id==close){hide();return;}if(id==fromCursor){load(true);return;}if(id==reload){load(false);return;}
    if(id==undo||id==redo){require(!draft(),"Apply or Reload the draft before document Undo/Redo");mutate(id==undo?"history.undo":"history.redo",{{"domain","document"}});return;}
    if(id==fit){if(region_.frames)viewport(0,region_.frames);return;}if(id==zoomIn||id==zoomOut){zoom(id==zoomIn?2:.5);return;}if(id==panLeft||id==panRight){pan(id==panLeft?-1:1);return;}
    if(id==zoomSelection){applyRange();if(region_.last>region_.first)viewport(region_.first,region_.last);else if(region_.frames){const auto count=std::min(128u,region_.frames),start=std::min(region_.frames-count,region_.first>count/2?region_.first-count/2:0);viewport(start,start+count);}return;}
    if(id==setView){viewport(integerField(viewStart,region_.frames),integerField(viewEnd,region_.frames));return;}
    if(id==drawMode){cancelGesture();drawing_=!drawing_;set(drawMode,drawing_?L"Draw on":L"Draw off");return;}
    if(id==setRange){applyRange();return;}if(id==all){region_.first=0;region_.last=region_.frames;clearFields({rangeStart,rangeEnd});syncFields();return;}
    if(id==stagePoint){const auto frame=integerField(pointFrame,region_.frames?region_.frames-1:0);stage(frame,number(pointValue));return;}
    if(id==applyDraw){applyDrawing();return;}if(id==discardDraw){cancelGesture();stroke_.clear();clearFields({pointFrame,pointValue,interpolation});++generation_;syncFields();pointFields(region_.start,0);status(L"Drawing discarded / audio unchanged");return;}
    if(id==previewProcess||id==applyProcess){process(id==previewProcess);return;}if(id==previewCross||id==applyCross){crossfade(id==previewCross);return;}if(id==snapRange){snap();return;}
    if(id==normalLoop||id==sustainLoop||id==disableNormal||id==disableSustain){loop(id==sustainLoop||id==disableSustain,id==disableNormal||id==disableSustain);return;}
    if(id==copy||id==cut||id==erase||id==paste||id==copyNew){clipboard(id);return;}
  }
  void timer(UINT_PTR id)override{if(id!=3)return;KillTimer(window_,3);if(visible()&&!pending_&&current()&&wantedBins()!=bins_&&!dragging_)refreshWave();}
  void layout()override{
    const auto [w,h]=size();const auto dpi=GetDpiForWindow(window_);
    // Native edits generate nested EN_CHANGE notifications. Fixed geometry
    // does not need to reposition all 54 controls for every edited character.
    if(w==layoutWidth_&&h==layoutHeight_&&dpi==layoutDpi_&&pending_==layoutPending_)return;
    layoutWidth_=w;layoutHeight_=h;layoutDpi_=dpi;layoutPending_=pending_;
    const auto oldWidth=canvas_.w;canvas_={14,140,w-28,std::max(90.f,h-540)};
    place(heading,14,12,156,23);place(sample,172,8,w-678,240);place(fromCursor,w-498,8,106,28);place(reload,w-388,8,88,28);place(undo,w-296,8,80,28);place(redo,w-212,8,80,28);place(close,w-128,8,114,28);
    place(targetLabel,14,44,w-28,23);float x=14;for(auto [id,width]:std::initializer_list<std::pair<int,float>>{{fit,58.f},{zoomIn,50.f},{zoomOut,50.f},{zoomSelection,124.f},{panLeft,66.f},{panRight,66.f},{channels,116.f},{drawMode,90.f},{audition,124.f}}){place(id,x,72,width,id==channels?180:28);x+=width+6;}
    place(viewLabel,14,112,58,20);place(viewStart,74,108,94,26);place(viewEnd,176,108,94,26);place(setView,278,108,88,26);place(helpLabel,380,110,w-394,24);
    const auto y=h-388;place(rangeLabel,14,y,74,25);place(rangeStart,90,y,94,26);place(rangeEnd,192,y,94,26);place(setRange,294,y,88,26);place(all,390,y,60,26);place(clipboardLabel,470,y,64,24);place(copy,536,y,58,26);place(cut,600,y,52,26);place(erase,658,y,64,26);place(pasteMode,728,y,110,180);place(paste,844,y,68,26);place(copyNew,918,y,w-932,26);
    place(pointLabel,14,y+40,74,25);place(pointFrame,90,y+40,94,26);place(pointValue,192,y+40,94,26);place(stagePoint,294,y+40,100,26);place(interpolation,400,y+40,116,180);place(applyDraw,524,y+40,132,26);place(discardDraw,664,y+40,130,26);
    place(processLabel,14,y+86,82,25);place(operation,98,y+82,156,250);place(fadeCurve,262,y+82,128,180);place(amount,398,y+82,92,26);place(exponent,498,y+82,76,26);place(previewProcess,584,y+82,118,26);place(applyProcess,710,y+82,122,26);
    place(crossLabel,14,y+128,82,25);place(crossLoop,98,y+124,112,180);place(crossMode,218,y+124,154,180);place(crossCurve,380,y+124,126,180);place(crossFrames,514,y+124,90,26);place(previewCross,612,y+124,118,26);place(applyCross,738,y+124,122,26);
    place(snapLabel,14,y+170,82,25);place(snapMode,98,y+166,126,180);place(snapDirection,232,y+166,112,180);place(snapSize,352,y+166,100,26);place(snapRange,460,y+166,144,26);
    place(loopsLabel,14,y+212,82,25);place(loopDirection,98,y+208,124,180);place(normalLoop,230,y+208,126,26);place(sustainLoop,364,y+208,126,26);place(disableNormal,498,y+208,128,26);place(disableSustain,634,y+208,128,26);
    place(statusLabel,14,h-128,w-28,108);for(auto [id,control]:controls_)if(id<heading)EnableWindow(control,!pending_||id==close);
    if(ready_&&oldWidth!=canvas_.w)SetTimer(window_,3,70,nullptr);
  }
  void paint(RenderSurface &s)override{
    const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);const auto &r=canvas_;s.fill(r.x,r.y,r.w,r.h,0x0c141c);const auto mid=r.y+r.h*.5f,amplitude=r.h*.44f;
    for(unsigned i=1;i<8;++i)s.line(r.x+r.w*i/8,r.y,r.x+r.w*i/8,r.y+r.h,0x1c2a36);s.line(r.x,mid,r.x+r.w,mid,0x344a57);
    const auto clipX=[&](double frame){return std::clamp(xAt(frame),r.x,r.x+r.w);};if(region_.last>region_.first)s.fill(clipX(region_.first),r.y,clipX(region_.last)-clipX(region_.first),r.h,0x244a50);
    const auto count=peaks_.size()/2;for(size_t i=0;i<count;++i){const auto x=xAt(waveStart_+(i+.5)*double(waveEnd_-waveStart_)/count);if(x<r.x||x>r.x+r.w)continue;s.line(x,mid-peaks_[2*i]*amplitude,x,mid-peaks_[2*i+1]*amplitude,0x79d8c8);if(precise()&&i){const auto before=xAt(waveStart_+(i-.5)*double(waveEnd_-waveStart_)/count);for(size_t channel=0;channel<2;++channel)s.line(before,mid-peaks_[2*(i-1)+channel]*amplitude,x,mid-peaks_[2*i+channel]*amplitude,0x79d8c8);}if(precise()&&r.w/count>=5)s.fill(x-1.5f,mid-peaks_[2*i]*amplitude-1.5f,3,3,0xb0f2df);}
    for(const auto frame:{region_.first,region_.last})if(frame>=region_.start&&frame<=region_.end)s.line(xAt(frame),r.y,xAt(frame),r.y+r.h,0xb0f2df);
    if(!info_.empty())for(int sustain=0;sustain<2;++sustain)if(info_.value(sustain?"sustainLoop":"loop",false))for(const auto *key:{sustain?"sustainStart":"loopStart",sustain?"sustainEnd":"loopEnd"}){const unsigned frame=info_.at(key);if(frame>=region_.start&&frame<=region_.end)s.line(xAt(frame),r.y,xAt(frame),r.y+r.h,sustain?0xb5a0e8:0xe2b86c,2);}
    std::optional<std::pair<float,float>> previous;for(const auto &[frame,value]:stroke_){if(frame<region_.start||frame>=region_.end)continue;const auto x=xAt(frame+.5),y=mid-float(value)*amplitude;if(previous)s.line(previous->first,previous->second,x,y,0xf3c778,2);s.fill(x-2,y-2,4,4,0xffe1a7);previous={{x,y}};}
    for(const auto frame:playbackFrames_)if(frame>=region_.start&&frame<region_.end)s.line(xAt(frame),r.y+24,xAt(frame),r.y+r.h,0xebaeed,1);
    s.outline(r.x,r.y,r.w,r.h,0x548e87);s.uiText(current()?(precise()?L"Individual frames · draw or enter exact values":L"Peak overview · zoom in to draw"):L"Stale sample · Reload retains the captured target",r.x+8,r.y+6,r.w-16,0x9bacbc);
    s.uiText(L"Process amount: gain / target dB, or odd smoothing window. Fade exponent applies to exponential/logarithmic curves.",14,h-144,w-28,0x9bacbc);
  }
public:
  SampleDetailWindow(HWND owner,Request request,std::function<Context(bool)> context,Audition auditionCallback):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),audition_(std::move(auditionCallback)){
    minimumWidth_=1080;minimumHeight_=790;create(L"ScreamSeq.SampleDetail",L"Sample detail",1180,880);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"SAMPLE DETAIL"},{targetLabel,L""},{rangeLabel,L"Selection"},{viewLabel,L"Visible"},{pointLabel,L"Frame / ±1"},{processLabel,L"Process"},{crossLabel,L"Crossfade"},{snapLabel,L"Snap range"},{loopsLabel,L"Set loop"},{clipboardLabel,L"Clipboard"},{statusLabel,L""},{helpLabel,L"Ctrl+wheel zoom · wheel pan · F6 canvas/fields · Escape cancels gesture"}})label(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{fromCursor,L"From cursor"},{reload,L"Reload"},{undo,L"Undo"},{redo,L"Redo"},{close,L"Close"},{fit,L"Fit"},{zoomIn,L"+"},{zoomOut,L"−"},{zoomSelection,L"Zoom selection"},{panLeft,L"← Pan"},{panRight,L"Pan →"},{drawMode,L"Draw off"},{setRange,L"Set range"},{all,L"All"},{setView,L"Set view"},{stagePoint,L"Stage point"},{applyDraw,L"Apply drawing"},{discardDraw,L"Discard drawing"},{previewProcess,L"Preview process"},{applyProcess,L"Apply process"},{previewCross,L"Preview fade"},{applyCross,L"Apply fade"},{snapRange,L"Snap selection"},{normalLoop,L"Normal selection"},{sustainLoop,L"Sustain selection"},{disableNormal,L"Disable normal"},{disableSustain,L"Disable sustain"},{copy,L"Copy"},{cut,L"Cut"},{erase,L"Delete"},{paste,L"Paste"},{copyNew,L"To new"},{audition,L"Audition…"}})button(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{rangeStart,L"0"},{rangeEnd,L"0"},{viewStart,L"0"},{viewEnd,L"0"},{pointFrame,L"0"},{pointValue,L"0"},{amount,L"0"},{exponent,L"3"},{crossFrames,L"64"},{snapSize,L"2048"}})edit(id,text,24);
    combo(sample);auto options=[&](int id,std::initializer_list<const wchar_t *> values){combo(id);for(auto text:values)SendMessageW(controls_.at(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));choose(id,0);};
    options(channels,{L"Both channels",L"Left",L"Right"});options(interpolation,{L"Linear draw",L"Step draw"});options(operation,{L"Reverse",L"Normalize",L"Gain",L"Fade in",L"Fade out",L"Invert",L"Remove DC",L"Smooth",L"Trim",L"Silence",L"Swap channels",L"Copy left",L"Copy right",L"Stereo average"});options(fadeCurve,{L"Linear fade",L"Smooth",L"Exponential",L"Logarithmic"});options(crossLoop,{L"Normal loop",L"Sustain loop"});options(crossMode,{L"Preserve duration",L"Overlap"});options(crossCurve,{L"Linear",L"Equal power"});options(snapMode,{L"Zero crossing",L"Grid"});options(snapDirection,{L"Nearest",L"Before",L"After"});options(loopDirection,{L"Forward",L"Ping pong",L"Reverse"});options(pasteMode,{L"Insert",L"Overwrite",L"Mix",L"Replace"});finish();
  }
  void openAt(){if(id_.empty())load(true);show();}
  Json musicalTarget()const{return {{"document",captured_.document},{"revision",captured_.revision},{"sample",true},{"slot",slot_},{"id",id_}};}
  void playback(const std::string &document,const Json &samples,const std::vector<Tracker::VoicePosition> &voices){if(!visible())return;std::vector<double> next;if(document==captured_.document&&std::any_of(samples.begin(),samples.end(),[&](const auto &v){return v.at("id")==id_&&v.at("index")==slot_;}))for(const auto &v:voices)if(v.sample==slot_)next.push_back(v.sampleFrame);if(next!=playbackFrames_){playbackFrames_=std::move(next);requestPaint();}}
  Json snapshot()const{Json points=Json::array();for(const auto &[frame,value]:stroke_)points.push_back({{"frame",frame},{"value",value}});return {{"visible",visible()},{"sample",slot_},{"id",id_},{"document",captured_.document},{"expectedRevision",captured_.revision},{"stale",!current()},{"pending",pending_},{"fieldDraft",fields_},{"drawing",drawing_},{"dragging",dragging_},{"points",points},{"start",region_.first},{"end",region_.last},{"frames",region_.frames},{"viewStart",region_.start},{"viewEnd",region_.end},{"channels",channelName()},{"precise",precise()},{"waveStart",waveStart_},{"waveEnd",waveEnd_},{"playbackFrames",playbackFrames_},{"waveBins",bins_},{"peaks",peaks_},{"canvas",Json::array({canvas_.x,canvas_.y,canvas_.w,canvas_.h})},{"status",utf8(status_)},{"report",report_}};}
};
}
