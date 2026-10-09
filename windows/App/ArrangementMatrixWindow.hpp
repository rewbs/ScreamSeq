#pragma once
#include "NativeToolWindow.hpp"
#include "NativeReportList.hpp"
#include <array>
#include <set>

namespace ScreamSeq {
// A retained view of a bounded worker snapshot. The private copy token is an
// intention to copy a captured block, never a clipboard payload or mutable song.
class ArrangementMatrixWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Callbacks {
    std::function<Json(std::string,Json)> operate; // {state,result}; raw API data
    std::function<void(std::string,std::string)> openBlock; // stable order/track
    std::function<void()> returnToPattern;
  };
private:
  enum:int {matrix=8401,earlier,later,previousTracks,nextTracks,refreshButton,
    openButton,copyButton,pasteButton,mode,independent,clip,clearCopy,returnPattern,
    close,heading=8500,pageLabel,selectionLabel,copyLabel,statusLabel,helpLabel};
  struct Block {unsigned channel{},events{},notes{},tracker{},precise{},fx{};std::string track;std::array<unsigned,16> bins{};bool operator==(const Block&)const=default;};
  struct Order {unsigned index{},pattern{},rows{};std::string id,patternID;std::wstring label,title,section,name;std::vector<Block> blocks;bool operator==(const Order&)const=default;};
  struct Track {unsigned channel{};std::string id;std::wstring label,name;bool operator==(const Track&)const=default;};
  struct Copy {std::string document,revision,sequence,orderID,trackID;unsigned order{},channel{};std::wstring label;uint64_t generation{};};
  Callbacks callbacks_;Json state_=Json::object();std::vector<Order> orders_;std::vector<Track> tracks_;
  std::optional<Copy> copied_;std::string selectedOrder_,selectedTrack_,viewDocument_,viewRevision_,viewSequence_,error_;
  std::optional<std::array<std::string,3>> failedReadContext_;
  unsigned startOrder_=0,startChannel_=0,totalOrders_=0,totalChannels_=1;uint64_t contextGeneration_=0,requestGeneration_=0,copyGeneration_=0,selectionGeneration_=0;
  bool loaded_=false,opened_=false,pending_=false,setting_=false,queued_=false,unique_=true,clip_=false;int mode_=0;
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    // A private copied block and the selected cell are navigation/clipboard
    // state. Only an in-flight request needs departure protection here.
    return describeDraft(viewDocument_,viewRevision_,viewSequence_,requestGeneration_,false,pending_);
  }
  HIMAGELIST rowHeight_{};UINT columnDpi_=0;std::map<std::string,float> widths_;HWND pendingFocus_{},retainedFocus_{};
  static constexpr UINT refreshMessage=WM_APP+186;static constexpr UINT_PTR subclassID=0x4d415458;
  static unsigned integer(const Json &value,unsigned maximum=65536){
    if(!value.is_number_integer()&&!value.is_number_unsigned())throw std::runtime_error("Invalid matrix number");
    const auto n=value.get<int64_t>();if(n<0||uint64_t(n)>maximum)throw std::runtime_error("Matrix number is outside its bounds");return unsigned(n);
  }
  static std::string identity(const Json &value){if(!value.is_string())throw std::runtime_error("Invalid matrix identity");auto s=value.get<std::string>();if(s.empty()||s.size()>200||s.find('\0')!=std::string::npos)throw std::runtime_error("Invalid matrix identity");return s;}
  static std::string sequence(const Json &state){const auto &doc=state.at("document");for(const auto &s:doc.at("sequences"))if(s.at("index")==doc.at("sequence"))return identity(s.at("id"));throw std::runtime_error("Current matrix sequence is unavailable");}
  bool busy()const{return !loaded_||pending_||state_.value("busy",false);}
  bool fresh()const{return loaded_&&viewDocument_==state_.at("documentId").get<std::string>()&&viewRevision_==state_.at("revision").get<std::string>()&&viewSequence_==sequence(state_);}
  bool copyStale()const{return copied_&&(!loaded_||copied_->document!=state_.at("documentId").get<std::string>()||copied_->revision!=state_.at("revision").get<std::string>()||copied_->sequence!=sequence(state_));}
  std::array<std::string,3> readContext()const{return {state_.at("documentId").get<std::string>(),state_.at("revision").get<std::string>(),sequence(state_)};}
  bool automaticRead()const{return loaded_&&!busy()&&!fresh()&&(!failedReadContext_||*failedReadContext_!=readContext());}
  int row()const{for(size_t i=0;i<orders_.size();++i)if(orders_[i].id==selectedOrder_)return int(i);return -1;}
  int track()const{for(size_t i=0;i<tracks_.size();++i)if(tracks_[i].id==selectedTrack_)return int(i);return -1;}
  bool validBlock()const{const auto r=row(),t=track();return r>=0&&t>=0&&orders_[size_t(r)].rows&&orders_[size_t(r)].blocks.size()==tracks_.size();}
  std::wstring chosenLabel(bool density=false)const{
    const auto r=row(),t=track();if(r<0||t<0)return L"Choose an order and track on this page.";const auto &order=orders_[size_t(r)];const auto &track=tracks_[size_t(t)];
    auto label=L"Order "+std::to_wstring(order.index)+L" · Track "+std::to_wstring(track.channel+1)+L" · "+(order.rows?L"P"+std::to_wstring(order.pattern):order.pattern==65534?L"Skip":order.pattern==65535?L"Stop":L"Missing pattern");
    if(density&&validBlock()){const auto &block=order.blocks[size_t(t)];label+=L" · tracker "+std::to_wstring(block.tracker)+L" / precise "+std::to_wstring(block.precise)+L" / FX "+std::to_wstring(block.fx);}
    // Put unambiguous coordinates before names so ellipsis cannot conceal the
    // selected or captured source track when a pattern/section name is long.
    for(const auto *name:{&track.name,&order.name,&order.section})if(!name->empty())label+=L" · "+*name;return label;
  }
  std::wstring cell(size_t r,unsigned c)const{
    if(r>=orders_.size())return {};const auto &order=orders_[r];if(!c)return order.label;
    if(c>order.blocks.size())return L"—";const auto &b=order.blocks[c-1];
    return std::to_wstring(b.notes)+L" notes · "+std::to_wstring(b.events)+L" events; tracker "+std::to_wstring(b.tracker)+L", precise "+std::to_wstring(b.precise)+L", FX "+std::to_wstring(b.fx);
  }
  void schedule(){if(ready_&&visible()&&automaticRead()&&!queued_){queued_=PostMessageW(window_,refreshMessage,0,0)!=FALSE;}}
  void statusText(){
    set(selectionLabel,chosenLabel(true));set(copyLabel,copied_?(copyStale()?L"STALE COPY · ":L"Copied · ")+copied_->label:L"No copied block. Copy captures its occurrence, track and song revision.");
    if(!error_.empty())status_=wide(error_);else if(pending_)status_=L"Reading or applying the captured matrix request…";
    else if(!fresh())status_=L"The matrix belongs to an earlier song revision. Refresh reads the current page; the copied source stays captured.";
    else if(copyStale())status_=L"The copied source is stale. Select its current block and Copy again before pasting.";
    else status_=L"Arrows select a block. Enter opens it. Ctrl+C copies; Ctrl+V pastes. Selection does not move playback.";
    set(statusLabel,status_);set(pageLabel,L"Orders "+std::to_wstring(orders_.empty()?0:startOrder_+1)+L"–"+std::to_wstring(startOrder_+orders_.size())+L" / "+std::to_wstring(totalOrders_)+L"   ·   Tracks "+std::to_wstring(tracks_.empty()?0:startChannel_+1)+L"–"+std::to_wstring(startChannel_+tracks_.size())+L" / "+std::to_wstring(totalChannels_));requestPaint();
  }
  void error(const std::exception &e)override{error_=e.what();statusText();layout();}
  void choose(std::string order,std::string track,bool reveal=false){
    if(order!=selectedOrder_||track!=selectedTrack_){selectedOrder_=std::move(order);selectedTrack_=std::move(track);++selectionGeneration_;}
    setting_=true;const auto list=controls_.at(matrix);const auto r=row();if(ListView_GetNextItem(list,-1,LVNI_SELECTED)!=r){ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);if(r>=0)ListView_SetItemState(list,r,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}setting_=false;
    if(reveal&&r>=0){ListView_EnsureVisible(list,r,FALSE);const auto t=trackIndex();if(t>=0){RECT part{},client{};Header_GetItemRect(ListView_GetHeader(list),t+1,&part);MapWindowPoints(ListView_GetHeader(list),list,reinterpret_cast<POINT *>(&part),2);GetClientRect(list,&client);if(part.left<0)ListView_Scroll(list,part.left,0);else if(part.right>client.right)ListView_Scroll(list,part.right-client.right,0);}}
    statusText();layout();InvalidateRect(list,nullptr,FALSE);
  }
  int trackIndex()const{return track();}
  void columns(){
    if(!ready_)return;const auto list=controls_.at(matrix);const auto dpi=GetDpiForWindow(window_);const auto horizontal=GetScrollPos(list,SB_HORZ);setting_=true;
    while(Header_GetItemCount(ListView_GetHeader(list)))ListView_DeleteColumn(list,0);
    for(size_t c=0;c<=tracks_.size();++c){const auto key=c?tracks_[c-1].id:std::string();const auto text=c?tracks_[c-1].label:L"Order / pattern / section";LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t *>(text.c_str());column.cx=int(std::lround((widths_.contains(key)?widths_.at(key):c?106.f:240.f)*dpi/96.f));ListView_InsertColumn(list,int(c),&column);}const auto wanted=columnDpi_?MulDiv(horizontal,dpi,columnDpi_):horizontal;columnDpi_=dpi;ListView_Scroll(list,wanted-GetScrollPos(list,SB_HORZ),0);setting_=false;
  }
  void accept(const Json &data,const std::string &document,const std::string &revision,const std::string &seq,bool pageChange){
    const auto &orderValues=data.at("orders"),&trackValues=data.at("tracks");if(!orderValues.is_array()||orderValues.size()>64||!trackValues.is_array()||trackValues.size()>12)throw std::runtime_error("Matrix response exceeds its page budget");
    const auto totalOrders=integer(data.at("totalOrders")),totalChannels=integer(data.at("totalChannels")),startOrder=integer(data.at("startOrder")),startChannel=integer(data.at("startChannel"));
    if(!totalChannels||startOrder>totalOrders||startChannel>=totalChannels||orderValues.size()!=std::min(64u,totalOrders-startOrder)||trackValues.size()!=std::min(12u,totalChannels-startChannel))throw std::runtime_error("Invalid or incomplete matrix page bounds");
    std::vector<Order> orders;std::vector<Track> tracks;std::set<std::string> ids;
    for(const auto &v:trackValues){Track t;t.id=identity(v.at("id"));t.channel=integer(v.at("channel"));if(!ids.insert(t.id).second||t.channel!=startChannel+tracks.size())throw std::runtime_error("Invalid matrix track identity");t.name=wide(v.value("name",std::string()));t.label=L"Track "+std::to_wstring(t.channel+1)+(t.name.empty()?L"":L" · "+t.name);tracks.push_back(std::move(t));}
    ids.clear();for(const auto &v:orderValues){Order o;o.id=identity(v.at("id"));o.index=integer(v.at("order"));o.pattern=integer(v.at("pattern"));o.rows=integer(v.at("rows"));if(!ids.insert(o.id).second||o.index!=startOrder+orders.size())throw std::runtime_error("Invalid matrix order identity");
      if(o.rows)o.patternID=identity(v.at("patternID"));const auto name=v.value("name",std::string()),patternName=v.value("patternName",std::string());o.name=wide(patternName);o.title=std::to_wstring(o.index)+L" · "+(o.rows?L"P"+std::to_wstring(o.pattern)+(patternName.empty()?L"":L" · "+o.name):o.pattern==65535?L"Stop":o.pattern==65534?L"Skip":L"Missing pattern");o.section=wide(name);o.label=o.title+(name.empty()?L"":L" / "+o.section);
      const auto &blocks=v.at("blocks");if(!blocks.is_array()||blocks.size()!=(o.rows?tracks.size():0))throw std::runtime_error("Invalid matrix block count");for(const auto &value:blocks){Block b;b.channel=integer(value.at("channel"));b.track=identity(value.at("trackID"));const auto index=o.blocks.size();if(b.channel!=tracks[index].channel||b.track!=tracks[index].id)throw std::runtime_error("Matrix block belongs to another track");b.events=integer(value.at("events"),UINT32_MAX);b.notes=integer(value.at("notes"),UINT32_MAX);b.tracker=integer(value.at("trackerEvents"),UINT32_MAX);b.precise=integer(value.at("preciseEvents"),UINT32_MAX);b.fx=integer(value.at("nativeFxEvents"),UINT32_MAX);const auto &bins=value.at("bins");if(!bins.is_array()||bins.size()!=16)throw std::runtime_error("Invalid matrix density bins");uint64_t sum=0;for(size_t i=0;i<16;++i){b.bins[i]=integer(bins[i],UINT32_MAX);sum+=b.bins[i];}if(sum!=b.events||uint64_t(b.tracker)+b.precise+b.fx!=b.events||b.notes>b.events)throw std::runtime_error("Inconsistent matrix density counts");o.blocks.push_back(std::move(b));}orders.push_back(std::move(o));}
    const auto list=controls_.at(matrix);const auto oldTop=ListView_GetTopIndex(list);const auto topID=oldTop>=0&&size_t(oldTop)<orders_.size()?orders_[size_t(oldTop)].id:std::string();
    const bool tracksChanged=tracks_!=tracks,changed=orders_!=orders,horizontalPageChanged=startChannel_!=startChannel||viewDocument_!=document||viewSequence_!=seq;tracks_=std::move(tracks);orders_=std::move(orders);totalOrders_=totalOrders;totalChannels_=totalChannels;startOrder_=startOrder;startChannel_=startChannel;viewDocument_=document;viewRevision_=revision;viewSequence_=seq;
    if(tracksChanged)columns();if(changed){setting_=true;ListView_SetItemCountEx(list,int(orders_.size()),LVSICF_NOSCROLL|LVSICF_NOINVALIDATEALL);setting_=false;}
    if(pageChange&&horizontalPageChanged)ListView_Scroll(list,-GetScrollPos(list,SB_HORZ),0);
    auto selectedOrder=selectedOrder_,selectedTrack=selectedTrack_;if(pageChange||selectedOrder.empty()){if(row()<0)selectedOrder=orders_.empty()?std::string():orders_.front().id;if(track()<0)selectedTrack=tracks_.front().id;}
    choose(std::move(selectedOrder),std::move(selectedTrack),pageChange);
    if(!pageChange&&!topID.empty())for(size_t i=0;i<orders_.size();++i)if(orders_[i].id==topID){RECT rect{};if(ListView_GetItemRect(list,int(i),&rect,LVIR_BOUNDS))ListView_Scroll(list,0,(int(i)-ListView_GetTopIndex(list))*(rect.bottom-rect.top));break;}
    if(pageChange&&row()>=0)ListView_EnsureVisible(list,row(),FALSE);if(changed||tracksChanged)InvalidateRect(list,nullptr,FALSE);
  }
  void load(unsigned order,unsigned channel,bool pageChange){
    if(busy()||!callbacks_.operate)return;const auto document=state_.at("documentId").get<std::string>(),revision=state_.at("revision").get<std::string>(),seq=sequence(state_);const auto context=contextGeneration_,request=++requestGeneration_;
    // An explicit read supersedes any posted automatic refresh. The old native
    // message may remain queued, but cannot issue a second worker request.
    queued_=false;
    pending_=true;error_.clear();statusText();layout();try{
      const auto remaining=totalChannels_>channel?totalChannels_-channel:1u;
      const auto response=callbacks_.operate("arrangement.matrix",{{"startOrder",order},{"orderCount",64},{"startChannel",channel},{"channelCount",std::min(12u,remaining)}});
      if(request!=requestGeneration_)return;pending_=false;if(context!=contextGeneration_||state_.at("documentId")!=document||state_.at("revision")!=revision||sequence(state_)!=seq){statusText();layout();schedule();return;}
      const auto &returned=response.at("state");if(returned.at("documentId")!=document||returned.at("revision")!=revision||sequence(returned)!=seq)throw std::runtime_error("Song changed while reading the matrix. Refresh to read the current page.");
      const auto &data=response.at("result");if(integer(data.at("startOrder"))!=order||integer(data.at("startChannel"))!=channel)throw std::runtime_error("Matrix response belongs to another page");if(integer(data.at("totalChannels"))!=totalChannels_||(state_.at("document").contains("orders")&&integer(data.at("totalOrders"))!=state_.at("document").at("orders").size()))throw std::runtime_error("Matrix response disagrees with its document catalog");accept(data,document,revision,seq,pageChange);failedReadContext_.reset();error_.clear();statusText();layout();
    }catch(const std::exception &e){if(request==requestGeneration_){pending_=false;failedReadContext_=std::array<std::string,3>{document,revision,seq};if(context==contextGeneration_)error_=e.what();statusText();layout();schedule();}}
  }
  void copy(){if(busy()||!fresh()||!validBlock())return;const auto &o=orders_[size_t(row())];const auto &t=tracks_[size_t(track())];copied_=Copy{viewDocument_,viewRevision_,viewSequence_,o.id,t.id,o.index,t.channel,chosenLabel(),++copyGeneration_};error_.clear();statusText();layout();}
  void paste(){
    if(busy()||!fresh()||!validBlock()||!copied_||!state_.at("document").value("editable",false))return;if(copyStale())throw std::runtime_error("The copied source is stale. Copy its current block again.");
    const auto source=*copied_;const auto target=orders_[size_t(row())];const auto destination=tracks_[size_t(track())];const auto context=contextGeneration_,request=++requestGeneration_;const auto capturedRevision=viewRevision_;pending_=true;error_.clear();statusText();layout();
    try{const auto response=callbacks_.operate("arrangement.copyBlock",{{"sourceOrder",source.order},{"sourceChannel",source.channel},{"targetOrder",target.index},{"targetChannel",destination.channel},{"channelCount",1},{"mode",mode_==1?"merge":mode_==2?"mix":"overwrite"},{"makeUnique",unique_},{"clip",clip_},{"expectedRevision",source.revision}});
      if(request!=requestGeneration_)return;pending_=false;if(context!=contextGeneration_){statusText();layout();schedule();return;}
      const auto &returned=response.at("state");if(returned.at("documentId")!=source.document||sequence(returned)!=source.sequence||(state_.at("revision")!=capturedRevision&&state_.at("revision")!=returned.at("revision")))throw std::runtime_error("Song changed while Paste completed. Current selection and copied source were retained.");
      update(returned);if(copied_&&copied_->generation==source.generation){copied_.reset();++copyGeneration_;}error_.clear();statusText();layout();schedule();
    }catch(const std::exception &e){if(request==requestGeneration_){pending_=false;if(context==contextGeneration_)error_=e.what();statusText();layout();}}
  }
  void openBlock(){if(!busy()&&fresh()&&validBlock()&&callbacks_.openBlock)callbacks_.openBlock(selectedOrder_,selectedTrack_);}
  void leave(){const bool owned=owns(GetFocus());hide();if(!owned&&callbacks_.returnToPattern)callbacks_.returnToPattern();}
  void action(int id,unsigned notification)override{
    if(setting_)return;if(id==mode&&notification==CBN_SELCHANGE){const auto selected=SendMessageW(controls_.at(mode),CB_GETCURSEL,0,0);if(selected>=0&&selected<3)mode_=int(selected);return;}
    if(id==independent){unique_=!unique_;layout();return;}if(id==clip){clip_=!clip_;layout();return;}if(id==clearCopy){copied_.reset();++copyGeneration_;error_.clear();statusText();layout();return;}
    if(id==returnPattern||id==close){leave();return;}if(busy())return;
    if(id==refreshButton)refresh();else if(id==earlier)load(startOrder_>=64?startOrder_-64:0,startChannel_,true);else if(id==later&&startOrder_+64<totalOrders_)load(startOrder_+64,startChannel_,true);
    else if(id==previousTracks)load(startOrder_,startChannel_>=12?startChannel_-12:0,true);else if(id==nextTracks&&startChannel_+12<totalChannels_)load(startOrder_,startChannel_+12,true);else if(id==openButton)openBlock();else if(id==copyButton)copy();else if(id==pasteButton)paste();
  }
  bool key(WPARAM value,bool ctrl,bool shift)override{
    const auto focus=GetFocus();if(!visible()||!owns(focus)||(GetKeyState(VK_MENU)&0x8000))return false;
    if(!ctrl&&!shift&&(value==VK_F6||value==VK_ESCAPE)){leave();return true;}if(!ctrl&&!shift&&value==VK_F5){action(refreshButton,BN_CLICKED);return true;}
    if(focus==window_&&value==VK_TAB&&!ctrl){SetFocus(GetNextDlgTabItem(window_,nullptr,shift));return true;}
    if(focus==controls_.at(matrix)){
      if(ctrl&&!shift&&(value=='C'||value=='V')){action(value=='C'?copyButton:pasteButton,BN_CLICKED);return true;}
      if(!ctrl&&!shift&&value==VK_RETURN){openBlock();return true;}
      if(!ctrl&&!shift&&(value==VK_LEFT||value==VK_RIGHT)){if(!tracks_.empty()){const int next=std::clamp(track()+(value==VK_LEFT?-1:1),0,int(tracks_.size())-1);choose(selectedOrder_,tracks_[size_t(next)].id,true);}return true;}
    }
    if(value==VK_RETURN&&!ctrl&&!shift)for(int id:{earlier,later,previousTracks,nextTracks,refreshButton,openButton,copyButton,pasteButton,independent,clip,clearCopy,returnPattern,close})if(focus==controls_.at(id)){if(IsWindowEnabled(focus))action(id,BN_CLICKED);return true;}
    return false;
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();const auto focus=GetFocus();place(heading,16,12,w-212,26);place(returnPattern,w-196,12,180,30);
    place(earlier,16,50,132,30);place(later,160,50,132,30);place(previousTracks,304,50,146,30);place(nextTracks,462,50,130,30);place(refreshButton,w-124,50,108,30);
    place(pageLabel,16,88,w-32,22);place(matrix,16,116,w-32,h-338);place(selectionLabel,16,h-210,w-32,24);
    place(openButton,16,h-176,112,30);place(copyButton,140,h-176,112,30);place(mode,264,h-176,192,30);place(pasteButton,468,h-176,112,30);place(independent,592,h-176,164,30);place(clip,16,h-136,210,30);place(clearCopy,238,h-136,118,30);
    place(copyLabel,16,h-98,w-32,24);place(statusLabel,16,h-68,w-116,40);place(close,w-96,h-42,80,30);place(helpLabel,370,h-135,w-386,32);
    set(independent,unique_?L"Independent: On":L"Independent: Off");set(clip,clip_?L"Clip to shorter: On":L"Clip to shorter: Off");
    const bool available=!busy();for(int id:{matrix,refreshButton})EnableWindow(controls_.at(id),available);EnableWindow(controls_.at(earlier),available&&startOrder_>0);EnableWindow(controls_.at(later),available&&startOrder_+64<totalOrders_);EnableWindow(controls_.at(previousTracks),available&&startChannel_>0);EnableWindow(controls_.at(nextTracks),available&&startChannel_+12<totalChannels_);
    for(int id:{openButton,copyButton})EnableWindow(controls_.at(id),available&&fresh()&&validBlock());EnableWindow(controls_.at(pasteButton),available&&fresh()&&validBlock()&&copied_.has_value()&&!copyStale()&&state_.at("document").value("editable",false));EnableWindow(controls_.at(clearCopy),copied_.has_value());
    if(!available&&focus&&owns(focus)&&!GetFocus())pendingFocus_=focus;if(available&&pendingFocus_){const auto previous=pendingFocus_;pendingFocus_=nullptr;if(!GetFocus()&&IsWindowVisible(previous)&&IsWindowEnabled(previous)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(previous);}
    if(columnDpi_!=GetDpiForWindow(window_))columns();
  }
  void fontsChanged()override{if(!controls_.contains(matrix))return;const auto image=ImageList_Create(1,std::max(1,MulDiv(48,GetDpiForWindow(window_),96)),ILC_COLOR32,1,1);if(image){ListView_SetImageList(controls_.at(matrix),image,LVSIL_SMALL);if(rowHeight_)ImageList_Destroy(rowHeight_);rowHeight_=image;}columnDpi_=0;}
  void paint(RenderSurface &surface)override{const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);}
  LRESULT draw(NMLVCUSTOMDRAW &value){
    if(NativeControls::highContrast())return NativeReportList::customDraw(value,[this](size_t r,unsigned c){return cell(r,c);});
    if(value.nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;if(value.nmcd.dwDrawStage!=CDDS_ITEMPREPAINT)return CDRF_DODEFAULT;
    const auto r=size_t(value.nmcd.dwItemSpec);if(r>=orders_.size())return CDRF_DODEFAULT;const auto list=controls_.at(matrix),header=ListView_GetHeader(list);RECT rowRect{},client{};if(!ListView_GetItemRect(list,int(r),&rowRect,LVIR_BOUNDS)||!GetClientRect(list,&client))return CDRF_DODEFAULT;
    const NativeReportList::SavedDC saved(value.nmcd.hdc);if(!saved.saved)return CDRF_DODEFAULT;const auto dc=value.nmcd.hdc;const auto colors=NativeReportList::palette(false);IntersectClipRect(dc,0,0,client.right,client.bottom);auto fill=rowRect;fill.left=0;fill.right=client.right;NativeControls::fill(dc,fill,colors.background);SelectObject(dc,font_);SetBkMode(dc,TRANSPARENT);const auto scale=GetDpiForWindow(list)/96.f;
    for(unsigned c=0;c<=tracks_.size();++c){RECT rect{};Header_GetItemRect(header,c,&rect);MapWindowPoints(header,list,reinterpret_cast<POINT *>(&rect),2);rect.top=rowRect.top;rect.bottom=rowRect.bottom;if(rect.right<=0||rect.left>=client.right)continue;const NativeReportList::SavedDC part(dc);if(!part.saved)continue;IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);const bool selected=orders_[r].id==selectedOrder_&&(c==0||tracks_[c-1].id==selectedTrack_);const bool focused=GetFocus()==list&&IsWindowEnabled(list);if(selected)NativeControls::fill(dc,rect,focused?colors.selected:colors.inactive);SetTextColor(dc,selected&&focused?colors.selectedText:colors.text);rect.left+=int(7*scale);rect.right-=int(7*scale);
      if(c&&c<=orders_[r].blocks.size()){const auto &b=orders_[r].blocks[c-1];const auto peak=std::max(1u,*std::max_element(b.bins.begin(),b.bins.end()));const float width=float(std::max(1L,rect.right-rect.left))/16;for(unsigned bin=0;bin<16;++bin)if(b.bins[bin]){RECT bar{LONG(rect.left+bin*width),rowRect.top+int(24*scale)-int(18*scale*b.bins[bin]/peak),LONG(rect.left+(bin+1)*width)-1,rowRect.top+int(24*scale)};NativeControls::fill(dc,bar,selected?RGB(110,218,197):RGB(82,138,149));}rect.top=rowRect.top+int(25*scale);const auto text=std::to_wstring(b.notes)+L"n / "+std::to_wstring(b.events)+L"e";DrawTextW(dc,text.c_str(),int(text.size()),&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);}
      else if(!c){auto upper=rect;upper.bottom=upper.top+int(25*scale);DrawTextW(dc,orders_[r].title.c_str(),int(orders_[r].title.size()),&upper,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);rect.top=upper.bottom;SetTextColor(dc,colors.headerText);DrawTextW(dc,orders_[r].section.c_str(),int(orders_[r].section.size()),&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);}
      else {const auto text=cell(r,c);DrawTextW(dc,text.c_str(),int(text.size()),&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);}
      if(selected&&c&&focused&&!(SendMessageW(list,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){RECT focusRect{};Header_GetItemRect(header,c,&focusRect);MapWindowPoints(header,list,reinterpret_cast<POINT *>(&focusRect),2);focusRect.top=rowRect.top;focusRect.bottom=rowRect.bottom;InflateRect(&focusRect,-1,-1);DrawFocusRect(dc,&focusRect);}
    }return CDRF_SKIPDEFAULT;
  }
  LRESULT notify(NMHDR *header){
    if(header->hwndFrom!=controls_.at(matrix))return 0;if(header->code==NM_CUSTOMDRAW)return draw(*reinterpret_cast<NMLVCUSTOMDRAW *>(header));
    if(header->code==LVN_GETDISPINFOW){auto &info=*reinterpret_cast<NMLVDISPINFOW *>(header);if((info.item.mask&LVIF_TEXT)&&info.item.pszText&&info.item.cchTextMax>0){const auto text=info.item.iItem>=0&&info.item.iSubItem>=0?cell(size_t(info.item.iItem),unsigned(info.item.iSubItem)):std::wstring();lstrcpynW(info.item.pszText,text.c_str(),info.item.cchTextMax);}return 0;}
    if(setting_||busy())return 0;
    if(header->code==LVN_ITEMCHANGED){const auto &v=*reinterpret_cast<NMLISTVIEW *>(header);if((v.uChanged&LVIF_STATE)&&(v.uNewState&LVIS_SELECTED)&&v.iItem>=0&&size_t(v.iItem)<orders_.size())choose(orders_[size_t(v.iItem)].id,selectedTrack_);}
    if(header->code==NM_CLICK||header->code==NM_DBLCLK){const auto &v=*reinterpret_cast<NMITEMACTIVATE *>(header);if(v.iItem>=0&&size_t(v.iItem)<orders_.size()){choose(orders_[size_t(v.iItem)].id,v.iSubItem>0&&size_t(v.iSubItem)<=tracks_.size()?tracks_[size_t(v.iSubItem-1)].id:selectedTrack_);if(header->code==NM_DBLCLK)openBlock();}}
    if(header->code==LVN_ODFINDITEMW){const auto &v=*reinterpret_cast<NMLVFINDITEMW *>(header);if(!(v.lvfi.flags&LVFI_STRING)||!v.lvfi.psz||orders_.empty())return -1;const auto length=wcslen(v.lvfi.psz);for(size_t i=0;i<orders_.size();++i){const auto r=(size_t(std::max(0,v.iStart))+i)%orders_.size();if(_wcsnicmp(orders_[r].label.c_str(),v.lvfi.psz,length)==0)return LRESULT(r);}return -1;}return 0;
  }
  static LRESULT CALLBACK notifications(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){auto &self=*reinterpret_cast<ArrangementMatrixWindow *>(data);if(m==WM_NCDESTROY)RemoveWindowSubclass(h,notifications,id);try{if(self.ready_&&NativeReportList::themeMessage(m))NativeReportList::refresh(self.controls_.at(matrix));if(m==refreshMessage){const bool queued=self.queued_;self.queued_=false;if(queued&&self.visible()&&self.automaticRead())self.refresh();return 0;}if(m==WM_NOTIFY&&self.ready_)return self.notify(reinterpret_cast<NMHDR *>(l));}catch(const std::exception &e){self.error(e);}return DefSubclassProc(h,m,w,l);}
  static LRESULT CALLBACK headers(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){auto &self=*reinterpret_cast<ArrangementMatrixWindow *>(data);if(m==WM_NCDESTROY)RemoveWindowSubclass(h,headers,id);if(m==WM_NOTIFY){auto header=reinterpret_cast<NMHDR *>(l);if(header->hwndFrom==ListView_GetHeader(h)){if(header->code==NM_CUSTOMDRAW)return NativeReportList::headerDraw(*reinterpret_cast<NMCUSTOMDRAW *>(header));if((header->code==HDN_ENDTRACKW||header->code==HDN_ENDTRACKA)&&!self.setting_){const auto &change=*reinterpret_cast<NMHEADERW *>(header);if(change.iItem>=0&&size_t(change.iItem)<=self.tracks_.size()&&change.pitem&&(change.pitem->mask&HDI_WIDTH))self.widths_[change.iItem?self.tracks_[size_t(change.iItem-1)].id:std::string()]=std::max(48.f,change.pitem->cxy*96.f/GetDpiForWindow(h));}}}return DefSubclassProc(h,m,w,l);}
public:
  ArrangementMatrixWindow(HWND owner,Callbacks callbacks):NativeToolWindow(owner),callbacks_(std::move(callbacks)){
    minimumClientWidth_=900;minimumClientHeight_=620;INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};if(!InitCommonControlsEx(&common))throw std::runtime_error("Cannot initialize matrix list");create(L"ScreamSeq.ArrangementMatrix",L"Arrangement matrix",1080,760);
    const auto dpi=GetDpiForWindow(window_);RECT outer{0,0,MulDiv(1080,dpi,96),MulDiv(760,dpi,96)};
    if(!AdjustWindowRectExForDpi(&outer,DWORD(GetWindowLongPtrW(window_,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window_,GWL_EXSTYLE)),dpi))throw std::runtime_error("Cannot size matrix window");
    RECT current{};GetWindowRect(window_,&current);MONITORINFO monitor{sizeof(monitor)};if(GetMonitorInfoW(MonitorFromWindow(owner_,MONITOR_DEFAULTTONEAREST),&monitor)){const auto &work=monitor.rcWork;const int width=std::min(outer.right-outer.left,work.right-work.left),height=std::min(outer.bottom-outer.top,work.bottom-work.top);SetWindowPos(window_,nullptr,std::clamp(current.left,work.left,work.right-width),std::clamp(current.top,work.top,work.bottom-height),width,height,SWP_NOZORDER|SWP_NOACTIVATE);}
    const auto list=add(matrix,WC_LISTVIEWW,L"Arrangement matrix",LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_BORDER);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);NativeReportList::install(list);
    if(const auto library=LoadLibraryW(L"uxtheme.dll")){using Theme=HRESULT(WINAPI *)(HWND,LPCWSTR,LPCWSTR);if(const auto theme=reinterpret_cast<Theme>(GetProcAddress(library,"SetWindowTheme")))theme(list,L"",L"");FreeLibrary(library);}
    for(const auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{earlier,L"Earlier orders"},{later,L"Later orders"},{previousTracks,L"Previous tracks"},{nextTracks,L"Next tracks"},{refreshButton,L"Refresh (F5)"},{openButton,L"Open block"},{copyButton,L"Copy block"},{pasteButton,L"Paste block"},{independent,L"Independent: On"},{clip,L"Clip to shorter: Off"},{clearCopy,L"Clear copy"},{returnPattern,L"Return to pattern (F6)"},{close,L"Close"}})button(id,text);
    combo(mode);for(const auto text:{L"Overwrite",L"Merge",L"Mix into empty fields"})SendMessageW(controls_.at(mode),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));SendMessageW(controls_.at(mode),CB_SETCURSEL,0,0);
    for(const auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"ARRANGEMENT MATRIX"},{pageLabel,L""},{selectionLabel,L""},{copyLabel,L""},{statusLabel,L""},{helpLabel,L"n = pitched notes; e = all stored events.\nIndependent clones a shared destination. One Undo restores Paste."}})label(id,text);
    for(int id:{pageLabel,selectionLabel,copyLabel})SetWindowLongPtrW(controls_.at(id),GWL_STYLE,GetWindowLongPtrW(controls_.at(id),GWL_STYLE)|SS_ENDELLIPSIS|SS_NOPREFIX);
    if(!SetWindowSubclass(window_,notifications,subclassID,reinterpret_cast<DWORD_PTR>(this))||!SetWindowSubclass(list,headers,subclassID,reinterpret_cast<DWORD_PTR>(this)))throw std::runtime_error("Cannot initialize matrix notifications");finish();statusText();
  }
  ~ArrangementMatrixWindow()override{if(window_)RemoveWindowSubclass(window_,notifications,subclassID);if(controls_.contains(matrix)){const auto list=controls_.at(matrix);RemoveWindowSubclass(list,headers,subclassID);ListView_SetImageList(list,nullptr,LVSIL_SMALL);}if(rowHeight_)ImageList_Destroy(rowHeight_);}
  void update(const Json &state){const auto document=identity(state.at("documentId")),revision=identity(state.at("revision"));const auto seq=sequence(state);const auto &doc=state.at("document");if(!state.at("busy").is_boolean()||!doc.at("editable").is_boolean())throw std::runtime_error("Invalid matrix state");const auto channels=doc.contains("channels")?integer(doc.at("channels")):doc.contains("tracks")?unsigned(doc.at("tracks").size()):totalChannels_;if(!channels||channels>65536)throw std::runtime_error("Invalid matrix track count");if(doc.contains("orders")&&(!doc.at("orders").is_array()||doc.at("orders").size()>65536))throw std::runtime_error("Invalid matrix order count");const auto selectedOrder=state.value("selectedOrderID",std::string()),selectedTrack=state.value("selectedTrackID",std::string());const bool first=!loaded_,replacement=loaded_&&(state_.at("documentId")!=document||sequence(state_)!=seq);if(first||replacement)++contextGeneration_;state_=state;loaded_=true;totalChannels_=channels;if(doc.contains("orders"))totalOrders_=unsigned(doc.at("orders").size());if(first){selectedOrder_=selectedOrder;selectedTrack_=selectedTrack;if(doc.contains("orderMetadata"))for(size_t i=0;i<doc.at("orderMetadata").size();++i)if(doc.at("orderMetadata")[i].at("id")==selectedOrder_)startOrder_=unsigned(i)/64*64;if(doc.contains("tracks"))for(const auto &t:doc.at("tracks"))if(t.at("id")==selectedTrack_)startChannel_=integer(t.at("index"))/12*12;}if(replacement){startOrder_=startChannel_=0;selectedOrder_.clear();selectedTrack_.clear();}statusText();layout();schedule();}
  void refresh(){if(!loaded_)return;const unsigned order=totalOrders_?std::min(startOrder_,(totalOrders_-1)/64*64):0,channel=totalChannels_?std::min(startChannel_,(totalChannels_-1)/12*12):0;load(order,channel,orders_.empty()||viewDocument_!=state_.at("documentId").get<std::string>()||viewSequence_!=sequence(state_));}
  void open(const Json &state){const bool first=!opened_;const auto focus=GetFocus();const bool retained=visible()&&owns(focus);update(state);if(first){opened_=true;clampToOwnerWorkArea();}show();const auto previous=retained?focus:retainedFocus_;if(previous&&IsWindowVisible(previous)&&IsWindowEnabled(previous))SetFocus(previous);else SetFocus(controls_.at(matrix));if(automaticRead())refresh();}
  void hide()override{const auto focus=GetFocus();const bool owned=owns(focus);if(owned)retainedFocus_=focus;NativeToolWindow::hide();if(owned&&callbacks_.returnToPattern)callbacks_.returnToPattern();}
  Json snapshot()const{
    Json bounds=Json::array();const auto scale=96.f/GetDpiForWindow(window_);for(const auto &[id,control]:controls_){RECT r{};GetWindowRect(control,&r);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT *>(&r),2);bounds.push_back({{"id",id},{"x",r.left*scale},{"y",r.top*scale},{"width",(r.right-r.left)*scale},{"height",(r.bottom-r.top)*scale},{"visible",IsWindowVisible(control)!=FALSE},{"enabled",IsWindowEnabled(control)!=FALSE}});}
    Json copied=nullptr;if(copied_)copied={{"documentId",copied_->document},{"revision",copied_->revision},{"sequenceID",copied_->sequence},{"orderID",copied_->orderID},{"trackID",copied_->trackID},{"order",copied_->order},{"channel",copied_->channel},{"label",utf8(copied_->label)},{"generation",copied_->generation},{"stale",copyStale()}};
    return {{"visible",visible()},{"pending",pending_},{"busy",busy()},{"documentId",state_.value("documentId",std::string())},{"revision",state_.value("revision",std::string())},{"viewRevision",viewRevision_},{"fresh",fresh()},{"startOrder",startOrder_},{"startChannel",startChannel_},{"orderCount",orders_.size()},{"trackCount",tracks_.size()},{"selectedOrderID",selectedOrder_},{"selectedTrackID",selectedTrack_},{"mode",mode_==1?"merge":mode_==2?"mix":"overwrite"},{"makeUnique",unique_},{"clip",clip_},{"copied",std::move(copied)},{"topIndex",ListView_GetTopIndex(controls_.at(matrix))},{"status",utf8(status_)},{"error",error_},{"controlBounds",std::move(bounds)}};
  }
};
}
