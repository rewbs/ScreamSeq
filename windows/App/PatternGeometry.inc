    // Legacy API columns still identify code/value. The local field index selects
    // a named value slot without changing musical or navigation wire addresses.
    size_t effectFieldIndex=0;
    mutable std::shared_ptr<const ScreamSeq::NativePatternView> effectLayoutSource;
    mutable unsigned effectLayoutPattern=UINT_MAX;
    mutable std::vector<uint8_t> effectLayoutCounts;
    mutable std::vector<std::vector<ScreamSeq::PatternFields::Column>> effectLayout;
    const Json &gridCommandFields(const std::optional<Tracker::PatternCommand> &command)const {
        static const auto all=[] {std::map<std::pair<int,int>,Json> result;result[{int(Tracker::PatternCommandKind::TrackerEffect),0}]=Json::array({ScreamSeq::PatternFields::trackerField()});
            for(const auto &entry:ScreamSeq::PatternJSON::catalog()){const auto kind=Tracker::patternCommandKind(entry.at("kind").get<std::string>());const auto op=kind==Tracker::PatternCommandKind::Native?int(Tracker::nativePatternCommand(entry.at("native").get<std::string>()).operation):0;result[{int(kind),op}]=ScreamSeq::PatternFields::fields(entry,true);}return result;}();
        return all.at(command?std::pair{int(command->kind),int(command->native)}:std::pair{int(Tracker::PatternCommandKind::TrackerEffect),0});
    }
    const Json &currentEffectFields()const {return gridCommandFields(column>=3?view->effect(patternIndex,row,channel,(column-3)/2):std::optional<Tracker::PatternCommand>{});}
    void ensureEffectLayout()const {
        if(effectLayoutSource==view->nativePattern&&effectLayoutPattern==patternIndex&&effectLayoutCounts==view->effectColumns)return;
        effectLayoutSource=view->nativePattern;effectLayoutPattern=patternIndex;effectLayoutCounts=view->effectColumns;effectLayout.clear();effectLayout.resize(view->channels);
        for(unsigned c=0;c<view->channels;++c)effectLayout[c].resize(view->effectColumns[c]);
        for(const auto &entry:view->nativePattern->effects)if(entry.pattern==patternIndex&&entry.channel<effectLayout.size()&&entry.command.column<effectLayout[entry.channel].size())effectLayout[entry.channel][entry.command.column].include(gridCommandFields(entry.command));
    }
    const ScreamSeq::PatternFields::Column &effectColumnLayout(unsigned c,unsigned fx)const {ensureEffectLayout();return effectLayout.at(c).at(fx);}
    float effectColumnStart(unsigned c,unsigned fx)const {ensureEffectLayout();float result=fxStart;for(unsigned i=0;i<fx;++i)result+=effectLayout.at(c).at(i).width()+7.2f;return result;}
    size_t selectedEffectField()const {const auto &fields=currentEffectFields();return fields.empty()?0:std::min(effectFieldIndex,fields.size()-1);}
    bool moveEffectField(int direction){
        if(column<4||column%2)return false;const auto &fields=currentEffectFields();if(fields.empty())return false;
        const auto selected=selectedEffectField();if(direction>0&&selected+1<fields.size()){effectFieldIndex=selected+1;ensureCursorVisible();frameRequested=true;return true;}
        if(direction<0&&selected){effectFieldIndex=selected-1;ensureCursorVisible();frameRequested=true;return true;}return false;
    }
