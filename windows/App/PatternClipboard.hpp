#pragma once
#include "../Session/DocumentController.hpp"
#include "editor/PatternTools.hpp"
#include <sstream>

namespace ScreamSeq {
inline constexpr size_t maximumPatternClipboardBytes=16u*1024u*1024u;
inline constexpr std::string_view patternClipboardPrefix="ScreamSeq Pattern 2\n";

// Immutable view input only. Called on a background task, never while drawing.
inline std::string patternClipboardText(const DocumentView &view,unsigned pattern,
    unsigned firstRow,unsigned lastRow,unsigned firstChannel,unsigned lastChannel) {
    if(firstRow>lastRow||firstChannel>lastChannel||lastRow>=view.pattern(pattern).rows||lastChannel>=view.channels)
        throw std::runtime_error("Invalid pattern copy selection");
    const auto rows=lastRow-firstRow+1,channels=lastChannel-firstChannel+1;
    if(size_t(rows)*channels>Tracker::maximumPatternToolCells)throw std::runtime_error("Copy at most 262144 pattern cells");
    Json cells=Json::array(),effects=Json::array(),bindings=Json::array();
    cells.get_ref<Json::array_t &>().reserve(size_t(rows)*channels);
    for(unsigned row=firstRow;row<=lastRow;++row)for(unsigned channel=firstChannel;channel<=lastChannel;++channel) {
        const auto c=view.cell(pattern,row,channel);cells.push_back({c.note,c.instrument,c.volumeCommand,c.volume,c.effect,c.parameter});
    }
    const std::array<const char *,6> names={"parameter-set","parameter-slide","pitch-set","pitch-slide","note-cut","tracker"};
    std::set<uint16_t> used;
    for(const auto &entry:view.nativePattern->effects) {
        const auto &c=entry.command;const auto row=c.position/Tracker::performanceUnitsPerRow;
        if(entry.pattern!=pattern||entry.channel<firstChannel||entry.channel>lastChannel||row<firstRow||row>lastRow)continue;
        effects.push_back({{"channel",entry.channel-firstChannel},{"position",c.position-firstRow*Tracker::performanceUnitsPerRow},
            {"duration",c.duration},{"column",c.column},{"kind",names.at(unsigned(c.kind))},{"binding",c.binding},{"value",c.value},
            {"pitchRange",c.pitchRange},{"effect",c.effect},{"parameter",c.parameter}});
        if(c.binding)used.insert(c.binding);
    }
    for(const auto id:used) {
        const auto &b=view.nativePattern->performance.bindings.at(id);
        bindings.push_back({{"id",id},{"plugin",b.plugin},{"parameter",b.parameter},{"name",b.name}});
    }
    Json payload={{"rows",rows},{"channels",channels},{"cells",std::move(cells)},{"effects",std::move(effects)},{"bindings",std::move(bindings)}};
    auto result=std::string(patternClipboardPrefix)+payload.dump();
    if(result.size()>maximumPatternClipboardBytes)throw std::runtime_error("Pattern clipboard exceeds 16 MiB");
    return result;
}

inline Json parsePatternClipboard(std::string_view text) {
    if(text.size()>maximumPatternClipboardBytes)throw std::runtime_error("Pattern clipboard exceeds 16 MiB");
    size_t prefix=0;
    if(text.starts_with(patternClipboardPrefix))prefix=patternClipboardPrefix.size();
    else if(text.starts_with("ScreamSeq Pattern 2\r\n"))prefix=std::string_view("ScreamSeq Pattern 2\r\n").size();
    if(prefix) {
        auto payload=Json::parse(text.substr(prefix));
        if(!payload.is_object())throw std::runtime_error("Invalid pattern clipboard object");
        const std::set<std::string> keys={"rows","channels","cells","effects","bindings"};
        for(auto i=payload.begin();i!=payload.end();++i)if(!keys.contains(i.key()))throw std::runtime_error("Invalid pattern clipboard field");
        return payload;
    }
    const std::string_view legacy="Resonance Pattern 1\n",legacyCR="Resonance Pattern 1\r\n";
    if(text.starts_with(legacy))prefix=legacy.size();else if(text.starts_with(legacyCR))prefix=legacyCR.size();
    if(!prefix)throw std::runtime_error("Clipboard has no ScreamSeq pattern");
    std::istringstream lines{std::string(text.substr(prefix))};std::string line;Json cells=Json::array();unsigned rows=0,channels=0;
    while(std::getline(lines,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        std::istringstream entries(line);std::string entry;unsigned width=0;
        while(std::getline(entries,entry,'\t')) {
            std::istringstream numbers(entry);std::string number;Json cell=Json::array();
            while(std::getline(numbers,number,',')) {
                if(number.empty()||number.size()>2||number.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)throw std::runtime_error("Invalid legacy pattern cell");
                cell.push_back(std::stoul(number,nullptr,16));
            }
            if(cell.size()!=6||entry.back()==',')throw std::runtime_error("Legacy pattern cells need six hex bytes");
            cells.push_back(std::move(cell));++width;
            if(cells.size()>Tracker::maximumPatternToolCells)throw std::runtime_error("Pattern clipboard exceeds 262144 cells");
        }
        if(!width||line.back()=='\t'||(rows&&width!=channels))throw std::runtime_error("Invalid legacy pattern dimensions");
        channels=width;++rows;
    }
    if(!rows)throw std::runtime_error("Empty pattern clipboard");
    return {{"rows",rows},{"channels",channels},{"cells",std::move(cells)}};
}
}
