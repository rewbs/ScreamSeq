namespace {
Json pastePattern(Tracker::Document &document,const Json &p,
    const std::function<void()> &stop,const PatternHostHooks &host) {
    using namespace Tracker;
    keys(p,{"pattern","startRow","startChannel","rows","channels","cells","fields","mode","clip","dryRun","effects","bindings"});
    need(document.editable(),"Document is not editable");
    const auto pattern=uint16_t(integer(field(p,"pattern"),0,UINT16_MAX));
    const auto row=uint16_t(integer(field(p,"startRow"),0,UINT16_MAX)),channel=uint16_t(integer(field(p,"startChannel"),0,UINT16_MAX));
    const auto rows=uint16_t(integer(field(p,"rows"),1,UINT16_MAX)),channels=uint16_t(integer(field(p,"channels"),1,UINT16_MAX));
    const auto &input=array(field(p,"cells"),maximumPatternToolCells);
    need(size_t(rows)*channels==input.size(),"Clipboard cell count does not match rows and channels");
    const auto mode=text(p.value("mode",Json("overwrite")),20);const bool clip=flag(p,"clip"),dry=flag(p,"dryRun");
    uint8_t fields=PatternAll;
    if(p.contains("fields")) {
        fields=0;for(const auto &raw:array(p.at("fields"),4)) {
            const auto name=text(raw,20);const uint8_t bit=name=="note"?PatternNote:name=="instrument"?PatternInstrument:name=="volume"?PatternVolume:name=="effect"?PatternEffect:0;
            need(bit&&!(fields&bit),"Invalid or repeated field mask");fields|=bit;
        }
        need(fields!=0,"Select at least one field");
    }
    std::vector<Cell> source;source.reserve(input.size());
    for(const auto &raw:input) {
        const auto &v=array(raw,6);need(v.size()==6,"Clipboard cells must contain six integer fields");
        Cell c{uint8_t(integer(v[0],0,255)),uint8_t(integer(v[1],0,255)),uint8_t(integer(v[2],0,MAX_VOLCMDS-1)),
            uint8_t(integer(v[3],0,255)),uint8_t(integer(v[4],0,MAX_EFFECTS-1)),uint8_t(integer(v[5],0,255))};
        need((c.volumeCommand!=VOLCMD_VOLUME&&c.volumeCommand!=VOLCMD_PANNING)||c.volume<=64,"Absolute volume/panning must be 0..64");source.push_back(c);
    }
    std::vector<Edit> edits;auto metadata=document.native();
    try {
        edits=preparePatternPaste(document,pattern,row,channel,rows,channels,source,fields,mode,clip);
        const auto &song=document.song();const auto patternID=metadata.patterns.at(pattern).id;
        using Key=std::tuple<uint64_t,uint64_t,uint32_t,uint8_t>;
        std::map<Key,PatternCommand> effects;
        for(const auto &c:metadata.performance.commands)effects.emplace(Key{c.pattern,c.track,c.position/performanceUnitsPerRow,c.column},c);
        std::map<std::pair<uint16_t,uint16_t>,Edit> changed;
        for(auto e:edits) {
            if((fields&PatternEffect)&&mode=="mix"&&effects.contains({patternID,metadata.tracks.at(e.channel).id,e.row,0})) {
                e.after.effect=e.before.effect;e.after.parameter=e.before.parameter;
            }
            if(e.before!=e.after)changed.emplace(std::pair{e.row,e.channel},e);
        }
        if(fields&PatternEffect) {
            std::map<uint16_t,uint16_t> bindings;
            if(p.contains("bindings"))for(const auto &b:array(p.at("bindings"),255)) {
                keys(b,{"id","plugin","parameter","name"});const auto id=uint16_t(integer(field(b,"id"),1,255));
                need(!bindings.contains(id),"Duplicate clipboard binding");
                ParameterBinding target{text(field(b,"plugin"),128),uint32_t(integer(field(b,"parameter"),0,UINT32_MAX)),text(b.value("name",Json("")),256)};
                const auto existing=std::find_if(metadata.performance.bindings.begin(),metadata.performance.bindings.end(),[&](const auto &entry){return entry.second.plugin==target.plugin&&entry.second.parameter==target.parameter;});
                uint16_t assigned=existing==metadata.performance.bindings.end()?0:existing->first;
                if(!assigned) {
                    for(uint16_t i=1;i<=255;++i)if(!metadata.performance.bindings.contains(i)){assigned=i;break;}
                    need(assigned!=0,"All 255 parameter bindings are in use");metadata.performance.bindings.emplace(assigned,std::move(target));
                }
                bindings[id]=assigned;
            }
            if(mode=="overwrite") {
                std::set<uint64_t> tracks;for(unsigned ch=channel;ch<std::min(unsigned(channel)+channels,unsigned(song.GetNumChannels()));++ch)tracks.insert(metadata.tracks.at(ch).id);
                std::erase_if(effects,[&](const auto &entry){const auto &c=entry.second;const auto r=c.position/performanceUnitsPerRow;return c.pattern==patternID&&tracks.contains(c.track)&&r>=row&&r<unsigned(row)+rows;});
            }
            std::set<std::tuple<uint16_t,uint32_t,uint8_t>> occupied;
            if(p.contains("effects"))for(const auto &raw:array(p.at("effects"),maximumPatternCommands)) {
                keys(raw,{"channel","position","duration","column","kind","binding","value","pitchRange","effect","parameter"});
                const auto localChannel=uint16_t(integer(field(raw,"channel"),0,channels-1));
                const auto column=uint8_t(integer(field(raw,"column"),0,maximumEffectColumns-1));
                const auto localPosition=uint32_t(integer(field(raw,"position"),0,uint64_t(rows)*performanceUnitsPerRow-1));
                need(occupied.emplace(localChannel,localPosition/performanceUnitsPerRow,column).second,"Duplicate clipboard FX cell");
                const auto destinationChannel=unsigned(channel)+localChannel;const auto position=uint64_t(row)*performanceUnitsPerRow+localPosition;
                if(destinationChannel>=song.GetNumChannels()||position>=uint64_t(song.Patterns[pattern].GetNumRows())*performanceUnitsPerRow) {need(clip,"Clipboard FX lies outside the pattern");continue;}
                const auto commandKind=kind(field(raw,"kind"));need(commandKind!=PatternCommandKind::TrackerEffect||column>0,"Clipboard primary tracker effects belong in cells");
                auto binding=uint16_t(integer(raw.value("binding",Json(0)),0,255));
                if(binding){need(bindings.contains(binding),"Clipboard parameter effect needs its stable binding");binding=bindings.at(binding);}
                PatternCommand command{patternID,metadata.tracks.at(destinationChannel).id,uint32_t(position),uint32_t(integer(raw.value("duration",Json(0)),0,UINT32_MAX)),column,commandKind,binding,
                    number(raw.value("value",Json(0)),-96,96),uint8_t(integer(raw.value("pitchRange",Json(2)),1,96)),uint8_t(integer(raw.value("effect",Json(0)),0,MAX_EFFECTS-1)),uint8_t(integer(raw.value("parameter",Json(0)),0,255))};
                const auto r=uint16_t(position/performanceUnitsPerRow),ch=uint16_t(destinationChannel);const Key key{patternID,command.track,r,column};
                if(mode=="mix"&&effects.contains(key))continue;
                if(!column) {
                    const auto before=document.cell(pattern,r,ch);auto it=changed.find({r,ch});auto after=it==changed.end()?before:it->second.after;
                    need(!ModCommand::IsPcNote(before.note)&&!ModCommand::IsPcNote(after.note),"Replace the imported parameter-control note before pasting precise FX 1");
                    if(mode=="mix"&&(after.effect||after.parameter))continue;
                    after.effect=after.parameter=0;
                    if(after==before)changed.erase({r,ch});else changed[{r,ch}]={pattern,r,ch,before,after};
                }
                if(column>0)metadata.performance.columns[command.track]=std::max(uint8_t(column+1),metadata.performance.columns.contains(command.track)?metadata.performance.columns.at(command.track):uint8_t(1));
                effects[key]=command;
            }
        }
        edits.clear();edits.reserve(changed.size());
        for(const auto &[coordinate,e]:changed) {
            const Key key{patternID,metadata.tracks.at(e.channel).id,e.row,0};
            if(e.after.effect||e.after.parameter)effects.erase(key);
            need(!ModCommand::IsPcNote(e.after.note)||!effects.contains(key),"Parameter-control notes own FX 1; clear its precise command before pasting this note");
            edits.push_back(e);
        }
        metadata.performance.commands.clear();metadata.performance.commands.reserve(effects.size());
        for(const auto &[key,command]:effects)metadata.performance.commands.push_back(command);
        std::sort(metadata.performance.commands.begin(),metadata.performance.commands.end(),[](const auto &a,const auto &b){return std::tie(a.pattern,a.track,a.position,a.column)<std::tie(b.pattern,b.track,b.position,b.column);});
        metadata.validate(song);document.validateEdits(edits);
    } catch(const std::invalid_argument &error){throw Api::ApiError(-32602,error.what());}
    const auto cell=[](const Cell &c)->Json{return {{"note",c.note},{"instrument",c.instrument},{"volumeCommand",c.volumeCommand},{"volume",c.volume},{"effect",c.effect},{"parameter",c.parameter}};};
    Json changes=Json::array();for(size_t i=0;i<std::min<size_t>(edits.size(),512);++i){const auto &e=edits[i];changes.push_back({{"pattern",e.pattern},{"row",e.row},{"channel",e.channel},{"before",cell(e.before)},{"after",cell(e.after)}});}
    const bool effectsChanged=metadata!=document.native();
    Json result={{"dryRun",dry},{"changedCells",edits.size()},{"effectsChanged",effectsChanged},{"changes",changes},{"previewTruncated",edits.size()>512}};
    if(host.validateCandidate)host.validateCandidate(metadata);
    if(!dry&&(effectsChanged||!edits.empty())){if(stop)stop();document.editNative(std::move(metadata),edits);}
    return result;
}
}
