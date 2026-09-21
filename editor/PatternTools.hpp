#pragma once
#include "TrackerDocument.hpp"

namespace Tracker
{
enum PatternField : uint8_t { PatternNote = 1, PatternInstrument = 2, PatternVolume = 4, PatternEffect = 8, PatternAll = 15 };
struct PatternRegion
{
	uint16_t pattern = 0, firstRow = 0, rows = 0, firstChannel = 0, channels = 0;
};
struct PatternTransform
{
	std::string operation, target = "volume", curve = "linear", only = "values";
	uint8_t fields = PatternAll;
	double from = 0, to = 64, amount = 1;
	uint32_t seed = 1;
	uint16_t instrumentFrom = 0, instrumentTo = 0;
	bool swap = false, allowDataLoss = false;
};
// Preparation never mutates the document. Both UI and API commit the returned
// validated edits as one normal document history entry.
inline constexpr size_t maximumPatternToolCells = 262144;
std::vector<Edit> preparePatternTransform(const Document &, const std::vector<PatternRegion> &, const PatternTransform &);
std::vector<Edit> preparePatternPaste(const Document &, uint16_t pattern, uint16_t row, uint16_t channel,
	uint16_t rows, uint16_t channels, const std::vector<Cell> &, uint8_t fields, const std::string &mode, bool clip);
} // namespace Tracker
namespace Tracker {
NativeSong prepareEffectTransform(const Document &, const std::vector<PatternRegion> &, const PatternTransform &);
}
