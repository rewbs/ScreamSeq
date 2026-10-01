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
  void status(std::wstring message){status_=std::move(message);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
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
  void roots(){setting_=true;SendMessageW(controls_.at(root),CB_RESETCONTENT,0,0);SendMessageW(controls_.at(root),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"All sample folders"));int choice=0;
    for(size_t i=0;i<state_.at("roots").size();++i){const auto path=state_.at("roots")[i].get<std::string>();const auto text=wide(path);SendMessageW(controls_.at(root),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(path==rootPath_)choice=int(i+1);}if(!choice)rootPath_.clear();SendMessageW(controls_.at(root),CB_SETCURSEL,choice,0);setting_=false;}
  std::vector<unsigned> selections(int id)const{const auto count=SendMessageW(controls_.at(id),LB_GETSELCOUNT,0,0);if(count<=0)return {};std::vector<int> items(size_t(count),0);SendMessageW(controls_.at(id),LB_GETSELITEMS,count,reinterpret_cast<LPARAM>(items.data()));return {items.begin(),items.end()};}
  void list(int id,const Json &items,const std::vector<std::string> &selected,const char *key){const auto top=SendMessageW(controls_.at(id),LB_GETTOPINDEX,0,0);SendMessageW(controls_.at(id),WM_SETREDRAW,FALSE,0);SendMessageW(controls_.at(id),LB_RESETCONTENT,0,0);
    for(size_t i=0;i<items.size();++i){const auto &item=items[i];auto name=wide(item.at("name").get<std::string>());if(id==tags)name+=L"  ("+std::to_wstring(item.at("count").get<unsigned>())+L")";SendMessageW(controls_.at(id),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(std::find(selected.begin(),selected.end(),item.at(key).get<std::string>())!=selected.end())SendMessageW(controls_.at(id),LB_SETSEL,TRUE,i);}
    if(top!=LB_ERR)SendMessageW(controls_.at(id),LB_SETTOPINDEX,top,0);SendMessageW(controls_.at(id),WM_SETREDRAW,TRUE,0);InvalidateRect(controls_.at(id),nullptr,FALSE);
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
  void importPaths(const std::vector<std::string> &paths,const Context &captured){
    if(paths.empty())return;if(paths.size()>128)throw std::runtime_error("Select at most 128 samples to import");
    auto current=context_();if(current.document!=captured.document||current.revision!=captured.revision)throw std::runtime_error("Song changed / select Import again");
    pending_=true;layout();try{auto result=call("sample.importMany",{{"paths",paths},{"createInstruments",mapped_},{"expectedRevision",captured.revision}});pending_=false;imported_(result,captured.document);status(std::to_wstring(result.at("count").get<unsigned>())+L" samples imported / one document Undo step");}catch(...){pending_=false;layout();throw;}layout();
  }
  void folderChange(bool add){const auto expected=state_.at("libraryRevision");auto paths=state_.at("roots");pending_=true;layout();
    try{if(add){const auto chosen=chooseSampleFolders(window_);if(chosen.empty()){pending_=false;layout();return;}for(const auto &path:chosen){auto s=path.u8string();paths.push_back(std::string(s.begin(),s.end()));}}
      else {if(rootPath_.empty())throw std::runtime_error("Choose one folder to remove");Json retained=Json::array();for(const auto &path:paths)if(path!=rootPath_)retained.push_back(path);paths=std::move(retained);}
      call("sample.library.roots.set",{{"roots",paths},{"expectedLibraryRevision",expected}});pending_=false;state_=statusRead_();roots();queue();status(L"Indexing sample folders / source files remain in place");
    }catch(...){pending_=false;layout();throw;}layout();}
  void action(int id,unsigned notification)override{
    if(setting_||!ready_)return;if(id==close){hide();return;}if(id==stop){++generation_;call("sample.library.preview.stop");playback();return;}
    if(id==gain&&notification==EN_CHANGE){updateGain(false);return;}if(pending_)return;
    if((id==search||id==tagSearch)&&notification==EN_CHANGE){queue();return;}
    if(id==root&&notification==CBN_SELCHANGE){const auto i=SendMessageW(controls_.at(root),CB_GETCURSEL,0,0);rootPath_=i>0&&size_t(i)<=state_.at("roots").size()?state_.at("roots")[size_t(i-1)].get<std::string>():"";queue();return;}
    if(id==tags&&notification==LBN_SELCHANGE){selectedTags_.clear();for(auto i:selections(tags))if(i<facets_.size())selectedTags_.push_back(facets_[i].at("name"));queue();return;}
    if(id==files&&notification==LBN_SELCHANGE){const auto paths=selectedPaths();const auto caret=SendMessageW(controls_.at(files),LB_GETCARETINDEX,0,0);const auto path=caret>=0&&size_t(caret)<entries_.size()?entries_[size_t(caret)].at("path").get<std::string>():paths.empty()?"":paths.front();if(path!=selectedPath_){selectedPath_=path;call("sample.library.preview.stop");inspect(auto_);}return;}
    if(id==files&&notification==LBN_DBLCLK){importPaths(selectedPaths(),context_());return;}
    if(notification!=BN_CLICKED)return;
    if(id==preview)inspect(true);else if(id==autoPreview){auto_=!auto_;if(!auto_)call("sample.library.preview.stop");}
    else if(id==mapped)mapped_=!mapped_;
    else if(id==addFolder||id==removeFolder)folderChange(id==addFolder);
    else if(id==rescan){call("sample.library.rescan",{{"expectedLibraryRevision",state_.at("libraryRevision")}});status(L"Refreshing sample folders");}
    else if(id==previous||id==next){if(id==previous)offset_=offset_>=200?offset_-200:0;else if(offset_+200<total_)offset_+=200;queued_=true;++generation_;SetTimer(window_,1,1,nullptr);}
    else if(id==importSelection)importPaths(selectedPaths(),context_());
    else if(id==chooseFiles){const auto captured=context_();pending_=true;layout();try{const auto chosen=chooseSampleFiles(window_,L"Import samples",true);pending_=false;std::vector<std::string> paths;for(const auto &p:chosen){auto s=p.u8string();paths.emplace_back(s.begin(),s.end());}importPaths(paths,captured);}catch(...){pending_=false;layout();throw;}}
    else if(id==family&&group_.is_object())multisample_->open(group_);
  }
  void timer(UINT_PTR id)override{
    if(id!=1||!visible())return;playback();if(pending_)return;auto state=statusRead_();if(state_!=state){const bool changed=state_.is_null()||state_.value("libraryRevision",std::string{})!=state.value("libraryRevision",std::string{});state_=std::move(state);roots();if(changed){queued_=true;++generation_;}set(heading,L"Sample library / "+std::to_wstring(state_.at("count").get<unsigned>())+(state_.at("indexing").get<bool>()?L" / indexing…":L""));if(!state_.at("error").is_null())status(wide(state_.at("error").get<std::string>()));layout();}
    if(queued_)load();SetTimer(window_,1,previewPlaying_?50:200,nullptr);
  }
  bool key(WPARAM value,bool ctrl,bool)override{
    if(value==VK_ESCAPE){++generation_;call("sample.library.preview.stop");playback();return true;}
    if(ctrl&&value=='F'){SetFocus(controls_.at(search));SendMessageW(controls_.at(search),EM_SETSEL,0,-1);return true;}
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
    for(int id:{search,tagSearch,root,tags,files,addFolder,removeFolder,rescan,chooseFiles,mapped,autoPreview,previous,next})EnableWindow(controls_.at(id),!pending_);
    EnableWindow(controls_.at(removeFolder),!pending_&&!rootPath_.empty());EnableWindow(controls_.at(rescan),!pending_&&!state_.value("indexing",true));EnableWindow(controls_.at(preview),!pending_&&!selectedPath_.empty());EnableWindow(controls_.at(family),!pending_&&group_.is_object());EnableWindow(controls_.at(importSelection),!pending_&&!queued_&&!selectedPaths().empty());EnableWindow(controls_.at(previous),!pending_&&offset_>0);EnableWindow(controls_.at(next),!pending_&&offset_+entries_.size()<total_);
    set(autoPreview,auto_?L"Auto-preview: on":L"Auto-preview: off");set(mapped,mapped_?L"Create instruments: on":L"Create instruments: off");}
  void paint(RenderSurface &s)override{const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);const float x=452,y=h-146,width=std::max(8.f,w-x-180),height=29;s.fill(x,y,width,height,0x111b25);const auto mid=y+height/2;s.line(x,mid,x+width,mid,0x344a57);for(size_t i=0;i+1<peaks_.size();i+=2){const float at=x+float(i/2)*width/float(peaks_.size()/2);s.line(at,mid-peaks_[i]*height*.45f,at,mid-peaks_[i+1]*height*.45f,0x79d8c8);}if(previewPlaying_){const float at=x+previewPosition_*width;s.line(at,y,at,y+height,0xf0bf72);}}
public:
  SampleLibraryWindow(HWND owner,Request request,std::function<Json()> statusRead,std::function<Json()> previewRead,std::function<void(double)> gainChanged,std::function<Context()> context,std::function<void(const Json &,const std::string &)> imported,std::function<void(unsigned,const std::string &)> multisampleApplied):NativeToolWindow(owner),request_(std::move(request)),statusRead_(std::move(statusRead)),context_(std::move(context)),previewRead_(std::move(previewRead)),gainChanged_(std::move(gainChanged)),imported_(std::move(imported)){
    minimumWidth_=920;minimumHeight_=600;create(L"ScreamSeq.SampleLibrary",L"Sample library",1120,760);mapped_=context_().instruments;state_=statusRead_();edit(search,L"",4096);edit(tagSearch,L"",200);combo(root);edit(gain,L"-12",8);
    add(tags,L"LISTBOX",L"Folder tags",LBS_EXTENDEDSEL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);add(files,L"LISTBOX",L"Sample files",LBS_EXTENDEDSEL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{addFolder,L"Add folders…"},{removeFolder,L"Remove"},{rescan,L"Rescan"},{chooseFiles,L"Choose files…"},{mapped,L""},{preview,L"Preview"},{stop,L"Stop"},{autoPreview,L""},{importSelection,L"Import selection"},{family,L"Review family…"},{previous,L"Previous"},{next,L"Next"},{close,L"Close"}})button(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"Sample library"},{searchLabel,L"Search samples / quotes group words, minus excludes"},{rootLabel,L"Folder"},{tagsLabel,L"Folder tags / Ctrl selects more"},{resultsLabel,L""},{detailLabel,L"Select a sample to inspect its waveform"},{statusLabel,L""},{gainLabel,L"Gain dB"}})label(id,text);
    multisample_=std::make_unique<MultisampleImportWindow>(window_,request_,[this]{const auto c=context_();return std::pair(c.document,c.revision);},std::move(multisampleApplied));finish();roots();
  }
  void show(){const bool wasVisible=visible();NativeToolWindow::show();if(!wasVisible)queued_=true;SetTimer(window_,1,1,nullptr);SetFocus(controls_.at(search));}
  void hide()override{++generation_;call("sample.library.preview.stop");playback();if(multisample_->visible())multisample_->hide();KillTimer(window_,1);NativeToolWindow::hide();}
  Json snapshot()const{return {{"visible",visible()},{"pending",pending_},{"queued",queued_},{"libraryRevision",revision_},{"search",utf8(field(search))},{"root",rootPath_},{"tags",selectedTags_},{"items",entries_},{"offset",offset_},{"total",total_},{"selectedPaths",selectedPaths()},{"selected",selectedPath_},{"inspection",inspection_},{"family",group_},{"createInstruments",mapped_},{"autoPreview",auto_},{"gainDB",utf8(field(gain))},{"previewPlaying",previewPlaying_},{"previewPosition",previewPosition_},{"status",utf8(status_)},{"multisample",multisample_->snapshot()}};}
};
}
