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
        const auto add=[&](const char *name,ScreamSeq::DocumentDraftRegistry::Summary summary,std::function<void()> review) {
            mainDraftRegistrations.push_back(documentDrafts.add(name,std::move(summary),std::move(review)));
        };
        add("Pattern FX",[this]{return mainDraft(effectDraftDocument,effectDraftRevision,
            Json::array({effectDraftPattern,effectDraftRow,effectDraftChannel,effectDraftColumn}).dump(),
            effectDraftGeneration,effectDraftExists&&effectDraftDirty);},[this]{reviewMainDraft("effects",effectKind);});
        add("In-grid nudge",[this]{
            const bool dirty=patternNudge.active&&(effectField(nudgeStrengthField)!=patternNudge.originalStrength||effectField(nudgeDurationField)!=patternNudge.originalDuration);
            return mainDraft(patternNudge.document,patternNudge.revision,
                Json::array({patternNudge.patternID,patternNudge.trackID,patternNudge.row,patternNudge.fx}).dump(),
                patternNudge.generation,dirty,patternNudge.pending);
        },[this]{reviewPatternNudge();});
        add("Sample range",[this]{return mainDraft(sampleRangeDocument,sampleDraftRevision,sampleDraftId,sampleDraftGeneration,sampleDraft);},
            [this]{reviewMainDraft("samples",sampleStartField);});
        add("Mixer",[this]{return mainDraft(mixerDocument,mixerRevision,mixerTarget,mixerGeneration,mixerDirty,mixerPending);},
            [this]{reviewMainDraft("mixer",mixerName);});
        add("Plugin parameters and programs",[this]{return mainDraft(pluginDocument,pluginDraftRevision,
            Json::array({pluginDraftTarget,pluginDraftParameter,pluginProgramDraft?selectedProgram:std::string()}).dump(),
            pluginDraftGeneration,pluginDraft,pluginPresetPending);},[this]{reviewMainDraft("plugins",pluginValue);});
        add("Graph recipe",[this]{return mainDraft(graphDocument,graphRevision,graphID,graphGeneration,
            graphDirty||graphFieldDirty||dragging==7,graphPending);},[this]{reviewMainDraft("graph",graphPropertyValue);});
        add("Render selection result",[this]{
            auto result=mainDraft(directSampleRenderTarget.value("documentId",std::string()),directSampleRenderTarget.value("expectedRevision",std::string()),
                directSampleRenderTarget.dump(),directSampleRenderGeneration,false,directSampleRenderPending);
            result.uncertain=!directSampleRenderPending&&directSampleRenderCompletion.retained();return result;
        },[this]{reviewDirectSampleRender();});
    }
