void applicationPluginPresetRecovery() {
    using Json=RestoreJson;
    {
        RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);
        app.selectedPlugin=plugin;app.revealWorkspaceMainEditor("plugins");app.layoutControls();app.refreshPluginDetails();
        const auto song=saveDepartureFixture(fixture),path=fixture.folder/L"native.screamseq-preset";
        unsigned choices=0;
        app.pluginPresetChooser=[&](bool save)->std::optional<std::filesystem::path>{
            ++choices;restoreCheck(save,"Save chooser changed mode");
            const auto pending=namedDraft(census(app),"Plugin preset result");
            restoreCheck(pending.pending&&!pending.uncertain&&pending.document==app.documentId,"Chooser has no captured pending owner");
            restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,song));});
            restoreRejected([&]{app.pluginPresetFile(false);});
            return path;
        };
        const auto before=app.view->session.revision;
        app.pluginPresetCompletionFault=[&]{
            rawDraftText(app.controls.at(pluginValue),L"-.");SetFocus(app.controls.at(pluginValue));
            throw std::runtime_error("Drop saved preset presentation after newer parameter draft");
        };
        restoreRejected([&]{app.pluginPresetFile(true);});
        const auto returned=app.pluginPresetCompletion.returned();const auto draft=app.pluginDraftGeneration;const auto focus=GetFocus();
        restoreCheck(returned&&returned->result.at("written")==true&&std::filesystem::is_regular_file(path)&&
            app.view->session.revision==before&&app.pluginDraft,"Save result or newer draft was lost");
        restoreCheck(namedDraft(census(app),"Plugin preset result").uncertain&&!app.canClose(),"Unreviewed file result allowed Close");
        restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,song));});
        // Review uses the historical receipt. It must not rewrite even when
        // the saved file is subsequently absent.
        const auto moved=fixture.folder/L"saved-away.screamseq-preset";std::filesystem::rename(path,moved);
        app.pluginPresetFile(true);
        restoreCheck(!app.pluginPresetCompletion.retained()&&app.pluginPresetReport.at("verified")==true&&
            app.pluginPresetReport.at("result")==returned->result&&!std::filesystem::exists(path)&&app.pluginPresetWrites==1&&choices==1,
            "Known Save Review rewrote a file, reopened a chooser or lost its exact result");
        restoreCheck(app.pluginDraft&&app.pluginDraftGeneration==draft&&restoreText(app.controls.at(pluginValue))==L"-."&&
            app.pluginDraftTarget==plugin&&GetFocus()==focus&&app.view->session.revision==before,"Review erased or rebased a newer draft/focus");
        app.pluginDraft=false;app.pluginDetailsRevision.clear();app.refreshPluginDetails();
        // Successful document adoption retires the completed report too.
        app.documentOperation("document.open",departureOpenRequest(app,song));
        restoreCheck(app.pluginPresetTarget.empty()&&app.pluginPresetReport.empty()&&app.pluginPresetStatus.empty(),"Preset report survived document retirement");
        fixture.close();std::cout<<"PASS preset receipt case\n";
    }
    {
        RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);
        app.selectedPlugin=plugin;app.revealWorkspaceMainEditor("plugins");app.layoutControls();app.refreshPluginDetails();
        const auto path=fixture.folder/L"load.screamseq-preset";
        app.pluginPresetChooser=[&](bool)->std::optional<std::filesystem::path>{return path;};
        app.pluginPresetFile(true);
        const auto baseline=app.documentOperation("plugin.state.get",{{"plugin",plugin}});
        const auto parameter=app.pluginParameters.at(0);const auto value=parameter.at("value")==parameter.at("min")?parameter.at("max"):parameter.at("min");
        app.edit("plugin.parameters.set",{{"plugin",plugin},{"values",Json::array({{{"id",parameter.at("id")},{"value",value}}})}});
        const auto changed=app.documentOperation("plugin.state.get",{{"plugin",plugin}});restoreCheck(changed!=baseline,"Preset load fixture did not change the sound");
        app.pluginPresetCompletionFault=[] {throw std::runtime_error("Drop loaded preset presentation");};
        restoreRejected([&]{app.pluginPresetFile(false);});
        restoreCheck(app.pluginPresetCompletion.returned()&&app.documentOperation("plugin.state.get",{{"plugin",plugin}})==baseline,"Load did not retain a successful result");
        app.edit("document.patch",{{"title","Later edit"}});const auto revision=app.view->session.revision;
        app.pluginPresetFile(false);
        restoreCheck(!app.pluginPresetCompletion.retained()&&app.pluginPresetWrites==2&&app.view->session.revision==revision,"Load Review replayed or mutated history");
        app.edit("history.undo",{{"domain","all"}});app.edit("history.undo",{{"domain","all"}});
        restoreCheck(app.documentOperation("plugin.state.get",{{"plugin",plugin}})==changed,"Preset load did not undo in exactly one step after the later edit");
        app.edit("history.redo",{{"domain","all"}});
        restoreCheck(app.documentOperation("plugin.state.get",{{"plugin",plugin}})==baseline,"Preset load Redo lost its baseline");
        fixture.close();std::cout<<"PASS preset load history\n";
    }
    for(bool save:{true,false}) {
        RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);
        app.selectedPlugin=plugin;app.revealWorkspaceMainEditor("plugins");app.layoutControls();app.refreshPluginDetails();
        const auto path=fixture.folder/L"unknown.screamseq-preset";
        app.pluginPresetChooser=[&](bool)->std::optional<std::filesystem::path>{throw std::runtime_error("Unknown result reopened chooser");};
        Json params={{"plugin",plugin},{"path",utf8Path(path)},{"expectedRevision",app.view->session.revision}};
        if(save){params["name"]="Unknown";params["overwrite"]=true;}
        else {
            const auto preset=app.documentOperation("plugin.preset.save",{{"plugin",plugin},{"path",utf8Path(path)},
                {"name","Load source"},{"overwrite",true},{"expectedRevision",app.view->session.revision}});
            params["expectedPresetRevision"]=preset.at("presetRevision");
        }
        app.pluginPresetTarget=params;app.pluginPresetTarget.update(Json{{"documentId",app.documentId},{"save",save}});
        // Exercise the no-receipt boundary independently of the production
        // ticket. The real operation commits, but its return is lost here.
        restoreRejected([&]{app.pluginPresetCompletion.submit([&](const auto &method,const auto &request)->ScreamSeq::Api::CompletedCall{
            (void)app.documentOperationWithOutcome(method,request);throw std::runtime_error("Lost untracked preset return");
        },save?"plugin.preset.save":"plugin.preset.load",params,app.documentId,++app.pluginPresetGeneration,app.pluginPresetTarget);});
        const auto writes=app.pluginPresetWrites;restoreCheck(!app.pluginPresetCompletion.returned(),"Unknown fixture accidentally supplied a ticket");
        const auto moved=fixture.folder/L"unknown-away.screamseq-preset";
        if(save)std::filesystem::rename(path,moved);else app.edit("plugin.remove",{{"slot",0}});
        restoreRejected([&]{app.reviewPluginPreset();});
        restoreCheck(app.pluginPresetCompletion.retained()&&!app.pluginPresetNeedsReload,"Missing original file/plugin was falsely reconciled");
        if(save)std::filesystem::rename(moved,path);else app.edit("history.undo",{{"domain","all"}});
        saveDepartureFixture(fixture); // Keep Close on the native-work gate, not the unsaved-song dialog.
        const auto before=songState(app);app.pluginPresetFile(true);
        restoreCheck(app.pluginPresetCompletion.retained()&&app.pluginPresetNeedsReload&&app.pluginPresetReport.at("verified")==false&&
            songState(app)==before&&!app.canClose(),"Unknown Review claimed success or discarded retained work");
        app.edit("document.patch",{{"title","Changed since readback"}});
        restoreRejected([&]{app.acknowledgePluginPreset();});
        restoreCheck(!app.pluginPresetNeedsReload&&app.pluginPresetCompletion.retained(),"Stale acknowledgement discarded uncertainty");
        app.pluginPresetFile(true);const auto observed=songState(app);app.pluginPresetFile(false);
        restoreCheck(!app.pluginPresetCompletion.retained()&&!app.pluginPresetNeedsReload&&app.pluginPresetWrites==writes&&
            app.pluginPresetReport.at("verified")==false&&songState(app)==observed,"Explicit acknowledgement repeated a write or claimed verification");
        fixture.close();std::cout<<"PASS preset receipt case\n";
    }
    {
        RestoreFixture fixture;auto &app=*fixture.app;app.selectedPlugin=addRestoreGain(app);app.layoutControls();
        unsigned choices=0;app.pluginPresetChooser=[&](bool)->std::optional<std::filesystem::path>{++choices;return {};};
        app.pluginPresetFile(true);restoreCheck(choices==1&&!app.pluginPresetPending&&!app.pluginPresetCompletion.retained(),"Cancel retained pending work");
        const auto path=fixture.folder/L"stale.screamseq-preset";
        app.pluginPresetChooser=[&](bool)->std::optional<std::filesystem::path>{app.edit("document.patch",{{"title","During picker"}});return path;};
        restoreRejected([&]{app.pluginPresetFile(true);});
        restoreCheck(!std::filesystem::exists(path)&&!app.pluginPresetPending&&!app.pluginPresetCompletion.retained(),"Stale chooser saved or retained an unsubmitted write");
        app.pluginPresetChooser=[&](bool)->std::optional<std::filesystem::path>{throw std::runtime_error("Picker failed");};
        restoreRejected([&]{app.pluginPresetFile(false);});
        restoreCheck(!app.pluginPresetPending&&!app.pluginPresetCompletion.retained(),"Failed picker became an unresolved write");fixture.close();
    }
}
