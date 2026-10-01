#pragma once
#include "NativeToolWindow.hpp"
#include <richedit.h>
#include <cwctype>
#include <optional>

namespace ScreamSeq {
// One captured point, independent of its parent editor's current selection.
// Preview requests own complete immutable parameters; paint only uses samples.
class FormulaWorkbenchWindow final : public NativeToolWindow {
  using Json=Api::Json;
  using Request=std::function<Json(const std::string &,const Json &)>;
  enum : int {code=2001,search,symbols,complete,insert,checkPreview,use,close,discard,suggestions,notes,statusLabel,heading,previewLabel,referenceHeading};
  Request request_;std::function<bool(const std::string &)> use_;std::function<bool()> sourceCurrent_;
  Json params_,reference_=Json::array(),values_=Json::array();
  std::vector<size_t> filtered_,matches_;
  size_t point_=0;uint64_t generation_=0,completionGeneration_=0;
  std::optional<uint64_t> validGeneration_;
  std::wstring initial_,completionText_;
  LONG completionStart_=0,completionEnd_=0,completionSelectionStart_=0;
  bool referenceOnly_=false,setting_=false,pending_=false,previewNeeded_=false,accepted_=false,completing_=false;
  AutomationCanvas canvas_;
  HFONT codeFont_{};UINT codeDpi_=0;

