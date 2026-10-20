    std::unique_ptr<ScreamSeq::EffectPickerWindow> effectPickerWindow;
    Json effectPickerContext()const {
        return {{"document",documentId},{"revision",view->session.revision},{"position",position()},
            {"patternID",workspacePlaybackPatternID(patternIndex)},{"columnID",noteColumn(channel).at("id")},
            {"draftGeneration",effectDraftGeneration},{"nudgeGeneration",patternNudge.generation},{"nudgeActive",patternNudge.active},
            {"plugin",selectedPlugin},{"parameter",selectedParameter},{"busy",busy||recoveryRestoring}};
    }
    void chooseEffectPicker(const Json &captured,const Json &choice) {
        rejectDepartureInput();
        if(captured!=effectPickerContext()||busy||recoveryRestoring||effectDraftDirty||patternNudge.active)
            throw std::runtime_error("Cursor, song or another FX draft changed; use current cursor before choosing");
        // The catalogue is immutable at this revision; use the same command
        // initialization and parameter fields as typed codes and the inspector.
        const auto previousGeneration=effectDraftGeneration;openEffectEditor(choice.at("displayCode").get<std::string>());
        if(effectDraftGeneration!=previousGeneration&&effectDraftExists&&effectDraftDocument==documentId&&effectDraftPattern==patternIndex&&effectDraftRow==row&&effectDraftChannel==channel)
            effectFieldChanged(); // Retain the newly selected command until Apply or explicit discard.
    }
    void openEffectPicker(bool recapture=false) {
        rejectDepartureInput();
        if(busy||recoveryRestoring)throw std::runtime_error("Wait for the document before choosing an effect");
        if(patternNudge.active){reviewPatternNudge();return;}
        if(effectDraftExists&&effectDraftDirty){openEffectEditor();return;}
        releaseTypedNotes();
        if(!effectPickerWindow)effectPickerWindow=std::make_unique<ScreamSeq::EffectPickerWindow>(window,ScreamSeq::EffectPickerWindow::Callbacks{
            [this]{return effectPickerContext();},[this](const auto &captured,const auto &choice){chooseEffectPicker(captured,choice);},
            [this]{openEffectPicker(true);}});
        auto choices=view->commands.at("effect");for(const auto &choice:view->commands.at("native"))choices.push_back(choice);
        const auto g=patternGridRect();const auto scale=GetDpiForWindow(window)/96.f;
        const auto fx=column>=3?(column-3)/2:0;
        const auto x=std::clamp(g.x+gutter+channelStart(channel)-horizontalScroll+effectColumnStart(channel,fx),g.x,g.x+g.w);
        const auto y=std::clamp(g.y+gridHeader+(float(row)-float(firstRow)+1)*rowHeight,g.y,g.y+g.h);
        POINT anchor{LONG(x*scale),LONG(y*scale)};ClientToScreen(window,&anchor);
        effectPickerWindow->open(effectPickerContext(),std::move(choices),anchor,recapture);
    }
