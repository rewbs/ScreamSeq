// One-shot routing intent. The native menu owns no editable fields; its source,
// destination and revision are captured across the message-pumping menu loop.
    struct MixerInstrumentChoice {
        std::string plugin,name;unsigned output=0;std::wstring label;bool enabled=false;
    };
    static std::vector<MixerInstrumentChoice> mixerInstrumentChoices(const Json &data) {
        std::vector<MixerInstrumentChoice> result;
        for(const auto &plugin:data.at("plugins"))if(plugin.at("instrument").get<bool>()) {
            const auto id=plugin.at("id").get<std::string>(),name=plugin.at("name").get<std::string>();
            bool hasOutput=false;
            for(const auto &port:plugin.at("audioBuses"))if(port.at("direction")=="output"){
                hasOutput=true;const auto output=port.at("index").get<unsigned>();
                std::wstring target;bool explicitRoute=false;
                for(const auto &route:data.at("instruments"))if(route.at("plugin")==id&&route.value("output",0u)==output){
                    if(explicitRoute)target+=L" + ";explicitRoute=true;
                    const auto &buses=data.at("buses");const auto found=std::find_if(buses.begin(),buses.end(),[&](const auto &bus){return bus.at("id")==route.at("target");});
                    target+=route.at("target")==""?L"Disconnected":found==buses.end()?L"Unavailable bus":wide(found->at("name").get<std::string>());
                }
                if(!explicitRoute)target=output==0?L"Master (implicit)":L"Unrouted";
                auto label=L"Out "+std::to_wstring(output+1)+L" / "+wide(port.at("name").get<std::string>())+L" → "+target;
                const bool supported=port.at("supported").get<bool>();
                if(!supported)label+=L" / unsupported port";
                else if(!port.at("active").get<bool>())label+=L" / enables on playback";
                result.push_back({id,name,output,std::move(label),supported});
            }
            if(!hasOutput)result.push_back({id,name,0,L"Outputs unavailable / check plugin location and audio ports",false});
        }
        return result;
    }
    static std::wstring mixerMenuText(const std::wstring &text){
        std::wstring result;for(auto ch:text){if(ch==L'&')result+=L'&';result+=ch==L'\t'||ch==L'\r'||ch==L'\n'?L' ':ch;}return result;
    }
    virtual unsigned chooseMixerInstrumentOutput(const std::wstring &destination,const std::vector<MixerInstrumentChoice> &choices){
        const auto menu=CreatePopupMenu();if(!menu)throw std::runtime_error("Cannot open instrument routing menu");
        struct MenuGuard{HMENU menu;~MenuGuard(){DestroyMenu(menu);}}guard{menu};
        const auto append=[](HMENU target,UINT flags,UINT_PTR id,const std::wstring &text){if(!AppendMenuW(target,flags,id,text.c_str()))throw std::runtime_error("Cannot populate instrument routing menu");};
        append(menu,MF_STRING|MF_GRAYED,0,mixerMenuText(L"Route one output only to "+destination));
        append(menu,MF_SEPARATOR,0,L"");
        if(choices.empty())append(menu,MF_STRING|MF_GRAYED,0,L"No instrument plugins / add one from the plugin library");
        std::map<std::string,HMENU> groups;
        for(size_t i=0;i<choices.size();++i){const auto &choice=choices[i];
            if(!groups.contains(choice.plugin)){
                const auto submenu=CreatePopupMenu();if(!submenu)throw std::runtime_error("Cannot open instrument output menu");
                if(!AppendMenuW(menu,MF_POPUP|MF_STRING,reinterpret_cast<UINT_PTR>(submenu),mixerMenuText(wide(choice.name)).c_str())){DestroyMenu(submenu);throw std::runtime_error("Cannot add instrument output menu");}
                groups[choice.plugin]=submenu;
            }
            append(groups.at(choice.plugin),MF_STRING|(choice.enabled?0:MF_GRAYED),UINT_PTR(i+1),mixerMenuText(choice.label));
        }
        RECT anchor{};GetWindowRect(controls.at(mixerRouteInstrument),&anchor);releaseTypedNotes();
        return TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY,anchor.left,anchor.bottom,window,nullptr);
    }
    void routeMixerInstrument(){
        if(mixerDirty)throw std::runtime_error("Apply or Reload the retained Mixer Details draft before routing an instrument");
        if(mixerSelected()<0||mixerDocument!=documentId||mixerRevision!=view->session.revision)throw std::runtime_error("Reload Mixer before routing an instrument");
        const auto document=mixerDocument,revision=mixerRevision,target=mixerTarget;const auto generation=mixerGeneration;
        const auto choices=mixerInstrumentChoices(mixerData);const auto name=wide(mixerDraft.at("name").get<std::string>())+L" ["+wide(target)+L"]";
        mixerPending=true;struct Pending{bool &flag;~Pending(){flag=false;}}pending{mixerPending};
        const auto selected=chooseMixerInstrumentOutput(name,choices);if(!selected)return;
        if(documentId!=document||view->session.revision!=revision||mixerTarget!=target||mixerGeneration!=generation||mixerDirty)
            throw std::runtime_error("Song, destination or draft changed while choosing the instrument / open Route instrument here again");
        if(selected>choices.size()||!choices[selected-1].enabled)throw std::runtime_error("Select an available instrument output");
        const auto &choice=choices[selected-1];
        retainedNativeCommand("mixer","mixer.instrument.route",{{"expectedRevision",revision},{"plugin",choice.plugin},{"output",choice.output},{"target",target}},
            {{"bus",target},{"plugin",choice.plugin},{"output",choice.output}},[&](const Json &){
                mixerPending=false;if(generation==mixerGeneration)refreshMixer();
                mixerStatus=L"Instrument output routed / other outputs retained / document Undo available";
            });
    }
