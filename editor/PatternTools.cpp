#include "PatternTools.hpp"
#include "soundlib/mod_specifications.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace Tracker
{
namespace
{
void require(bool condition, const char *message)
{
	if(!condition) throw std::invalid_argument(message);
}
bool present(const Cell &c, uint8_t field)
{
	switch(field)
	{
	case PatternNote: return c.note != 0;
	case PatternInstrument: return c.instrument != 0;
	case PatternVolume: return c.volumeCommand != 0 || c.volume != 0;
	case PatternEffect: return c.effect != 0 || c.parameter != 0;
	}
	return false;
}
bool presentMasked(const Cell &c, uint8_t fields)
{
	for(uint8_t bit = 1; bit <= PatternEffect; bit <<= 1)
		if((fields & bit) && present(c, bit)) return true;
	return false;
}
Cell merge(Cell destination, const Cell &source, uint8_t fields, const std::string &mode = "overwrite")
{
	for(uint8_t bit = 1; bit <= PatternEffect; bit <<= 1)
	{
		if(!(fields & bit)) continue;
		if(mode != "overwrite" && !present(source, bit)) continue;
		if(mode == "mix" && present(destination, bit)) continue;
		switch(bit)
		{
		case PatternNote: destination.note = source.note; break;
		case PatternInstrument: destination.instrument = source.instrument; break;
		case PatternVolume: destination.volumeCommand = source.volumeCommand; destination.volume = source.volume; break;
		case PatternEffect: destination.effect = source.effect; destination.parameter = source.parameter; break;
		}
	}
	return destination;
}
bool pitched(const Cell &c) { return c.note >= NOTE_MIN && c.note <= NOTE_MAX; }
bool eligible(const Cell &c, const PatternTransform &t)
{
	// Pitch transforms never turn note-offs/cuts/fades into pitched notes, nor
	// invent notes in empty cells. Use pattern.apply for explicit note creation.
	if(t.target == "note" && !pitched(c)) return false;
	if(t.target == "effectParameter" && !c.effect) return false;
	if(t.only == "notes") return pitched(c);
	if(t.only == "all") return true;
	if(t.target == "volume") return c.volumeCommand == VOLCMD_VOLUME;
	if(t.target == "panning") return c.volumeCommand == VOLCMD_PANNING;
	if(t.target == "instrument") return c.instrument != 0;
	return true;
}
int value(const Cell &c, const std::string &target)
{
	if(target == "note") return c.note;
	if(target == "instrument") return c.instrument;
	if(target == "effectParameter") return c.parameter;
	if(target == "panning") return c.volumeCommand == VOLCMD_PANNING ? c.volume : 32;
	return c.volumeCommand == VOLCMD_VOLUME ? c.volume : 64;
}
void setValue(Cell &c, const std::string &target, int n)
{
	if(target == "note") c.note = uint8_t(n);
	else if(target == "instrument") c.instrument = uint8_t(n);
	else if(target == "effectParameter") c.parameter = uint8_t(n);
	else { c.volumeCommand = target == "panning" ? VOLCMD_PANNING : VOLCMD_VOLUME; c.volume = uint8_t(n); }
}
class Random
{
	uint32_t state_;
public:
	explicit Random(uint32_t seed) : state_(seed ? seed : 0x6d2b79f5u) {}
	uint32_t next(uint32_t bound)
	{
		state_ ^= state_ << 13; state_ ^= state_ >> 17; state_ ^= state_ << 5;
		return uint32_t((uint64_t(state_) * bound) >> 32);
	}
};
void validateRegions(const Document &doc, const std::vector<PatternRegion> &regions)
{
	require(!regions.empty(), "No patterns selected");
	size_t cells = 0;
	std::set<std::tuple<uint16_t, uint16_t, uint16_t>> seen;
	for(const auto &r : regions)
	{
		require(r.rows && r.channels && doc.valid(r.pattern, r.firstRow, r.firstChannel)
			&& doc.valid(r.pattern, int(r.firstRow) + r.rows - 1, int(r.firstChannel) + r.channels - 1), "Region is outside the pattern");
		cells += size_t(r.rows) * r.channels;
		require(cells <= maximumPatternToolCells, "Pattern tool selection exceeds 262144 cells; select a smaller region");
		for(unsigned row = r.firstRow; row < unsigned(r.firstRow) + r.rows; ++row)
			for(unsigned ch = r.firstChannel; ch < unsigned(r.firstChannel) + r.channels; ++ch)
				require(seen.emplace(r.pattern, uint16_t(row), uint16_t(ch)).second, "Pattern tool regions overlap");
	}
}
void append(std::vector<Edit> &edits, uint16_t p, uint16_t r, uint16_t c, const Cell &before, const Cell &after)
{
	if(before != after) edits.push_back({p, r, c, before, after});
}
} // namespace

std::vector<Edit> preparePatternTransform(const Document &doc, const std::vector<PatternRegion> &regions, const PatternTransform &t)
{
	validateRegions(doc, regions);
	const std::set<std::string> operations = {"clear", "reverse", "rotate", "expand", "shrink", "insertRows", "deleteRows", "transpose", "remapInstrument", "interpolate", "scale", "randomize", "humanize", "fill"};
	require(operations.contains(t.operation), "Unknown pattern transform");
	require(t.fields > 0 && t.fields <= PatternAll, "Select at least one valid field");
	require(std::isfinite(t.from) && std::isfinite(t.to) && std::isfinite(t.amount), "Pattern values must be finite");
	require(t.only == "values" || t.only == "notes" || t.only == "all", "Unknown cell filter");
	require(t.target == "note" || t.target == "instrument" || t.target == "volume" || t.target == "panning" || t.target == "effectParameter", "Unknown target field");
	require(t.curve == "linear" || t.curve == "exponential" || t.curve == "logarithmic", "Unknown interpolation curve");
	const auto &spec = doc.song().GetModSpecifications();
	const int minimum = t.target == "note" ? spec.noteMin : 0;
	const int maximum = t.target == "note" ? spec.noteMax : (t.target == "volume" || t.target == "panning") ? 64 : 255;
	const bool numeric = t.operation == "interpolate" || t.operation == "scale" || t.operation == "randomize" || t.operation == "humanize" || t.operation == "fill";
	if(numeric && (t.operation == "interpolate" || t.operation == "randomize" || t.operation == "fill"))
	{
		require(t.from >= minimum && t.from <= maximum, "Start value is outside the target field's range");
		if(t.operation != "fill") require(t.to >= minimum && t.to <= maximum, "End value is outside the target field's range");
	}
	if(t.operation == "interpolate" && t.curve == "exponential") require(t.from > 0 && t.to > 0, "Exponential interpolation needs positive endpoints");
	if(t.operation == "randomize") require(t.from <= t.to && std::floor(t.from) == t.from && std::floor(t.to) == t.to, "Random bounds must be ordered integers");
	if(t.operation == "scale") require(t.amount >= 0 && t.amount <= 16, "Scale factor must be 0..16");
	if(t.operation == "humanize") require(t.amount >= 0 && t.amount <= 255 && std::floor(t.amount) == t.amount, "Humanize amount must be an integer from 0..255");
	if(t.operation == "transpose") require(t.amount >= -127 && t.amount <= 127 && std::floor(t.amount) == t.amount, "Transpose amount must be an integer from -127..127");
	if(t.operation == "rotate") require(t.amount >= -65535 && t.amount <= 65535 && std::floor(t.amount) == t.amount, "Row offset must be an integer from -65535..65535");
	const bool spliceRows = t.operation == "insertRows" || t.operation == "deleteRows";
	if(spliceRows) require(t.amount >= 1 && t.amount <= 65535 && std::floor(t.amount) == t.amount, "Row count must be an integer from 1..65535");
	if(t.operation == "expand" || t.operation == "shrink") require(t.amount >= 2 && t.amount <= 16 && std::floor(t.amount) == t.amount, "Time factor must be an integer from 2..16");
	require(t.instrumentFrom <= 255 && t.instrumentTo <= 255, "Pattern instrument numbers must be 0..255");
	Random random(t.seed);
	std::vector<Edit> edits;
	for(const auto &region : regions)
	{
		if(spliceRows) require(t.amount <= region.rows, "Row count exceeds the selected region");
		for(unsigned ch = region.firstChannel; ch < unsigned(region.firstChannel) + region.channels; ++ch)
		{
			std::vector<Cell> source;
			source.reserve(region.rows);
			int first = -1, last = -1;
			for(int row = 0; row < region.rows; ++row)
			{
				const auto cell = doc.cell(region.pattern, region.firstRow + row, ch);
				source.push_back(cell);
				if(eligible(cell, t)) { if(first < 0) first = row; last = row; }
				if(!t.allowDataLoss && presentMasked(cell, t.fields))
				{
					if(t.operation == "expand") require(row * int(t.amount) < region.rows, "Expansion would discard fields beyond the selection; enlarge the selection or allow data loss");
					if(t.operation == "shrink") require(row % int(t.amount) == 0, "Shrink would discard fields between retained rows; allow data loss explicitly");
					if(t.operation == "insertRows") require(row + int(t.amount) < region.rows, "Insertion would discard fields at the end of the region; enlarge it or allow data loss in Pattern Tools");
					if(t.operation == "deleteRows") require(row >= int(t.amount), "Deletion would remove fields at the start of the region; allow data loss explicitly");
				}
			}
			for(int row = 0; row < region.rows; ++row)
			{
				Cell after = source[row];
				if(t.operation == "clear") after = merge(after, {}, t.fields);
				else if(t.operation == "reverse") after = merge(after, source[region.rows - row - 1], t.fields);
				else if(t.operation == "rotate")
				{
					const int offset = int(t.amount) % region.rows;
					after = merge(after, source[(row - offset + region.rows) % region.rows], t.fields);
				}
				else if(t.operation == "expand") after = merge(after, row % int(t.amount) ? Cell{} : source[row / int(t.amount)], t.fields);
				else if(t.operation == "shrink") after = merge(after, row * int(t.amount) >= region.rows ? Cell{} : source[row * int(t.amount)], t.fields);
				else if(spliceRows)
				{
					const int fromRow = row + (t.operation == "insertRows" ? -int(t.amount) : int(t.amount));
					after = merge(after, fromRow < 0 || fromRow >= region.rows ? Cell{} : source[fromRow], t.fields);
				}
				else if(t.operation == "transpose" && pitched(after)) after.note = uint8_t(std::clamp(int(after.note) + int(t.amount), int(spec.noteMin), int(spec.noteMax)));
				else if(t.operation == "remapInstrument")
				{
					if(after.instrument == t.instrumentFrom) after.instrument = uint8_t(t.instrumentTo);
					else if(t.swap && after.instrument == t.instrumentTo) after.instrument = uint8_t(t.instrumentFrom);
				}
				else if(numeric && eligible(after, t))
				{
					double n = value(after, t.target);
					if(t.operation == "fill") n = t.from;
					if(t.operation == "scale") n *= t.amount;
					if(t.operation == "humanize") n += int(random.next(uint32_t(t.amount) * 2 + 1)) - int(t.amount);
					if(t.operation == "randomize") n = t.from + random.next(uint32_t(t.to - t.from) + 1);
					if(t.operation == "interpolate")
					{
						double x = first == last ? 0 : double(row - first) / (last - first);
						if(t.curve == "logarithmic") x = std::log1p(9 * x) / std::log(10.0);
						n = t.curve == "exponential" ? t.from * std::pow(t.to / t.from, x) : t.from + (t.to - t.from) * x;
					}
					setValue(after, t.target, std::clamp(int(std::lround(n)), minimum, maximum));
				}
				append(edits, region.pattern, uint16_t(region.firstRow + row), uint16_t(ch), source[row], after);
			}
		}
	}
	doc.validateEdits(edits);
	return edits;
}

std::vector<Edit> preparePatternPaste(const Document &doc, uint16_t pattern, uint16_t row, uint16_t channel,
	uint16_t rows, uint16_t channels, const std::vector<Cell> &cells, uint8_t fields, const std::string &mode, bool clip)
{
	require(rows && channels && size_t(rows) * channels == cells.size() && cells.size() <= maximumPatternToolCells, "Invalid or oversized clipboard dimensions");
	require(doc.valid(pattern, row, channel), "Paste origin is outside the pattern");
	require(fields > 0 && fields <= PatternAll, "Select at least one valid field");
	require(mode == "overwrite" || mode == "merge" || mode == "mix", "Unknown paste mode");
	require(clip || doc.valid(pattern, int(row) + rows - 1, int(channel) + channels - 1), "Clipboard exceeds the pattern; clipping must be explicit");
	std::vector<Edit> edits;
	for(unsigned r = 0; r < rows; ++r)
		for(unsigned c = 0; c < channels; ++c)
		{
			if(!doc.valid(pattern, int(row) + r, int(channel) + c)) continue;
			const auto before = doc.cell(pattern, int(row) + r, int(channel) + c);
			append(edits, pattern, uint16_t(row + r), uint16_t(channel + c), before, merge(before, cells[r * channels + c], fields, mode));
		}
	doc.validateEdits(edits);
	return edits;
}
} // namespace Tracker

