void applicationPluginSearch() {
    RestoreFixture fixture;auto &app=*fixture.app;
    // Establish logical client size independently of desktop DPI/monitor caps.
    resizeRestoreClient(app,1440,900);
    const auto plugin=addRestoreGain(app);
    app.restoreLayoutConfiguration(layoutFor(app,"plugins"));app.selectedPlugin=plugin;
    app.refreshPluginDetails();const auto before=songState(app);
    restoreCheck(app.pluginEditorVisible(),"Plugin search fixture did not expose the rack");
    const auto query=app.controls.at(pluginParameterSearch),selector=app.controls.at(pluginParameter),value=app.controls.at(pluginValue);
    restoreCheck(ScreamSeq::Tests::accessibleName(query)==L"Search parameters by name, unit or ID","Parameter search has no native accessible name");
    // A large deterministic catalogue exercises filtered row indices separately
    // from stable parameter IDs; musical writes still use the actual gain fixture.
    app.pluginParameters=RestoreJson::array();
    for(unsigned i=0;i<200;++i)app.pluginParameters.push_back({{"id",1000+i},{"name",i==179?"Needle frequency":"Band "+std::to_string(i)},
        {"unitLabel","Hz"},{"min",0},{"max",200},{"value",double(i)},{"writable",true}});
    app.selectedParameter=1000;app.filterPluginChoices(false);app.setPluginParameterField();app.layoutControls();
    restoreCheck(IsWindowVisible(query)&&IsWindowVisible(selector)&&IsWindowEnabled(selector),"Plugin search fixture did not expose its search/results controls");
    restoreCheck(GetNextDlgTabItem(app.window,query,FALSE)==selector,"Tab order does not lead from parameter search to results");
    SetFocus(query);rawDraftText(query,L"NEEDLE");
    restoreCheck(SendMessageW(selector,CB_GETCOUNT,0,0)==2&&app.selectedParameter==1000&&GetFocus()==query,
        "Parameter search did not filter or silently changed the selected target/focus");
    restoreCheck(ScreamSeq::NativeControls::itemText(selector,0).find(L"outside search")!=std::wstring::npos,
        "Filtered-out selection was hidden without an explicit retained row");
    SendMessageW(selector,CB_SETCURSEL,1,0);app.pluginCommand(pluginParameter);
    restoreCheck(app.selectedParameter==1179&&restoreText(value)==L"179","Filtered parameter row was used as an unfiltered catalogue index");
    restoreCheck(SendMessageW(selector,CB_GETCOUNT,0,0)==1,"Choosing a matching parameter retained a stale outside-search row");
    rawDraftText(value,L"-");const auto generation=app.pluginDraftGeneration;const auto target=app.pluginDraftTarget;
    SetFocus(query);rawDraftText(query,L"1000");
    restoreCheck(app.pluginDraft&&app.pluginDraftParameter==1179&&app.pluginDraftGeneration==generation&&app.pluginDraftTarget==target&&restoreText(value)==L"-",
        "Searching erased, rebased or retargeted the raw parameter draft");
    SendMessageW(selector,CB_SETCURSEL,0,0);restoreRejected([&]{app.pluginCommand(pluginParameter);});
    restoreCheck(app.selectedParameter==1179&&app.pluginChoiceIndex(pluginParameter)==179&&restoreText(value)==L"-",
        "Rejected parameter selection did not restore the retained filtered target");
    SetFocus(query);restoreCheck(app.key(VK_ESCAPE)&&restoreText(query).empty()&&app.pluginDraft&&restoreText(value)==L"-",
        "Escape in search discarded the parameter draft instead of clearing the query");
    restoreCheck(app.key(VK_RETURN)&&GetFocus()==selector&&app.pluginDraft,"Enter in search applied a parameter instead of focusing results");
    restoreCheck(app.pluginSearchMatches(L"Fréquence",L"FRÉQ"),"Parameter search lost ordinal Unicode case matching");
    app.pluginDraft=false;app.pluginProgramDraft=false;app.pluginParameters=app.documentOperation("plugin.parameters.get",{{"slot",0}});
    app.selectedParameter=1;app.filterPluginChoices(false);app.setPluginParameterField();
    app.pluginDetailPage=1;app.layoutControls();
    app.pluginPrograms=RestoreJson::array({{{"id","keep"},{"name","Current"},{"group","General"},{"loadable",true}},
        {{"id","hidden"},{"name","Other"},{"group","General"},{"loadable",true}},
        {{"id","target"},{"name","Needle program"},{"group","Textures"},{"loadable",false}}});
    app.selectedProgram="keep";app.programCatalogRevision="catalog:stable";app.filterPluginChoices(true);
    const auto programQuery=app.controls.at(pluginProgramSearch),programs=app.controls.at(pluginProgram);
    app.layoutControls();
    restoreCheck(ScreamSeq::Tests::accessibleName(programQuery)==L"Search factory programs by name or group"&&GetNextDlgTabItem(app.window,programQuery,FALSE)==programs,"Program search lost its accessible name or results tab order");
    rawDraftText(programQuery,L"textures");
    restoreCheck(SendMessageW(programs,CB_GETCOUNT,0,0)==2&&app.selectedProgram=="keep"&&app.programCatalogRevision=="catalog:stable",
        "Program search changed its selected ID or revision guard");
    restoreCheck(ScreamSeq::NativeControls::itemText(programs,1).find(L"unavailable")!=std::wstring::npos,"Search hid unavailable-program information");
    SendMessageW(programs,CB_SETCURSEL,1,0);app.pluginCommand(pluginProgram);
    restoreCheck(app.selectedProgram=="target"&&app.pluginProgramDraft&&app.pluginDraft,"Filtered program selection did not retain the exact program ID");
    restoreCheck(SendMessageW(programs,CB_GETCOUNT,0,0)==1,"Choosing a matching program retained a stale outside-search row");
    const auto programGeneration=app.pluginDraftGeneration;rawDraftText(programQuery,L"no such program");
    restoreCheck(SendMessageW(programs,CB_GETCOUNT,0,0)==1&&app.pluginChoiceIndex(pluginProgram)==2&&app.selectedProgram=="target"&&app.pluginDraftGeneration==programGeneration,
        "Program search erased a retained selection or invented a new program draft");
    SetFocus(programQuery);restoreCheck(app.key(VK_ESCAPE)&&restoreText(programQuery).empty()&&app.pluginProgramDraft&&app.selectedProgram=="target"&&app.pluginDraftGeneration==programGeneration,"Escape in program search discarded the captured program draft");
    restoreCheck(songState(app)==before,"Searching or selecting a catalogue entry changed musical history");
    app.pluginDraft=false;app.pluginProgramDraft=false;app.pluginPrograms=RestoreJson::array();app.filterPluginChoices(true);
    restoreCheck(app.pluginChoiceIndex(pluginProgram)==-1,"Empty-program placeholder became a loadable catalogue index");
}

