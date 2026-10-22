template<typename T,typename U>ScreamSeq::ComPtr<T> accessibleInterface(U *source) {
    ScreamSeq::ComPtr<T> result;ScreamSeq::check(source->QueryInterface(IID_PPV_ARGS(&result)),"Query accessibility interface");return result;
}
void verifyNativeFocusField(IUIAutomationElement *focused,HWND expected,const std::wstring &expectedText) {
    restoreCheck(focused!=nullptr,"UIA lost focused native field");
    UIA_HWND handle{};ScreamSeq::check(focused->get_CurrentNativeWindowHandle(&handle),"Read focused native HWND");
    restoreCheck(reinterpret_cast<HWND>(handle)==expected,"UIA returned a different HWND instead of the focused native field");
    ScreamSeq::ComPtr<IUIAutomationValuePattern> value;
    ScreamSeq::check(focused->GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(&value)),"Focused field retained its native Value pattern");
    BSTR text=nullptr;ScreamSeq::check(value->get_CurrentValue(&text),"Read focused native edit value");
    const bool sameValue=(text?std::wstring_view(text,SysStringLen(text)):std::wstring_view())==expectedText;SysFreeString(text);
    restoreCheck(sameValue,"Focus handoff changed or concealed the native edit value");
    ScreamSeq::ComPtr<IUIAutomationTextPattern> pattern;
    ScreamSeq::check(focused->GetCurrentPatternAs(UIA_TextPatternId,IID_PPV_ARGS(&pattern)),"Focused field retained native Text/selection support");
    ScreamSeq::ComPtr<IUIAutomationTextRangeArray> selected;
    ScreamSeq::check(pattern->GetSelection(&selected),"Read focused native text selection");
    restoreCheck(bool(selected),"Focus handoff omitted native text selection");
}
// Read-only foreground gate, explicitly separate from never-switched private
// desktop fixtures. Computer Use must focus the QA field before invoking this;
// this mode never launches an app, activates a window or supplies input.
void inspectForegroundNativeFocus(DWORD pid,HWND root,HWND field) {
    DWORD rootPid=0,fieldPid=0;const auto rootThread=GetWindowThreadProcessId(root,&rootPid);
    const auto fieldThread=GetWindowThreadProcessId(field,&fieldPid);
    restoreCheck(pid&&rootThread&&fieldThread==rootThread&&rootPid==pid&&fieldPid==pid,"Foreground focus target is not the specified QA process");
    auto owner=GetAncestor(field,GA_ROOT);
    for(unsigned depth=0;owner&&owner!=root&&depth<64;++depth)owner=GetWindow(owner,GW_OWNER);
    restoreCheck(owner==root&&field!=root,"Foreground field is not owned by the specified QA Main window");
    GUITHREADINFO native{sizeof(native)};restoreCheck(GetGUIThreadInfo(fieldThread,&native)&&native.hwndFocus==field,"QA native keyboard focus differs from the requested field");
    ScreamSeq::check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Initialize foreground UIA observer");
    struct Com{~Com(){CoUninitialize();}}com;
    ScreamSeq::ComPtr<IUIAutomation2> client;ScreamSeq::check(CoCreateInstance(CLSID_CUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&client)),"Create foreground UIA observer");
    ScreamSeq::check(client->put_ConnectionTimeout(3000),"Bound foreground UIA connection");
    ScreamSeq::check(client->put_TransactionTimeout(3000),"Bound foreground UIA transaction");
    ScreamSeq::ComPtr<IUIAutomationElement> direct,focused;
    ScreamSeq::check(client->ElementFromHandle(field,&direct),"Read expected foreground field");
    ScreamSeq::ComPtr<IUIAutomationValuePattern> value;
    ScreamSeq::check(direct->GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(&value)),"Read expected foreground Value pattern");
    BSTR text=nullptr;ScreamSeq::check(value->get_CurrentValue(&text),"Read expected foreground value");
    const std::wstring expectedText=text?std::wstring(text,SysStringLen(text)):L"";SysFreeString(text);
    ScreamSeq::check(client->GetFocusedElement(&focused),"Read actual global UIA keyboard focus");
    verifyNativeFocusField(focused.Get(),field,expectedText);
    BOOL hasFocus=FALSE;ScreamSeq::check(focused->get_CurrentHasKeyboardFocus(&hasFocus),"Read global UIA focus property");
    restoreCheck(hasFocus!=FALSE,"Global UIA element did not advertise keyboard focus");
    restoreCheck(GetGUIThreadInfo(fieldThread,&native)&&native.hwndFocus==field,"QA focus moved during the read-only assertion");
    std::cout<<RestoreJson{{"status","passed"},{"pid",pid},{"root",reinterpret_cast<uintptr_t>(root)},
        {"field",reinterpret_cast<uintptr_t>(field)},{"globalFocusExact",true},{"nativeFocusExact",true},
        {"valuePattern",true},{"textPattern",true},{"selection",true},{"readOnly",true}}.dump()<<std::endl;
}
void patternAccessibilityClient(HWND root,HWND nativeChild,HWND namedField) {
    // UIA clients run on an MTA worker; the main STA pumps normal messages. This
    // exercises WM_GETOBJECT, UIA marshaling and native/virtual child merging.
    auto pending=std::async(std::launch::async,[root,nativeChild,namedField] {
        ScreamSeq::check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"Initialize UIA client COM");
        struct Com{~Com(){CoUninitialize();}} com;
        ScreamSeq::ComPtr<IUIAutomation2> client;ScreamSeq::check(CoCreateInstance(CLSID_CUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&client)),"Create native UIA client");
        ScreamSeq::check(client->put_ConnectionTimeout(3000),"Bound UIA connection wait");ScreamSeq::check(client->put_TransactionTimeout(3000),"Bound UIA transaction wait");
        ScreamSeq::ComPtr<IUIAutomationElement> window;ScreamSeq::check(client->ElementFromHandle(root,&window),"Read UIA main HWND");
        VARIANT id{};id.vt=VT_BSTR;id.bstrVal=SysAllocString(L"pattern-grid");restoreCheck(id.bstrVal,"Allocate UIA grid condition");
        ScreamSeq::ComPtr<IUIAutomationCondition> condition;const auto result=client->CreatePropertyCondition(UIA_AutomationIdPropertyId,id,&condition);VariantClear(&id);ScreamSeq::check(result,"Create UIA grid condition");
        ScreamSeq::ComPtr<IUIAutomationElement> grid;ScreamSeq::check(window->FindFirst(TreeScope_Children,condition.Get(),&grid),"Find virtual grid beside native HWNDs");restoreCheck(bool(grid),"UIA main tree lost its virtual pattern grid");
        VARIANT handle{};handle.vt=VT_I4;handle.lVal=LONG(reinterpret_cast<INT_PTR>(nativeChild));condition.Reset();
        ScreamSeq::check(client->CreatePropertyCondition(UIA_NativeWindowHandlePropertyId,handle,&condition),"Create native HWND condition");
        ScreamSeq::ComPtr<IUIAutomationElement> native;ScreamSeq::check(window->FindFirst(TreeScope_Children,condition.Get(),&native),"Find native child beside virtual grid");restoreCheck(bool(native),"Custom provider hid the native main-window controls");
        ScreamSeq::ComPtr<IUIAutomationElement> field;ScreamSeq::check(client->ElementFromHandle(namedField,&field),"Read named native field through UIA bridge");
        BSTR fieldName=nullptr;const auto nameResult=field->get_CurrentName(&fieldName);const bool named=fieldName&&std::wstring_view(fieldName,SysStringLen(fieldName))==L"Song title";
        SysFreeString(fieldName);ScreamSeq::check(nameResult,"Read explicit native UIA Name");restoreCheck(named,"UIA bridge lost the annotated native edit name");
        ScreamSeq::ComPtr<IUIAutomationValuePattern> originalValue;
        ScreamSeq::check(field->GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(&originalValue)),"Read native edit Value pattern before focus handoff");
        BSTR originalText=nullptr;ScreamSeq::check(originalValue->get_CurrentValue(&originalText),"Read native edit before focus handoff");
        const std::wstring expectedText=originalText?std::wstring(originalText,SysStringLen(originalText)):L"";SysFreeString(originalText);
        // Exercise marshaled focus and native patterns on the private desktop.
        // Global GetFocusedElement observes the input desktop, not this one;
        // its exact-HWND assertion lives in the mandatory foreground gate above.
        ScreamSeq::check(field->SetFocus(),"Focus named native field through UIA");
        GUITHREADINFO input{sizeof(input)};
        restoreCheck(GetGUIThreadInfo(GetWindowThreadProcessId(namedField,nullptr),&input)&&input.hwndFocus==namedField,
            "Marshaled UIA focus did not reach the exact private-desktop native field");
        verifyNativeFocusField(field.Get(),namedField,expectedText);
        ScreamSeq::ComPtr<IUIAutomationGridPattern> pattern;ScreamSeq::check(grid->GetCurrentPatternAs(UIA_GridPatternId,IID_PPV_ARGS(&pattern)),"Read UIA Grid pattern");
        ScreamSeq::ComPtr<IUIAutomationElement> cell;ScreamSeq::check(pattern->GetItem(2,0,&cell),"Read actual UIA pattern cell");
        ScreamSeq::ComPtr<IUIAutomationValuePattern> value;ScreamSeq::check(cell->GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(&value)),"Read cell Value pattern");
        BSTR text=nullptr;const auto read=value->get_CurrentValue(&text);const bool hasValue=text&&SysStringLen(text)>0;SysFreeString(text);ScreamSeq::check(read,"Read UIA cell music");restoreCheck(hasValue,"UIA cell omitted music text");
        BOOL readOnly=FALSE;ScreamSeq::check(value->get_CurrentIsReadOnly(&readOnly),"Read cell write policy");restoreCheck(readOnly!=FALSE,"Descriptive grid value advertised unguarded text editing");
        ScreamSeq::check(cell->SetFocus(),"Focus an actual UIA grid cell");
    });
    const auto deadline=GetTickCount64()+30000;
    while(pending.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
        restoreCheck(GetTickCount64()<deadline,"UIA client did not complete within its bounded transaction window");
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){restoreCheck(message.message!=WM_QUIT,"UIA client unexpectedly closed its fixture");TranslateMessage(&message);DispatchMessageW(&message);}
        MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
    }
    pending.get();
}
void applicationPatternAccessibility() {
    RestoreFixture fixture;auto &app=*fixture.app;app.command(patternCommand);SetFocus(app.window);
    app.edit("pattern.notes.set",{{"pattern",app.patternIndex},{"events",RestoreJson::array({{{"channel",0},{"position",2*65536+16384},{"note",61},{"instrument",7},{"velocity",99}}})}});
    app.edit("pattern.effect.set",{{"pattern",app.patternIndex},{"row",2},{"channel",0},{"column",0},{"command",{{"kind","native"},{"native","vibrato"},
        {"parameters",{{"depth",.123456789012345},{"rate",2.3456789012345},{"rateMode","beat"},{"shape","triangle"},{"phase",.125},{"reset",true}}},{"offset",1234},{"duration",70001}}}});
    app.openPatternTools();auto *tool=app.patternToolsWindow.get();const auto field=GetDlgItem(tool->window(),8006);ScreamSeq::Tests::ownGuiWindow(tool->window());
    rawDraftText(field,L"-");SetFocus(field);const auto draft=tool->snapshot().at("draft"),captured=tool->snapshot().at("captured");
    restoreCheck(bool(app.patternAccessibility),"Missing native pattern accessibility host");
    const auto before=songState(app),cursor=app.position(),selection=app.selection();
    auto root=accessibleInterface<IRawElementProviderFragment>(app.patternAccessibility->provider());
    ScreamSeq::ComPtr<IRawElementProviderFragment> gridNode;ScreamSeq::check(root->Navigate(NavigateDirection_FirstChild,&gridNode),"Read virtual grid");restoreCheck(bool(gridNode),"Visible pattern grid missing from root");
    auto grid=accessibleInterface<IGridProvider>(gridNode.Get());int rows=0,columns=0;
    ScreamSeq::check(grid->get_RowCount(&rows),"Read pattern row count");ScreamSeq::check(grid->get_ColumnCount(&columns),"Read pattern column count");
    restoreCheck(rows==int(app.patternRows())&&columns==int(app.view->channels),"Accessible dimensions differ from actual pattern");
    ScreamSeq::ComPtr<IRawElementProviderSimple> musical;ScreamSeq::check(grid->GetItem(2,0,&musical),"Read precise/typed cell");
    auto musicalValue=accessibleInterface<IValueProvider>(musical.Get());BSTR music=nullptr;ScreamSeq::check(musicalValue->get_Value(&music),"Read precise/typed music text");
    const std::wstring musicText=music?std::wstring(music,SysStringLen(music)):L"";SysFreeString(music);
    restoreCheck(musicText.find(L"at +0.25 rows")!=std::wstring::npos&&musicText.find(L"velocity 99")!=std::wstring::npos&&
        musicText.find(L"2.3456789012345")!=std::wstring::npos&&musicText.find(L"triangle")!=std::wstring::npos,
        "Accessible music omitted precise timing/velocity or rounded typed effect fields to painted widths");
    ScreamSeq::ComPtr<IRawElementProviderSimple> invalid;restoreCheck(grid->GetItem(-1,0,&invalid)==E_INVALIDARG&&!invalid,"Negative grid row was accepted");
    restoreCheck(grid->GetItem(rows,0,&invalid)==E_INVALIDARG&&!invalid,"Out-of-range grid row was accepted");
    ScreamSeq::ComPtr<IRawElementProviderSimple> cell;ScreamSeq::check(grid->GetItem(rows-1,columns-1,&cell),"Read virtual offscreen cell");
    auto value=accessibleInterface<IValueProvider>(cell.Get());BSTR text=nullptr;ScreamSeq::check(value->get_Value(&text),"Read actual cell contents");
    const std::wstring spoken=text?std::wstring(text,SysStringLen(text)):L"";SysFreeString(text);restoreCheck(spoken.find(L"Note ")==0,"Accessible cell omitted its musical contents");
    restoreCheck(value->SetValue(L"C-4")==UIA_E_INVALIDOPERATION,"Read-only description accepted a musical write");
    auto item=accessibleInterface<IGridItemProvider>(cell.Get());int r=-1,c=-1;ScreamSeq::check(item->get_Row(&r),"Read virtual row");ScreamSeq::check(item->get_Column(&c),"Read virtual column");
    restoreCheck(r==rows-1&&c==columns-1,"GridItem indices disagree with Grid.GetItem");
    auto scroll=accessibleInterface<IScrollItemProvider>(cell.Get());ScreamSeq::check(scroll->ScrollIntoView(),"Reveal virtual cell");
    auto scrolledCursor=cursor;scrolledCursor["following"]=false;
    restoreCheck(app.position()==scrolledCursor&&app.selection()==selection&&GetFocus()==field&&songState(app)==before,"Accessibility scrolling changed cursor, selection, focus or song history");
    auto fragment=accessibleInterface<IRawElementProviderFragment>(cell.Get());UiaRect bounds{};ScreamSeq::check(fragment->get_BoundingRectangle(&bounds),"Read clipped cell bounds");
    restoreCheck(bounds.width>0&&bounds.height>0,"Revealed cell has no screen bounds");
    auto rootFragment=accessibleInterface<IRawElementProviderFragmentRoot>(app.patternAccessibility->provider());ScreamSeq::ComPtr<IRawElementProviderFragment> hit;
    const auto windowBounds=app.accessiblePatternInfo().windowBounds;
    ScreamSeq::check(rootFragment->ElementProviderFromPoint(windowBounds.left-1,windowBounds.top-1,&hit),"Hit test outside the fragment");
    restoreCheck(!hit,"Pattern provider claimed another window's screen point");
    ScreamSeq::check(rootFragment->ElementProviderFromPoint(std::numeric_limits<double>::quiet_NaN(),windowBounds.top,&hit),"Hit test invalid screen point");
    restoreCheck(!hit,"Pattern provider claimed a non-finite screen point");
    ScreamSeq::check(rootFragment->ElementProviderFromPoint(bounds.left+bounds.width/2,bounds.top+bounds.height/2,&hit),"Hit test accessible grid geometry");
    auto hitItem=accessibleInterface<IGridItemProvider>(hit.Get());ScreamSeq::check(hitItem->get_Row(&r),"Read hit row");ScreamSeq::check(hitItem->get_Column(&c),"Read hit column");restoreCheck(r==rows-1&&c==columns-1,"UIA hit test disagrees with rendered row/channel geometry");
    ScreamSeq::check(fragment->SetFocus(),"Focus virtual cell through native cursor navigation");
    restoreCheck(app.row==unsigned(rows-1)&&app.channel==unsigned(columns-1)&&!app.follow&&GetFocus()==app.window&&songState(app)==before,"UIA focus failed to navigate or changed music");
    app.busy=true;restoreCheck(fragment->SetFocus()==UIA_E_ELEMENTNOTENABLED,"Busy application accepted accessibility navigation");app.busy=false;
    const auto wrongThread=std::async(std::launch::async,[fragment]{UiaRect unused{};return fragment->get_BoundingRectangle(&unused);}).get();
    restoreCheck(wrongThread==RPC_E_WRONG_THREAD,"Provider read Application from a foreign thread");
    app.documentOperation("document.patch",{{"expectedRevision",app.view->session.revision},{"title","Accessibility revision guard"}});
    const auto changed=songState(app),changedCursor=app.position();
    restoreCheck(fragment->SetFocus()==UIA_E_ELEMENTNOTAVAILABLE&&scroll->ScrollIntoView()==UIA_E_ELEMENTNOTAVAILABLE&&app.position()==changedCursor&&songState(app)==changed,"Old UIA cell acted against a newer song revision");
    app.command(songPropertiesCommand);ScreamSeq::Tests::ownGuiWindow(app.songPropertiesWindow->window());
    const auto songTitle=GetDlgItem(app.songPropertiesWindow->window(),8601);
    restoreCheck(GetFocus()==songTitle,"Song settings did not focus its native title field");
    restoreCheck(GetAncestor(songTitle,GA_ROOT)==app.songPropertiesWindow->window()&&
        GetWindow(app.songPropertiesWindow->window(),GW_OWNER)==app.window&&
        (GetWindowLongPtrW(app.songPropertiesWindow->window(),GWL_STYLE)&WS_CHILD)==0,
        "Accessibility fixture must retain the production floating inspector owner chain");
    ScreamSeq::ComPtr<IRawElementProviderFragment> nativeFocus;
    const auto nativeFocusResult=rootFragment->GetFocus(&nativeFocus);
    if(FAILED(nativeFocusResult)||!nativeFocus)std::cout<<"Native focus delegation: result="<<nativeFocusResult
        <<" focus="<<GetFocus()<<" expected="<<songTitle<<" parentRoot="<<GetAncestor(songTitle,GA_ROOT)
        <<" rootOwner="<<GetAncestor(songTitle,GA_ROOTOWNER)<<" explicitOwner="<<GetWindow(GetAncestor(songTitle,GA_ROOT),GW_OWNER)
        <<" Main="<<app.window<<std::endl;
    ScreamSeq::check(nativeFocusResult,"Delegate native field focus from the pattern root");
    restoreCheck(bool(nativeFocus),"Pattern root omitted its focused native inspector");
    auto nativeProvider=accessibleInterface<IRawElementProviderSimple>(nativeFocus.Get());VARIANT nativeHandle{};
    ScreamSeq::check(nativeProvider->GetPropertyValue(UIA_NativeWindowHandlePropertyId,&nativeHandle),"Read native focus handoff HWND");
    const bool exactHandle=nativeHandle.vt==VT_I4&&LongToHandle(nativeHandle.lVal)==songTitle;VariantClear(&nativeHandle);
    restoreCheck(exactHandle,"Pattern focus handoff returned a different native HWND");
    ScreamSeq::ComPtr<IRawElementProviderSimple> nativeHost;
    ScreamSeq::check(nativeProvider->get_HostRawElementProvider(&nativeHost),"Get focused field's native HWND host");
    restoreCheck(bool(nativeHost),"Native focus handoff omitted its HWND host");
    // Same UI thread is insufficient: another top-level window must not be
    // claimed by Main's fragment merely because it owns keyboard focus.
    const auto unrelated=CreateWindowExW(0,L"STATIC",L"Owned unrelated accessibility fixture",
        WS_OVERLAPPEDWINDOW,10,10,240,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    restoreCheck(unrelated!=nullptr,"Create unrelated native focus fixture");ScreamSeq::Tests::ownGuiWindow(unrelated);
    struct CloseUnrelated{HWND window;~CloseUnrelated(){DestroyWindow(window);}} closeUnrelated{unrelated};
    const auto unrelatedField=CreateWindowExW(0,L"EDIT",L"Unrelated field",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
        10,10,160,24,unrelated,nullptr,GetModuleHandleW(nullptr),nullptr);
    restoreCheck(unrelatedField!=nullptr,"Create unrelated field");ScreamSeq::Tests::ownGuiWindow(unrelatedField);
    ShowWindow(unrelated,SW_SHOWNOACTIVATE);SetActiveWindow(unrelated);SetFocus(unrelatedField);
    restoreCheck(GetFocus()==unrelatedField,"Unrelated focus fixture did not acquire native focus");
    ScreamSeq::ComPtr<IRawElementProviderFragment> foreignFocus;
    ScreamSeq::check(rootFragment->GetFocus(&foreignFocus),"Read unrelated native focus");
    restoreCheck(!foreignFocus,"Pattern root claimed another top-level window's native field");
    ShowWindow(unrelated,SW_HIDE);SetActiveWindow(app.songPropertiesWindow->window());SetFocus(songTitle);
    patternAccessibilityClient(app.window,app.controls.at(patternChooser),songTitle);
    restoreCheck(app.row==2&&app.channel==0&&GetFocus()==app.window&&songState(app)==changed,"Marshaled UIA navigation failed or changed song/history");
    restoreCheck(tool->snapshot().at("draft")==draft&&tool->snapshot().at("captured")==captured&&restoreText(field)==L"-","UIA navigation overwrote or rebased a retained invalid inspector draft");
    ShowWindow(app.window,SW_HIDE);restoreCheck(grid->GetItem(0,0,&invalid)==UIA_E_ELEMENTNOTAVAILABLE,"Hidden pattern accepted a new virtual item");ShowWindow(app.window,SW_SHOWNOACTIVATE);
    fixture.close();text=nullptr;
    restoreCheck(value->get_Value(&text)==UIA_E_ELEMENTNOTAVAILABLE&&!text,"Retired HWND provider accessed its destroyed Application");
    UiaRect retiredBounds{};restoreCheck(nativeFocus->get_BoundingRectangle(&retiredBounds)==UIA_E_ELEMENTNOTAVAILABLE,
        "Retired native focus handoff accessed a destroyed or reused HWND");
}
