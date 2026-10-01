// Same contract and shared preparation as the Mac pattern tools. Preparation,
// cache accounting and preview generation precede the single Undo transaction.
namespace {
Json transformPattern(Tracker::Document &document,const Json &p,
    const std::function<void()> &stop,const PatternHostHooks &host) {
    using namespace Tracker;
    need(document.editable(),"Document is not editable");
    const auto &song=document.song();
    PatternTransform transform;
    transform.operation=text(field(p,"operation"),30);
    const auto &op=transform.operation;
    const auto oneOf=[&](std::initializer_list<const char *> names){return std::any_of(names.begin(),names.end(),[&](auto n){return op==n;});};
    std::set<std::string> allowed={"operation","scope","pattern","track","startRow","rowCount","startChannel","channelCount","dryRun"};
    if(oneOf({"clear","reverse","rotate","expand","shrink","insertRows","deleteRows"}))allowed.insert("fields");
    if(oneOf({"rotate","expand","shrink","insertRows","deleteRows","transpose","scale","humanize"}))allowed.insert("amount");
    if(oneOf({"expand","shrink","insertRows","deleteRows"}))allowed.insert("allowDataLoss");
    if(oneOf({"interpolate","scale","randomize","humanize","fill"})){allowed.insert("target");allowed.insert("only");}
    if(oneOf({"interpolate","randomize","fill"}))allowed.insert("from");
    if(oneOf({"interpolate","randomize"}))allowed.insert("to");
    if(op=="interpolate")allowed.insert("curve");
    if(oneOf({"randomize","humanize"}))allowed.insert("seed");
    if(op=="remapInstrument"){allowed.insert("fromInstrument");allowed.insert("toInstrument");allowed.insert("swap");}
    for(auto i=p.begin();i!=p.end();++i)need(allowed.contains(i.key()),"Unknown pattern transform field");
    if(p.contains("fields")) {
        transform.fields=0;
        for(const auto &raw:array(p.at("fields"),4)) {
            const auto name=text(raw,20);
            const uint8_t bit=name=="note"?PatternNote:name=="instrument"?PatternInstrument:name=="volume"?PatternVolume:name=="effect"?PatternEffect:0;
            need(bit&&!(transform.fields&bit),"Invalid or repeated field mask");transform.fields|=bit;
        }
        need(transform.fields!=0,"Select at least one field");
    }
    if(allowed.contains("amount"))transform.amount=number(field(p,"amount"),-65535,65535);
    if(allowed.contains("from"))transform.from=number(field(p,"from"),0,255);
    if(allowed.contains("to"))transform.to=number(field(p,"to"),0,255);
    if(allowed.contains("seed"))transform.seed=uint32_t(integer(field(p,"seed"),0,UINT32_MAX));
    if(p.contains("target"))transform.target=text(p.at("target"),30);
    if(p.contains("only"))transform.only=text(p.at("only"),20);
    if(p.contains("curve"))transform.curve=text(p.at("curve"),20);
    transform.allowDataLoss=flag(p,"allowDataLoss");
    if(op=="remapInstrument") {
        transform.instrumentFrom=uint16_t(integer(field(p,"fromInstrument"),0,255));
        transform.instrumentTo=uint16_t(integer(field(p,"toInstrument"),0,255));transform.swap=flag(p,"swap");
    }
    const auto scope=text(p.value("scope",Json("selection")),20);
    need(scope=="selection"||scope=="channel"||scope=="note-track"||scope=="pattern"||scope=="song","Unknown pattern scope");
    need(scope=="note-track"||!p.contains("track"),"Track identity requires note-track scope");
    const auto noRegion=[&]{need(!p.contains("startRow")&&!p.contains("rowCount")&&!p.contains("startChannel")&&!p.contains("channelCount"),"This scope does not accept a region");};
    std::vector<PatternRegion> regions;
    if(scope=="song") {
        noRegion();need(!p.contains("pattern"),"Song scope does not accept a pattern");
        for(size_t i=0;i<song.Patterns.Size();++i)if(song.Patterns.IsValidPat(PATTERNINDEX(i)))regions.push_back({uint16_t(i),0,uint16_t(song.Patterns[i].GetNumRows()),0,uint16_t(song.GetNumChannels())});
    } else {
        const auto index=uint16_t(integer(field(p,"pattern"),0,UINT16_MAX));need(song.Patterns.IsValidPat(index),"Pattern does not exist");
        const auto rows=uint16_t(song.Patterns[index].GetNumRows()),channels=uint16_t(song.GetNumChannels());
        if(scope=="pattern") {noRegion();regions.push_back({index,0,rows,0,channels});}
        else if(scope=="note-track") {
            noRegion();const auto id=text(field(p,"track"),32);const auto &native=document.native();
            auto track=std::find_if(native.noteTracks.begin(),native.noteTracks.end(),[&](const auto &t){return id=="n"+std::to_string(t.bus);});
            need(track!=native.noteTracks.end(),"Note track does not exist");
            auto first=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &t){return t.second.id==track->columns.front();});
            regions.push_back({index,0,rows,first->first,uint16_t(track->columns.size())});
        } else if(scope=="channel") {
            need(!p.contains("startRow")&&!p.contains("rowCount")&&!p.contains("channelCount"),"Channel scope requires only startChannel");
            regions.push_back({index,0,rows,uint16_t(integer(field(p,"startChannel"),0,channels-1)),1});
        } else regions.push_back({index,uint16_t(integer(field(p,"startRow"),0,rows-1)),uint16_t(integer(field(p,"rowCount"),1,rows)),
            uint16_t(integer(field(p,"startChannel"),0,channels-1)),uint16_t(integer(field(p,"channelCount"),1,channels))});
    }
    std::vector<Edit> edits;auto metadata=document.native();
    try {
        edits=preparePatternTransform(document,regions,transform);
        metadata=prepareEffectTransform(document,regions,transform);
        // Any primary module effect written by the transform owns FX 1.
        std::set<std::tuple<uint64_t,uint64_t,uint32_t>> primary;
        for(const auto &e:edits)if(e.after.effect||e.after.parameter)primary.emplace(metadata.patterns.at(e.pattern).id,metadata.tracks.at(e.channel).id,e.row);
        std::erase_if(metadata.performance.commands,[&](const auto &c){return !c.column&&primary.contains({c.pattern,c.track,c.position/performanceUnitsPerRow});});
        std::sort(metadata.performance.commands.begin(),metadata.performance.commands.end(),[](const auto &a,const auto &b){return std::tie(a.pattern,a.track,a.position,a.column)<std::tie(b.pattern,b.track,b.position,b.column);});
        metadata.validate(song);document.validateEdits(edits);
    } catch(const std::invalid_argument &e) {throw Api::ApiError(-32602,e.what());}
    const auto cell=[](const Cell &c)->Json{return {{"note",c.note},{"instrument",c.instrument},{"volumeCommand",c.volumeCommand},{"volume",c.volume},{"effect",c.effect},{"parameter",c.parameter}};};
    Json changes=Json::array();
    for(size_t i=0;i<std::min<size_t>(edits.size(),512);++i){const auto &e=edits[i];changes.push_back({{"pattern",e.pattern},{"row",e.row},{"channel",e.channel},{"before",cell(e.before)},{"after",cell(e.after)}});}
    const bool dry=flag(p,"dryRun"),effectsChanged=metadata!=document.native();
    Json result={{"dryRun",dry},{"changedCells",edits.size()},{"effectsChanged",effectsChanged},{"changes",changes},{"previewTruncated",edits.size()>512}};
    if(host.validateCandidate)host.validateCandidate(metadata);
    if(!dry&&(effectsChanged||!edits.empty())){if(stop)stop();document.editNative(std::move(metadata),edits);}
    return result;
}
}
