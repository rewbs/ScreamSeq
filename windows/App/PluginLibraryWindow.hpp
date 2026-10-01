#pragma once
#include "NativeToolWindow.hpp"
namespace ScreamSeq {
class PluginLibraryWindow final : public NativeToolWindow {
  using Json=Api::Json;
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
  enum : int {search=3201,kind,format,category,favorites,hidden,plugins,favorite,hidePlugin,customCategory,saveCategory,insert,reload,rescan,close,
    heading=3300,searchLabel,kindLabel,formatLabel,categoryLabel,detailLabel,customLabel,statusLabel};
  Request request_;Context context_;
  Json entries_=Json::array(),allEntries_=Json::array(),categories_=Json::array();
  std::string selected_,revision_,filterCategory_,warning_;
  bool favoritesOnly_=false,includeHidden_=false,pending_=false,setting_=false,categoryDraft_=false,refreshQueued_=false,readQueued_=false;
  const Json *selected()const{for(const auto &p:entries_)if(p.at("catalogID")==selected_)return &p;return nullptr;}
  void status(std::wstring message){status_=std::move(message);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  int choice(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  void strings(int id,const std::vector<std::wstring> &items,int selection=0){SendMessageW(controls_.at(id),CB_RESETCONTENT,0,0);for(const auto &item:items)SendMessageW(controls_.at(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item.c_str()));SendMessageW(controls_.at(id),CB_SETCURSEL,selection,0);}
  void fields(){setting_=true;const auto *p=selected();if(!categoryDraft_)set(customCategory,p?wide(p->at("customCategory").get<std::string>()):L"");
    set(favorite,p&&p->at("favorite").get<bool>()?L"Unfavorite":L"Favorite");set(hidePlugin,p&&p->at("hidden").get<bool>()?L"Unhide":L"Hide");
    set(detailLabel,p?wide(p->at("name").get<std::string>()+" / "+p->at("format").get<std::string>()+" / "+p->at("category").get<std::string>()):L"Choose a plugin to add or organize");setting_=false;layout();
  }
  void list(){
    const auto top=SendMessageW(controls_.at(plugins),LB_GETTOPINDEX,0,0);SendMessageW(controls_.at(plugins),WM_SETREDRAW,FALSE,0);SendMessageW(controls_.at(plugins),LB_RESETCONTENT,0,0);
    int selection=-1;for(size_t i=0;i<entries_.size();++i){const auto &p=entries_[i];auto label=wide((p.at("favorite").get<bool>()?"★  ":"   ")+p.at("name").get<std::string>()+"    / "+p.at("format").get<std::string>()+" / "+p.at("category").get<std::string>()+(p.at("hidden").get<bool>()?"  [hidden]":""));SendMessageW(controls_.at(plugins),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(p.at("catalogID")==selected_)selection=int(i);}
    if(selection<0)selected_.clear(); // Never retarget an edit when its row disappears.
    SendMessageW(controls_.at(plugins),LB_SETCURSEL,selection,0);if(top!=LB_ERR)SendMessageW(controls_.at(plugins),LB_SETTOPINDEX,top,0);SendMessageW(controls_.at(plugins),WM_SETREDRAW,TRUE,0);InvalidateRect(controls_.at(plugins),nullptr,FALSE);fields();
  }
  void filter(){
    refreshQueued_=false;entries_=Json::array();const auto query=field(search);const auto k=choice(kind),f=choice(format);
    for(const auto &p:allEntries_){
      if(f>0&&p.at("format")!=(f==1?"Built-in":f==2?"VST3":"AU"))continue;
      if(k>0&&p.at("isInstrument").get<bool>()!=(k==2))continue;
      if(favoritesOnly_&&!p.at("favorite").get<bool>())continue;
      if(!includeHidden_&&p.at("hidden").get<bool>())continue;
      if(!filterCategory_.empty()&&p.at("category")!=filterCategory_)continue;
      if(!query.empty()){const auto haystack=wide(p.at("name").get<std::string>()+" "+p.at("format").get<std::string>()+" "+p.at("category").get<std::string>());if(FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,FIND_FROMSTART|NORM_IGNORECASE|NORM_IGNORENONSPACE,haystack.data(),int(haystack.size()),query.data(),int(query.size()),nullptr,nullptr,nullptr,0)<0)continue;}
      entries_.push_back(p);
    }list();status(warning_.empty()?std::to_wstring(entries_.size())+L" of "+std::to_wstring(allEntries_.size())+L" plugins / browser preferences are saved separately from songs":wide(warning_));
  }
  void load(bool scan=false){
    if(pending_){readQueued_=refreshQueued_=true;return;}KillTimer(window_,1);readQueued_=refreshQueued_=false;pending_=true;layout();
    try{auto data=request_("plugin.library.get",{{"includeHidden",true},{"rescan",scan}});pending_=false;
      allEntries_=data.at("plugins");categories_=data.at("categories");revision_=data.at("libraryRevision").get<std::string>();warning_=data.at("warning").get<std::string>();
      setting_=true;std::vector<std::wstring> names{L"All categories"};int index=0;
      for(size_t i=0;i<categories_.size();++i){const auto name=categories_[i].get<std::string>();names.push_back(wide(name));if(name==filterCategory_)index=int(i+1);}
      if(!filterCategory_.empty()&&!index){names.push_back(wide(filterCategory_));index=int(names.size()-1);}strings(category,names,index);setting_=false;filter();
    }catch(...){pending_=false;layout();throw;}layout();if(readQueued_)queue(true);
  }
  void queue(bool read=false){refreshQueued_=true;readQueued_|=read;SetTimer(window_,1,read?100:30,nullptr);}
  void change(const Json &patch){
    if(pending_||refreshQueued_)return;const auto *p=selected();if(!p||revision_.empty())throw std::runtime_error("Reload available preferences before editing them");
    const auto target=selected_,expected=revision_;Json params=patch;params["catalogID"]=target;params["expectedLibraryRevision"]=expected;pending_=true;layout();
    try{const auto result=request_("plugin.library.set",params);pending_=false;revision_=result.at("libraryRevision").get<std::string>();categoryDraft_=false;load();}
    catch(...){pending_=false;layout();throw;}layout();
  }
  void addPlugin(){
    if(pending_||refreshQueued_)return;const auto *p=selected();if(!p)return;const auto captured=context_();const auto descriptor=p->at("descriptor");pending_=true;layout();
    try{if(context_()!=captured)throw std::runtime_error("Document changed; add again");request_("plugin.add",{{"expectedRevision",captured.second},{"descriptor",descriptor}});pending_=false;status(L"Plugin added to the rack / Undo FX removes it");}
    catch(...){pending_=false;layout();throw;}layout();
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;if(id==close&&notification==BN_CLICKED){hide();return;}if(pending_)return;
    if(id==customCategory&&notification==EN_CHANGE){if(refreshQueued_)return;categoryDraft_=true;status(L"Category draft / Apply category saves; Escape discards");return;}
    if(id==saveCategory&&notification==BN_CLICKED){change({{"category",utf8(field(customCategory))}});return;}
    if(id==reload&&notification==BN_CLICKED){categoryDraft_=false;load();return;}
    if(categoryDraft_&&id!=insert)throw std::runtime_error("Apply the category or press Escape before changing the selection or filters");
    if(id==search&&notification==EN_CHANGE){queue();return;}
    if(id==kind||id==format||id==category){if(notification!=CBN_SELCHANGE)return;if(id==category){const auto index=choice(category);filterCategory_=index<=0?"":utf8([&]{const auto n=SendMessageW(controls_.at(category),CB_GETLBTEXTLEN,index,0);std::wstring value(size_t(n)+1,0);SendMessageW(controls_.at(category),CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(value.data()));value.resize(size_t(n));return value;}());}queue();return;}
    if(id==plugins){if(notification==LBN_SELCHANGE){const auto index=SendMessageW(controls_.at(plugins),LB_GETCURSEL,0,0);if(index>=0&&size_t(index)<entries_.size()){selected_=entries_[size_t(index)].at("catalogID").get<std::string>();fields();}}else if(notification==LBN_DBLCLK)addPlugin();return;}
    if(notification!=BN_CLICKED)return;
    if(id==favorites){favoritesOnly_=!favoritesOnly_;queue();}else if(id==hidden){includeHidden_=!includeHidden_;queue();}
    else if(id==rescan)load(true);else if(id==insert)addPlugin();
    else if(const auto *p=selected()){if(id==favorite)change({{"favorite",!p->at("favorite").get<bool>()}});else if(id==hidePlugin)change({{"hidden",!p->at("hidden").get<bool>()}});}
  }
  void timer(UINT_PTR id)override{if(id==1){KillTimer(window_,1);if(categoryDraft_)return;try{if(readQueued_)load();else filter();}catch(const Api::ApiError &e){if(e.code==-32002){queue(true);layout();return;}throw;}}}
  bool key(WPARAM value,bool ctrl,bool)override{
    if(value==VK_ESCAPE){if(categoryDraft_){categoryDraft_=false;fields();status(L"Category draft discarded");}else hide();return true;}
    if(ctrl&&value=='F'){SetFocus(controls_.at(search));SendMessageW(controls_.at(search),EM_SETSEL,0,-1);return true;}
    if(ctrl&&value=='R'){action(reload,BN_CLICKED);return true;}
    if(value==VK_F6){SetFocus(controls_.at(GetFocus()==controls_.at(plugins)?search:plugins));return true;}
    if(value==VK_RETURN){const auto focus=GetFocus();if(focus==controls_.at(customCategory)){action(saveCategory,BN_CLICKED);return true;}if(focus==controls_.at(plugins)){addPlugin();return true;}wchar_t type[32]{};GetClassNameW(focus,type,32);if(_wcsicmp(type,L"Button")==0){action(GetDlgCtrlID(focus),BN_CLICKED);return true;}}return false;
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();place(heading,16,14,w-254,26);place(reload,w-232,14,96,26);place(rescan,w-128,14,112,26);
    place(searchLabel,16,54,160,20);place(search,16,77,w-32,26);
    const auto third=(w-48)/3;place(kindLabel,16,115,third,20);place(formatLabel,24+third,115,third,20);place(categoryLabel,32+2*third,115,third,20);
    place(kind,16,138,third,200);place(format,24+third,138,third,220);place(category,32+2*third,138,third,280);
    place(favorites,16,176,150,26);place(hidden,174,176,150,26);
    place(plugins,16,214,w-32,std::max(70.0f,h-434));place(detailLabel,16,h-208,w-32,22);
    place(favorite,16,h-177,110,26);place(hidePlugin,134,h-177,100,26);place(insert,w-222,h-177,126,26);place(close,w-88,h-177,72,26);
    place(customLabel,16,h-138,w-32,20);place(customCategory,16,h-112,w-178,26);place(saveCategory,w-154,h-112,138,26);place(statusLabel,16,h-70,w-32,56);
    const bool has=selected()!=nullptr,ready=!pending_;for(int id:{search,kind,format,category,favorites,hidden,plugins,rescan})EnableWindow(controls_.at(id),ready&&!categoryDraft_);
    EnableWindow(controls_.at(reload),ready);EnableWindow(controls_.at(plugins),ready&&!refreshQueued_&&!categoryDraft_);EnableWindow(controls_.at(insert),ready&&!refreshQueued_&&has);
    for(int id:{favorite,hidePlugin,customCategory,saveCategory})EnableWindow(controls_.at(id),ready&&!refreshQueued_&&has&&!revision_.empty()&&(!categoryDraft_||id==customCategory||id==saveCategory));
    set(favorites,favoritesOnly_?L"★ Favorites only":L"Favorites filter: off");set(hidden,includeHidden_?L"Hidden included":L"Hidden excluded");
  }
  void paint(RenderSurface &surface)override{const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);}
  void fontsChanged()override{if(controls_.contains(plugins))SendMessageW(controls_.at(plugins),LB_SETITEMHEIGHT,0,LPARAM(29*GetDpiForWindow(window_)/96));}
  void drawControl(const DRAWITEMSTRUCT &d)override{
    if(d.CtlID!=plugins){NativeToolWindow::drawControl(d);return;}
    const bool chosen=(d.itemState&ODS_SELECTED)!=0;RECT row=d.rcItem;SetDCBrushColor(d.hDC,chosen?RGB(36,77,89):RGB(24,34,45));FillRect(d.hDC,&row,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if(d.itemID>=entries_.size())return;const auto &p=entries_[d.itemID];SetBkMode(d.hDC,TRANSPARENT);SelectObject(d.hDC,font_);
    const auto width=row.right-row.left;const auto inset=LONG(8*GetDpiForWindow(window_)/96);
    const auto cell=[&](const std::wstring &text,float left,float right,COLORREF color){auto r=row;r.left+=LONG(width*left)+inset;r.right=row.left+LONG(width*right)-inset;SetTextColor(d.hDC,color);DrawTextW(d.hDC,text.c_str(),int(text.size()),&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);};
    cell((p.at("favorite").get<bool>()?L"★  ":L"")+wide(p.at("name").get<std::string>())+(p.at("hidden").get<bool>()?L" [hidden]":L""),0,.5f,RGB(218,232,241));
    cell(wide(p.at("format").get<std::string>())+(p.at("isInstrument").get<bool>()?L" / Instrument":L" / Effect"),.5f,.77f,RGB(153,181,196));
    cell(wide(p.at("category").get<std::string>()),.77f,1,RGB(153,181,196));if(d.itemState&ODS_FOCUS){InflateRect(&row,-2,-2);DrawFocusRect(d.hDC,&row);}
  }
public:
  PluginLibraryWindow(HWND owner,Request request,Context context):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)){
    minimumWidth_=640;minimumHeight_=540;create(L"ScreamSeq.PluginLibrary",L"Plugin library",820,680);
    edit(search,L"",200);combo(kind);combo(format);combo(category);edit(customCategory,L"",80);
    add(plugins,L"LISTBOX",L"Available plugins",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_HASSTRINGS|LBS_OWNERDRAWFIXED|WS_VSCROLL);
    for(auto [id,label]:std::initializer_list<std::pair<int,const wchar_t *>>{{favorites,L"Favorites filter: off"},{hidden,L"Hidden excluded"},{favorite,L"Favorite"},{hidePlugin,L"Hide"},{saveCategory,L"Apply category"},{insert,L"Add to rack"},{reload,L"Reload"},{rescan,L"Rescan"},{close,L"Close"}})button(id,label);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"Plugin library"},{searchLabel,L"Search plugins"},{kindLabel,L"Kind"},{formatLabel,L"Format"},{categoryLabel,L"Category"},{detailLabel,L""},{customLabel,L"Custom category / empty uses the default"},{statusLabel,L""}})label(id,text);
    strings(kind,{L"All kinds",L"Effects",L"Instruments"});strings(format,{L"All formats",L"Built-in",L"VST3",L"AU"});strings(category,{L"All categories"});finish();queue(true);
  }
  void show(){const bool existing=visible();NativeToolWindow::show();if(!existing){SetFocus(controls_.at(search));if(!categoryDraft_)queue(true);}}
  Json snapshot()const{return {{"visible",visible()},{"pending",pending_},{"refreshQueued",refreshQueued_},{"selected",selected_},{"plugins",entries_},{"categories",categories_},{"libraryRevision",revision_},{"categoryDraft",categoryDraft_},{"search",utf8(field(search))},{"category",filterCategory_},{"favoritesOnly",favoritesOnly_},{"includeHidden",includeHidden_},{"status",utf8(status_)}};}
};
}
