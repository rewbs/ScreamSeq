    std::unique_ptr<ScreamSeq::GraphWorkflowWindow> graphWorkflowWindow;
    Json graphWorkflowContext()const {
        return {{"documentId",documentId},{"revision",view->session.revision},{"busy",busy||recoveryRestoring},
            {"fieldDraft",graphDirty||graphFieldDirty||graphPending||graphDragging()},
            {"graph",graphID.empty()?Json(nullptr):Json(graphID)},{"node",graphNode}};
    }
    void openWorkflowCurve(const Json &graph,const std::string &node) {
        const auto &patterns=view->session.document.at("patterns");
        const auto pattern=std::find_if(patterns.begin(),patterns.end(),[&](const auto &p){return p.at("index")==patternIndex;});
        if(pattern==patterns.end())throw std::runtime_error("Select a current pattern for the source curve");
        if(!graphCurveWindow)graphCurveWindow=makeGraphCurveWindow();
        configureWorkspaceEditor("graphCurve");EditorPlacementGuard placement(changingEditorPlacement);
        graphCurveWindow->openTarget({graph.is_null()?std::string():graph.get<std::string>(),node,pattern->at("id").get<std::string>(),patternIndex});
        // Explicit source navigation captures this source independently from the
        // routing selection. Follow remains an explicit command in its header.
        workspaceEditors[workspaceEditorIndex("graphCurve")].pinned=true;
        finishWorkspaceEditorOpen("graphCurve");
    }
    void openGraphWorkflow() {
        if(!graphWorkflowWindow) {
            ScreamSeq::GraphWorkflowWindow::Callbacks callbacks;
            callbacks.context=[this]{return graphWorkflowContext();};
            callbacks.request=[this](const auto &method,const auto &params){return documentOperation(method,params);};
            callbacks.curve=[this](const auto &graph,const auto &node){openWorkflowCurve(graph,node);};
            callbacks.activity=[this](const auto &plugin,uint32_t parameter){openParameterActivity(plugin,parameter);};
            callbacks.provenance=[this](const auto &source){inspectParameterSource(source);};
            graphWorkflowWindow=std::make_unique<ScreamSeq::GraphWorkflowWindow>(window,std::move(callbacks));
        }
        graphWorkflowWindow->open();
    }
