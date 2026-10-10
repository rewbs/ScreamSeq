void applicationPluginSearch() {
    RestoreFixture fixture;auto &app=*fixture.app;
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
        {"unitLabel","Hz"},{"value",double(i)},{"writable",true}});
    app.selectedParameter=1000;app.filterPluginChoices(false);app.setPluginParameterField();
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
