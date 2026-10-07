#pragma once
#include "NativeToolWindow.hpp"
#include "NativeReportList.hpp"
#include <array>
#include <set>

namespace ScreamSeq {
// Retained UI only: all musical changes use the application's guarded worker.
class ArrangementWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Callbacks {
    std::function<Json(std::string,Json)> operate;
    std::function<void(std::string)> selectOrder;
    std::function<void(std::string)> playOrder;
    std::function<void()> returnToPattern;
  };
private:
  enum:int {orders=8001,sequence,assignment,assign,before,after,up,down,remove,play,
    rows,source,createNew,duplicate,reload,returnPattern,close,previousSection,nextSection,
    ordersPage,sectionPage,patternPage,sectionName,sectionApply,sectionReload,
    patternName,patternNotes,patternApply,patternReload,
    heading=8100,sequenceLabel,countLabel,assignmentLabel,creationLabel,rowsLabel,
    sourceLabel,helpLabel,statusLabel,sectionTarget,sectionNameLabel,sectionHelp,
    patternTarget,patternNameLabel,patternNotesLabel};
  struct Pattern {std::string id,name,annotation;unsigned index{},rows{};std::wstring label;bool operator==(const Pattern &)const=default;};
  struct Order {std::string id,name;unsigned pattern{};bool playable=false;std::array<std::wstring,4> cells;bool operator==(const Order &)const=default;};
  struct DetailDraft {std::string document,revision,sequence,target,error;std::wstring label;uint64_t generation=0;bool bound=false,dirty=false;};
  Callbacks callbacks_;
  Json state_=Json::object();
  std::vector<Pattern> patterns_,draftPatterns_;
  std::vector<Order> orders_;
  std::vector<std::pair<unsigned,std::wstring>> sequences_;
  std::string selected_,assignmentID_,sourceID_,draftDocument_,draftRevision_,mode_="arrange",error_;
  std::string page_="orders";
  DetailDraft sectionDraft_,patternDraft_;
  std::array<HWND,3> pageFocus_{};
  struct FieldView {DWORD first{},last{};int line{};};
  std::array<FieldView,3> fieldViews()const{
    std::array<FieldView,3> views{};size_t index=0;
    for(int id:{sectionName,patternName,patternNotes}){auto &view=views[index++];const auto control=controls_.at(id);SendMessageW(control,EM_GETSEL,reinterpret_cast<WPARAM>(&view.first),reinterpret_cast<LPARAM>(&view.last));view.line=int(SendMessageW(control,EM_GETFIRSTVISIBLELINE,0,0));}return views;
  }
  void restoreFieldViews(const std::array<FieldView,3> &views){
    size_t index=0;for(int id:{sectionName,patternName,patternNotes}){const auto &view=views[index++];const auto control=controls_.at(id);DWORD first=0,last=0;SendMessageW(control,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));if(first!=view.first||last!=view.last)SendMessageW(control,EM_SETSEL,view.first,view.last);if(id==patternNotes){const auto line=int(SendMessageW(control,EM_GETFIRSTVISIBLELINE,0,0));if(line!=view.line)SendMessageW(control,EM_LINESCROLL,0,view.line-line);}}
  }
  uint64_t documentGeneration_=0,draftGeneration_=0,selectionGeneration_=0,requestGeneration_=0;
  bool loaded_=false,opened_=false,setting_=false,pending_=false,dirty_=false,resizingColumns_=false;
  HWND pendingFocus_{};
  HIMAGELIST rowHeight_{};
  std::array<float,4> columnWidths_{68,290,64,200};
  float listWidth_=-1;UINT columnDpi_=0;
  static constexpr UINT_PTR notificationsID=0x41525731,headerID=0x41525732;
  static unsigned unsignedValue(const Json &value,unsigned maximum=65535){
    if(!value.is_number_unsigned()&&!value.is_number_integer())throw std::runtime_error("Invalid arrangement number");
    const auto number=value.get<int64_t>();if(number<0||number>maximum)throw std::runtime_error("Arrangement number is outside its range");return unsigned(number);
  }
  static std::string identity(const Json &value){
    if(!value.is_string())throw std::runtime_error("Invalid arrangement identity");
    const auto text=value.get<std::string>();if(text.empty()||text.size()>200||text.find('\0')!=std::string::npos)throw std::runtime_error("Invalid arrangement identity");return text;
  }
  const Json &document()const{return state_.at("document");}
  bool unavailable()const{return !loaded_||pending_||state_.value("busy",false);}
  bool editable()const{return loaded_&&document().value("editable",false);}
  bool stale()const{return loaded_&&(draftDocument_!=state_.at("documentId").get<std::string>()||draftRevision_!=state_.at("revision").get<std::string>());}
  int orderIndex(const std::string &id)const{for(size_t i=0;i<orders_.size();++i)if(orders_[i].id==id)return int(i);return -1;}
  static const Pattern *pattern(const std::vector<Pattern> &values,const std::string &id){for(const auto &value:values)if(value.id==id)return &value;return nullptr;}
  const Pattern *patternAt(unsigned index)const{for(const auto &value:patterns_)if(value.index==index)return &value;return nullptr;}
  bool roomForOrder()const{return loaded_&&orders_.size()<unsignedValue(document().at("formatLimits").at("ordersMax"));}
  bool roomForPattern()const{
    if(!loaded_||!roomForOrder())return false;
    const auto maximum=unsignedValue(document().at("formatLimits").at("patternsMax"));
    size_t used=0;for(const auto &value:patterns_)if(value.index<maximum)++used;return used<maximum;
  }
  void fillPatterns(int control,const std::vector<Pattern> &values,const std::string &chosen){
    const auto handle=controls_.at(control);SendMessageW(handle,CB_RESETCONTENT,0,0);int selection=-1;
    for(size_t i=0;i<values.size();++i){SendMessageW(handle,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(values[i].label.c_str()));if(values[i].id==chosen)selection=int(i);}
    SendMessageW(handle,CB_SETCURSEL,selection,0);
  }
  void captureDraft(std::string requested={}){
    if(!loaded_)return;
    if(requested.empty()&&pattern(patterns_,sourceID_))requested=sourceID_;
    if(requested.empty()){const auto index=orderIndex(selected_);if(index>=0){if(const auto value=patternAt(orders_[size_t(index)].pattern))requested=value->id;}}
    if(!pattern(patterns_,requested))requested=patterns_.empty()?std::string():patterns_.front().id;
    draftPatterns_=patterns_;sourceID_=requested;draftDocument_=state_.at("documentId");draftRevision_=state_.at("revision");
    const auto &limits=document().at("formatLimits");const auto minimum=unsignedValue(limits.at("patternRowsMin")),maximum=unsignedValue(limits.at("patternRowsMax"));
    const auto value=pattern(draftPatterns_,sourceID_);const auto count=value?value->rows:64u;
    setting_=true;set(rows,std::to_wstring(std::clamp(count,minimum,maximum)));fillPatterns(source,draftPatterns_,sourceID_);setting_=false;
    dirty_=false;++draftGeneration_;error_.clear();
  }
  int pageIndex()const{return page_=="section"?1:page_=="pattern"?2:0;}
  std::string selectedPatternID()const{const auto i=orderIndex(selected_);if(i>=0)if(const auto p=patternAt(orders_[size_t(i)].pattern))return p->id;return {};}
  std::string sequenceID()const{if(loaded_)for(const auto &value:document().at("sequences"))if(value.at("index")==document().at("sequence"))return value.at("id").get<std::string>();return {};}
  bool detailStale(const DetailDraft &draft)const{
    return draft.bound&&(!loaded_||draft.document!=state_.at("documentId").get<std::string>()||draft.revision!=state_.at("revision").get<std::string>());
  }
  bool detailDiffers(bool section)const{const auto &draft=section?sectionDraft_:patternDraft_;return draft.bound&&draft.target!=(section?selected_:selectedPatternID());}
  int sectionIndex(bool next)const{
    const auto selected=orderIndex(selected_);if(selected<0)return -1;
    for(int i=selected+(next?1:-1);i>=0&&size_t(i)<orders_.size();i+=next?1:-1)if(!orders_[size_t(i)].name.empty())return i;
    return -1;
  }
  void captureDetail(bool section){
    if(!loaded_||unavailable())return;
    auto &draft=section?sectionDraft_:patternDraft_;draft.document=state_.at("documentId");draft.revision=state_.at("revision");draft.sequence=sequenceID();
    draft.target=section?selected_:selectedPatternID();draft.bound=!draft.target.empty();draft.dirty=false;draft.error.clear();++draft.generation;
    std::string name,notes;draft.label.clear();const auto current=orderIndex(selected_);std::wstring sequenceLabel=L"Sequence "+std::to_wstring(unsignedValue(document().at("sequence"))+1);
    for(const auto &value:document().at("sequences"))if(value.at("id")==draft.sequence){const auto sequenceName=value.value("name",std::string());if(!sequenceName.empty())sequenceLabel+=L" · "+wide(sequenceName);break;}
    if(section){const auto index=orderIndex(draft.target);if(index>=0){const auto &order=orders_[size_t(index)];name=order.name;draft.label=L"Order "+std::to_wstring(index)+L" · "+(order.playable?L"Pattern ":L"")+order.cells[1]+L" · "+sequenceLabel;}}
    else if(const auto value=pattern(patterns_,draft.target)){name=value->name;notes=value->annotation;draft.label=L"Pattern "+value->label+L" · captured from Order "+std::to_wstring(current)+L" · "+sequenceLabel;}
    setting_=true;set(section?sectionName:patternName,wide(name));if(!section)set(patternNotes,wide(notes));setting_=false;error_.clear();
  }
  void changePage(std::string page){
    if(page_==page)return;
    const auto views=fieldViews();
    const auto focus=GetFocus();if(owns(focus)&&focus!=controls_.at(ordersPage)&&focus!=controls_.at(sectionPage)&&focus!=controls_.at(patternPage))pageFocus_[size_t(pageIndex())]=focus;
    page_=std::move(page);
    if(page_=="section"&&!sectionDraft_.bound&&sectionDraft_.generation==0)captureDetail(true);
    if(page_=="pattern"&&!patternDraft_.bound&&patternDraft_.generation==0)captureDetail(false);
    error_.clear();statusText();layout();
    const auto retained=pageFocus_[size_t(pageIndex())];if(visible()&&retained&&IsWindowVisible(retained)&&IsWindowEnabled(retained))SetFocus(retained);
    else if(visible()&&owns(focus)&&!IsWindowVisible(focus))SetFocus(controls_.at(page_=="section"?sectionPage:page_=="pattern"?patternPage:ordersPage));
    restoreFieldViews(views);
  }
  void applyDetail(bool section){
    if(unavailable()||!editable())return;
    auto &draft=section?sectionDraft_:patternDraft_;
    if(!draft.bound)throw std::runtime_error(section?"Select an order and Reload section first":"Select a playable order and Reload pattern first");
    if(detailStale(draft))throw std::runtime_error("This detail draft is stale. Reload its current selection before applying.");
    const auto name=field(section?sectionName:patternName),notes=section?std::wstring():field(patternNotes);
    if(name.size()>256||notes.size()>4096)throw std::runtime_error("Names allow 256 UTF-16 units; pattern notes allow 4096.");
    const auto captured=draft;const auto documentGeneration=documentGeneration_,request=++requestGeneration_;
    Json params={{"id",draft.target},{"name",utf8(name)},{"expectedRevision",draft.revision}};if(!section)params["annotation"]=utf8(notes);
    pending_=true;draft.error.clear();error_.clear();statusText();layout();
    try{
      if(!callbacks_.operate)throw std::runtime_error("Annotation editing is unavailable");
      const auto response=callbacks_.operate("song.annotate",std::move(params));if(request!=requestGeneration_)return;pending_=false;
      if(documentGeneration!=documentGeneration_||state_.at("documentId")!=captured.document){statusText();layout();return;}
      auto returned=response.at("state");
      if(returned.at("documentId")!=captured.document||(state_.at("revision")!=captured.revision&&state_.at("revision")!=returned.at("revision")))throw std::runtime_error("The song changed while details completed. The current song and raw draft were retained.");
      if(response.at("result").at("id")!=captured.target)throw std::runtime_error("The annotation response belongs to another target.");
      // Selection is independent of metadata writes, including navigation pumped
      // by the host while the worker request completes.
      returned["selectedOrderID"]=selected_;update(returned);
      if(draft.generation==captured.generation&&draft.document==captured.document&&draft.target==captured.target){draft.revision=state_.at("revision");draft.dirty=false;draft.error.clear();}
      statusText();layout();
    }catch(const std::exception &value){if(request==requestGeneration_){pending_=false;if(documentGeneration==documentGeneration_){draft.error=value.what();error_=value.what();}statusText();layout();}}
  }
  void statusText(){
    if(!error_.empty())status_=wide(error_);
    else if(pending_)status_=L"Applying the captured arrangement request…";
    else if(!loaded_)status_=L"Open a song to arrange its orders.";
    else if(page_!="orders"){
      const bool section=page_=="section";const auto &draft=section?sectionDraft_:patternDraft_;
      if(!draft.error.empty())status_=wide(draft.error);
      else if(!draft.bound)status_=section?L"Select an order, then Reload section to capture it.":L"Stop and Skip have no pattern details. Select a playable order, then Reload pattern.";
      else if(detailStale(draft))status_=L"These details belong to an earlier song revision. Raw text is retained; Reload captures the current selection.";
      else if(detailDiffers(section))status_=L"Editing the captured target shown above. Reload explicitly switches these details to the selected occurrence.";
      else status_=section?L"A nonempty section name begins a section at this occurrence. Empty clears its marker.":L"Pattern name and notes are shared by every occurrence of this pattern.";
    }
    else if(stale())status_=L"The pattern creation draft belongs to an earlier song revision. Reload to use the current song.";
    else if(!editable())status_=L"This imported song is read-only. Existing orders remain available for inspection.";
    else status_=L"Select an occurrence to inspect its pattern. Play selected starts that occurrence; the edit cursor remains independent.";
    set(statusLabel,status_);requestPaint();
  }
  void error(const std::exception &value)override{error_=value.what();if(page_=="section")sectionDraft_.error=error_;else if(page_=="pattern")patternDraft_.error=error_;statusText();layout();}
  void select(const std::string &id,bool navigate){
    if(unavailable()||orderIndex(id)<0)return;
    if(selected_!=id){selected_=id;++selectionGeneration_;}
    error_.clear();statusText();layout();
    if(navigate&&callbacks_.selectOrder)callbacks_.selectOrder(id);
  }
  void execute(const std::string &method,Json params,bool creation=false){
    if(unavailable()||!editable())return;
    if(creation&&stale())throw std::runtime_error("The pattern draft is stale. Reload before creating a pattern.");
    const auto capturedDocument=state_.at("documentId").get<std::string>(),capturedRevision=state_.at("revision").get<std::string>();
    const auto documentGeneration=documentGeneration_,draftGeneration=draftGeneration_,selectionGeneration=selectionGeneration_,request=++requestGeneration_;
    params["expectedRevision"]=creation?draftRevision_:capturedRevision;
    pending_=true;error_.clear();statusText();layout();
    try {
      if(!callbacks_.operate)throw std::runtime_error("Arrangement editing is unavailable");
      const auto response=callbacks_.operate(method,std::move(params));
      if(request!=requestGeneration_)return;
      pending_=false;
      if(documentGeneration!=documentGeneration_||state_.at("documentId")!=capturedDocument){statusText();layout();return;}
      auto returned=response.at("state");
      if(returned.at("documentId")!=capturedDocument||
          (state_.at("revision")!=capturedRevision&&state_.at("revision")!=returned.at("revision"))){
        error_="The song changed while this request completed. The current selection and draft were retained.";statusText();layout();return;
      }
      const bool retainCommandSelection=selectionGeneration==selectionGeneration_;
      if(!retainCommandSelection)returned["selectedOrderID"]=selected_;
      update(returned);
      // Host publication during the pumped callback may already have advanced
      // selectionGeneration_. Reveal the accepted current selection, including
      // a newer retained selection, without changing ordinary update() scroll.
      if(visible()){const auto selected=orderIndex(selected_);if(selected>=0)ListView_EnsureVisible(controls_.at(orders),selected,FALSE);}
      if(creation&&draftGeneration==draftGeneration_){draftRevision_=state_.at("revision");dirty_=false;}
      error_.clear();statusText();layout();
    } catch(const std::exception &value){
      if(request==requestGeneration_){pending_=false;if(documentGeneration==documentGeneration_)error_=value.what();statusText();layout();}
    }
  }
  void editOrder(const char *operation){
    const auto index=orderIndex(selected_);if(index<0||unavailable()||!editable())return;
    Json params={{"order",index},{"operation",operation}};
    const std::string action=operation;
    if(action=="assign"||action=="before"||action=="after"){
      const auto chosen=pattern(patterns_,assignmentID_);if(!chosen)throw std::runtime_error("Choose an available pattern first");params["pattern"]=chosen->index;
      if(action!="assign"&&!roomForOrder())throw std::runtime_error("The format has no more order slots");
    }
    if((action=="up"&&index==0)||(action=="down"&&size_t(index+1)==orders_.size()))return;
    if(action=="remove"&&orders_.size()<=1)return;
    execute("order.edit",std::move(params));
  }
  void createPattern(bool copy){
    if(unavailable()||!editable())return;
    if(stale())throw std::runtime_error("The pattern draft is stale. Reload before creating a pattern.");
    if(!roomForPattern())throw std::runtime_error("The format has no more pattern or order slots");
    const auto raw=field(rows);if(raw.empty()||raw.size()>5||!std::all_of(raw.begin(),raw.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';}))throw std::runtime_error("Enter a whole number of pattern rows");
    const auto count=std::stoul(raw);const auto &limits=document().at("formatLimits");
    if(count<unsignedValue(limits.at("patternRowsMin"))||count>unsignedValue(limits.at("patternRowsMax")))throw std::runtime_error("The row count is outside this format's limits");
    Json params={{"rows",count}};
    if(copy){const auto from=pattern(patterns_,sourceID_);if(!from)throw std::runtime_error("The source pattern is unavailable. Reload the draft.");params["source"]=from->index;}
    execute("pattern.create",std::move(params),true);
  }
  void leave(){const bool focused=owns(GetFocus());hide();if(!focused&&callbacks_.returnToPattern)callbacks_.returnToPattern();}
  void action(int id,unsigned notification)override{
    if(setting_)return;
    if(id==sectionName||id==patternName||id==patternNotes){
      const bool section=id==sectionName;auto &draft=section?sectionDraft_:patternDraft_;
      if(notification==EN_SETFOCUS)pageFocus_[section?1:2]=controls_.at(id);
      if(notification==EN_CHANGE){draft.dirty=true;++draft.generation;draft.error.clear();error_.clear();statusText();}return;
    }
    if(id==rows&&notification==EN_CHANGE){dirty_=true;++draftGeneration_;error_.clear();statusText();return;}
    if(id==source&&notification==CBN_SELCHANGE){const auto index=SendMessageW(controls_.at(source),CB_GETCURSEL,0,0);if(index>=0&&size_t(index)<draftPatterns_.size()){sourceID_=draftPatterns_[size_t(index)].id;dirty_=true;++draftGeneration_;}error_.clear();statusText();return;}
    if(id==assignment&&notification==CBN_SELCHANGE){const auto index=SendMessageW(controls_.at(assignment),CB_GETCURSEL,0,0);assignmentID_=index>=0&&size_t(index)<patterns_.size()?patterns_[size_t(index)].id:std::string();layout();return;}
    if(id==sequence&&notification==CBN_SELCHANGE){
      const auto index=SendMessageW(controls_.at(sequence),CB_GETCURSEL,0,0);
      if(index>=0&&size_t(index)<sequences_.size())execute("sequence.select",{{"sequence",sequences_[size_t(index)].first}});
      setting_=true;for(size_t i=0;i<sequences_.size();++i)if(sequences_[i].first==unsignedValue(document().at("sequence")))SendMessageW(controls_.at(sequence),CB_SETCURSEL,i,0);setting_=false;return;
    }
    if(notification!=BN_CLICKED)return;
    if(id==close){leave();return;}if(id==returnPattern){if(callbacks_.returnToPattern)callbacks_.returnToPattern();return;}
    if(id==ordersPage||id==sectionPage||id==patternPage){changePage(id==ordersPage?"orders":id==sectionPage?"section":"pattern");return;}
    if(id==previousSection||id==nextSection){navigateSection(id==nextSection);return;}
    if(id==sectionReload||id==patternReload){if(page_==(id==sectionReload?"section":"pattern"))captureDetail(id==sectionReload);statusText();layout();return;}
    if(id==sectionApply||id==patternApply){if(page_==(id==sectionApply?"section":"pattern"))applyDetail(id==sectionApply);return;}
    if(page_!="orders")return;
    if(id==reload){if(!unavailable())captureDraft();statusText();layout();return;}
    if(id==assign)editOrder("assign");else if(id==before)editOrder("before");else if(id==after)editOrder("after");
    else if(id==up)editOrder("up");else if(id==down)editOrder("down");else if(id==remove)editOrder("remove");
    else if(id==createNew)createPattern(false);else if(id==duplicate)createPattern(true);
    else if(id==play&&!unavailable()){const auto index=orderIndex(selected_);if(index>=0&&orders_[size_t(index)].playable&&callbacks_.playOrder)callbacks_.playOrder(selected_);}
  }
  bool key(WPARAM value,bool ctrl,bool shift)override{
    if(!visible()||!owns(GetFocus())||(GetKeyState(VK_MENU)&0x8000))return false;
    if(value==VK_ESCAPE&&!ctrl&&!shift){leave();return true;}
    if(value==VK_F6&&!ctrl&&!shift){if(callbacks_.returnToPattern)callbacks_.returnToPattern();return true;}
    if(value==VK_F5&&!ctrl&&!shift){action(page_=="section"?sectionReload:page_=="pattern"?patternReload:reload,BN_CLICKED);return true;}
    const auto focus=GetFocus();
    if(value==VK_TAB&&!ctrl&&focus==window_){const auto next=GetNextDlgTabItem(window_,nullptr,shift);if(next)SetFocus(next);return true;}
    if(focus==controls_.at(orders)){
      if(page_=="orders"&&value==VK_DELETE&&!ctrl&&!shift){action(remove,BN_CLICKED);return true;}
      if(page_=="orders"&&ctrl&&!shift&&(value==VK_UP||value==VK_DOWN)){action(value==VK_UP?up:down,BN_CLICKED);return true;}
      if(value==VK_RETURN&&!ctrl&&!shift){select(selected_,true);return true;}
    }
    if(value==VK_RETURN&&ctrl&&!shift&&((page_=="section"&&focus==controls_.at(sectionName))||(page_=="pattern"&&(focus==controls_.at(patternName)||focus==controls_.at(patternNotes))))){action(page_=="section"?sectionApply:patternApply,BN_CLICKED);return true;}
    if(value==VK_RETURN&&!shift){for(const int id:{assign,before,after,up,down,remove,play,createNew,duplicate,reload,returnPattern,close,previousSection,nextSection,ordersPage,sectionPage,patternPage,sectionApply,sectionReload,patternApply,patternReload})if(focus==controls_.at(id)){if(IsWindowEnabled(focus))action(id,BN_CLICKED);return true;}}
    return false;
  }
  void resizeColumns(){
    const auto list=controls_.at(orders);RECT bounds{};GetClientRect(list,&bounds);const auto dpi=GetDpiForWindow(window_);const float width=bounds.right*96.f/dpi;
    if(width==listWidth_&&dpi==columnDpi_)return;
    columnWidths_[1]=std::max(180.f,columnWidths_[1]+(listWidth_>0?width-listWidth_:width-columnWidths_[0]-columnWidths_[1]-columnWidths_[2]-columnWidths_[3]-18));
    listWidth_=width;columnDpi_=dpi;resizingColumns_=true;for(int i=0;i<4;++i)ListView_SetColumnWidth(list,i,int(std::lround(columnWidths_[size_t(i)]*dpi/96.f)));resizingColumns_=false;
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();const auto focus=GetFocus();
    const bool orderPage=page_=="orders",section=page_=="section",details=page_=="pattern";
    place(heading,16,12,w-200,26);place(returnPattern,w-180,12,164,30);
    place(sequenceLabel,16,54,66,20);place(sequence,88,48,280,28);
    place(previousSection,384,48,174,30);place(nextSection,570,48,174,30);
    place(orders,16,88,w-32,std::max(100.f,h-424));
    place(ordersPage,16,h-326,100,30);place(sectionPage,128,h-326,110,30);place(patternPage,250,h-326,148,30);place(countLabel,414,h-321,w-430,20);
    place(assignmentLabel,16,h-286,w-32,18,orderPage);place(assignment,16,h-264,w-446,28,orderPage);
    place(assign,w-414,h-264,90,30,orderPage);place(before,w-312,h-264,140,30,orderPage);place(after,w-160,h-264,144,30,orderPage);
    place(up,16,h-226,104,30,orderPage);place(down,132,h-226,112,30,orderPage);place(remove,256,h-226,96,30,orderPage);place(play,364,h-226,176,30,orderPage);
    place(creationLabel,16,h-185,w-32,18,orderPage);place(rowsLabel,16,h-155,34,22,orderPage);place(rows,56,h-160,70,28,orderPage);
    place(sourceLabel,140,h-155,44,22,orderPage);place(source,188,h-160,w-530,28,orderPage);
    place(createNew,w-326,h-160,126,30,orderPage);place(duplicate,w-188,h-160,172,30,orderPage);
    place(helpLabel,16,h-122,w-32,32,orderPage);place(statusLabel,16,h-84,w-32,36);
    place(reload,16,h-42,132,30,orderPage);place(close,w-96,h-42,80,30);
    place(sectionTarget,16,h-286,w-32,22,section);place(sectionNameLabel,16,h-252,w-32,20,section);
    place(sectionName,16,h-224,w-32,30,section);place(sectionHelp,16,h-182,w-32,42,section);
    place(sectionApply,176,h-42,144,30,section);place(sectionReload,16,h-42,148,30,section);
    place(patternTarget,16,h-286,w-32,22,details);place(patternNameLabel,16,h-250,68,20,details);place(patternName,96,h-256,w-112,28,details);
    place(patternNotesLabel,16,h-220,w-32,18,details);place(patternNotes,16,h-198,w-32,104,details);
    place(patternApply,176,h-42,182,30,details);place(patternReload,16,h-42,148,30,details);resizeColumns();
    auto targetText=[&](const DetailDraft &draft,const wchar_t *type){
      if(!draft.bound)return std::wstring(type)+L" · no captured target";
      return (detailStale(draft)?L"STALE · ":L"Captured · ")+draft.label;
    };
    set(sectionTarget,targetText(sectionDraft_,L"CAPTURED ORDER"));set(patternTarget,targetText(patternDraft_,L"CAPTURED PATTERN"));
    const bool available=!unavailable()&&editable();const auto selected=orderIndex(selected_);const auto assigned=pattern(patterns_,assignmentID_);
    EnableWindow(controls_.at(orders),!unavailable());
    for(const int id:{assign,before,after})EnableWindow(controls_.at(id),available&&selected>=0&&assigned&&(id==assign||roomForOrder()));
    EnableWindow(controls_.at(up),available&&selected>0);EnableWindow(controls_.at(down),available&&selected>=0&&size_t(selected+1)<orders_.size());
    EnableWindow(controls_.at(remove),available&&selected>=0&&orders_.size()>1);
    EnableWindow(controls_.at(play),!unavailable()&&selected>=0&&orders_[size_t(selected)].playable);
    EnableWindow(controls_.at(sequence),available&&sequences_.size()>1);
    EnableWindow(controls_.at(createNew),available&&!stale()&&roomForPattern());EnableWindow(controls_.at(duplicate),available&&!stale()&&roomForPattern()&&pattern(patterns_,sourceID_));
    EnableWindow(controls_.at(reload),!unavailable());
    EnableWindow(controls_.at(previousSection),!unavailable()&&sectionIndex(false)>=0);EnableWindow(controls_.at(nextSection),!unavailable()&&sectionIndex(true)>=0);
    EnableWindow(controls_.at(sectionApply),available&&sectionDraft_.bound&&!detailStale(sectionDraft_));EnableWindow(controls_.at(patternApply),available&&patternDraft_.bound&&!detailStale(patternDraft_));
    EnableWindow(controls_.at(sectionReload),!unavailable());EnableWindow(controls_.at(patternReload),!unavailable());
    // Retained fields stay editable during a pending request so the generation
    // guard can preserve newer input. Only explicit actions are disabled.
    EnableWindow(controls_.at(sectionName),sectionDraft_.bound);EnableWindow(controls_.at(patternName),patternDraft_.bound);EnableWindow(controls_.at(patternNotes),patternDraft_.bound);
    if(unavailable()&&focus&&owns(focus)&&!GetFocus())pendingFocus_=focus;
    if(!unavailable()&&pendingFocus_){const auto previous=pendingFocus_;pendingFocus_=nullptr;if(!GetFocus()&&IsWindowVisible(previous)&&IsWindowEnabled(previous)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(previous);}
  }
  void drawControl(const DRAWITEMSTRUCT &draw)override{
    NativeToolWindow::drawControl(draw);if(draw.CtlType!=ODT_BUTTON||draw.CtlID!=unsigned(ordersPage+pageIndex()))return;
    auto rect=draw.rcItem;rect.top=rect.bottom-std::max(2,MulDiv(2,GetDpiForWindow(window_),96));SetDCBrushColor(draw.hDC,NativeControls::highContrast()?GetSysColor(COLOR_HIGHLIGHT):RGB(110,218,197));FillRect(draw.hDC,&rect,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  }
  void paint(RenderSurface &surface)override{const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);}
  void fontsChanged()override{
    if(!controls_.contains(orders))return;const auto replacement=ImageList_Create(1,std::max(1,MulDiv(27,GetDpiForWindow(window_),96)),ILC_COLOR32,1,1);
    if(replacement){ListView_SetImageList(controls_.at(orders),replacement,LVSIL_SMALL);if(rowHeight_)ImageList_Destroy(rowHeight_);rowHeight_=replacement;}columnDpi_=0;
  }
  LRESULT notify(NMHDR *header){
    if(header->hwndFrom!=controls_.at(orders))return 0;
    if(header->code==LVN_GETDISPINFOW){auto &info=*reinterpret_cast<NMLVDISPINFOW *>(header);if((info.item.mask&LVIF_TEXT)&&info.item.pszText&&info.item.cchTextMax>0){const bool valid=info.item.iItem>=0&&size_t(info.item.iItem)<orders_.size()&&info.item.iSubItem>=0&&info.item.iSubItem<4;lstrcpynW(info.item.pszText,valid?orders_[size_t(info.item.iItem)].cells[size_t(info.item.iSubItem)].c_str():L"",info.item.cchTextMax);}return 0;}
    if(header->code==LVN_ITEMCHANGED&&!setting_){const auto &change=*reinterpret_cast<NMLISTVIEW *>(header);if((change.uChanged&LVIF_STATE)&&(change.uNewState&LVIS_SELECTED)&&!(change.uOldState&LVIS_SELECTED)){const auto index=ListView_GetNextItem(controls_.at(orders),-1,LVNI_SELECTED);if(index>=0&&size_t(index)<orders_.size())select(orders_[size_t(index)].id,true);}return 0;}
    if(header->code==NM_DBLCLK&&!setting_){const auto row=reinterpret_cast<NMITEMACTIVATE *>(header)->iItem;if(row>=0&&size_t(row)<orders_.size())select(orders_[size_t(row)].id,true);return 0;}
    if(header->code==LVN_ODFINDITEMW){const auto &find=*reinterpret_cast<NMLVFINDITEMW *>(header);if(!(find.lvfi.flags&LVFI_STRING)||!find.lvfi.psz||orders_.empty())return -1;const auto query=std::wstring_view(find.lvfi.psz);for(size_t i=0;i<orders_.size();++i){const auto row=(size_t(std::max(0,find.iStart))+i)%orders_.size();const auto &label=orders_[row].cells[1];if(label.size()>=query.size()&&CompareStringOrdinal(label.data(),int(query.size()),query.data(),int(query.size()),TRUE)==CSTR_EQUAL)return LRESULT(row);}return -1;}
    if(header->code==NM_CUSTOMDRAW)return NativeReportList::customDraw(*reinterpret_cast<NMLVCUSTOMDRAW *>(header),[this](size_t row,unsigned column){return row<orders_.size()&&column<orders_[row].cells.size()?std::wstring_view(orders_[row].cells[column]):std::wstring_view();});
    return 0;
  }
  static LRESULT CALLBACK notifications(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
    auto &self=*reinterpret_cast<ArrangementWindow *>(data);if(m==WM_NCDESTROY)RemoveWindowSubclass(h,notifications,id);
    if(self.ready_&&NativeReportList::themeMessage(m))NativeReportList::refresh(self.controls_.at(orders));
    if(m==WM_NOTIFY&&self.ready_)try{return self.notify(reinterpret_cast<NMHDR *>(l));}catch(const std::exception &value){self.error(value);return 0;}return DefSubclassProc(h,m,w,l);
  }
  static LRESULT CALLBACK headers(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
    auto &self=*reinterpret_cast<ArrangementWindow *>(data);if(m==WM_NCDESTROY)RemoveWindowSubclass(h,headers,id);
    if(m==WM_NOTIFY){const auto header=reinterpret_cast<NMHDR *>(l);if(header->hwndFrom==ListView_GetHeader(h)){
      if((header->code==HDN_ENDTRACKW||header->code==HDN_ENDTRACKA)&&!self.resizingColumns_){for(int i=0;i<4;++i)self.columnWidths_[size_t(i)]=ListView_GetColumnWidth(h,i)*96.f/GetDpiForWindow(h);const auto &changed=*reinterpret_cast<NMHEADERW *>(header);if(changed.iItem>=0&&changed.iItem<4&&changed.pitem&&(changed.pitem->mask&HDI_WIDTH))self.columnWidths_[size_t(changed.iItem)]=changed.pitem->cxy*96.f/GetDpiForWindow(h);}
      if(header->code==NM_CUSTOMDRAW)return NativeReportList::headerDraw(*reinterpret_cast<NMCUSTOMDRAW *>(header));
    }}return DefSubclassProc(h,m,w,l);
  }
public:
  ArrangementWindow(HWND owner,Callbacks callbacks):NativeToolWindow(owner),callbacks_(std::move(callbacks)){
    minimumClientWidth_=760;minimumClientHeight_=600;INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};if(!InitCommonControlsEx(&common))throw std::runtime_error("Cannot initialize arrangement list");
    create(L"ScreamSeq.Arrangement",L"Arrange orders",860,650);
    // Initial dimensions are client DIPs, just like the minimum. Constrain the
    // outer frame to the monitor work area without affecting another window.
    const auto dpi=GetDpiForWindow(window_);RECT outer{0,0,MulDiv(860,dpi,96),MulDiv(650,dpi,96)};
    if(!AdjustWindowRectExForDpi(&outer,DWORD(GetWindowLongPtrW(window_,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window_,GWL_EXSTYLE)),dpi))throw std::runtime_error("Cannot size arrangement window");
    RECT current{};GetWindowRect(window_,&current);MONITORINFO monitor{sizeof(monitor)};
    if(GetMonitorInfoW(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST),&monitor)){const auto &work=monitor.rcWork;const int width=std::min(outer.right-outer.left,work.right-work.left),height=std::min(outer.bottom-outer.top,work.bottom-work.top);SetWindowPos(window_,nullptr,std::clamp(current.left,work.left,work.right-width),std::clamp(current.top,work.top,work.bottom-height),width,height,SWP_NOZORDER|SWP_NOACTIVATE);}
    const auto list=add(orders,WC_LISTVIEWW,L"Complete order list",LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_BORDER);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    if(const auto library=LoadLibraryW(L"uxtheme.dll")){using Theme=HRESULT(WINAPI *)(HWND,LPCWSTR,LPCWSTR);if(const auto theme=reinterpret_cast<Theme>(GetProcAddress(library,"SetWindowTheme")))theme(list,L"",L"");FreeLibrary(library);}
    NativeReportList::install(list);
    int column=0;for(const auto name:{L"Order",L"Pattern",L"Rows",L"Section"}){LVCOLUMNW item{};item.mask=LVCF_TEXT|LVCF_WIDTH;item.pszText=const_cast<wchar_t *>(name);item.cx=100;ListView_InsertColumn(list,column++,&item);}
    combo(sequence);combo(assignment);edit(rows,L"64",5);combo(source);
    for(const auto [id,label]:std::initializer_list<std::pair<int,const wchar_t *>>{{assign,L"Assign"},{before,L"Insert before"},{after,L"Insert after"},{up,L"Move up"},{down,L"Move down"},{remove,L"Remove"},{play,L"Play selected"},{createNew,L"New + append"},{duplicate,L"Duplicate + append"},{reload,L"Reload draft"},{returnPattern,L"Return to pattern (F6)"},{close,L"Close"}})button(id,label);
    for(const auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"ARRANGE ORDERS"},{sequenceLabel,L"Sequence"},{countLabel,L""},{assignmentLabel,L"PATTERN FOR THE SELECTED ORDER"},{creationLabel,L"CREATE A PATTERN AND APPEND ONE ORDER"},{rowsLabel,L"Rows"},{sourceLabel,L"Source"},{helpLabel,L""},{statusLabel,L""}})label(id,text);
    for(const auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{previousSection,L"Previous section"},{nextSection,L"Next section"},{ordersPage,L"Orders"},{sectionPage,L"Section"},{patternPage,L"Pattern details"},{sectionApply,L"Set section"},{sectionReload,L"Reload section"},{patternApply,L"Save pattern details"},{patternReload,L"Reload pattern"}})button(id,text);
    edit(sectionName,L"",256);edit(patternName,L"",256);
    const auto notes=add(patternNotes,L"EDIT",L"",ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL|WS_BORDER);SendMessageW(notes,EM_SETLIMITTEXT,4096,0);
    for(const auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{sectionTarget,L""},{sectionNameLabel,L"Section beginning at this captured order"},{sectionHelp,L"Section names belong to order occurrences, including Stop and Skip.\nClear the name and choose Set section to remove this marker."},{patternTarget,L""},{patternNameLabel,L"Name"},{patternNotesLabel,L"Notes · shared by every use of this pattern"}})label(id,text);
    for(int id:{sectionTarget,patternTarget,countLabel})SetWindowLongPtrW(controls_.at(id),GWL_STYLE,GetWindowLongPtrW(controls_.at(id),GWL_STYLE)|SS_ENDELLIPSIS|SS_NOPREFIX);
    if(!SetWindowSubclass(window_,notifications,notificationsID,reinterpret_cast<DWORD_PTR>(this))||!SetWindowSubclass(list,headers,headerID,reinterpret_cast<DWORD_PTR>(this)))throw std::runtime_error("Cannot initialize arrangement notifications");
    finish();statusText();
  }
  ~ArrangementWindow()override{
    if(window_)RemoveWindowSubclass(window_,notifications,notificationsID);if(controls_.contains(orders)){const auto list=controls_.at(orders);RemoveWindowSubclass(list,headers,headerID);ListView_SetImageList(list,nullptr,LVSIL_SMALL);}if(rowHeight_)ImageList_Destroy(rowHeight_);
  }
  void hide()override{const bool focused=owns(GetFocus());NativeToolWindow::hide();if(focused&&callbacks_.returnToPattern)callbacks_.returnToPattern();}
  void update(const Json &state){
    const auto docID=identity(state.at("documentId"));identity(state.at("revision"));if(!state.at("busy").is_boolean())throw std::runtime_error("Invalid arrangement busy state");
    const auto &doc=state.at("document"),&orderValues=doc.at("orders"),&metadata=doc.at("orderMetadata"),&patternValues=doc.at("patterns"),&sequenceValues=doc.at("sequences"),&limits=doc.at("formatLimits");
    if(!orderValues.is_array()||orderValues.size()>65536||!metadata.is_array()||metadata.size()!=orderValues.size()||!patternValues.is_array()||patternValues.size()>65536||!sequenceValues.is_array()||sequenceValues.empty()||sequenceValues.size()>256||!doc.at("editable").is_boolean())throw std::runtime_error("Invalid arrangement catalog");
    const auto minimum=unsignedValue(limits.at("patternRowsMin")),maximum=unsignedValue(limits.at("patternRowsMax"));if(!minimum||minimum>maximum)throw std::runtime_error("Invalid pattern row limits");unsignedValue(limits.at("patternsMax"));unsignedValue(limits.at("ordersMax"));
    std::set<std::string> patternIDs,orderIDs;std::set<unsigned> patternIndexes;std::vector<Pattern> patterns;std::vector<Order> orderList;std::vector<std::pair<unsigned,std::wstring>> sequenceList;
    for(const auto &value:patternValues){Pattern item;item.id=identity(value.at("id"));item.index=unsignedValue(value.at("index"));item.rows=unsignedValue(value.at("rows"));if(!patternIDs.insert(item.id).second||!patternIndexes.insert(item.index).second)throw std::runtime_error("Duplicate pattern identity");item.name=value.value("name",std::string());item.annotation=value.value("annotation",std::string());item.label=std::to_wstring(item.index)+(item.name.empty()?L"":L" · "+wide(item.name));patterns.push_back(std::move(item));}
    for(size_t i=0;i<orderValues.size();++i){Order item;item.id=identity(metadata[i].at("id"));if(!orderIDs.insert(item.id).second)throw std::runtime_error("Duplicate order identity");item.name=metadata[i].value("name",std::string());item.pattern=unsignedValue(orderValues[i]);const auto found=std::find_if(patterns.begin(),patterns.end(),[&](const auto &p){return p.index==item.pattern;});item.playable=found!=patterns.end();item.cells={std::to_wstring(i),item.playable?found->label:item.pattern==65535?L"— Stop":item.pattern==65534?L"+++ Skip":L"Missing pattern "+std::to_wstring(item.pattern),item.playable?std::to_wstring(found->rows):L"—",wide(item.name)};orderList.push_back(std::move(item));}
    std::set<unsigned> sequenceIndexes;std::set<std::string> sequenceIDs;
    for(const auto &value:sequenceValues){const auto index=unsignedValue(value.at("index"),255);if(!sequenceIndexes.insert(index).second||!sequenceIDs.insert(identity(value.at("id"))).second)throw std::runtime_error("Duplicate sequence identity");const auto name=value.value("name",std::string());sequenceList.emplace_back(index,name.empty()?L"Sequence "+std::to_wstring(index+1):wide(name));}
    const auto currentSequence=unsignedValue(doc.at("sequence"),255);if(!sequenceIndexes.contains(currentSequence))throw std::runtime_error("Selected sequence is unavailable");
    const auto selected=state.at("selectedOrderID").get<std::string>();if(!selected.empty()&&!orderIDs.contains(selected))throw std::runtime_error("Selected order identity is unavailable");
    const bool first=!loaded_,replacement=loaded_&&state_.at("documentId")!=docID,patternsChanged=patterns_!=patterns,sequencesChanged=sequences_!=sequenceList;
    const auto list=controls_.at(orders);const auto previousTop=ListView_GetTopIndex(list);const auto topID=previousTop>=0&&size_t(previousTop)<orders_.size()?orders_[size_t(previousTop)].id:std::string();
    if(first||replacement)++documentGeneration_;
    state_=state;patterns_=std::move(patterns);sequences_=std::move(sequenceList);loaded_=true;
    if(selected_!=selected){selected_=selected;++selectionGeneration_;}
    setting_=true;
    if(patternsChanged){if(!pattern(patterns_,assignmentID_))assignmentID_=patterns_.empty()?std::string():patterns_.front().id;fillPatterns(assignment,patterns_,assignmentID_);}
    if(sequencesChanged){SendMessageW(controls_.at(sequence),CB_RESETCONTENT,0,0);for(const auto &[index,label]:sequences_)SendMessageW(controls_.at(sequence),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
    for(size_t i=0;i<sequences_.size();++i)if(sequences_[i].first==currentSequence)SendMessageW(controls_.at(sequence),CB_SETCURSEL,i,0);
    const auto chosen=std::find_if(orderList.begin(),orderList.end(),[&](const auto &value){return value.id==selected_;});const auto chosenIndex=chosen==orderList.end()?-1:int(chosen-orderList.begin());
    if(orders_!=orderList||ListView_GetNextItem(list,-1,LVNI_SELECTED)!=chosenIndex){
      orders_=std::move(orderList);SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_SetItemCountEx(list,int(orders_.size()),LVSICF_NOINVALIDATEALL|LVSICF_NOSCROLL);ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);const auto selectedIndex=orderIndex(selected_);if(selectedIndex>=0)ListView_SetItemState(list,selectedIndex,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
      const auto top=orderIndex(topID);if(top>=0){RECT row{};if(ListView_GetItemRect(list,top,&row,LVIR_BOUNDS))ListView_Scroll(list,0,(top-ListView_GetTopIndex(list))*(row.bottom-row.top));}SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);
    }
    setting_=false;
    if(first)captureDraft();
    set(countLabel,std::to_wstring(orders_.size())+L" orders · "+std::to_wstring(patterns_.size())+L" patterns");
    set(helpLabel,L"Rows: "+std::to_wstring(minimum)+L"–"+std::to_wstring(maximum)+L". New and Duplicate append to the end of this sequence.\nDuplicate copies its source up to the chosen row count.");
    statusText();layout();
  }
  void open(const Json &state,std::string mode="arrange",std::string sourcePatternID={}){
    if(mode!="arrange"&&mode!="new"&&mode!="duplicate"&&mode!="section"&&mode!="pattern")throw std::runtime_error("Unknown arrangement mode");
    const bool first=!opened_;const auto focused=GetFocus();const bool retainedFocus=visible()&&owns(focused);update(state);
    if(first){mode_=mode;captureDraft(std::move(sourcePatternID));clampToOwnerWorkArea();opened_=true;}
    if(mode=="section"||mode=="pattern")changePage(mode);else if(mode=="new"||mode=="duplicate")changePage("orders");
    const auto views=fieldViews();NativeToolWindow::show();if(retainedFocus&&IsWindowVisible(focused)&&IsWindowEnabled(focused))SetFocus(focused);else {const auto retained=pageFocus_[size_t(pageIndex())];if(retained&&IsWindowVisible(retained)&&IsWindowEnabled(retained))SetFocus(retained);else SetFocus(controls_.at(page_=="section"?sectionPage:page_=="pattern"?patternPage:first&&mode_!="arrange"?rows:orders));}restoreFieldViews(views);
    if(first){const auto selected=orderIndex(selected_);if(selected>=0)ListView_EnsureVisible(controls_.at(orders),selected,FALSE);}
  }
  void navigateSection(bool next){
    if(unavailable())return;const auto index=sectionIndex(next);if(index<0)return;
    const auto id=orders_[size_t(index)].id;select(id,true);
    setting_=true;ListView_SetItemState(controls_.at(orders),-1,0,LVIS_SELECTED|LVIS_FOCUSED);const auto current=orderIndex(selected_);if(current>=0){ListView_SetItemState(controls_.at(orders),current,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(controls_.at(orders),current,FALSE);}setting_=false;
  }
  Json snapshot()const{
    Json bounds=Json::array();const float scale=96.f/GetDpiForWindow(window_);
    for(const auto &[id,control]:controls_){RECT rect{};GetWindowRect(control,&rect);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT *>(&rect),2);bounds.push_back({{"id",id},{"x",rect.left*scale},{"y",rect.top*scale},{"width",(rect.right-rect.left)*scale},{"height",(rect.bottom-rect.top)*scale},{"visible",IsWindowVisible(control)!=FALSE},{"enabled",IsWindowEnabled(control)!=FALSE}});}
    auto detail=[&](const DetailDraft &draft,bool section)->Json{return {{"documentId",draft.document},{"revision",draft.revision},{"sequenceID",draft.sequence},{"targetID",draft.target},{"targetLabel",utf8(draft.label)},{"generation",draft.generation},{"bound",draft.bound},{"dirty",draft.dirty},{"stale",detailStale(draft)},{"selectionDiffers",detailDiffers(section)},{"nameText",utf8(field(section?sectionName:patternName))},{"annotationText",section?std::string():utf8(field(patternNotes))},{"error",draft.error}};};
    return {{"visible",visible()},{"pending",unavailable()},{"mode",mode_},{"page",page_},{"sectionDraft",detail(sectionDraft_,true)},{"patternDraft",detail(patternDraft_,false)},{"documentId",state_.value("documentId",std::string())},{"revision",state_.value("revision",std::string())},{"selectedOrderID",selected_},{"assignmentPatternID",assignmentID_},{"sourcePatternID",sourceID_},{"rowsText",utf8(field(rows))},{"draftDocumentId",draftDocument_},{"draftRevision",draftRevision_},{"draftGeneration",draftGeneration_},{"dirty",dirty_},{"stale",stale()},{"status",utf8(status_)},{"error",error_},{"controlBounds",std::move(bounds)}};
  }
};
}
