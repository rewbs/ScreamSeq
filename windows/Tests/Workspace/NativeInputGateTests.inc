// Uses real HWND classes and editor subclasses on the fixture's private desktop.
// The gate is installed after custom handling, before final draft capture/admit.
struct InputGateProbe {
  HWND other{};
  unsigned keys=0,commands=0,focus=0,reentrant=0,textCalls=0;
  bool recurse=false;
  static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR opaque) {
    auto &probe=*reinterpret_cast<InputGateProbe *>(opaque);
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,procedure,id);
    if(m==WM_KEYDOWN)++probe.keys;
    if(m==WM_COMMAND||m==WM_NOTIFY)++probe.commands;
    if(m==WM_KILLFOCUS)++probe.focus;
    if(m==WM_SETTEXT)++probe.textCalls;
    if(m==WM_SETTEXT&&probe.recurse) {
      probe.recurse=false;++probe.reentrant;
      // Same HWND/message/arguments cannot reuse the consumed permit. Also
      // exercise a sibling edit, direct notification and combo selection.
      SendMessageW(h,m,w,l);
      SetWindowTextW(probe.other,L"reentrant corruption");
      SendMessageW(GetParent(h),WM_COMMAND,MAKEWPARAM(1,EN_CHANGE),reinterpret_cast<LPARAM>(h));
    }
    return DefSubclassProc(h,m,w,l);
  }
};
void nativeInputGate(HWND main) {
  using ScreamSeq::NativeInputGate;
  Tool docked(main),floating(main);
  docked.dock(main);docked.dockBounds(0,0,640,420);docked.show();floating.show();
  const auto edit=docked.control(1),other=floating.control(1),combo=docked.control(3);
  SetFocus(edit);SendMessageW(edit,EM_SETSEL,2,7);
  InputGateProbe probe;probe.other=other;
  for(auto h:{docked.window(),floating.window(),edit})
    require(SetWindowSubclass(h,InputGateProbe::procedure,77,reinterpret_cast<DWORD_PTR>(&probe)),"Install input-boundary probe");
  const std::array roots{main,floating.window()};
  const auto initialLayouts=docked.layouts;
  {
    NativeInputGate gate(roots);
    require(NativeInputGate::protectedWindow(edit)&&NativeInputGate::protectedWindow(other),"Input lease missed docked or floating controls");
    // No modal disabling is involved. Both queued and synchronous entry points
    // must be stopped before raw values or editor handlers can change.
    require(IsWindowEnabled(edit)&&IsWindowEnabled(other),"Input fixture relies on disabled HWND behavior");
    SetWindowTextW(edit,L"overwritten");
    require(text(edit)==L"Captured draft"&&probe.textCalls==0,"Direct raw-text replacement bypassed input lease");
    require(SendMessageW(edit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"also overwritten"))==0,
        "Rejected WM_SETTEXT reported acceptance");
    SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"replacement"));
    SendMessageW(edit,EM_SETSEL,0,-1);
    SendMessageW(edit,WM_CHAR,'x',0);SendMessageW(edit,WM_PASTE,0,0);SendMessageW(edit,WM_UNDO,0,0);
    SendMessageW(edit,WM_KEYDOWN,VK_F6,0);SendMessageW(edit,WM_KILLFOCUS,reinterpret_cast<WPARAM>(other),0);
    SendMessageW(docked.window(),WM_COMMAND,MAKEWPARAM(2,BN_CLICKED),reinterpret_cast<LPARAM>(docked.control(2)));
    SendMessageW(docked.control(2),BM_CLICK,0,0);
    require(SendMessageW(combo,CB_SETCURSEL,0,0)==CB_ERR,"Direct combo selection bypassed input lease");
    require(SendMessageW(combo,CB_RESETCONTENT,0,0)==CB_ERR,"Combo destruction bypassed input lease");
    PostMessageW(edit,WM_CHAR,'q',0);MSG queued{};
    require(PeekMessageW(&queued,edit,WM_CHAR,WM_CHAR,PM_REMOVE)!=FALSE,"Queued character was not available");
    DispatchMessageW(&queued);
    DWORD first=0,last=0;SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
    require(text(edit)==L"Captured draft"&&first==2&&last==7,"Input lease altered raw text/caret before admission");
    require(SendMessageW(combo,CB_GETCURSEL,0,0)==1&&SendMessageW(combo,CB_GETCOUNT,0,0)==2,"Input lease changed combo contents or selection");
    require(probe.keys==0&&probe.commands==0&&probe.focus==0&&docked.localKeys==0&&docked.layouts==initialLayouts,"Blocked input reached an editor handler");
    rejected([&]{NativeInputGate::present(edit,WM_KEYDOWN,VK_F6,0);},"Refresh authority admitted keyboard input");
    rejected([&]{NativeInputGate::present(docked.window(),WM_COMMAND,2,0);},"Refresh authority admitted arbitrary command");
    {
      // Invalid roots fail closed and roll back only that attempted lease.
      const std::array invalid{floating.window(),HWND{}};
      rejected([&]{NativeInputGate duplicate(invalid);},"Allowed conflicting input leases");
      require(NativeInputGate::protectedWindow(other),"Rejected lease removed existing protection");
    }
    probe.recurse=true;
    ScreamSeq::NativeControls::text(edit,L"New document value");
    ScreamSeq::NativeControls::select(combo,0);
    require(text(edit)==L"New document value"&&text(other)==L"Captured draft"&&probe.reentrant==1&&probe.textCalls==1,"One-shot refresh permitted recursive or sibling mutation");
    require(probe.commands==0&&SendMessageW(combo,CB_GETCURSEL,0,0)==0,"Host refresh leaked notification or failed to update selection");
    NativeInputGate::present(combo,CB_RESETCONTENT);
    require(NativeInputGate::present(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"New song target"))==0,"Guarded selector refresh could not add the new target");
    NativeInputGate::present(combo,CB_SETITEMDATA,0,123);
    NativeInputGate::present(combo,CB_SETCURSEL,0);
    require(SendMessageW(combo,CB_GETCOUNT,0,0)==1&&SendMessageW(combo,CB_GETITEMDATA,0,0)==123,"Guarded selector refresh lost item identity");
    SetWindowTextW(edit,L"after permitted call");
    require(text(edit)==L"New document value"&&probe.textCalls==1,"Refresh permit survived its concrete send");
    // A protected HWND may be retired while the lease survives through host
    // refresh. New fully initialized trees need explicit registration.
    HWND retired{};
    {Tool created(main);retired=created.window();gate.protect(retired);SetWindowTextW(created.control(1),L"wrong");require(text(created.control(1))==L"Captured draft","New native tree missed explicit protection");}
    require(!IsWindow(retired)&&NativeInputGate::protectedWindow(edit),"Retirement released another owner's protection");
  }
  require(!NativeInputGate::protectedWindow(edit)&&!NativeInputGate::protectedWindow(other),"Input lease leaked after refresh");
  SendMessageW(edit,WM_KEYDOWN,VK_F6,0);SetWindowTextW(edit,L"Editable again");
  require(docked.localKeys==1&&probe.keys==1&&text(edit)==L"Editable again","Input did not resume after lease release");
  for(auto h:{docked.window(),floating.window(),edit})RemoveWindowSubclass(h,InputGateProbe::procedure,77);

  // Construction failure after a successfully attached root must unwind it.
  {const std::array invalid{docked.window(),HWND{}};rejected([&]{NativeInputGate gate(invalid);},"Invalid root admitted native input lease");}
  require(!NativeInputGate::protectedWindow(edit),"Failed construction left controls locked");

  INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};require(InitCommonControlsEx(&common),"Initialize list input fixture");
  Window listBox(L"LISTBOX",main),report(WC_LISTVIEWW,main);
  SendMessageW(listBox.value,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"First"));
  SendMessageW(listBox.value,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Second"));
  SendMessageW(listBox.value,LB_SETCURSEL,1,0);
  SetWindowLongPtrW(report.value,GWL_STYLE,GetWindowLongPtrW(report.value,GWL_STYLE)|LVS_REPORT);
  LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t *>(L"Owner");column.cx=100;
  require(ListView_InsertColumn(report.value,0,&column)==0,"Initialize report header");
  LVITEMW item{};item.mask=LVIF_TEXT;item.pszText=const_cast<wchar_t *>(L"Retained row");
  require(ListView_InsertItem(report.value,&item)==0,"Initialize report row");
  ListView_SetItemState(report.value,0,LVIS_SELECTED,LVIS_SELECTED);
  {
    const std::array lists{listBox.value,report.value};NativeInputGate gate(lists);
    SendMessageW(listBox.value,LB_SETCURSEL,0,0);SendMessageW(listBox.value,LB_RESETCONTENT,0,0);
    ListView_SetItemState(report.value,0,0,LVIS_SELECTED);ListView_DeleteAllItems(report.value);
    ListView_SetColumnWidth(report.value,0,12);
    HDITEMW header{};header.mask=HDI_WIDTH;header.cxy=17;Header_SetItem(ListView_GetHeader(report.value),0,&header);
    require(SendMessageW(listBox.value,LB_GETCURSEL,0,0)==1&&SendMessageW(listBox.value,LB_GETCOUNT,0,0)==2,"Native listbox draft changed under input lease");
    require(ListView_GetItemCount(report.value)==1&&(ListView_GetItemState(report.value,0,LVIS_SELECTED)&LVIS_SELECTED),"Report selection or rows changed under input lease");
    require(ListView_GetColumnWidth(report.value,0)==100,"Report header changed under input lease");
    NativeInputGate::present(report.value,LVM_SETCOLUMNWIDTH,0,MAKELPARAM(160,0));
    require(ListView_GetColumnWidth(report.value,0)==160,"Guarded report refresh did not update its native header width");
    LVITEMW deselect{};deselect.stateMask=LVIS_SELECTED;
    NativeInputGate::present(report.value,LVM_SETITEMSTATE,0,reinterpret_cast<LPARAM>(&deselect));
    require(!ListView_GetItemState(report.value,0,LVIS_SELECTED),"Guarded report refresh did not update selection");
    NativeInputGate::present(listBox.value,LB_RESETCONTENT);
    require(NativeInputGate::present(listBox.value,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"New list target"))==0,"Guarded list refresh could not add the new target");
    NativeInputGate::present(listBox.value,LB_SETITEMDATA,0,456);NativeInputGate::present(listBox.value,LB_SETCURSEL,0);
    require(SendMessageW(listBox.value,LB_GETCOUNT,0,0)==1&&SendMessageW(listBox.value,LB_GETITEMDATA,0,0)==456&&SendMessageW(listBox.value,LB_GETCURSEL,0,0)==0,
        "Guarded list refresh lost selection or item identity");
  }
  std::cout<<"Native input lease: raw values, caret, queued keys, focus, commands and reentrant refresh protected\n";
}
