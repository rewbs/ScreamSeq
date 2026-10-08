// Private class-body presentation helpers. No field normalization or musical writes.
  static std::wstring decimal(double value,int precision=7){std::wostringstream out;out<<std::setprecision(precision)<<value;return out.str();}
  std::wstring effectCode(const Json &event)const{
    const auto command=event.value("effect",0u),parameter=event.value("parameter",0u);if(!command)return L"—";
    const auto found=std::find_if(effects_.begin(),effects_.end(),[&](const auto &effect){return effect.at("command")==command&&
      (parameter&effect.at("parameterMask").template get<unsigned>())==effect.at("parameterValue").template get<unsigned>();});
    wchar_t hex[8]{};swprintf_s(hex,L"%02X",parameter);
    return (found!=effects_.end()?wide(found->at("displayCode").template get<std::string>()):L"?")+L" "+hex;
  }
  std::array<std::wstring,6> hitColumns(const Json &event)const{
    const auto rowOffset=double(event.at("position").get<unsigned>()%unitsPerRow)/unitsPerRow;
    const bool onset=event.at("note").get<unsigned>()<128;
    return {decimal(rowOffset/rowsPerBeat_)+L" b",decimal(rowOffset)+L" r",noteName(event.at("note").get<unsigned>()),
      onset?L"I"+std::to_wstring(event.at("instrument").get<unsigned>()):L"I—",
      onset?L"V"+std::to_wstring(event.at("velocity").get<unsigned>()):L"V—",effectCode(event)};
  }
  Json hitSummary(const Json &event)const{Json result=Json::array();for(const auto &column:hitColumns(event))result.push_back(utf8(column));return result;}
  std::wstring details()const{
    std::wstring out=L"TIMING AND SELECTED HIT\r\n";
    if(!hasTarget())out+=L"Tools → Load selection captures a pattern row.\r\n";
    else {
      out+=L"Pattern "+std::to_wstring(target_.pattern)+L" · Row "+std::to_wstring(target_.row)+L" · Channel "+std::to_wstring(target_.channel+1)+L"\r\n";
      out+=L"Beat "+std::to_wstring(target_.row/rowsPerBeat_+1)+L" · row "+std::to_wstring(target_.row%rowsPerBeat_+1)+L" of "+std::to_wstring(rowsPerBeat_)+L"\r\n";
      out+=L"This row spans 1 row = "+decimal(1.0/rowsPerBeat_,17)+L" beat; its endpoint belongs to the next row.\r\n";
      if(selected_>=0){const auto &event=draft_.at(size_t(selected_));const auto position=event.at("position").get<unsigned>()%unitsPerRow;
        out+=noteName(event.at("note").get<unsigned>())+L" · "+(event.at("note").get<unsigned>()<128?L"Instrument "+std::to_wstring(event.at("instrument").get<unsigned>())+L" · Velocity "+std::to_wstring(event.at("velocity").get<unsigned>()):L"Release event; instrument and velocity do not apply")+L"\r\n";
        out+=L"Staged offset: "+decimal(double(position)/unitsPerRow,17)+L" row = "+decimal(double(position)/unitsPerRow/rowsPerBeat_,17)+L" beat ("+std::to_wstring(position)+L" / 65536 row units).\r\n";
      }else out+=L"No selected hit. Timeline: double-click to add one.\r\n";
      out+=L"Offset field: "+field(offset)+(units_==0?L" beats":L" rows")+L"\r\n";
      try{const auto amount=numeric(offset,0,1)*(units_==0?rowsPerBeat_:1);require(amount<1,"Offset outside row");
        out+=L"Visible offset = "+decimal(amount,17)+L" row = "+decimal(amount/rowsPerBeat_,17)+L" beat.\r\n";
      }catch(const std::exception &){out+=L"The offset remains an invalid raw draft; correct it before Check or Apply.\r\n";}
    }
    out+=L"\r\nNOTE-LOCAL EFFECT\r\n";const auto choice=selection(localEffect);
    if(choice>=0&&size_t(choice)<effects_.size()){
      const auto &effect=effects_.at(size_t(choice));out+=wide(effect.at("displayCode").get<std::string>()+" · "+effect.at("name").get<std::string>())+L"\r\n";
      const auto description=effect.value("description",std::string{});if(!description.empty())out+=wide(description)+L"\r\n";
      out+=L"Hex field: "+field(parameter)+L"\r\nAllowed hex values: ";bool first=true;
      for(const auto &value:effect.at("allowedParameters")){wchar_t hex[8]{};swprintf_s(hex,L"%02X",value.get<unsigned>());if(!first)out+=L", ";out+=hex;first=false;}
      out+=L"\r\nOff and Cut do not carry a note-local effect.\r\n";
    }else out+=L"Capture a row to see its supported note-local effects.\r\n";
    out+=L"\r\nLOCAL KEYS AND POINTER\r\nTimeline: drag timing and velocity; Shift locks velocity; Alt bypasses snap. Double-click adds a hit.\r\nTimeline arrows change timing or velocity; Alt Left/Right moves one row unit; Shift Up/Down changes velocity by eight. Delete removes the selected hit.\r\nCtrl+D duplicates on Timeline or Hit list. F6 switches Timeline and Hit.\r\nEnter applies from editable fields, Timeline or Hit list. Enter on a button activates that button. Ctrl+Enter applies explicitly.\r\nEscape cancels a drag first; otherwise it returns to Pattern.\r\nDetails is read-only: select text and use native Copy. Enter here does not save.\r\nCheck validates the draft without playing audio or saving. Apply saves all hits in one document Undo step.";
    return out;
  }
