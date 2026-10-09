#pragma once
#include "NativeToolWindow.hpp"
#include "SampleFileDialog.hpp"
#include "MultisampleImportWindow.hpp"

namespace ScreamSeq {
class SampleLibraryWindow final : public NativeToolWindow {
  using Json=Api::Json;
public:
  struct Context{std::string document,revision;bool instruments=false;};
  using Request=std::function<Json(const std::string &,const Json &)>;
private:
  enum:int{search=5401,root,tags,files,addFolder,removeFolder,rescan,chooseFiles,mapped,preview,stop,gain,autoPreview,importSelection,family,previous,next,close,tagSearch,
    heading=5500,searchLabel,rootLabel,tagsLabel,resultsLabel,detailLabel,statusLabel,gainLabel};
  Request request_;std::function<Json()> statusRead_;std::function<Context()> context_;
  std::function<Json()> previewRead_;std::function<void(double)> gainChanged_;
  std::function<void(const Json &,const std::string &)> imported_;
  Json state_,entries_=Json::array(),facets_=Json::array(),inspection_,group_;
  std::string selectedPath_,rootPath_,revision_;std::vector<std::string> selectedTags_;
  std::vector<float> peaks_;
  bool setting_=false,pending_=false,queued_=true,mapped_=false,auto_=false;
  size_t offset_=0,total_=0;uint64_t generation_=0;
  bool previewPlaying_=false;float previewPosition_=0;
  std::unique_ptr<MultisampleImportWindow> multisample_;
  std::function<void(unsigned,const std::string &)> multisampleApplied_;
  NativeWriteCompletion::Write write_;
  NativeWriteCompletion importCompletion_;Context importContext_;Json importParams_,importReport_;uint64_t importGeneration_=0;
  bool importNeedsRebase_=false,choosingImport_=false;
  NativeWriteCompletion libraryCompletion_;Json libraryParams_,libraryReport_;std::string libraryMethod_;
  bool libraryWorking_=false,libraryNeedsReload_=false;
  bool libraryFrozen()const noexcept{return libraryCompletion_.retained()||libraryNeedsReload_;}
  bool browserFrozen()const noexcept{return importFrozen()||libraryFrozen();}
  bool importFrozen()const noexcept{return importCompletion_.retained()||importNeedsRebase_;}
  void requireBrowserResolved()const{if(importFrozen())throw std::runtime_error("Review the previous import before changing this browser selection");if(libraryFrozen())throw std::runtime_error("Review the previous library result before changing the browser");}
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    if(!choosingImport_&&!importCompletion_.retained())return {};
    return describeDraft(importContext_.document,importContext_.revision,importParams_.dump(),importGeneration_,false,pending_,!pending_);
  }
  MultisampleImportWindow &multisampleEditor() {
    if(!multisample_||multisample_->retired())
      multisample_=std::make_unique<MultisampleImportWindow>(window_,request_,[this]{const auto c=context_();return std::pair(c.document,c.revision);},multisampleApplied_,write_);
    return *multisample_;
  }
  void status(std::wstring message){status_=std::move(message);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what())+(importCompletion_.retained()?L" / Review import before importing again":libraryCompletion_.retained()?L" / Review library before changing folders or rescanning":L""));}
  Json call(const std::string &method,const Json &p=Json::object()){return request_(method,p);}
  void updateGain(bool required){
    double db=0;try{db=number(gain);if(db<-60||db>0)throw std::runtime_error("range");}
    catch(const std::exception &){if(required)throw std::runtime_error("Preview gain must be -60 to 0 dB");return;}
    gainChanged_(db);
  }
  void playback(){
    const auto state=previewRead_();const bool playing=state.at("playing").get<bool>()&&state.at("path")==selectedPath_;
    const auto frames=state.at("frames").get<uint64_t>();const float position=playing&&frames?float(std::min(1.,double(state.at("renderedFrames").get<uint64_t>())/frames)):0;
    if(playing!=previewPlaying_){previewPlaying_=playing;set(preview,playing?L"Playing…":L"Preview");requestPaint();}
    if(position!=previewPosition_){previewPosition_=position;requestPaint();}
  }
  void queue(){queued_=true;offset_=0;++generation_;SetTimer(window_,1,100,nullptr);}
  void roots(){setting_=true;ScreamSeq::NativeInputGate::present(controls_.at(root),CB_RESETCONTENT,0,0);ScreamSeq::NativeInputGate::present(controls_.at(root),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"All sample folders"));int choice=0;
    for(size_t i=0;i<state_.at("roots").size();++i){const auto path=state_.at("roots")[i].get<std::string>();const auto text=wide(path);ScreamSeq::NativeInputGate::present(controls_.at(root),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(path==rootPath_)choice=int(i+1);}if(!choice)rootPath_.clear();ScreamSeq::NativeInputGate::present(controls_.at(root),CB_SETCURSEL,choice,0);setting_=false;}
  std::vector<unsigned> selections(int id)const{const auto count=SendMessageW(controls_.at(id),LB_GETSELCOUNT,0,0);if(count<=0)return {};std::vector<int> items(size_t(count),0);SendMessageW(controls_.at(id),LB_GETSELITEMS,count,reinterpret_cast<LPARAM>(items.data()));return {items.begin(),items.end()};}
  void list(int id,const Json &items,const std::vector<std::string> &selected,const char *key){const auto top=SendMessageW(controls_.at(id),LB_GETTOPINDEX,0,0);SendMessageW(controls_.at(id),WM_SETREDRAW,FALSE,0);ScreamSeq::NativeInputGate::present(controls_.at(id),LB_RESETCONTENT,0,0);
    for(size_t i=0;i<items.size();++i){const auto &item=items[i];auto name=wide(item.at("name").get<std::string>());if(id==tags)name+=L"  ("+std::to_wstring(item.at("count").get<unsigned>())+L")";ScreamSeq::NativeInputGate::present(controls_.at(id),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(std::find(selected.begin(),selected.end(),item.at(key).get<std::string>())!=selected.end())ScreamSeq::NativeInputGate::present(controls_.at(id),LB_SETSEL,TRUE,i);}
    if(top!=LB_ERR)ScreamSeq::NativeInputGate::present(controls_.at(id),LB_SETTOPINDEX,top,0);SendMessageW(controls_.at(id),WM_SETREDRAW,TRUE,0);InvalidateRect(controls_.at(id),nullptr,FALSE);
  }
  void load(){
    if(pending_)return;queued_=false;const auto generation=generation_;pending_=true;layout();
    Json params={{"query",utf8(field(search))},{"tagQuery",utf8(field(tagSearch))},{"tags",selectedTags_},{"offset",offset_},{"limit",200}};if(!rootPath_.empty())params["root"]=rootPath_;
    try{auto data=call("sample.library.search",params);pending_=false;if(generation!=generation_||!visible()){layout();return;}
      const auto old=selectedPaths();entries_=data.at("items");facets_=data.at("tags");total_=data.at("total");revision_=data.at("libraryRevision").get<std::string>();list(files,entries_,old,"path");
      // Keep selected facets visible even when no current result matches them.
      for(const auto &tag:selectedTags_)if(std::none_of(facets_.begin(),facets_.end(),[&](const auto &f){return f.at("name")==tag;}))facets_.push_back({{"name",tag},{"count",0}});
      list(tags,facets_,selectedTags_,"name");if(std::none_of(entries_.begin(),entries_.end(),[&](const auto &e){return e.at("path")==selectedPath_;})){selectedPath_.clear();inspection_=group_=nullptr;peaks_.clear();set(detailLabel,L"Select a sample to inspect its waveform");call("sample.library.preview.stop");}
      set(resultsLabel,std::to_wstring(total_)+L" samples / "+std::to_wstring(offset_+std::min<size_t>(1,entries_.size()))+L"–"+std::to_wstring(offset_+entries_.size()));
      status(data.value("indexing",false)?L"Refreshing folders / previous results remain available":L"Select to inspect / Space previews / Enter imports / folder tags are combined");
    }catch(...){pending_=false;layout();throw;}layout();requestPaint();
  }
  std::vector<std::string> selectedPaths()const{std::vector<std::string> paths;for(auto i:selections(files))if(i<entries_.size())paths.push_back(entries_[i].at("path"));return paths;}
  void inspect(bool audible){
    if(selectedPath_.empty())return;const auto path=selectedPath_;const auto generation=++generation_;pending_=true;layout();
    try{Json p={{"path",path}};if(audible){updateGain(true);p["gainDB"]=number(gain);}auto data=call(audible?"sample.library.preview":"sample.library.inspect",p);
      auto group=call("sample.library.multisample.get",{{"path",path}});pending_=false;if(generation!=generation_||path!=selectedPath_||!visible()){layout();return;}
      inspection_=std::move(data);group_=group.at("group");peaks_=inspection_.at("peaks").get<std::vector<float>>();
      set(detailLabel,wide(path));
      status(std::to_wstring(inspection_.at("rate").get<unsigned>())+L" Hz / "+std::to_wstring(inspection_.at("channels").get<unsigned>())+L" channels / "+std::to_wstring(inspection_.at("seconds").get<double>())+L" seconds"+(group_.is_object()?L" / multi-sample family found":L""));
    }catch(...){pending_=false;layout();throw;}layout();playback();SetTimer(window_,1,previewPlaying_?50:200,nullptr);requestPaint();
  }
  HWND suspendImportFocus(){
    const auto focus=GetFocus();if(!owns(focus)||focus==window_)return nullptr;
    // Disabling a focused child lets Win32 move focus outside the browser.
    // Park it on this owner until completion; later navigation wins.
    SetFocus(window_);return focus;
  }
  void restoreImportFocus(HWND focus){
    if(!focus||!visible()||GetFocus()!=window_)return;
    if(IsWindow(focus)&&IsWindowVisible(focus)&&IsWindowEnabled(focus))SetFocus(focus);
    else if(libraryFrozen()&&IsWindowEnabled(controls_.at(rescan)))SetFocus(controls_.at(rescan));
    else if(IsWindowEnabled(controls_.at(importSelection)))SetFocus(controls_.at(importSelection));
  }
  void finishImport(bool reveal){
    const auto receipt=importCompletion_.returned();if(!receipt)throw std::runtime_error("Import result is still unknown");
    const auto &result=receipt->result;const auto &samples=result.at("samples");
    if(!samples.is_array()||samples.empty()||result.at("count").get<size_t>()!=samples.size())throw std::runtime_error("Malformed import result");
    for(const auto &sample:samples){(void)sample.at("path").get<std::string>();(void)sample.at("sample").get<unsigned>();(void)sample.at("instrument").get<unsigned>();}
    auto report=Json{{"outcome","returned"},{"submission",importCompletion_.snapshot()},{"params",importParams_},{"result",result}};
    const auto current=context_();
    // Result slots belong to the captured revision. Review never replays reveal
    // after a partial native callback, nor retargets today's sample selection.
    const bool revealCurrent=reveal&&current.document==receipt->document&&current.revision==receipt->revision;
    if(revealCurrent)imported_(result,receipt->document);
    status(std::to_wstring(samples.size())+L" samples imported / one document Undo step"+(!revealCurrent?L" / result reviewed; current selection retained":L""));
    importReport_=std::move(report);importCompletion_.finish();
  }
  void reviewImport(){
    if(pending_)return;
    if(importNeedsRebase_){importNeedsRebase_=false;queue();layout();status(L"Current song and library selection adopted / Import starts a new request");return;}
    if(!importCompletion_.retained())return;const auto focus=suspendImportFocus();pending_=true;layout();
    try{
      call("synchronizeView");
      if(importCompletion_.returned())finishImport(false);
      else {
        const auto captured=context_();if(captured.document!=importContext_.document)throw std::runtime_error("Original import song is no longer open / result retained");
        auto observed=call("document.get");const auto after=context_();
        if(after.document!=captured.document||after.revision!=captured.revision)throw std::runtime_error("Song changed during import Review / result retained");
        for(const char *key:{"samples","instruments"}){
          const auto &items=observed.at(key);if(!items.is_array())throw std::runtime_error("Malformed import observation");
          for(const auto &item:items){(void)item.at("id").get<std::string>();(void)item.at("index").get<unsigned>();}
        }
        auto report=Json{{"outcome","unverified"},{"submission",importCompletion_.snapshot()},{"params",importParams_},{"observedRevision",captured.revision},{"observed",std::move(observed)}};
        status(L"Current samples inspected / earlier import unverified / Use current song before a new import");
        importReport_=std::move(report);importNeedsRebase_=true;importCompletion_.finish();
      }
    }catch(...){pending_=false;layout();restoreImportFocus(focus);throw;}pending_=false;layout();restoreImportFocus(focus);
  }
  void importPaths(const std::vector<std::string> &paths,const Context &captured,bool instruments){
    requireBrowserResolved();if(paths.empty())return;if(paths.size()>128)throw std::runtime_error("Select at most 128 samples to import");
    auto current=context_();if(current.document!=captured.document||current.revision!=captured.revision)throw std::runtime_error("Song changed / select Import again");
    importParams_={{"paths",paths},{"createInstruments",instruments},{"expectedRevision",captured.revision}};importContext_=captured;importGeneration_=++generation_;
    const auto focus=suspendImportFocus();pending_=true;layout();try{
      importCompletion_.submit(write_,"sample.importMany",importParams_,captured.document,importGeneration_,importParams_);
      finishImport(true);
    }catch(...){pending_=false;layout();restoreImportFocus(focus);throw;}pending_=false;layout();restoreImportFocus(focus);
  }
  static void validateLibraryState(const Json &data){
    if(!data.at("roots").is_array()||!data.at("warnings").is_array()||!data.at("extensions").is_array()||
        (!data.at("error").is_null()&&!data.at("error").is_string()))throw std::runtime_error("Malformed library observation");
    for(const auto &path:data.at("roots"))(void)path.get<std::string>();
    (void)data.at("libraryRevision").get<std::string>();(void)data.at("count").get<size_t>();
    (void)data.at("indexing").get<bool>();(void)data.at("ready").get<bool>();
  }
  void adoptLibraryState(Json data){validateLibraryState(data);state_=std::move(data);roots();queue();}
  void finishLibrary(){
    const auto returned=libraryCompletion_.returned();if(!returned)throw std::runtime_error("Library result is still unknown");
    validateLibraryState(returned->result);
    auto report=Json{{"outcome","returned"},{"submission",libraryCompletion_.snapshot()},{"params",libraryParams_},{"result",returned->result}};
    auto current=call("sample.library.get");adoptLibraryState(std::move(current));
    status(!state_.at("error").is_null()?L"Library request completed / scan error: "+wide(state_.at("error").get<std::string>()):
      state_.at("indexing").get<bool>()?L"Library request accepted / indexing continues":L"Library result reviewed / song and Undo history unchanged");
    libraryReport_=std::move(report);libraryCompletion_.finish();libraryMethod_.clear();
  }
  void reviewLibrary(){
    if(pending_||!libraryFrozen())return;const auto focus=suspendImportFocus();pending_=libraryWorking_=true;
    try{
      layout();
      if(libraryNeedsReload_){adoptLibraryState(call("sample.library.get"));libraryNeedsReload_=false;status(L"Current library adopted / Rescan starts a new request");}
      else if(libraryCompletion_.returned())finishLibrary();
      else {
        auto observed=call("sample.library.get");validateLibraryState(observed);
        auto report=Json{{"outcome","unverified"},{"submission",libraryCompletion_.snapshot()},{"params",libraryParams_},{"observed",std::move(observed)}};
        status(L"Current library inspected / earlier outcome unverified / Reload before another change");
        libraryReport_=std::move(report);libraryNeedsReload_=true;libraryCompletion_.finish();libraryMethod_.clear();
      }
    }catch(...){pending_=libraryWorking_=false;layout();restoreImportFocus(focus);throw;}
    pending_=libraryWorking_=false;layout();restoreImportFocus(focus);
  }
  void submitLibrary(const std::string &method,Json params){
    requireBrowserResolved();const auto document=context_().document;
    libraryMethod_=method;libraryParams_=std::move(params);const auto generation=++generation_;
    const auto fields=Json{{"search",utf8(field(search))},{"tagSearch",utf8(field(tagSearch))},{"root",rootPath_},{"tags",selectedTags_},{"selectedPaths",selectedPaths()},{"libraryRevision",state_.at("libraryRevision")}};
    const auto focus=suspendImportFocus();pending_=libraryWorking_=true;
    try{layout();libraryCompletion_.submit(write_,method,libraryParams_,document,generation,fields);finishLibrary();}
    catch(...){if(!libraryCompletion_.retained())libraryMethod_.clear();pending_=libraryWorking_=false;layout();restoreImportFocus(focus);throw;}
    pending_=libraryWorking_=false;layout();restoreImportFocus(focus);
  }
  void folderChange(bool add){const auto expected=state_.at("libraryRevision");auto paths=state_.at("roots");pending_=libraryWorking_=true;
    try{layout();if(add){const auto chosen=chooseSampleFolders(window_);if(chosen.empty()){pending_=libraryWorking_=false;layout();return;}for(const auto &path:chosen){auto s=path.u8string();paths.push_back(std::string(s.begin(),s.end()));}}
      else {if(rootPath_.empty())throw std::runtime_error("Choose one folder to remove");Json retained=Json::array();for(const auto &path:paths)if(path!=rootPath_)retained.push_back(path);paths=std::move(retained);}
    }catch(...){pending_=libraryWorking_=false;layout();throw;}
    pending_=libraryWorking_=false;submitLibrary("sample.library.roots.set",{{"roots",paths},{"expectedLibraryRevision",expected}});
  }
  void action(int id,unsigned notification)override{
    if(setting_||!ready_)return;if(id==close){hide();return;}if(id==stop){++generation_;call("sample.library.preview.stop");playback();return;}
    if(id==gain&&notification==EN_CHANGE){updateGain(false);return;}if(pending_)return;
    if(id==importSelection&&importFrozen()){reviewImport();return;}if(id==rescan&&libraryFrozen()){reviewLibrary();return;}requireBrowserResolved();
    if((id==search||id==tagSearch)&&notification==EN_CHANGE){queue();return;}
    if(id==root&&notification==CBN_SELCHANGE){const auto i=SendMessageW(controls_.at(root),CB_GETCURSEL,0,0);rootPath_=i>0&&size_t(i)<=state_.at("roots").size()?state_.at("roots")[size_t(i-1)].get<std::string>():"";queue();return;}
    if(id==tags&&notification==LBN_SELCHANGE){selectedTags_.clear();for(auto i:selections(tags))if(i<facets_.size())selectedTags_.push_back(facets_[i].at("name"));queue();return;}
    if(id==files&&notification==LBN_SELCHANGE){const auto paths=selectedPaths();const auto caret=SendMessageW(controls_.at(files),LB_GETCARETINDEX,0,0);const auto path=caret>=0&&size_t(caret)<entries_.size()?entries_[size_t(caret)].at("path").get<std::string>():paths.empty()?"":paths.front();if(path!=selectedPath_){selectedPath_=path;call("sample.library.preview.stop");inspect(auto_);}return;}
    if(id==files&&notification==LBN_DBLCLK){importPaths(selectedPaths(),context_(),mapped_);return;}
    if(notification!=BN_CLICKED)return;
    if(id==preview)inspect(true);else if(id==autoPreview){auto_=!auto_;if(!auto_)call("sample.library.preview.stop");}
    else if(id==mapped)mapped_=!mapped_;
    else if(id==addFolder||id==removeFolder)folderChange(id==addFolder);
    else if(id==rescan)submitLibrary("sample.library.rescan",{{"expectedLibraryRevision",state_.at("libraryRevision")}});
    else if(id==previous||id==next){if(id==previous)offset_=offset_>=200?offset_-200:0;else if(offset_+200<total_)offset_+=200;queued_=true;++generation_;SetTimer(window_,1,1,nullptr);}
    else if(id==importSelection)importPaths(selectedPaths(),context_(),mapped_);
    else if(id==chooseFiles)chooseAndImport(mapped_,[this]{return chooseSampleFiles(window_,L"Import samples",true);});
    else if(id==family&&group_.is_object())reviewFamily(group_);
  }
  void timer(UINT_PTR id)override{
    if(id!=1||!visible())return;playback();if(pending_||browserFrozen())return;auto state=statusRead_();if(state_!=state){const bool changed=state_.is_null()||state_.value("libraryRevision",std::string{})!=state.value("libraryRevision",std::string{});state_=std::move(state);roots();if(changed){queued_=true;++generation_;}set(heading,L"Sample library / "+std::to_wstring(state_.at("count").get<unsigned>())+(state_.at("indexing").get<bool>()?L" / indexing…":L""));if(!state_.at("error").is_null())status(wide(state_.at("error").get<std::string>()));layout();}
    if(queued_)load();SetTimer(window_,1,previewPlaying_?50:200,nullptr);
  }
  bool key(WPARAM value,bool ctrl,bool)override{
    if(value==VK_ESCAPE){++generation_;call("sample.library.preview.stop");playback();return true;}
    if(ctrl&&value=='F'){SetFocus(controls_.at(search));ScreamSeq::NativeInputGate::present(controls_.at(search),EM_SETSEL,0,-1);return true;}
    if(ctrl&&value=='R'){action(rescan,BN_CLICKED);return true;}
    if(value==VK_SPACE&&GetFocus()==controls_.at(files)){action(preview,BN_CLICKED);return true;}
    if(value==VK_RETURN){if(GetFocus()==controls_.at(gain)){updateGain(true);return true;}if(GetFocus()==controls_.at(files)){action(importSelection,BN_CLICKED);return true;}wchar_t type[32]{};GetClassNameW(GetFocus(),type,32);if(_wcsicmp(type,L"Button")==0){action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}}
    return false;
  }
  void layout()override{if(!ready_)return;const auto [w,h]=size();const auto left=190.f;place(heading,16,12,w-300,26);place(addFolder,w-276,12,100,26);place(removeFolder,w-168,12,78,26);place(rescan,w-82,12,66,26);
    place(searchLabel,16,48,w-left-36,18);place(search,16,70,w-left-36,26);place(rootLabel,w-left-12,48,left-4,18);place(root,w-left-12,70,left-4,280);
    place(tagsLabel,16,108,left-16,20);place(tagSearch,16,132,left-16,26);place(tags,16,166,left-16,std::max(60.f,h-355));place(resultsLabel,left+12,108,w-left-28,20);place(files,left+12,132,w-left-28,std::max(80.f,h-321));
    place(detailLabel,16,h-179,w-32,20);place(preview,16,h-145,78,26);place(stop,102,h-145,62,26);place(autoPreview,172,h-145,138,26);place(gainLabel,320,h-140,60,20);place(gain,381,h-145,55,26);place(previous,w-158,h-145,66,26);place(next,w-84,h-145,68,26);
    place(mapped,16,h-107,156,26);place(chooseFiles,180,h-107,100,26);place(family,w-430,h-107,148,26);place(importSelection,w-274,h-107,170,26);place(close,w-96,h-107,80,26);place(statusLabel,16,h-65,w-32,50);
    const bool available=!pending_&&!browserFrozen();
    for(int id:{search,tagSearch,root,tags,files,addFolder,removeFolder,rescan,chooseFiles,mapped,autoPreview,previous,next})EnableWindow(controls_.at(id),available);
    EnableWindow(controls_.at(removeFolder),available&&!rootPath_.empty());set(rescan,libraryCompletion_.retained()?L"Review":libraryNeedsReload_?L"Reload":L"Rescan");EnableWindow(controls_.at(rescan),!pending_&&!importFrozen()&&(libraryFrozen()||!state_.value("indexing",true)));EnableWindow(controls_.at(preview),available&&!selectedPath_.empty());EnableWindow(controls_.at(family),available&&group_.is_object());set(importSelection,importCompletion_.retained()?L"Review import":importNeedsRebase_?L"Use current song":L"Import selection");EnableWindow(controls_.at(importSelection),!pending_&&!libraryFrozen()&&(importFrozen()||(!queued_&&!selectedPaths().empty())));EnableWindow(controls_.at(previous),available&&offset_>0);EnableWindow(controls_.at(next),available&&offset_+entries_.size()<total_);
    set(autoPreview,auto_?L"Auto-preview: on":L"Auto-preview: off");set(mapped,mapped_?L"Create instruments: on":L"Create instruments: off");}
  void paint(RenderSurface &s)override{const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);const float x=452,y=h-146,width=std::max(8.f,w-x-180),height=29;s.fill(x,y,width,height,0x111b25);const auto mid=y+height/2;s.line(x,mid,x+width,mid,0x344a57);for(size_t i=0;i+1<peaks_.size();i+=2){const float at=x+float(i/2)*width/float(peaks_.size()/2);s.line(at,mid-peaks_[i]*height*.45f,at,mid-peaks_[i+1]*height*.45f,0x79d8c8);}if(previewPlaying_){const float at=x+previewPosition_*width;s.line(at,y,at,y+height,0xf0bf72);}}