void applicationPluginValueControls() {
    using Field=ScreamSeq::PluginParameterField;
    RestoreJson metadata={{"name","Mode"},{"min",0},{"max",2},{"value",2},{"manualValue",1},
        {"choices",{"Low","Middle","High"}},{"unit",1},{"step",1},{"writable",true}};
    const auto interpreted=Field::read(metadata);
    restoreCheck(interpreted.editable()&&interpreted.kind==Field::Kind::Choice&&interpreted.value==1,"Choice field did not use the manual base");
    for(const auto *key:{"min","max","manualValue","step"}) {
        auto invalid=metadata;invalid[key]=std::numeric_limits<double>::infinity();
        restoreCheck(!Field::read(invalid).editable(),"Nonfinite vendor metadata enabled a native editor");
    }
    auto invalid=metadata;invalid["max"]=8;restoreCheck(!Field::read(invalid).editable(),"Incompatible choice range enabled a retargeted editor");
    invalid=metadata;invalid["manualValue"]=.5;restoreCheck(!Field::read(invalid).editable(),"Fractional choice silently selected a different value");
    invalid=metadata;invalid["writable"]=false;restoreCheck(!Field::read(invalid).editable(),"Read-only parameter became writable");
    invalid=metadata;invalid["min"]=false;restoreCheck(!Field::read(invalid).editable(),"Boolean range metadata became numeric");
    RestoreFixture fixture;auto &app=*fixture.app;const auto gain=addRestoreGain(app);
    app.selectedPlugin=gain;app.selectedParameter=0;app.restoreLayoutConfiguration(layoutFor(app,"plugins"));app.refreshPluginDetails();app.layoutControls();
    const auto toggle=app.controls.at(pluginValueToggle);
    restoreCheck(app.pluginValueControl()==pluginValueToggle&&IsWindowVisible(toggle)&&!IsWindowVisible(app.controls.at(pluginValue)),"Boolean did not use the native checkbox");
    restoreCheck(ScreamSeq::Tests::accessibleName(toggle)==L"Enabled manual value"&&SendMessageW(toggle,BM_GETCHECK,0,0)==BST_CHECKED,"Boolean lost its label or checked state");
    const auto baseline=app.documentOperation("plugin.state.get",{{"plugin",gain}});
    SetFocus(toggle);restoreCheck(app.key(VK_RETURN),"Enter did not operate the native parameter checkbox");
    const auto changed=app.documentOperation("plugin.state.get",{{"plugin",gain}});
    restoreCheck(changed!=baseline&&!app.pluginDraft&&SendMessageW(toggle,BM_GETCHECK,0,0)==BST_UNCHECKED&&restoreText(toggle)==L"Off","Checkbox did not commit the exact manual setting");
    app.edit("history.undo",{{"domain","all"}});
    restoreCheck(app.documentOperation("plugin.state.get",{{"plugin",gain}})==baseline,"Checkbox edit was not exactly one Undo");
    app.refreshPluginDetails();app.layoutControls();const auto noOp=songState(app);
    app.pluginCommand(pluginValueToggle); // Re-submit the already checked state.
    restoreCheck(songState(app)==noOp,"Unchanged checkbox added history or erased Redo");
    app.edit("history.redo",{{"domain","all"}});
    restoreCheck(app.documentOperation("plugin.state.get",{{"plugin",gain}})==changed,"Checkbox no-op lost the redo value");
    const auto catalog=app.documentOperation("plugin.discover",{{"format","Built-in"}});
    const auto filter=std::find_if(catalog.begin(),catalog.end(),[](const auto &entry){return entry.at("classID")=="resonance.digital-filter.v1";});
    restoreCheck(filter!=catalog.end(),"Digital Filter fixture missing");
    app.edit("plugin.add",{{"descriptor",*filter}});const auto plugin=app.view->session.document.at("nativePlugins").back().at("instanceID").get<std::string>();
    app.selectedPlugin=plugin;app.selectedParameter=1;app.refreshPluginDetails();app.layoutControls();
    const auto choices=app.controls.at(pluginValueChoice);
    restoreCheck(app.pluginValueControl()==pluginValueChoice&&IsWindowVisible(choices)&&SendMessageW(choices,CB_GETCOUNT,0,0)==5&&
        ScreamSeq::NativeControls::itemText(choices,3)==L"Notch","Native choices did not expose the effect's names");
    const auto filterBefore=app.documentOperation("plugin.state.get",{{"plugin",plugin}});
    SendMessageW(choices,CB_SETCURSEL,3,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(pluginValueChoice,CBN_SELCHANGE),reinterpret_cast<LPARAM>(choices));
    const auto filterAfter=app.documentOperation("plugin.state.get",{{"plugin",plugin}});
    restoreCheck(filterAfter!=filterBefore&&!app.pluginDraft&&app.pluginParameterField.value==3,"Labelled choice did not save its zero-based value");
    restoreCheck(app.documentOperation("plugin.state.get",{{"plugin",gain}})==changed,"Choice edit changed another plugin's state");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("plugin.state.get",{{"plugin",plugin}})==filterBefore,"Choice edit required more than one Undo");
    app.edit("history.redo",{{"domain","all"}});restoreCheck(app.documentOperation("plugin.state.get",{{"plugin",plugin}})==filterAfter,"Choice Redo did not restore the exact opaque state");
    app.refreshPluginDetails();const auto captured=app.pluginDetailsRevision;
    app.edit("document.patch",{{"title","Changed after catalog read"}});app.pluginDetailsRevision=captured;
    const auto current=songState(app);SendMessageW(choices,CB_SETCURSEL,2,0);
    restoreRejected([&]{app.pluginCommand(pluginValueChoice);});
    restoreCheck(app.pluginDraft&&app.pluginDraftRevision==captured&&app.pluginDraftTarget==plugin&&app.pluginDraftParameter==1&&
        restoreText(app.controls.at(pluginValue))==L"2"&&songState(app)==current,"Stale discrete edit was rebased, applied or discarded");
    const auto generation=app.pluginDraftGeneration;SendMessageW(choices,CB_SETCURSEL,4,0);
    restoreRejected([&]{app.pluginCommand(pluginValueChoice);});
    restoreCheck(SendMessageW(choices,CB_GETCURSEL,0,0)==2&&app.pluginDraftGeneration==generation&&restoreText(app.controls.at(pluginValue))==L"2","A second choice replaced the retained stale draft");
    app.pluginDraft=false;app.refreshPluginDetails();app.layoutControls();
    const auto saved=saveDepartureFixture(fixture);app.documentOperation("document.open",departureOpenRequest(app,saved));
    restoreCheck(app.documentOperation("plugin.state.get",{{"plugin",plugin}})==filterAfter&&app.documentOperation("plugin.state.get",{{"plugin",gain}})==changed,"Native discrete controls lost stable identities or opaque state on save/reopen");
}

