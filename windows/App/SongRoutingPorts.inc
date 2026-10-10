  struct PortCatalog {
    std::string plugin,document,revision,error;Json ports=Json::array();
    std::vector<unsigned> indices;std::vector<bool> supported;
  };
  PortCatalog inputCatalog_,outputCatalog_;
  bool pluginPortMode(int id)const{
    const auto mode=SendMessageW(controls_.at(kind),CB_GETCURSEL,0,0);
    return id==input?(mode==4||mode==6):(mode==5||mode==6);
  }
  PortCatalog &portCatalog(int id){return id==input?inputCatalog_:outputCatalog_;}
  const PortCatalog &portCatalog(int id)const{return id==input?inputCatalog_:outputCatalog_;}
  int pickerForPort(int id)const{return id==input?inputPorts:outputPorts;}
  std::string pluginForPort(int id)const{
    const auto &node=chosenNode(id==input?destination:source);
    return pluginID(node);
  }
  void portPlaceholder(int id,const std::wstring &message){
    const auto picker=controls_.at(pickerForPort(id));
    NativeInputGate::present(picker,CB_RESETCONTENT,0,0);NativeInputGate::present(picker,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(message.c_str()));
    NativeInputGate::present(picker,CB_SETCURSEL,0,0);
  }
  void resetPluginPorts(){
    inputCatalog_=PortCatalog{};outputCatalog_=PortCatalog{};
    for(int id:{input,output})portPlaceholder(id,L"Choose a plugin");
  }
  void refreshPluginPorts(int id){
    if(!pluginPortMode(id))return;
    auto &catalog=portCatalog(id);std::string plugin;
    try{plugin=pluginForPort(id);}catch(const std::exception &){catalog=PortCatalog{};portPlaceholder(id,L"Choose a plugin");return;}
    if(catalog.plugin==plugin&&catalog.document==document_&&catalog.revision==revision_&&!catalog.ports.empty())return;
    const auto captured=context_();const auto generation=generation_;catalog=PortCatalog{};
    if(captured!=std::pair(document_,revision_)){portPlaceholder(id,L"Song changed / Reload");return;}
    pending_=true;layout();
    try{
      const auto result=request_("plugin.buses.get",{{"plugin",plugin}});
      if(context_()!=captured||generation_!=generation||pluginForPort(id)!=plugin)throw std::runtime_error("Route changed while reading plugin ports / Reload");
      if(result.at("plugin")!=plugin||!result.at("buses").is_array())throw std::runtime_error("Plugin port catalog is unavailable");
      PortCatalog next;next.plugin=plugin;next.document=captured.first;next.revision=captured.second;next.ports=result.at("buses");
      std::vector<std::wstring> labels{L"Choose a port"};int selected=0;std::set<unsigned> seen;
      std::optional<unsigned> retainedPort;
      if(wire_>=0){
        const auto &action=canvas_.edges.at(size_t(wire_)).action;const auto type=action.value("kind",std::string{});std::string savedPlugin;
        if(type=="plugin-input"&&id==input)savedPlugin=data_.at("mixer").at("sidechains").at(action.at("index").get<size_t>()).at("plugin").get<std::string>();
        if(type=="plugin-output"&&id==output)savedPlugin=action.at("plugin").get<std::string>();
        if(type=="plugin-connection")savedPlugin=action.at(id==input?"target":"source").get<std::string>();
        if(savedPlugin==plugin)retainedPort=port(id);
      }
      for(const auto &entry:next.ports)if(entry.at("direction")==(id==input?"input":"output")){
        const auto &index=entry.at("index");if(!index.is_number_integer()||index.get<double>()<0||index.get<double>()>63)throw std::runtime_error("Invalid plugin port index");
        const auto number=index.get<unsigned>();if(!seen.insert(number).second)throw std::runtime_error("Duplicate plugin port index");
        const bool supported=entry.at("supported").get<bool>();const auto channels=entry.at("channels").get<unsigned>();
        auto label=std::to_wstring(number)+L" · "+wide(entry.at("name").get<std::string>())+L" / "+std::to_wstring(channels)+L" ch";
        if(!supported)label+=L" / unsupported";else if(!entry.at("active").get<bool>())label+=L" / enables on playback";
        next.indices.push_back(number);next.supported.push_back(supported);labels.push_back(std::move(label));
        if(retainedPort==number)selected=int(labels.size()-1);
      }
      if(labels.size()==1)labels[0]=id==input?L"No declared plugin inputs":L"No declared plugin outputs";
      else if(retainedPort&&!selected)labels[0]=L"Saved port "+std::to_wstring(*retainedPort)+L" unavailable";
      const auto picker=controls_.at(pickerForPort(id));NativeInputGate::present(picker,CB_RESETCONTENT,0,0);
      for(const auto &label:labels)NativeInputGate::present(picker,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
      NativeInputGate::present(picker,CB_SETCURSEL,selected,0);catalog=std::move(next);
    }catch(const std::exception &error){catalog=PortCatalog{};catalog.error=error.what();portPlaceholder(id,L"Ports unavailable / Reload");status(wide(error.what()));}
    pending_=false;layout();
  }
  void refreshRoutePorts(){for(int id:{input,output})refreshPluginPorts(id);layout();}
  unsigned selectedPluginPort(int id)const{
    const auto &catalog=portCatalog(id);const auto selected=SendMessageW(controls_.at(pickerForPort(id)),CB_GETCURSEL,0,0);
    if(catalog.document!=document_||catalog.revision!=revision_||catalog.plugin!=pluginForPort(id)||selected<=0||size_t(selected)>catalog.indices.size())
      throw std::runtime_error("Choose a declared plugin port before connecting / unavailable ports cannot be guessed");
    if(!catalog.supported[size_t(selected-1)])throw std::runtime_error("This plugin port layout is unsupported; its saved connection is retained");
    return catalog.indices[size_t(selected-1)];
  }
  void choosePluginPort(int picker){
    changed();const auto id=picker==inputPorts?input:output;const auto value=selectedPluginPort(id);
    setting_=true;set(id,Json(value));setting_=false;
  }
