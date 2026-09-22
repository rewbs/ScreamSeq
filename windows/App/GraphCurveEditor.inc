// A graph curve owns a captured source, pattern, revision and local draft. It
// never follows another selection while that draft or its fields are pending.
    std::string curveGraph,curveNode,curveDocument,curveRevision;
    unsigned curvePatternIndex=0,curveRows=64,curveRowsPerBeat=4,curveSnapUnits=256;
    int curveSelected=-1,curveKindIndex=1,curveSnapIndex=0;
    uint64_t curveGeneration=0;
    bool curveDirty=false,curveFieldsDirty=false,curvePending=false,settingCurveFields=false,curveEnabled=true,curvePreviewNeeded=false;
    Json curvePoints=Json::array(),curvePreviewValues=Json::array(),curveDragBefore=Json::array();
    bool curveDragWasDirty=false;int curveDragSelection=-1;
    std::wstring curveStatus=L"Select an automation source node to edit its pattern curve";
    std::wstring curveTitle=L"Pattern automation";
    ScreamSeq::AutomationCanvas curveCanvas;
    std::unique_ptr<ScreamSeq::EnvelopeBankWindow> envelopeBank;
    std::unique_ptr<ScreamSeq::FormulaWorkbenchWindow> formulaWorkbench,formulaReference;
    void openFormulaReference(){
        if(!formulaReference)formulaReference=std::make_unique<ScreamSeq::FormulaWorkbenchWindow>(window,L"Envelope formula reference","",Json::object(),-1,[this](const auto &method,const auto &params){return documentOperation(method,params);});
        formulaReference->show();
    }
    void openCurveFormula(){
        if(formulaWorkbench&&(formulaWorkbench->visible()||formulaWorkbench->retainedDraft())){formulaWorkbench->show();return;}
        if(!curveCurrent())throw std::runtime_error("Load a current curve before expanding its formula");
        if(curveFieldsDirty)curveReplace(curvePointFromFields());
        if(curveSelected<0||size_t(curveSelected)>=curvePoints.size()||curvePoints[size_t(curveSelected)].at("curve")!="scripted")throw std::runtime_error("Select a scripted point to expand its formula");
        const auto document=curveDocument,revision=curveRevision,graph=curveGraph,node=curveNode;const auto pattern=curvePatternIndex;const auto generation=curveGeneration;const auto selected=curveSelected;const auto point=curvePoints[size_t(selected)];
        auto current=[this,document,revision,graph,node,pattern,generation,selected,point]{return documentId==document&&curveDocument==document&&curveRevision==revision&&curveGraph==graph&&curveNode==node&&curvePatternIndex==pattern&&curveGeneration==generation&&curveSelected==selected&&!curvePending&&!curveFieldsDirty&&size_t(selected)<curvePoints.size()&&curvePoints[size_t(selected)]==point;};
        auto use=[this,current](const std::string &text){if(!current())return false;auto point=curvePoints[size_t(curveSelected)];point["formula"]=text;curveReplace(std::move(point));return true;};
        formulaWorkbench=std::make_unique<ScreamSeq::FormulaWorkbenchWindow>(window,curveTitle+L" · Formula",point.at("formula").get<std::string>(),Json{{"points",curvePoints},{"rows",curveRows},{"rowsPerBeat",curveRowsPerBeat}},selected,[this](const auto &method,const auto &params){return documentOperation(method,params);},std::move(current),std::move(use));
        formulaWorkbench->show();
    }
    void openCurveBank(){
        if(envelopeBank&&(envelopeBank->visible()||envelopeBank->retainedDraft())){envelopeBank->show();return;}
        if(curveGraph.empty()||!curveCurrent()||curveFieldsDirty)throw std::runtime_error("Load a current curve and set its pending point fields before opening the bank");
        const auto document=curveDocument,graph=curveGraph,node=curveNode;const auto pattern=curvePatternIndex;
        auto generation=std::make_shared<uint64_t>(curveGeneration);
        auto sourceCurrent=[this,document,graph,node,pattern,generation]{return documentId==document&&curveDocument==document&&curveGraph==graph&&curveNode==node&&curvePatternIndex==pattern&&curveGeneration==*generation&&!curveFieldsDirty;};
        auto context=[this]{return std::pair(documentId,view->session.revision);};
        auto request=[this,sourceCurrent,generation](const std::string &method,const Json &params){
            const bool source=sourceCurrent(),previousPending=curvePending;
            if(source)curvePending=true;Json result;
            try{result=documentOperation(method,params);}catch(...){curvePending=previousPending;throw;}
            curvePending=previousPending;
            if(params.contains("expectedRevision")&&source&&sourceCurrent()){
                curveRevision=view->session.revision;
                if(method=="envelope.bank.apply"||((method=="envelope.bank.unlink"||method=="envelope.bank.save")&&!curveDraft())){
                    const auto before=curveGeneration;curveLoad(true);
                    if(!curveDraft()&&curveGeneration==before+1)*generation=curveGeneration;
                }
            }
            return result;
        };
        Json target={{"kind","graph"},{"graph",graph},{"node",node},{"pattern",pattern}},shape;
        if(!curvePoints.empty())shape={{"span",curveRows*256},{"rowsPerBeat",curveRowsPerBeat},{"points",curvePoints}};
        auto next=std::make_unique<ScreamSeq::EnvelopeBankWindow>(window,target,shape,document,curveRevision,curveTitle+L" · Pattern "+std::to_wstring(pattern),std::move(request),std::move(context),std::move(sourceCurrent));
        envelopeBank=std::move(next);envelopeBank->show();
    }
    static constexpr const char *curveNames[]={"step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"};
    bool curveDraft()const{return curveDirty||curveFieldsDirty;}
    bool graphCurveVisible()const{return graphEditorVisible()&&graphPageIndex==5;}
    bool curveCurrent()const{return curveDocument==documentId&&curveRevision==view->session.revision;}
    bool curveContextChanged(){auto node=graphSelectedNode();return node&&node->at("kind")=="automation"?(curveGraph!=graphID||curveNode!=graphNode||!curveCurrent()):!curveGraph.empty();}
    void installGraphCurveControls(){
        for(auto [id,label]:std::initializer_list<std::pair<int,const wchar_t *>>{{curveApply,L"Apply curve"},{curveReload,L"Reload"},{curveSetPoint,L"Set point"},{curveDelete,L"Delete point"},{curveRamp,L"Ramp"},{curveClear,L"Clear"},{curveFit,L"Fit"},{curveZoomIn,L"+"},{curveZoomOut,L"−"},{curvePreview,L"Check / preview"},{curveEnable,L"Enabled"},{curveBank,L"Bank…"},{curveExpand,L"Expand"},{curveReference,L"Guide"}})
            controls[id]=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        for(int id:{curvePattern,curveKind,curveSnap})controls[id]=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        for(int id:{curveRow,curveValue,curveFormula}){controls[id]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(controls.at(id),EM_SETLIMITTEXT,id==curveFormula?2048:32,0);}
        for(auto label:{L"Step",L"Linear",L"Smooth",L"Exponential",L"Logarithmic",L"Step at start",L"Exponential reversed",L"Logarithmic reversed",L"Scripted"})SendMessageW(controls.at(curveKind),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        for(auto label:{L"Row",L"½ row",L"¼ row",L"1/256 row"})SendMessageW(controls.at(curveSnap),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(controls.at(curveKind),CB_SETCURSEL,1,0);SendMessageW(controls.at(curveSnap),CB_SETCURSEL,0,0);
        for(int id=curvePattern;id<=curveReference;++id)if(!controls.at(id))throw std::runtime_error("Cannot create curve controls");
    }
    void curveShowFields(){
        settingCurveFields=true;const Json point=curveSelected>=0&&size_t(curveSelected)<curvePoints.size()?curvePoints[size_t(curveSelected)]:Json{{"position",0},{"value",.5},{"curve","linear"}};
        graphText(curveRow,point.at("position").get<double>()/256);graphText(curveValue,point.at("value").get<double>()*100);graphText(curveFormula,point.value("formula",std::string("mix(start,end,t)")));
        curveKindIndex=1;for(int i=0;i<9;++i)if(point.at("curve")==curveNames[i])curveKindIndex=i;SendMessageW(controls.at(curveKind),CB_SETCURSEL,curveKindIndex,0);
        SetWindowTextW(controls.at(curveEnable),curveEnabled?L"Enabled":L"Disabled");curveFieldsDirty=false;settingCurveFields=false;
    }
    void curveFieldChanged(){if(settingCurveFields)return;curveFieldsDirty=true;++curveGeneration;frameRequested=true;}
    void rebuildCurveCanvas(){auto r=graphCanvasRect();curveCanvas.viewport={r.x+38,r.y+18,std::max(1.0f,r.w-50),std::max(1.0f,r.h-41)};curveCanvas.rebuild(curvePoints,curvePreviewValues);frameRequested=true;}
    void curveChanged(){curveDirty=true;++curveGeneration;curvePreviewValues=Json::array();curvePreviewNeeded=true;rebuildCurveCanvas();if(graphCurveVisible())SetTimer(window,5,120,nullptr);}
    void curveLoad(bool discard=false,std::optional<unsigned> requestedPattern={}){
        if(busy||curvePending||(!discard&&curveDraft()))return;
        const bool captured=discard&&!curveGraph.empty()&&curveDocument==documentId;
        auto node=graphSelectedNode();if(!captured&&(!node||node->at("kind")!="automation")){if(!curveDraft()){curveGraph.clear();curveNode.clear();curvePoints=curvePreviewValues=Json::array();curveSelected=-1;curvePreviewNeeded=false;rebuildCurveCanvas();}return;}
        const auto targetGraph=captured?curveGraph:graphID,targetNode=captured?curveNode:graphNode,targetDocument=documentId;const auto generation=curveGeneration;
        unsigned targetPattern=requestedPattern.value_or(curvePatternIndex);bool found=false;for(const auto &p:graphData.at("patterns"))if(p.at("index")==targetPattern)found=true;
        if(requestedPattern&&!found)throw std::runtime_error("Pattern selection changed; reload the graph");
        if(!found)targetPattern=patternIndex;
        curvePending=true;try{auto data=documentOperation("graph.automation.get",{{"graph",targetGraph},{"node",targetNode},{"pattern",targetPattern}});curvePending=false;
            if(generation!=curveGeneration||documentId!=targetDocument)return;
            const bool changed=curveGraph!=targetGraph||curveNode!=targetNode||curvePatternIndex!=targetPattern||curveDocument!=targetDocument;
            curveGraph=targetGraph;curveNode=targetNode;curvePatternIndex=targetPattern;curveDocument=targetDocument;curveRevision=view->session.revision;
            curveTitle=L"Pattern automation";for(const auto &definition:graphData.at("library"))if(definition.at("id")==targetGraph)for(const auto &source:definition.at("nodes"))if(source.at("id")==targetNode)curveTitle=wide(definition.at("name").get<std::string>()+" / "+source.at("name").get<std::string>());
            curveRows=data.at("rows").get<unsigned>();curveRowsPerBeat=data.at("rowsPerBeat").get<unsigned>();curveEnabled=data.at("enabled").get<bool>();curvePoints=data.at("points");curveDirty=curveFieldsDirty=false;curveSelected=-1;
            SendMessageW(controls.at(curvePattern),CB_RESETCONTENT,0,0);for(const auto &p:graphData.at("patterns")){auto label=L"Pattern "+std::to_wstring(p.at("index").get<unsigned>())+L" / "+wide(p.at("name").get<std::string>());const auto i=SendMessageW(controls.at(curvePattern),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));SendMessageW(controls.at(curvePattern),CB_SETITEMDATA,i,p.at("index").get<unsigned>());if(p.at("index")==targetPattern)SendMessageW(controls.at(curvePattern),CB_SETCURSEL,i,0);}
            if(changed)curveCanvas.fit(curveRows);else {curveCanvas.pan(0,curveRows);if(curveCanvas.end>curveRows*256)curveCanvas.fit(curveRows);}
            ++curveGeneration;curveShowFields();curvePreviewNeeded=true;curvePreviewValues=Json::array();curveStatus=L"Click to add · drag points · Ctrl+Enter applies one Undo step";rebuildCurveCanvas();
        }catch(...){curvePending=false;throw;}
    }
    void curvePreviewNow(){
        if(curveGraph.empty()||busy||curvePending)return;const auto generation=curveGeneration;curvePending=true;curvePreviewNeeded=false;
        if(curvePoints.empty()){curvePending=false;curvePreviewValues=Json::array({Json::array({curveCanvas.start,0}),Json::array({curveCanvas.end,0})});curveStatus=L"No curve / automation source outputs zero";rebuildCurveCanvas();return;}
        try{auto result=documentOperation("automation.formula.preview",{{"points",curvePoints},{"rows",curveRows},{"rowsPerBeat",curveRowsPerBeat},{"start",curveCanvas.start},{"end",curveCanvas.end},{"samples",1024}});curvePending=false;if(generation==curveGeneration){curvePreviewValues=result.at("values");curveStatus=L"Preview validated / values clamped to 0…1";rebuildCurveCanvas();}}
        catch(const std::exception &e){curvePending=false;if(generation==curveGeneration){curvePreviewValues=Json::array();curveStatus=wide(e.what());rebuildCurveCanvas();}}
    }
    void graphCurveTimer(){
        KillTimer(window,5);if(!graphCurveVisible())return;if(busy||curvePending||dragging>=6){SetTimer(window,5,120,nullptr);return;}
        try{if(!curveDraft()&&curveContextChanged())curveLoad();if(curvePreviewNeeded)curvePreviewNow();}catch(const std::exception &e){curveStatus=wide(e.what());}layoutControls();
    }
    template<class Place>void layoutGraphCurveControls(Place place){
        const auto r=sampleEditorRect();const float x=r.x+r.w-256;const bool show=graphCurveVisible();std::set<int> placed;
        auto at=[&](int id,float a,float b,float w,float h,bool visible=true){placed.insert(id);place(id,a,b,w,h,show&&visible);};
        at(curvePattern,r.x+6,r.y+36,std::max(90.0f,r.w-600),230);at(curveSnap,x-326,r.y+36,110,230);at(curveFit,x-210,r.y+36,44,25);at(curveZoomOut,x-162,r.y+36,38,25);at(curveZoomIn,x-120,r.y+36,38,25);at(curveEnable,x-78,r.y+36,72,25);
        at(curveRow,x,r.y+86,120,25);at(curveValue,x+128,r.y+86,120,25);at(curveKind,x,r.y+117,248,220);at(curveFormula,x,r.y+148,120,25,curveKindIndex==8);at(curveExpand,x+128,r.y+148,58,25,curveKindIndex==8);at(curveReference,x+190,r.y+148,58,25);
        at(curveSetPoint,x,r.y+179,120,25);at(curveDelete,x+128,r.y+179,120,25);at(curveRamp,x,r.y+210,60,25);at(curveClear,x+64,r.y+210,58,25);at(curvePreview,x+128,r.y+210,120,25);
        at(curveApply,x-260,r.y+5,92,25);at(curveReload,x-162,r.y+5,74,25);at(curveBank,x-82,r.y+5,70,25);
        for(int id=curvePattern;id<=curveReference;++id){if(!placed.contains(id))ScreamSeq::NativeControls::show(controls.at(id),false);EnableWindow(controls.at(id),!curvePending&&(id==curveReload||!curveGraph.empty()));}
        rebuildCurveCanvas();if(show&&((!curveDraft()&&curveContextChanged())||curvePreviewNeeded))SetTimer(window,5,120,nullptr);
    }
    Json curvePointFromFields(){
        const double position=graphNumber(curveRow)*256,value=graphNumber(curveValue)/100;if(position<0||position>double(curveRows)*256-1||value<0||value>1)throw std::runtime_error("Use a position inside the pattern and 0–100%");
        Json point={{"position",uint32_t(std::llround(position))},{"value",value},{"curve",curveNames[curveKindIndex]}};
        if(curveKindIndex==8){auto formula=utf8Path(effectField(curveFormula));if(formula.empty())throw std::runtime_error("A scripted point needs a formula");point["formula"]=formula;}return point;
    }
    void curveReplace(Json point){
        const auto position=point.at("position");for(size_t i=0;i<curvePoints.size();++i)if(int(i)!=curveSelected&&curvePoints[i].at("position")==position)throw std::runtime_error("Another point already occupies this position");
        auto next=curvePoints;if(curveSelected>=0)next[size_t(curveSelected)]=std::move(point);else {if(next.size()>=4096)throw std::runtime_error("A curve supports at most 4096 points");next.push_back(std::move(point));}
        std::sort(next.begin(),next.end(),[](const auto &a,const auto &b){return a.at("position").template get<uint32_t>()<b.at("position").template get<uint32_t>();});
        curveSelected=-1;for(size_t i=0;i<next.size();++i)if(next[i].at("position")==position)curveSelected=int(i);if(next!=curvePoints){curvePoints=std::move(next);curveChanged();}curveShowFields();
    }
    bool graphCurveCommand(int id){
        if(id<curvePattern||id>curveReference)return false;if(busy||curvePending||dragging>=6)throw std::runtime_error("Curve editor is busy");
        if(id==curveReference){openFormulaReference();return true;}workspaceState.focus="graphCurve";
        if(id==curveBank){openCurveBank();return true;}if(id==curveExpand){openCurveFormula();return true;}
        if(id==curveReload){if(graphDocument!=documentId)throw std::runtime_error("Document changed; reload the graph before replacing its curve draft");++curveGeneration;curveLoad(true);return true;}
        if(curveGraph.empty())throw std::runtime_error("Select an automation source node");
        if(id==curvePattern){const auto index=SendMessageW(controls.at(id),CB_GETCURSEL,0,0);auto restore=[&]{for(int i=0;i<SendMessageW(controls.at(id),CB_GETCOUNT,0,0);++i)if(SendMessageW(controls.at(id),CB_GETITEMDATA,i,0)==curvePatternIndex)SendMessageW(controls.at(id),CB_SETCURSEL,i,0);};if(curveDraft()){restore();throw std::runtime_error("Apply or reload this curve before changing pattern");}if(index>=0)try{curveLoad(false,unsigned(SendMessageW(controls.at(id),CB_GETITEMDATA,index,0)));}catch(...){restore();throw;}return true;}
        if(id==curveKind){curveKindIndex=int(SendMessageW(controls.at(id),CB_GETCURSEL,0,0));curveFieldChanged();return true;}
        if(id==curveSnap){curveSnapIndex=int(SendMessageW(controls.at(id),CB_GETCURSEL,0,0));curveSnapUnits=std::array<unsigned,4>{256,128,64,1}.at(size_t(curveSnapIndex));return true;}
        if(id==curveFit||id==curveZoomIn||id==curveZoomOut){if(id==curveFit)curveCanvas.fit(curveRows);else curveCanvas.zoom(id==curveZoomIn?2:.5,curveRows);curvePreviewNeeded=true;rebuildCurveCanvas();return true;}
        if(id==curveSetPoint){curveReplace(curvePointFromFields());return true;}
        if(id==curvePreview){if(curveFieldsDirty)curveReplace(curvePointFromFields());curvePreviewNow();return true;}
        if(curveFieldsDirty&&id!=curveApply)throw std::runtime_error("Set the point or press Escape before changing the curve");
        if(id==curveEnable){curveEnabled=!curveEnabled;curveChanged();curveShowFields();return true;}
        if(id==curveDelete){if(curveSelected>=0){curvePoints.erase(curvePoints.begin()+curveSelected);curveSelected=curvePoints.empty()?-1:std::min(curveSelected,int(curvePoints.size())-1);curveChanged();curveShowFields();}return true;}
        if(id==curveClear||id==curveRamp){curvePoints=id==curveClear?Json::array():Json::array({{{"position",0},{"value",0},{"curve","linear"}},{{"position",curveRows*256-1},{"value",1},{"curve","linear"}}});curveSelected=curvePoints.empty()?-1:0;curveChanged();curveShowFields();return true;}
        if(id==curveApply){if(curveFieldsDirty)throw std::runtime_error("Use Set point before applying the curve");if(!curveCurrent())throw std::runtime_error("Song changed; captured curve retained. Reload before applying");const auto generation=curveGeneration;const auto document=curveDocument;curvePending=true;
            try{documentOperation("graph.automation.set",{{"expectedRevision",curveRevision},{"graph",curveGraph},{"node",curveNode},{"pattern",curvePatternIndex},{"enabled",curveEnabled},{"points",curvePoints}});curvePending=false;if(document==documentId){curveRevision=view->session.revision;if(generation==curveGeneration){curveDirty=false;curveStatus=L"Curve applied / document Undo available";}else curveStatus=L"Earlier curve applied / newer edits retained";}if(!curveDraft())refreshGraph();}catch(...){curvePending=false;throw;}return true;}
        return true;
    }
    uint32_t curvePositionAt(float x)const{const auto raw=std::round(curveCanvas.position(x)/curveSnapUnits)*curveSnapUnits;return uint32_t(std::clamp(raw,0.0,double(curveRows)*256-1));}
    bool curveMouseDown(float x,float y){
        const auto &r=curveCanvas.viewport;const ScreamSeq::WorkspaceRect hit{r.x-7,r.y-7,r.w+14,r.h+14};if(!graphCurveVisible()||!hit.contains(x,y))return false;if(curveGraph.empty()||busy||curvePending)return true;if(curveFieldsDirty)throw std::runtime_error("Set or discard the point fields before selecting another point");
        workspaceState.focus="graphCurve";SetFocus(window);curveDragBefore=curvePoints;curveDragWasDirty=curveDirty;curveDragSelection=curveSelected;curveSelected=curveCanvas.hit(x,y);
        if(curveSelected<0){Json point={{"position",curvePositionAt(x)},{"value",curveCanvas.value(y)},{"curve",curveNames[curveKindIndex]}};if(curveKindIndex==8)point["formula"]=utf8Path(effectField(curveFormula));curveReplace(std::move(point));}else curveShowFields();dragging=9;SetCapture(window);frameRequested=true;return true;
    }
    void curveMouseMove(float x,float y){if(dragging!=9||curveSelected<0)return;auto point=curvePoints[size_t(curveSelected)];point["position"]=curvePositionAt(x);point["value"]=curveCanvas.value(y);try{curveReplace(std::move(point));}catch(const std::exception &e){curveStatus=wide(e.what());}}
    void curveCancelDrag(){curvePoints=curveDragBefore;curveDirty=curveDragWasDirty;curveSelected=curveDragSelection;++curveGeneration;curvePreviewValues=Json::array();curvePreviewNeeded=true;dragging=0;ReleaseCapture();curveShowFields();rebuildCurveCanvas();}
    bool curveKey(WPARAM key,bool ctrl,bool alt,bool shift){
        if(!graphCurveVisible())return false;if(key==VK_ESCAPE&&dragging==9){curveCancelDrag();return true;}const auto focus=GetFocus();const int id=focus==window?0:GetDlgCtrlID(focus);
        if(id>=curvePattern&&id<=curveReference){if(key==VK_ESCAPE){++curveGeneration;curveShowFields();return true;}if(ctrl&&key==VK_RETURN){command(curveApply);return true;}if(key==VK_RETURN&&(id==curveRow||id==curveValue||id==curveFormula)){command(curveSetPoint);return true;}return false;}
        if(focus!=window||workspaceState.focus!="graphCurve")return false;if(ctrl&&key==VK_RETURN){command(curveApply);return true;}if(ctrl||alt)return false;
        if(key==VK_ESCAPE){++curveGeneration;curveShowFields();return true;}if(key==VK_DELETE){command(curveDelete);return true;}if(key==VK_HOME){command(curveFit);return true;}if(key==VK_OEM_PLUS||key==VK_ADD){command(curveZoomIn);return true;}if(key==VK_OEM_MINUS||key==VK_SUBTRACT){command(curveZoomOut);return true;}
        if(curveFieldsDirty)return false;
        if(key==VK_TAB){if(!curvePoints.empty()){curveSelected=(curveSelected+(shift?-1:1)+int(curvePoints.size()))%int(curvePoints.size());curveShowFields();frameRequested=true;}return true;}
        if(curveSelected>=0&&(key==VK_LEFT||key==VK_RIGHT||key==VK_UP||key==VK_DOWN)){auto p=curvePoints[size_t(curveSelected)];if(key==VK_LEFT||key==VK_RIGHT)p["position"]=uint32_t(std::clamp(p.at("position").get<double>()+(key==VK_LEFT?-1:1)*(shift?1.0:curveSnapUnits),0.0,double(curveRows)*256-1));else p["value"]=std::clamp(p.at("value").get<double>()+(key==VK_UP?1:-1)*(shift?.001:.01),0.0,1.0);curveReplace(std::move(p));return true;}return false;
    }
    bool curveWheel(float x,float y,int delta,bool horizontal,bool zoom){if(!graphCurveVisible()||!curveCanvas.viewport.contains(x,y))return false;if(zoom)curveCanvas.zoom(std::pow(2.0,double(delta)/WHEEL_DELTA),curveRows);else curveCanvas.pan((horizontal?1:-1)*double(delta)/WHEEL_DELTA*(curveCanvas.end-curveCanvas.start)*.125,curveRows);curvePreviewNeeded=true;rebuildCurveCanvas();SetTimer(window,5,120,nullptr);return true;}
    void drawGraphCurve(){
        auto &s=*surface;const auto r=sampleEditorRect(),c=curveCanvas.viewport;const float x=r.x+r.w-256;s.clip(r.x,r.y,r.w,r.h);s.fill(r.x,r.y,r.w,r.h,0x17202a);s.fill(c.x,c.y,c.w,c.h,0x10171f);
        if(!curveGraph.empty())s.uiText(curveTitle+L" · Pattern "+std::to_wstring(curvePatternIndex),c.x,c.y-18,c.w,0xa4b7c7);
        if(!curveGraph.empty()){s.clip(c.x,c.y,c.w,c.h);for(int i=0;i<=4;++i){const auto y=c.y+c.h*i/4;s.line(c.x,y,c.x+c.w,y,0x273541);}const auto step=std::max(256.0,double(curveRowsPerBeat)*256*std::ceil((curveCanvas.end-curveCanvas.start)/std::max(1.0,double(curveRowsPerBeat)*256)/16));
            for(double p=std::ceil(curveCanvas.start/step)*step;p<=curveCanvas.end;p+=step){auto at=curveCanvas.screen(p,0);s.line(at.x,c.y,at.x,c.y+c.h,0x273541);}
            for(size_t i=1;i<curveCanvas.curve.size();++i)s.line(curveCanvas.curve[i-1].x,curveCanvas.curve[i-1].y,curveCanvas.curve[i].x,curveCanvas.curve[i].y,curveEnabled?0x68d3bc:0x63727f,2);
            for(size_t i=0;i<curveCanvas.handles.size();++i){const auto p=curveCanvas.handles[i];if(p.x<c.x-6||p.x>c.x+c.w+6)continue;const auto color=int(i)==curveSelected?0xf3dfb0:0x7ce5cd;s.fill(p.x-4,p.y-4,8,8,color);}s.unclip();
            s.uiText(L"100%",c.x-36,c.y-5,35,0x94a4b4);s.uiText(L"0%",c.x-36,c.y+c.h-12,35,0x94a4b4);s.uiText(L"Row "+std::to_wstring(unsigned(curveCanvas.start/256)),c.x,c.y+c.h+5,110,0x94a4b4);s.uiText(L"Row "+std::to_wstring(unsigned(curveCanvas.end/256)),c.x+c.w-78,c.y+c.h+5,80,0x94a4b4);
        }else s.uiText(L"Select an automation source node to edit its pattern curve",c.x+10,c.y+20,c.w-20,0xb5c5d5);
        s.uiText(L"Row / 1⁄256 precision",x,r.y+68,120,0x8390a2);s.uiText(L"Value / percent",x+128,r.y+68,120,0x8390a2);
        const auto label=!curveCurrent()&&curveDraft()?L"Song changed / captured curve retained — Reload before applying":curveFieldsDirty?L"Point fields pending / Set point before Apply":curveDirty?L"Curve draft / captured source and pattern retained":curveStatus;
        s.uiText(label,r.x+8,r.y+r.h-23,r.w-16,!curveCurrent()&&curveDraft()?0xe7bb79:0x94a4b4);s.unclip();s.outline(r.x,r.y,r.w,r.h,workspaceState.focus=="graphCurve"?0x6edac5:0x303b49);
    }
    Json graphCurveSnapshot()const{Json handles=Json::array();for(size_t i=0;i<curveCanvas.handles.size();++i)handles.push_back({{"index",i},{"x",curveCanvas.handles[i].x},{"y",curveCanvas.handles[i].y}});const auto &r=curveCanvas.viewport;return {{"visible",graphCurveVisible()},{"graph",curveGraph},{"node",curveNode},{"pattern",curvePatternIndex},{"expectedRevision",curveRevision},{"stale",!curveCurrent()},{"dirty",curveDirty},{"fieldDraft",curveFieldsDirty},{"pending",curvePending},{"selected",curveSelected},{"points",curvePoints},{"enabled",curveEnabled},{"start",curveCanvas.start},{"end",curveCanvas.end},{"previewSamples",curveCanvas.curve.size()},{"status",utf8Path(curveStatus)},{"handles",handles},{"canvas",{{"x",r.x},{"y",r.y},{"width",r.w},{"height",r.h}}}};}