void applicationPluginParameterNavigation() {
    RestoreFixture fixture;auto &app=*fixture.app;const auto first=addRestoreGain(app),second=addRestoreGain(app);
    app.edit("automation.pattern.set",{{"pattern",0},{"plugin",second},{"parameter",1},{"points",RestoreJson::array({{{"position",0},{"value",.5}}})}});
    app.selectedPlugin=first;app.selectedParameter=1;app.restoreLayoutConfiguration(layoutFor(app,"plugins"));app.refreshPluginDetails();app.layoutControls();
    restoreCheck(app.menuCommandState(pluginAutomate).enabled&&app.menuCommandState(pluginActivity).enabled,"Selected parameter actions are unavailable in native menus");
    const auto music=songState(app),cursor=app.position();app.pluginCommand(pluginAutomate);
    const auto owner=app.parameterAutomationWindow->window();
    restoreCheck(app.parameterAutomationWindow->snapshot().at("plugin")==first&&app.parameterAutomationWindow->snapshot().at("parameter")==1,"Rack automation action did not open its explicit parameter");
    SendMessageW(owner,WM_TIMER,3,0);
    app.workspaceEditors[0].pinned=true;const auto pinned=app.parameterAutomationWindow->snapshot();
    app.selectedPlugin=second;app.refreshPluginDetails();restoreRejected([&]{app.pluginCommand(pluginAutomate);});
    restoreCheck(app.parameterAutomationWindow->snapshot()==pinned&&app.workspaceEditors[0].pinned,"Rack action retargeted an independently pinned automation editor");
    app.workspaceEditors[0].pinned=false;app.pluginCommand(pluginAutomate);
    restoreCheck(app.parameterAutomationWindow->window()==owner&&app.parameterAutomationWindow->snapshot().at("plugin")==second,"Explicit rack navigation left a clean retained editor on its old target");
    SendMessageW(owner,WM_TIMER,3,0);rawDraftText(GetDlgItem(owner,4208),L"-.");const auto draft=app.parameterAutomationWindow->snapshot();
    app.selectedPlugin=first;app.refreshPluginDetails();restoreRejected([&]{app.pluginCommand(pluginAutomate);});
    restoreCheck(app.parameterAutomationWindow->snapshot()==draft,"Rack action erased a retained curve draft");
    app.selectedPlugin=second;app.refreshPluginDetails();app.pluginCommand(pluginAutomate);
    restoreCheck(app.parameterAutomationWindow->snapshot().at("generation")==draft.at("generation")&&app.parameterAutomationWindow->snapshot().at("fieldDraft")==true,"Returning to the same parameter reloaded its draft");
    restoreCheck(songState(app)==music&&app.position()==cursor,"Parameter navigation changed musical history or moved the editing cursor");
    app.selectedPlugin.clear();restoreCheck(!app.menuCommandState(pluginAutomate).enabled&&!app.menuCommandState(pluginActivity).enabled,"Missing rack target remained actionable");
}
