// Main retains one native curve editor; editable curve and child-tool state
// belong to that owner rather than the routing editor.
    std::unique_ptr<ScreamSeq::GraphCurveWindow> graphCurveWindow;
    Json graphCurveSelectionIdentity=Json::array();
    uint64_t graphCurveSelectionSerial=0;

    ScreamSeq::GraphCurveWindow::Context graphCurveContext()const {
        ScreamSeq::GraphCurveWindow::Context value;
        value.document=documentId;value.revision=view->session.revision;
        value.patterns=view->session.document.at("patterns");
        value.selectionGeneration=graphCurveSelectionSerial;
        value.busy=busy||recoveryRestoring;
        // Selection comes from the routing editor, but its saved curve is read
        // from the worker. A newly drafted/removed source is never fabricated.
        const Json *node=nullptr;
        if(graphDraft.contains("nodes"))for(const auto &candidate:graphDraft.at("nodes"))
            if(candidate.at("id")==graphNode){node=&candidate;break;}
        if(node&&node->at("kind")=="automation") {
            for(const auto &pattern:value.patterns)if(pattern.at("index")==patternIndex) {
                value.selected=ScreamSeq::GraphCurveWindow::Target{
                    graphID,graphNode,pattern.at("id").get<std::string>(),patternIndex};
                value.selectedTitle=wide(graphDraft.value("name",std::string("Graph"))+" / "+node->value("name",std::string("Automation")));
                break;
            }
        }
        return value;
    }
    void noteGraphCurveSelection() {
        const auto current=graphCurveContext();
        auto identity=Json::array({current.document,graphID,graphNode,patternIndex});
        identity.push_back(current.selected?Json(current.selected->patternID):Json());
        if(identity!=graphCurveSelectionIdentity){graphCurveSelectionIdentity=std::move(identity);++graphCurveSelectionSerial;}
        if(graphCurveWindow)graphCurveWindow->observeContext();
    }
    std::unique_ptr<ScreamSeq::GraphCurveWindow> makeGraphCurveWindow(std::shared_ptr<bool> preparing={}) {
        return std::make_unique<ScreamSeq::GraphCurveWindow>(window,
            [this,preparing](const auto &method,const auto &params){
                return preparing&&*preparing?workspacePreparationRead(method,params):documentOperation(method,params);
            },[this]{return graphCurveContext();},[this]{
                workspaceEditorRequest({{"panel","graphCurve"},{"return",true}});
            });
    }
    void openGraphCurve() {
        noteGraphCurveSelection();
        if(!graphCurveWindow)graphCurveWindow=makeGraphCurveWindow();
        configureWorkspaceEditor("graphCurve");
        EditorPlacementGuard placement(changingEditorPlacement);
        const auto &state=workspaceEditors[workspaceEditorIndex("graphCurve")];
        if(graphCurveWindow->capturedTarget().has_value()&&!state.origin.empty()&&state.pinned){graphCurveWindow->show();SetFocus(graphCurveWindow->window());}
        else graphCurveWindow->openAt();
        finishWorkspaceEditorOpen("graphCurve");
    }
    bool curveDraft()const{return graphCurveWindow&&graphCurveWindow->retainedDraft();}
    bool graphCurvePending()const{return graphCurveWindow&&graphCurveWindow->pending();}
    bool graphCurveVisible()const{return graphCurveWindow&&graphCurveWindow->visible();}
    Json graphCurveOperationGuard()const{return graphCurveWindow?graphCurveWindow->operationGuard():Json::object();}
    Json graphCurveSnapshot()const{return graphCurveWindow?graphCurveWindow->snapshot():Json{{"visible",false},{"pending",false}};}
    Json curveFormulaWorkbenchSnapshot()const{return graphCurveWindow?graphCurveWindow->formulaWorkbenchSnapshot():Json{{"visible",false}};}
    Json curveFormulaReferenceSnapshot()const{return graphCurveWindow?graphCurveWindow->formulaReferenceSnapshot():Json{{"visible",false}};}
    Json curveEnvelopeBankSnapshot()const{return graphCurveWindow?graphCurveWindow->envelopeBankSnapshot():Json{{"visible",false}};}
    bool graphCurveCommand(int id) {
        if(id<curveApply||id>curveReference)return false;
        // Reference is useful before any graph/source is selected. Other legacy
        // palette actions require/reveal the retained curve owner normally.
        if(id==curveReference){
            if(!graphCurveWindow){graphCurveWindow=makeGraphCurveWindow();configureWorkspaceEditor("graphCurve");}
            graphCurveWindow->dispatchLegacyAction(id);return true;
        }
        if(!graphCurveWindow)openGraphCurve();
        return graphCurveWindow->dispatchLegacyAction(id);
    }