namespace Tracker {
NativeSong prepareEffectTransform(const Document &doc,const std::vector<PatternRegion> &regions,const PatternTransform &t) {
  NativeSong next=doc.native();
  const bool move=t.operation=="clear"||t.operation=="reverse"||t.operation=="rotate"||t.operation=="expand"||t.operation=="shrink"||t.operation=="insertRows"||t.operation=="deleteRows";
  if(!move || !(t.fields&PatternEffect)) return next;
  for(const auto &region:regions) {
    const auto pattern=next.patterns.at(region.pattern).id;
    std::set<uint64_t> tracks;
    for(uint16_t ch=region.firstChannel;ch<region.firstChannel+region.channels;++ch)tracks.insert(next.tracks.at(ch).id);
    std::vector<PatternCommand> commands;
    commands.reserve(next.performance.commands.size());
    for(auto c:next.performance.commands) {
      const int row=int(c.position/performanceUnitsPerRow)-region.firstRow;
      if(c.pattern!=pattern||!tracks.contains(c.track)||row<0||row>=region.rows){commands.push_back(c);continue;}
      if(t.operation=="clear")continue;
      int target=row;uint32_t fraction=c.position%performanceUnitsPerRow;
      if(t.operation=="reverse") target=region.rows-1-row;
      else if(t.operation=="rotate")target=(row+int(t.amount)%region.rows+region.rows)%region.rows;
      else if(t.operation=="insertRows")target+=int(t.amount);
      else if(t.operation=="deleteRows")target-=int(t.amount);
      else if(t.operation=="expand"){target*=int(t.amount);c.duration*=uint32_t(t.amount);}
      else if(t.operation=="shrink"){
        if(row%int(t.amount)){if(!t.allowDataLoss)throw std::invalid_argument("Shrink would discard FX columns; allow data loss explicitly");continue;}
        target/=int(t.amount);if(c.duration)c.duration=std::max(1u,c.duration/uint32_t(t.amount));
      }
      if(target<0||target>=region.rows){if(!t.allowDataLoss)throw std::invalid_argument("This row edit would discard FX columns; allow data loss explicitly");continue;}
      c.position=uint32_t(region.firstRow+target)*performanceUnitsPerRow+fraction;
      const auto end=uint32_t(doc.song().Patterns[region.pattern].GetNumRows())*performanceUnitsPerRow;
      if(c.duration>end-c.position){if(!t.allowDataLoss)throw std::invalid_argument("This row edit would move an FX slide beyond the pattern");c.duration=end-c.position;}
      commands.push_back(c);
    }
    next.performance.commands=std::move(commands);
  }
  next.validate(doc.song());return next;
}
}
