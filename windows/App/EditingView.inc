// Native input maps to the same revision-guarded operations as the pipe.
    unsigned octave=4, editStep=1;
    uint64_t listedCatalog=UINT64_MAX;
    std::string inspectionClipboard;
    std::string effectPrefix,effectPrefixRevision;
    std::pair<std::wstring,std::wstring> effectText(const std::optional<Tracker::PatternCommand> &command) const {
        if(!command)return {L"..",L"...."};
        const auto &c=*command;wchar_t code[4]{},value[24]{};
        using K=Tracker::PatternCommandKind;
        switch(c.kind) {
        case K::TrackerEffect:
            if(!c.effect)wcscpy_s(code,L"..");
            else if(view->effectMasks[c.effect])swprintf_s(code,L"%c%X",view->effectLetters[c.effect],unsigned(c.parameter>>4));
            else swprintf_s(code,L"0%c",view->effectLetters[c.effect]);
            swprintf_s(value,L"%04X",unsigned(c.parameter));break;
        case K::ParameterSet:case K::ParameterSlide:
            wcscpy_s(code,c.kind==K::ParameterSet?L"PS":L"PL");swprintf_s(value,L"%02X:%04X",unsigned(c.binding),unsigned(std::lround(c.value*65535)));break;
        case K::PitchSet:case K::PitchSlide:
            wcscpy_s(code,c.kind==K::PitchSet?L"BS":L"BL");swprintf_s(value,L"%+.3f",c.value);break;
        case K::NoteCut:wcscpy_s(code,L"NC");swprintf_s(value,L"%04X",c.position%Tracker::performanceUnitsPerRow);break;
        }
        return {code,value};
    }
    void setTrackerEffect(unsigned effect,unsigned parameter) {
        edit("pattern.effect.set",{{"pattern",patternIndex},{"row",row},{"channel",channel},{"column",(column-3)/2},
            {"command",{{"kind","tracker"},{"effect",effect},{"parameter",parameter}}}});
    }
    void clearPatternSelection() {
        effectPrefix.clear();
        if(!selecting && column>=3) {
            edit("pattern.effect.set",{{"pattern",patternIndex},{"row",row},{"channel",channel},{"column",(column-3)/2},{"command",nullptr}});
            return;
        }
        const auto s=selection();
        edit("pattern.transform",{{"operation","clear"},{"pattern",patternIndex},{"startRow",s.at("startRow")},
            {"rowCount",s.at("endRow").get<unsigned>()-s.at("startRow").get<unsigned>()+1},
            {"startChannel",s.at("startChannel")},{"channelCount",s.at("endChannel").get<unsigned>()-s.at("startChannel").get<unsigned>()+1}});
    }
    void transformPatternSelection(int id) {
        const auto s=selection();const bool rows=id==patternInsertRows||id==patternDeleteRows;
        Json params={{"pattern",patternIndex},{"startRow",s.at("startRow")},
            {"rowCount",rows&&!selecting?patternRows()-row:s.at("endRow").get<unsigned>()-s.at("startRow").get<unsigned>()+1},
            {"startChannel",s.at("startChannel")},{"channelCount",s.at("endChannel").get<unsigned>()-s.at("startChannel").get<unsigned>()+1}};
        const std::array<const char *,8> operations={"reverse","rotate","expand","shrink","insertRows","deleteRows","transpose","transpose"};
        params["operation"]=operations.at(size_t(id-patternReverse));
        if(id!=patternReverse)params["amount"]=id==patternTransposeDown?-1:(id==patternExpand||id==patternShrink?2:1);
        if(id==patternDeleteRows)params["allowDataLoss"]=true;
        edit("pattern.transform",params);status=L"Pattern transform applied / document Undo available";
    }
    bool enterEffect(WPARAM key) {
        const int digit=key>='0'&&key<='9'?int(key-'0'):key>='A'&&key<='F'?int(key-'A'+10):-1;
        auto current=view->effect(patternIndex,row,channel,(column-3)/2);
        if(column%2==0) {
            if(current && current->kind!=Tracker::PatternCommandKind::TrackerEffect) {openEffectEditor();return true;}
            if(digit<0)return false;
            const unsigned effect=current?current->effect:0,old=current?current->parameter:0;
            setTrackerEffect(effect,view->effectMasks[effect]?((old&0xf0)|digit):((old*16+digit)&255));return true;
        }
        if(key<'0'||key>'Z')return false;
        if(!effectPrefix.empty()&&effectPrefixRevision!=view->session.revision) {
            effectPrefix.clear();status=L"Pattern changed / effect prefix cancelled";return true;
        }
        const std::string candidate=effectPrefix+char(key);
        for(const auto &entry:view->commands.at("effect")) if(entry.at("displayCode")==candidate) {
            const unsigned mask=entry.at("parameterMask"),value=entry.at("parameterValue");
            const unsigned old=current&&current->kind==Tracker::PatternCommandKind::TrackerEffect?current->parameter:0;
            effectPrefix.clear();setTrackerEffect(entry.at("command"),mask?(old&~mask)|value:old);
            auto n=position();n["column"]=column+1;navigate(n);return true;
        }
        for(auto code:{"PS","PL","BS","BL","NC"})if(candidate==code){effectPrefix.clear();openEffectEditor(candidate);return true;}
        const bool prefix=std::any_of(view->commands.at("effect").begin(),view->commands.at("effect").end(),[&](const auto &entry){return entry.at("displayCode").template get<std::string>().starts_with(candidate);}) || candidate=="P" || candidate=="B" || candidate=="N";
        if(candidate.size()==1&&prefix){effectPrefix=candidate;effectPrefixRevision=view->session.revision;status=L"Effect "+wide(candidate)+L"_ / type the second character";return true;}
        effectPrefix.clear();
        // Single-letter entry remains available for source-format commands.
        for(const auto &entry:view->commands.at("effect")) {
            const auto label=entry.at("label").get<std::string>();
            if(!label.empty()&&std::toupper(static_cast<unsigned char>(label[0]))==key) {
                setTrackerEffect(entry.at("command"),entry.at("suggestedParameter"));return true;
            }
        }
        return false;
    }
    void updateTitle() {
        if(!window) return;
        auto name=view->path.empty() ? L"Demo" : view->path.filename().wstring();
        auto title=(view->dirty ? L"* " : L"")+name+L" - "+wide(view->session.document.at("format").get<std::string>())+L" - ScreamSeq";
        if(busy) title+=L" [Document worker busy]";
        SetWindowTextW(window,title.c_str());
    }
    Json edit(const std::string &method,Json params) {
        params["expectedRevision"]=view->session.revision;
        return documentOperation(method,params);
    }
    bool textOwnsKeyboard() const {
        wchar_t name[32]{};auto focus=GetFocus();GetClassNameW(focus,name,32);
        return focus!=window && (_wcsicmp(name,L"Edit")==0 || _wcsicmp(name,L"ComboBox")==0 || _wcsicmp(name,L"ListBox")==0);
    }
    void advance() {auto n=position();n["row"]=std::min(row+editStep,patternRows()-1);n["following"]=false;navigate(n);}
    bool enterCell(WPARAM key) {
        if(workspaceState.focus!="pattern") return false;
        if(busy) {status=L"Document busy / Stop and navigation remain available";return true;}
        if(column>=3)return enterEffect(key);
        auto c=view->cell(patternIndex,row,channel);
        Json cell={{"pattern",patternIndex},{"row",row},{"channel",channel}};
        if(column==0) {
            const std::string lower="ZSXDCVGBHNJM",upper="Q2W3ER5T6Y7UI";
            auto low=lower.find(char(key)),high=upper.find(char(key));
            if(low!=std::string::npos) cell["note"]=1+octave*12+low;
            else if(high!=std::string::npos) cell["note"]=1+(octave+1)*12+high;
            else if(key==VK_OEM_1) cell["note"]=255;
            else return false;
        } else if(column==1) {
            int digit=key>='0' && key<='9' ? int(key-'0') : key>='A' && key<='F' ? int(key-'A'+10) : -1;
            if(digit<0) return false;
            cell[column==1 ? "instrument" : "parameter"]=(unsigned(column==1 ? c.instrument : c.parameter)*16+digit)&255;
        } else if(column==2 && key>='0' && key<='9') {
            cell["volumeCommand"]=c.volumeCommand ? c.volumeCommand : 1;
            cell["volume"]=(c.volume%10)*10+unsigned(key-'0');
        } else {
            bool found=false;
            for(const auto &command:view->commands.at(column==2 ? "volume" : "effect")) {
                auto label=command.at("label").get<std::string>();
                if(!label.empty() && std::toupper(static_cast<unsigned char>(label[0]))==key) {
                    cell[column==2 ? "volumeCommand" : "effect"]=command.at("command");
                    cell[column==2 ? "volume" : "parameter"]=command.at("suggestedParameter");found=true;break;
                }
            }
            if(!found) return false;
        }
        edit("pattern.apply",{{"cells",Json::array({cell})}});
        if(column==0) advance();return true;
    }
    Json rectangle(bool clear) const {
        Json cells=Json::array();auto selected=selection();
        const auto nr=selected.at("endRow").get<uint64_t>()-selected.at("startRow").get<uint64_t>()+1;
        const auto nc=selected.at("endChannel").get<uint64_t>()-selected.at("startChannel").get<uint64_t>()+1;
        if(nr*nc>4096) throw ScreamSeq::Api::ApiError(-32602,"Select at most 4096 cells per operation");
        for(unsigned r=selected.at("startRow");r<=selected.at("endRow").get<unsigned>();++r)
            for(unsigned c=selected.at("startChannel");c<=selected.at("endChannel").get<unsigned>();++c) {
                auto v=clear ? Tracker::Cell{} : view->cell(patternIndex,r,c);
                cells.push_back({{"pattern",patternIndex},{"row",r},{"channel",c},{"note",v.note},{"instrument",v.instrument},
                    {"volumeCommand",v.volumeCommand},{"volume",v.volume},{"effect",v.effect},{"parameter",v.parameter}});
            }
        return cells;
    }
    void copyCells() {
        const auto region=selection();
        if(std::any_of(view->nativePattern->effects.begin(),view->nativePattern->effects.end(),[&](const auto &e){return e.pattern==patternIndex&&e.channel>=region.at("startChannel").get<unsigned>()&&e.channel<=region.at("endChannel").get<unsigned>()&&e.command.position/Tracker::performanceUnitsPerRow>=region.at("startRow").get<unsigned>()&&e.command.position/Tracker::performanceUnitsPerRow<=region.at("endRow").get<unsigned>();}))
            throw std::runtime_error("This selection has native FX. Pattern 2 clipboard support is pending; no partial copy was made");
        auto s=selection();Json payload={{"row",s.at("startRow")},{"channel",s.at("startChannel")},{"cells",rectangle(false)}};
        auto data=payload.dump();if(data.size()>1024*1024) throw std::runtime_error("Copy at most 4096 cells");
        // Private QA editing must not replace the musician's desktop clipboard.
        if(inspection) {inspectionClipboard=std::move(data);return;}
        auto memory=GlobalAlloc(GMEM_MOVEABLE,data.size()+1);if(!memory) throw std::bad_alloc();
        auto target=GlobalLock(memory);if(!target) {GlobalFree(memory);throw std::runtime_error("Cannot allocate clipboard data");}
        memcpy(target,data.c_str(),data.size()+1);GlobalUnlock(memory);
        if(!OpenClipboard(window)) {GlobalFree(memory);throw std::runtime_error("Clipboard busy");}
        const bool published=EmptyClipboard() && SetClipboardData(RegisterClipboardFormatW(L"ScreamSeq.PatternCells.v1"),memory);
        CloseClipboard();if(!published) {GlobalFree(memory);throw std::runtime_error("Cannot publish pattern clipboard");}
    }
    void pasteCells() {
        std::string text=inspectionClipboard;
        if(!inspection) {
            if(!OpenClipboard(window)) throw std::runtime_error("Clipboard busy");
            auto memory=GetClipboardData(RegisterClipboardFormatW(L"ScreamSeq.PatternCells.v1"));
            if(memory) {auto size=GlobalSize(memory);if(size && size<=1024*1024) {auto p=static_cast<const char *>(GlobalLock(memory));if(p) {text.assign(p,strnlen(p,size));GlobalUnlock(memory);}}}
            CloseClipboard();
        }
        if(text.empty()) throw std::runtime_error("Clipboard has no ScreamSeq cells");
        auto payload=Json::parse(text);auto cells=payload.at("cells");
        auto coordinate=[](const Json &v) {if(!v.is_number_integer() || v<0 || v>65535) throw std::runtime_error("Invalid clipboard coordinate");};
        coordinate(payload.at("row"));coordinate(payload.at("channel"));
        if(!cells.is_array() || cells.empty() || cells.size()>4096) throw std::runtime_error("Clipboard needs 1..4096 cells");
        for(const auto &c:cells) {coordinate(c.at("row"));coordinate(c.at("channel"));}
        for(auto &c:cells) {c["row"]=int(c.at("row"))-int(payload.at("row"))+int(row);c["channel"]=int(c.at("channel"))-int(payload.at("channel"))+int(channel);c["pattern"]=patternIndex;}
        std::set<std::pair<int,int>> occupied;
        for(const auto &e:view->nativePattern->effects)if(e.pattern==patternIndex)occupied.emplace(int(e.command.position/Tracker::performanceUnitsPerRow),int(e.channel));
        for(const auto &cell:cells)
            if(occupied.contains({cell.at("row").get<int>(),cell.at("channel").get<int>()}))
                throw std::runtime_error("The paste destination has native FX. Pattern 2 clipboard support is pending; no partial paste was made");
        edit("pattern.apply",{{"cells",cells}});
    }
    std::filesystem::path chooseFile(bool save) {
        std::vector<wchar_t> path(32768);if(save) wcscpy_s(path.data(),path.size(),L"Untitled.screamseq");
        OPENFILENAMEW choice{};choice.lStructSize=sizeof(choice);choice.hwndOwner=window;
        choice.lpstrFile=path.data();choice.nMaxFile=DWORD(path.size());choice.lpstrDefExt=L"screamseq";
        choice.lpstrFilter=save ? L"ScreamSeq project\0*.screamseq;*.resonance\0\0" : L"Projects and tracker modules\0*.screamseq;*.resonance;*.mod;*.xm;*.s3m;*.it;*.mptm\0All files\0*.*\0\0";
        choice.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT|OFN_PATHMUSTEXIST|(save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        bool selected=save ? GetSaveFileNameW(&choice)!=FALSE : GetOpenFileNameW(&choice)!=FALSE;
        return selected ? std::filesystem::path(path.data()) : std::filesystem::path{};
    }
    bool saveFile(bool saveAs=false) {
        if(busy) throw ScreamSeq::Api::ApiError(-32002,"Document worker busy");
        documentOperation("flushPluginEditors",{{"force",true}});
        auto revision=view->session.revision;auto path=view->path;
        auto ext=path.extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),towlower);
        if(saveAs || path.empty() || (ext!=L".screamseq" && ext!=L".resonance")) path=chooseFile(true);
        if(path.empty()) return false;
        documentOperation("document.save",{{"expectedRevision",revision},{"path",utf8Path(path)},{"overwrite",true}});
        status=L"Native project saved atomically";return true;
    }
    bool protectUnsaved() {
        documentOperation("flushPluginEditors",{{"force",true}});
        if(!view->dirty) return true;
        auto revision=view->session.revision;
        int choice=MessageBoxW(window,L"Save changes before leaving this document?\nYes: Save    No: Discard    Cancel: Keep editing",L"Unsaved ScreamSeq project",MB_YESNOCANCEL|MB_ICONWARNING|MB_DEFBUTTON3);
        if(choice==IDCANCEL) return false;
        if(view->session.revision!=revision) throw ScreamSeq::Api::ApiError(-32001,"Document changed during the prompt; review changes again");
        return choice==IDNO || saveFile();
    }
    void openFile() {
        if(busy) throw ScreamSeq::Api::ApiError(-32002,"Document worker busy");
        if(!protectUnsaved()) return;
        auto revision=view->session.revision;auto path=chooseFile(false);if(path.empty()) return;
        documentOperation("document.open",{{"expectedRevision",revision},{"path",utf8Path(path)},{"discard",true}});
        status=L"Opened "+path.filename().wstring();
    }
    void refreshChoosers() {
        if(!controls.contains(patternChooser)) return;
        if(listedCatalog!=view->catalogRevision) {
            const auto sampleSelection=SendMessageW(controls.at(sampleCommandBase),LB_GETCURSEL,0,0);
            const auto orderSelection=SendMessageW(controls.at(orderChooser),CB_GETCURSEL,0,0);
            for(int id:{patternChooser,orderChooser,sampleCommandBase}) SendMessageW(controls.at(id),id==sampleCommandBase ? LB_RESETCONTENT : CB_RESETCONTENT,0,0);
            for(const auto &[i,sample]:view->samples) {
                auto label=std::to_wstring(i)+L"  "+wide(sample.at("name").get<std::string>());
                auto n=SendMessageW(controls.at(sampleCommandBase),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));SendMessageW(controls.at(sampleCommandBase),LB_SETITEMDATA,n,i);
            }
            for(const auto &[i,p]:view->patterns) {auto label=L"Pattern "+std::to_wstring(i);auto n=SendMessageW(controls.at(patternChooser),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));SendMessageW(controls.at(patternChooser),CB_SETITEMDATA,n,i);}
            unsigned order=0;for(const auto &p:view->session.document.at("orders")) {auto label=std::to_wstring(order++)+L" : P"+std::to_wstring(p.get<unsigned>());SendMessageW(controls.at(orderChooser),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
            if(sampleSelection!=LB_ERR) SendMessageW(controls.at(sampleCommandBase),LB_SETCURSEL,sampleSelection,0);
            if(order) SendMessageW(controls.at(orderChooser),CB_SETCURSEL,orderSelection>=0 && size_t(orderSelection)<order ? orderSelection : 0,0);
            listedCatalog=view->catalogRevision;
        }
        unsigned selected=0;for(auto &[p,v]:view->patterns) {if(p==patternIndex) break;++selected;}SendMessageW(controls.at(patternChooser),CB_SETCURSEL,selected,0);
        SendMessageW(controls.at(octaveChooser),CB_SETCURSEL,octave,0);SendMessageW(controls.at(stepChooser),CB_SETCURSEL,editStep,0);
        SendMessageW(controls.at(effectColumnChooser),CB_SETCURSEL,view->effectColumns.at(channel)-1,0);
    }