  std::wstring source()const{
    // Rich Edit positions count one CR per paragraph. GT_DEFAULT keeps those
    // positions aligned with EM_EXGETSEL, including after multiline insertion.
    GETTEXTLENGTHEX length{GTL_PRECISE|GTL_NUMCHARS,1200};
    const auto count=SendMessageW(controls_.at(code),EM_GETTEXTLENGTHEX,reinterpret_cast<WPARAM>(&length),0);
    std::wstring text(size_t(std::max<LRESULT>(0,count))+1,0);
    GETTEXTEX get{DWORD(text.size()*sizeof(wchar_t)),GT_DEFAULT,1200,nullptr,nullptr};
    const auto written=SendMessageW(controls_.at(code),EM_GETTEXTEX,reinterpret_cast<WPARAM>(&get),reinterpret_cast<LPARAM>(text.data()));
    text.resize(size_t(std::max<LRESULT>(0,written)));return text;
  }
  CHARRANGE selection()const{CHARRANGE range{};SendMessageW(controls_.at(code),EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&range));return range;}
  void setSelection(LONG first,LONG last){CHARRANGE range{first,last};SendMessageW(controls_.at(code),EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));}
  void status(std::wstring text){status_=std::move(text);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  void dismissCompletion(){completing_=false;matches_.clear();ShowWindow(controls_.at(suggestions),SW_HIDE);requestPaint();}
  void changed(){
    if(setting_||referenceOnly_)return;++generation_;accepted_=false;validGeneration_.reset();values_=Json::array();canvas_.rebuild(Json::array(),values_);
    previewNeeded_=true;SetTimer(window_,3,120,nullptr);status(L"Checking formula… / Ctrl+Space completes; Ctrl+Enter uses this point's draft");
    dismissCompletion();SetTimer(window_,4,120,nullptr);layout();
  }
  static bool identifier(wchar_t c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';}
  void suggest(bool explicitRequest){
    if(referenceOnly_)return;const auto text=source();const auto range=selection();
    if(range.cpMin<0||range.cpMax<range.cpMin||size_t(range.cpMax)>text.size()){dismissCompletion();return;}
    LONG start=range.cpMin;if(range.cpMin==range.cpMax)while(start>0&&identifier(text[size_t(start-1)]))--start;
    const auto prefix=text.substr(size_t(start),size_t(range.cpMax-start));
    if(!explicitRequest&&(GetFocus()!=controls_.at(code)||prefix.size()<2)){dismissCompletion();return;}
    matches_.clear();SendMessageW(controls_.at(suggestions),LB_RESETCONTENT,0,0);
    for(size_t i=0;i<reference_.size();++i){auto name=wide(reference_[i].at("name").get<std::string>());if(name.starts_with(prefix)){matches_.push_back(i);auto text=wide(reference_[i].at("insert").get<std::string>());SendMessageW(controls_.at(suggestions),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}}
    if(matches_.empty()){dismissCompletion();if(explicitRequest)status(L"No matching formula value or function");return;}
    completionText_=text;completionStart_=start;completionEnd_=range.cpMax;completionSelectionStart_=range.cpMin;completionGeneration_=generation_;completing_=true;
    SendMessageW(controls_.at(suggestions),LB_SETCURSEL,0,0);SetFocus(controls_.at(code));layout();
  }
  void insertText(const std::wstring &text){
    const auto range=selection();const auto current=source();
    if(range.cpMin<0||range.cpMax<range.cpMin||size_t(range.cpMax)>current.size()||current.size()-size_t(range.cpMax-range.cpMin)+text.size()>2048)
      throw std::runtime_error("The complete insertion would exceed the 2,048-character formula limit");
    SetFocus(controls_.at(code));SendMessageW(controls_.at(code),EM_STOPGROUPTYPING,0,0);
    SendMessageW(controls_.at(code),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(controls_.at(code),EM_STOPGROUPTYPING,0,0);
  }
  void acceptCompletion(){
    const auto selected=SendMessageW(controls_.at(suggestions),LB_GETCURSEL,0,0);const auto range=selection();
    if(!completing_||selected<0||size_t(selected)>=matches_.size())return;
    if(completionGeneration_!=generation_||source()!=completionText_||range.cpMax!=completionEnd_||range.cpMin!=completionSelectionStart_){dismissCompletion();status(L"Text or caret changed / request completion again");return;}
    auto text=wide(reference_[matches_[size_t(selected)]].at("insert").get<std::string>());
    // Preflight before changing selection, so a rejected snippet leaves the
    // exact text and selection intact instead of inserting a truncated formula.
    if(source().size()-size_t(completionEnd_-completionStart_)+text.size()>2048)throw std::runtime_error("The complete insertion would exceed the 2,048-character formula limit");
    setSelection(completionStart_,completionEnd_);dismissCompletion();insertText(text);
  }
  void filter(){
    auto query=field(search);std::transform(query.begin(),query.end(),query.begin(),towlower);
    filtered_.clear();SendMessageW(controls_.at(symbols),LB_RESETCONTENT,0,0);
    for(size_t i=0;i<reference_.size();++i){auto full=wide(reference_[i].at("name").get<std::string>()+" "+reference_[i].at("description").get<std::string>()+" "+reference_[i].at("category").get<std::string>());std::transform(full.begin(),full.end(),full.begin(),towlower);if(full.find(query)==std::wstring::npos)continue;filtered_.push_back(i);auto text=wide(reference_[i].at("insert").get<std::string>()+" — "+reference_[i].at("description").get<std::string>());SendMessageW(controls_.at(symbols),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
    if(!filtered_.empty())SendMessageW(controls_.at(symbols),LB_SETCURSEL,0,0);layout();
  }
  void insertReference(){if(referenceOnly_)return;const auto index=SendMessageW(controls_.at(symbols),LB_GETCURSEL,0,0);if(index<0||size_t(index)>=filtered_.size())return;dismissCompletion();insertText(wide(reference_[filtered_[size_t(index)]].at("insert").get<std::string>()));}
  void previewNow(){
    if(referenceOnly_||pending_||!previewNeeded_)return;
    const auto token=generation_;const auto text=source();auto params=params_;
    params["points"][point_]["formula"]=utf8(text);params["points"][point_]["curve"]="scripted";
    pending_=true;previewNeeded_=false;layout();
    try{
      auto result=request_("automation.formula.preview",params);pending_=false;
      if(token==generation_){validGeneration_=token;values_=result.at("values");canvas_.rebuild(Json::array(),values_);status(L"Valid / Use formula returns this draft to its captured point; the parent editor applies it");}
    }catch(const Api::ApiError &e){pending_=false;if(token==generation_){validGeneration_.reset();values_=Json::array();canvas_.rebuild(Json::array(),values_);if(e.code==-32002){previewNeeded_=true;SetTimer(window_,3,120,nullptr);status(L"Waiting for the document worker / formula draft retained");}else error(e);}}
    catch(const std::exception &e){pending_=false;if(token==generation_){validGeneration_.reset();values_=Json::array();canvas_.rebuild(Json::array(),values_);error(e);}}
    layout();requestPaint();
  }
  void apply(){
    if(referenceOnly_||pending_||!validGeneration_||*validGeneration_!=generation_){status(L"Wait for a valid preview of the current text before using it");return;}
    const auto text=source();if(!sourceCurrent_()||!use_(utf8(text))){status(L"The envelope or selected point changed / formula retained; reopen the original point or copy this text");return;}
    initial_=text;accepted_=true;dismissCompletion();hide();
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;
    if(id==code&&notification==EN_CHANGE){changed();return;}
    if(id==search&&notification==EN_CHANGE){filter();return;}
    if(id==symbols){if(notification==LBN_DBLCLK)insertReference();return;}
    if(id==suggestions){if(notification==LBN_DBLCLK)acceptCompletion();return;}
    if(notification!=BN_CLICKED)return;
    if(id==complete){suggest(true);return;}if(id==insert){insertReference();return;}
    if(id==checkPreview){previewNeeded_=true;previewNow();return;}if(id==use){apply();return;}
    if(id==close){dismissCompletion();hide();return;}
    if(id==discard){setting_=true;set(code,initial_);setting_=false;++generation_;validGeneration_.reset();values_=Json::array();canvas_.rebuild(Json::array(),values_);previewNeeded_=true;dismissCompletion();hide();return;}
  }
  bool key(WPARAM value,bool ctrl,bool shift)override{
    const auto focus=GetFocus();const int id=focus==window_?0:GetDlgCtrlID(focus);
    if(ctrl&&value==VK_RETURN){apply();return true;}
    if(ctrl&&value==VK_SPACE&&!referenceOnly_){suggest(true);return true;}
    if(value==VK_F6){dismissCompletion();SetFocus(referenceOnly_?(id==search?controls_.at(symbols):controls_.at(search)):(id==code?controls_.at(search):controls_.at(code)));return true;}
    if(value==VK_ESCAPE){if(completing_){dismissCompletion();return true;}hide();return true;}
    if(id==code){
      if(completing_&&(value==VK_UP||value==VK_DOWN)){auto index=SendMessageW(controls_.at(suggestions),LB_GETCURSEL,0,0);index=std::clamp<LRESULT>(index+(value==VK_UP?-1:1),0,LRESULT(matches_.size()-1));SendMessageW(controls_.at(suggestions),LB_SETCURSEL,index,0);return true;}
      if(completing_&&(value==VK_RETURN||value==VK_TAB)){acceptCompletion();return true;}
      if(value==VK_LEFT||value==VK_RIGHT||value==VK_HOME||value==VK_END||value==VK_PRIOR||value==VK_NEXT)dismissCompletion();
      if(value==VK_TAB&&!ctrl&&!shift){insertText(L"    ");return true;}
      return false;
    }
    if(value==VK_RETURN&&(id==search||id==symbols)){insertReference();return !referenceOnly_;}
    if(value==VK_RETURN&&id==suggestions){acceptCompletion();return true;}
    if(id==search&&(value==VK_UP||value==VK_DOWN)){auto index=SendMessageW(controls_.at(symbols),LB_GETCURSEL,0,0);if(!filtered_.empty())SendMessageW(controls_.at(symbols),LB_SETCURSEL,std::clamp<LRESULT>(index+(value==VK_UP?-1:1),0,LRESULT(filtered_.size()-1)),0);return true;}
    if(value==VK_RETURN){wchar_t type[32]{};GetClassNameW(focus,type,32);if(_wcsicmp(type,L"Button")==0){action(id,BN_CLICKED);return true;}}return false;
  }
  void timer(UINT_PTR id)override{
    if(id==3){KillTimer(window_,3);if(!visible())return;if(pending_){SetTimer(window_,3,120,nullptr);return;}previewNow();}
    if(id==4){KillTimer(window_,4);if(visible())suggest(false);}
  }
  void fontsChanged()override{
    const auto dpi=GetDpiForWindow(window_);if(codeDpi_!=dpi){auto font=CreateFontW(-int(15*dpi/96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Consolas");SendMessageW(controls_.at(code),WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);if(codeFont_)DeleteObject(codeFont_);codeFont_=font;codeDpi_=dpi;}
    else SendMessageW(controls_.at(code),WM_SETFONT,reinterpret_cast<WPARAM>(codeFont_),FALSE);
    SendMessageW(controls_.at(symbols),LB_SETITEMHEIGHT,0,LPARAM(56*dpi/96));SendMessageW(controls_.at(suggestions),LB_SETITEMHEIGHT,0,LPARAM(22*dpi/96));
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();const float leftWidth=w-372,referenceX=referenceOnly_?16:w-340,referenceWidth=referenceOnly_?w-32:324;
    place(referenceHeading,referenceX,14,referenceWidth,24);place(search,referenceX,46,referenceWidth,26);place(symbols,referenceX,82,referenceWidth,std::max(60.0f,h-330));place(notes,referenceX,h-238,referenceWidth,184);
    place(insert,referenceX,h-40,referenceWidth,26,!referenceOnly_);place(close,referenceOnly_?w-132:leftWidth-81,h-(referenceOnly_?40:111),referenceOnly_?116:97,26);
    place(heading,16,14,leftWidth,24,!referenceOnly_);place(code,16,46,leftWidth,std::max(100.0f,h-366),!referenceOnly_);
    place(previewLabel,16,h-308,leftWidth,22,!referenceOnly_);canvas_.viewport={54,h-281,std::max(1.0f,leftWidth-42),140};canvas_.rebuild(Json::array(),values_);
    place(complete,16,h-111,156,26,!referenceOnly_);place(checkPreview,180,h-111,88,26,!referenceOnly_);place(discard,276,h-111,std::max(70.0f,leftWidth-461),26,!referenceOnly_);place(use,leftWidth-93,h-77,109,26,!referenceOnly_);place(statusLabel,16,h-77,std::max(1.0f,leftWidth-122),63,!referenceOnly_);
    EnableWindow(controls_.at(use),!referenceOnly_&&!pending_&&validGeneration_&&*validGeneration_==generation_);EnableWindow(controls_.at(checkPreview),!pending_);EnableWindow(controls_.at(insert),!filtered_.empty());
    if(completing_&&!referenceOnly_){POINT caret{};GetCaretPos(&caret);MapWindowPoints(controls_.at(code),window_,&caret,1);const auto scale=96.0f/GetDpiForWindow(window_);const auto height=std::min(6.0f,float(matches_.size()))*22+4;place(suggestions,std::clamp(caret.x*scale,16.0f,std::max(16.0f,leftWidth-270)),std::clamp(caret.y*scale+22,46.0f,std::max(46.0f,h-height-140)),std::min(300.0f,leftWidth),height);SetWindowPos(controls_.at(suggestions),HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
    else ShowWindow(controls_.at(suggestions),SW_HIDE);
  }
  void drawControl(const DRAWITEMSTRUCT &d)override{
    if(d.CtlID!=symbols||d.CtlType!=ODT_LISTBOX){NativeToolWindow::drawControl(d);return;}
    RECT rect=d.rcItem;SetDCBrushColor(d.hDC,(d.itemState&ODS_SELECTED)?RGB(35,65,71):RGB(24,34,45));FillRect(d.hDC,&rect,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if(d.itemID>=filtered_.size())return;const auto &symbol=reference_[filtered_[d.itemID]];const int scale=GetDpiForWindow(window_);rect.left+=8*scale/96;rect.right-=8*scale/96;rect.top+=4*scale/96;auto title=wide(symbol.at("insert").get<std::string>()),detail=wide(symbol.at("description").get<std::string>());SetBkMode(d.hDC,TRANSPARENT);SetTextColor(d.hDC,RGB(218,232,241));SelectObject(d.hDC,font_);auto top=rect;top.bottom=top.top+19*scale/96;DrawTextW(d.hDC,title.c_str(),int(title.size()),&top,DT_SINGLELINE|DT_END_ELLIPSIS);rect.top+=19*scale/96;SetTextColor(d.hDC,RGB(156,177,192));DrawTextW(d.hDC,detail.c_str(),int(detail.size()),&rect,DT_WORDBREAK|DT_END_ELLIPSIS);if(d.itemState&ODS_FOCUS){rect=d.rcItem;InflateRect(&rect,-2,-2);DrawFocusRect(d.hDC,&rect);}
  }
  void paint(RenderSurface &surface)override{
    const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);if(referenceOnly_)return;const auto &r=canvas_.viewport;surface.fill(r.x,r.y,r.w,r.h,0x10171f);surface.clip(r.x,r.y,r.w,r.h);for(int i=0;i<=4;++i)surface.line(r.x,r.y+r.h*i/4,r.x+r.w,r.y+r.h*i/4,0x2a3948);for(size_t i=1;i<canvas_.curve.size();++i)surface.line(canvas_.curve[i-1].x,canvas_.curve[i-1].y,canvas_.curve[i].x,canvas_.curve[i].y,0x68d3bc,2);surface.unclip();surface.uiText(L"100%",r.x-38,r.y,36,0x94a4b4);surface.uiText(L"0%",r.x-38,r.y+r.h-16,36,0x94a4b4);
  }
public:
  FormulaWorkbenchWindow(HWND owner,const std::wstring &title,const std::string &sourceText,Json params,int selected,Request request,std::function<bool()> sourceCurrent={},std::function<bool(const std::string &)> applySource={})
    :NativeToolWindow(owner),request_(std::move(request)),use_(std::move(applySource)),sourceCurrent_(std::move(sourceCurrent)),params_(std::move(params)),point_(size_t(std::max(0,selected))),referenceOnly_(selected<0){
    static HMODULE richEdit=LoadLibraryExW(L"Msftedit.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!richEdit)throw std::runtime_error("Cannot load the native formula text editor");minimumWidth_=referenceOnly_?440:840;minimumHeight_=referenceOnly_?450:600;
    create(referenceOnly_?L"ScreamSeq.FormulaReference":L"ScreamSeq.FormulaWorkbench",title.c_str(),referenceOnly_?640:960,680);
    add(code,MSFTEDIT_CLASS,L"",ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|ES_NOHIDESEL|WS_VSCROLL|WS_BORDER);
    SendMessageW(controls_.at(code),EM_SETTEXTMODE,TM_PLAINTEXT|TM_MULTILEVELUNDO,0);SendMessageW(controls_.at(code),EM_EXLIMITTEXT,0,2048);SendMessageW(controls_.at(code),EM_SETUNDOLIMIT,128,0);SendMessageW(controls_.at(code),EM_SETEVENTMASK,0,ENM_CHANGE);SendMessageW(controls_.at(code),EM_SETBKGNDCOLOR,0,RGB(16,23,31));
    CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR;format.crTextColor=RGB(218,232,241);SendMessageW(controls_.at(code),EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&format));
    edit(search,L"",256);SendMessageW(controls_.at(search),EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Find a value or function"));
    add(symbols,L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_VSCROLL);
    add(suggestions,L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_BORDER);
    add(notes,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{complete,L"Complete · Ctrl+Space"},{insert,L"Insert selected value / function"},{checkPreview,L"Check"},{use,L"Use formula"},{close,L"Close"},{discard,L"Discard"}})button(id,text);
    label(heading,L"Formula · Ctrl+Enter to use · F6 for reference");label(previewLabel,L"Preview / normalized value · captured envelope");label(referenceHeading,L"Values and functions");label(statusLabel,L"");
    setting_=true;set(code,wide(sourceText));setting_=false;initial_=source();SendMessageW(controls_.at(code),EM_EMPTYUNDOBUFFER,0,0);
    const auto reference=request_("automation.formula.reference",Json::object());reference_=reference.at("symbols");auto noteText=wide(reference.at("notes").get<std::string>());std::wstring lines;for(auto c:noteText){if(c=='\n')lines+='\r';lines+=c;}set(notes,lines);
    if(!referenceOnly_){if(!params_.contains("points")||point_>=params_.at("points").size())throw std::runtime_error("Formula point no longer exists");params_["samples"]=1024;canvas_.start=0;canvas_.end=params_.value("span",params_.at("rows").get<double>()*256);previewNeeded_=true;}
    finish();filter();status(L"Checking formula…");
  }
  ~FormulaWorkbenchWindow()override{if(codeFont_)DeleteObject(codeFont_);}
  void show(){const bool wasVisible=visible();NativeToolWindow::show();if(!wasVisible)SetFocus(controls_.at(referenceOnly_?search:code));if(previewNeeded_)SetTimer(window_,3,120,nullptr);}
  bool retainedDraft()const{return !referenceOnly_&&(pending_||(!accepted_&&source()!=initial_));}
  Json snapshot()const{Json names=Json::array(),matches=Json::array();for(auto i:filtered_)names.push_back(reference_[i].at("name"));for(auto i:matches_)matches.push_back(reference_[i].at("insert"));return {{"visible",visible()},{"referenceOnly",referenceOnly_},{"source",utf8(source())},{"dirty",retainedDraft()},{"pending",pending_},{"checking",previewNeeded_||pending_},{"valid",validGeneration_&&*validGeneration_==generation_},{"sourceCurrent",referenceOnly_||sourceCurrent_()},{"previewSamples",values_.size()},{"values",values_},{"point",point_},{"symbols",names},{"completionVisible",completing_},{"completions",matches},{"status",utf8(status_)}};}
};
}