public:
  SampleLibraryWindow(HWND owner,Request request,std::function<Json()> statusRead,std::function<Json()> previewRead,std::function<void(double)> gainChanged,std::function<Context()> context,std::function<void(const Json &,const std::string &)> imported,std::function<void(unsigned,const std::string &)> multisampleApplied,NativeWriteCompletion::Write write):NativeToolWindow(owner),request_(std::move(request)),statusRead_(std::move(statusRead)),context_(std::move(context)),previewRead_(std::move(previewRead)),gainChanged_(std::move(gainChanged)),imported_(std::move(imported)){
    minimumWidth_=920;minimumHeight_=600;create(L"ScreamSeq.SampleLibrary",L"Sample library",1120,760);mapped_=context_().instruments;state_=statusRead_();edit(search,L"",4096);edit(tagSearch,L"",200);combo(root);edit(gain,L"-12",8);
    add(tags,L"LISTBOX",L"Folder tags",LBS_EXTENDEDSEL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);add(files,L"LISTBOX",L"Sample files",LBS_EXTENDEDSEL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{addFolder,L"Add folders…"},{removeFolder,L"Remove"},{rescan,L"Rescan"},{chooseFiles,L"Choose files…"},{mapped,L""},{preview,L"Preview"},{stop,L"Stop"},{autoPreview,L""},{importSelection,L"Import selection"},{family,L"Review family…"},{previous,L"Previous"},{next,L"Next"},{close,L"Close"}})button(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"Sample library"},{searchLabel,L"Search samples / quotes group words, minus excludes"},{rootLabel,L"Folder"},{tagsLabel,L"Folder tags / Ctrl selects more"},{resultsLabel,L""},{detailLabel,L"Select a sample to inspect its waveform"},{statusLabel,L""},{gainLabel,L"Gain dB"}})label(id,text);
    multisampleApplied_=std::move(multisampleApplied);write_=std::move(write);multisampleEditor();finish();roots();
  }
  // The picker is native to its caller, but owns the same captured import as
  // library selection. A direct command can retain its result here while this
  // browser stays hidden; invoking it again reveals Review without replay.
  bool chooseAndImport(bool instruments,const std::function<std::vector<std::filesystem::path>()> &choose){
    if(pending_)throw std::runtime_error("Wait for the sample browser request before importing");
    if(importFrozen()){show();SetFocus(controls_.at(importSelection));throw std::runtime_error("Review the previous import in Sample library before importing again");}
    requireBrowserResolved();const auto captured=context_();importContext_=captured;
    importParams_={{"createInstruments",instruments},{"expectedRevision",captured.revision}};importGeneration_=++generation_;
    const auto focus=suspendImportFocus();pending_=choosingImport_=true;layout();
    std::vector<std::string> paths;
    try{for(const auto &path:choose()){auto text=path.u8string();paths.emplace_back(text.begin(),text.end());}}
    catch(...){pending_=choosingImport_=false;layout();restoreImportFocus(focus);throw;}
    pending_=choosingImport_=false;layout();restoreImportFocus(focus);
    if(paths.empty())return false;
    try{importPaths(paths,captured,instruments);}
    catch(const std::exception &e){error(e);throw;}
    return true;
  }
  void reviewFamily(const Json &group){requireBrowserResolved();multisampleEditor().open(group);}
  void show(){const bool wasVisible=visible();NativeToolWindow::show();if(!wasVisible&&!browserFrozen())queued_=true;SetTimer(window_,1,1,nullptr);SetFocus(controls_.at(search));}
  void hide()override{++generation_;call("sample.library.preview.stop");playback();if(multisample_->visible())multisample_->hide();KillTimer(window_,1);NativeToolWindow::hide();}
  bool protectsClose()const noexcept{return libraryWorking_||libraryCompletion_.retained();}
  Json snapshot()const{return {{"libraryCompletion",libraryCompletion_.snapshot()},{"libraryReport",libraryReport_},{"libraryNeedsReload",libraryNeedsReload_},{"choosingImport",choosingImport_},{"importCompletion",importCompletion_.snapshot()},{"importReport",importReport_},{"importNeedsRebase",importNeedsRebase_},{"visible",visible()},{"pending",pending_},{"queued",queued_},{"libraryRevision",revision_},{"search",utf8(field(search))},{"root",rootPath_},{"tags",selectedTags_},{"items",entries_},{"offset",offset_},{"total",total_},{"selectedPaths",selectedPaths()},{"selected",selectedPath_},{"inspection",inspection_},{"family",group_},{"createInstruments",mapped_},{"autoPreview",auto_},{"gainDB",utf8(field(gain))},{"previewPlaying",previewPlaying_},{"previewPosition",previewPosition_},{"status",utf8(status_)},{"multisample",multisample_&&!multisample_->retired()?multisample_->snapshot():Json{{"visible",false}}}};}
};
}
