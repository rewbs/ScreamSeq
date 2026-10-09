// The Main-bottom and in-grid owners have no NativeToolWindow lifetime. Their
// registrations follow Application, including when another panel hides them.
    std::vector<ScreamSeq::DocumentDraftRegistry::Registration> mainDraftRegistrations;
    static Tracker::DocumentDraft mainDraft(std::string document,std::string revision,std::string target,
        uint64_t generation,bool dirty,bool pending=false) {
        Tracker::DocumentDraft result;result.document=std::move(document);result.revision=std::move(revision);
        result.target=std::move(target);result.generation=generation;result.dirty=dirty;result.pending=pending;return result;
    }
    void reviewMainDraft(const char *editor,int control) {
        revealWorkspaceMainEditor(editor);layoutControls();
        const auto found=controls.find(control);SetFocus(found!=controls.end()&&IsWindowVisible(found->second)?found->second:window);
    }
    void registerMainDrafts() {
        if(!mainDraftRegistrations.empty())return;
        // Only the admitted replacement invokes these actions, after adoption.
        // Retire captured state without touching HWND text, pumping messages or
        // reading the worker. The host refreshes controls under its input lease.
        // Keep generations monotonic so an old asynchronous completion cannot
        // match the next document's first draft. Layout/unit preferences survive.
        const auto add=[&](const char *name,ScreamSeq::DocumentDraftRegistry::Summary summary,std::function<void()> review,
            ScreamSeq::DocumentDraftRegistry::RetireAction retire) {
            mainDraftRegistrations.push_back(documentDrafts.add(name,std::move(summary),std::move(review),std::move(retire)));
        };
        add("Pattern FX",[this]{return mainDraft(effectDraftDocument,effectDraftRevision,
            Json::array({effectDraftPattern,effectDraftRow,effectDraftChannel,effectDraftColumn}).dump(),
            effectDraftGeneration,effectDraftExists&&effectDraftDirty);},[this]{reviewMainDraft("effects",effectKind);},[this]()noexcept{
            ++effectDraftGeneration;effectDraftExists=effectDraftDirty=effectEditorOpen=effectInline=false;
            effectDraftDocument.clear();effectDraftRevision.clear();effectPrefix.clear();effectPrefixRevision.clear();
            effectChoices.clear();effectBindingChoices.clear();effectFieldsDraft.clear();effectDraftCommand.clear();
            visibleEffectChoices.clear();effectRenderedText.clear();effectSelected=-1;effectInlineControl=0;effectInlineField=0;
            effectEditorStatus.clear();return true;
        });
        add("In-grid nudge",[this]{
            const bool dirty=patternNudge.active&&(effectField(nudgeStrengthField)!=patternNudge.originalStrength||effectField(nudgeDurationField)!=patternNudge.originalDuration);
            return mainDraft(patternNudge.document,patternNudge.revision,
                Json::array({patternNudge.patternID,patternNudge.trackID,patternNudge.row,patternNudge.fx}).dump(),
                patternNudge.generation,dirty,patternNudge.pending);
        },[this]{reviewPatternNudge();},[this]()noexcept{
            if(patternNudge.pending)return false;
            const auto generation=patternNudge.generation+1;
            patternNudge=PatternNudgeDraft{};patternNudge.generation=generation;return true;
        });
        add("Sample range",[this]{return mainDraft(sampleRangeDocument,sampleDraftRevision,sampleDraftId,sampleDraftGeneration,sampleDraft);},
            [this]{reviewMainDraft("samples",sampleStartField);},[this]()noexcept{
                ++sampleDraftGeneration;sampleDraft=false;sampleRanges.clear();sampleRangeCatalog=UINT64_MAX;
                sampleRangeDocument.clear();sampleDraftRevision.clear();sampleDraftId.clear();
                sampleDragId.clear();sampleDragRevision.clear();sampleDragBefore={};sampleAnchor=0;
                if(dragging==4)dragging=0;return true;
            });
        add("Mixer",[this]{return mainDraft(mixerDocument,mixerRevision,mixerTarget,mixerGeneration,mixerDirty,mixerPending);},
            [this]{reviewMainDraft("mixer",mixerName);},[this]()noexcept{
                if(mixerPending)return false;
                ++mixerGeneration;mixerDirty=mixerEditorOpen=false;
                mixerDocument.clear();mixerRevision.clear();mixerTarget.clear();mixerData.clear();mixerDraft.clear();
                mixerMeters.clear();mixerStatus.clear();return true;
            });
        add("Plugin parameters and programs",[this]{return mainDraft(pluginDocument,pluginDraftRevision,
            Json::array({pluginDraftTarget,pluginDraftParameter,pluginProgramDraft?selectedProgram:std::string()}).dump(),
            pluginDraftGeneration,pluginDraft);},[this]{reviewMainDraft("plugins",pluginValue);},[this]()noexcept{
                if(pluginPresetPending)return false;
                ++pluginDraftGeneration;pluginDraft=pluginProgramDraft=pluginExtrasError=false;
                pluginDocument.clear();pluginDraftRevision.clear();pluginDraftTarget.clear();pluginDetailsRevision.clear();pluginDetailsTarget.clear();
                selectedPlugin.clear();selectedProgram.clear();programCatalogRevision.clear();selectedParameter=pluginDraftParameter=0;selectedPort=-1;
                listedPlugins=nullptr;pluginParameters.clear();pluginPrograms.clear();pluginPorts.clear();pendingPluginTriggers.clear();return true;
            });
        add("Plugin preset result",[this]{
            auto result=mainDraft(pluginPresetTarget.value("documentId",std::string()),pluginPresetTarget.value("expectedRevision",std::string()),
                pluginPresetTarget.dump(),pluginPresetGeneration,false,pluginPresetPending);
            result.uncertain=!pluginPresetPending&&pluginPresetCompletion.retained();return result;
        },[this]{showPluginPresetReview();},[this]()noexcept{
            if(pluginPresetPending||pluginPresetCompletion.retained())return false;
            ++pluginPresetGeneration;pluginPresetCompletion=ScreamSeq::NativeWriteCompletion{};
            pluginPresetTarget.clear();pluginPresetReport.clear();pluginPresetStatus.clear();pluginPresetNeedsReload=false;return true;
        });
        add("Graph recipe",[this]{return mainDraft(graphDocument,graphRevision,graphID,graphGeneration,
            graphDirty||graphFieldDirty||dragging==7,graphPending);},[this]{reviewMainDraft("graph",graphPropertyValue);},[this]()noexcept{
                if(graphPending)return false;
                ++graphGeneration;graphDirty=graphFieldDirty=graphEditorOpen=graphDragDirty=false;
                graphDocument.clear();graphRevision.clear();graphID.clear();graphNode.clear();graphExposedNode.clear();graphPluginTarget.clear();
                graphEditorToken.clear();graphEditorTokenNode.clear();graphEditorTokenGraph.clear();graphDragNode.clear();graphDragSocket={};graphDragReplace.reset();
                graphData.clear();graphDraft.clear();graphPluginData.clear();graphExposedParameter=0;graphWireIndex=-1;
                graphCanvas.nodes.clear();graphCanvas.sockets.clear();graphCanvas.wires.clear();graphCanvas.regions.clear();
                graphCanvas.panX=graphCanvas.panY=0;graphStatus.clear();if(graphDragging())dragging=0;return true;
            });
        add("Command result",[this]{
            auto result=mainDraft(nativeCommandTarget.value("documentId",std::string()),nativeCommandTarget.value("revision",std::string()),nativeCommandTarget.dump(),nativeCommandGeneration,false,nativeCommandPending);
            result.uncertain=!nativeCommandPending&&nativeCommandCompletion.retained();return result;
        },[this]{reviewNativeCommand();},[this]()noexcept{
            if(nativeCommandPending||nativeCommandCompletion.retained())return false;
            ++nativeCommandGeneration;nativeCommandCompletion=ScreamSeq::NativeWriteCompletion{};nativeCommandTarget.clear();nativeCommandReport.clear();return true;
        });
        add("Render selection result",[this]{
            auto result=mainDraft(directSampleRenderTarget.value("documentId",std::string()),directSampleRenderTarget.value("expectedRevision",std::string()),
                directSampleRenderTarget.dump(),directSampleRenderGeneration,false,directSampleRenderPending);
            result.uncertain=!directSampleRenderPending&&directSampleRenderCompletion.retained();return result;
        },[this]{reviewDirectSampleRender();},[this]()noexcept{
            if(directSampleRenderPending||directSampleRenderCompletion.retained())return false;
            ++directSampleRenderGeneration;directSampleRenderCompletion=ScreamSeq::NativeWriteCompletion{};directSampleRenderObservation.clear();
            directSampleRenderTarget.clear();directSampleRenderReport.clear();return true;
        });
    }
