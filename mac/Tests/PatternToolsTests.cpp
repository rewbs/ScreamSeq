#include "editor/PatternTools.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace Tracker;
namespace {
void check(bool value, const char *message) { if(!value) throw std::runtime_error(message); }
template <typename F> void rejects(F action) {
	try { action(); } catch(const std::invalid_argument &) { return; }
	throw std::runtime_error("Expected validation rejection");
}
void put(Document &doc, int row, int channel, Cell cell) {
	doc.edit({Edit{0, uint16_t(row), uint16_t(channel), {}, cell}});
}
void apply(Document &doc, PatternTransform t, PatternRegion r = {0, 0, 8, 0, 1}) {
	doc.edit(preparePatternTransform(doc, {r}, t));
}
void rowSplices() {
	size_t cases = 0;
	for(auto format : {MOD_TYPE_MOD, MOD_TYPE_S3M, MOD_TYPE_XM, MOD_TYPE_IT, MOD_TYPE_MPT})
		for(bool insert : {false, true}) for(uint8_t mask = 1; mask <= PatternAll; ++mask) for(int count : {1, 3, 9}) {
			Document doc(format, 4);
			for(int row = 0; row < 20; ++row) for(int channel = 0; channel < 4; ++channel) {
				Cell c{uint8_t(37 + row), uint8_t(1 + channel), 0, 0, CMD_VIBRATO, uint8_t(0x10 + row)};
				if(format != MOD_TYPE_MOD) { c.volumeCommand = VOLCMD_VOLUME; c.volume = uint8_t(row + 16); }
				put(doc, row, channel, c);
			}
			doc.annotate([](NativeSong &n) {
				n.automation.push_back({n.makeEntity().id, n.patterns.at(0).id, "unresolved-fixture", 7, true,
					{{3 * 256, .2, AutomationCurve::Linear}, {8 * 256, .8, AutomationCurve::Step}}});
			});
			const auto metadata = doc.native();
			std::vector<Cell> original;
			for(int row = 0; row < 64; ++row) for(int channel = 0; channel < 4; ++channel) original.push_back(doc.cell(0, row, channel));
			auto expected = original;
			for(int channel = 1; channel <= 2; ++channel) {
				std::vector<Cell> column;
				for(int row = 3; row < 12; ++row) column.push_back(original[row * 4 + channel]);
				if(insert) column.insert(column.begin(), count, Cell{});
				else column.erase(column.begin(), column.begin() + count);
				column.resize(9);
				for(int row = 0; row < 9; ++row) {
					auto &c = expected[(row + 3) * 4 + channel]; const auto moved = column[row];
					if(mask & PatternNote) c.note = moved.note;
					if(mask & PatternInstrument) c.instrument = moved.instrument;
					if(mask & PatternVolume) { c.volumeCommand = moved.volumeCommand; c.volume = moved.volume; }
					if(mask & PatternEffect) { c.effect = moved.effect; c.parameter = moved.parameter; }
				}
			}
			PatternTransform t; t.operation = insert ? "insertRows" : "deleteRows"; t.amount = count; t.fields = mask;
			const auto revision = doc.revision;
			// A MOD volume-only mask is empty, so it needs no loss override.
			if(format != MOD_TYPE_MOD || mask != PatternVolume) rejects([&] { preparePatternTransform(doc, {{0,3,9,1,2}}, t); });
			check(doc.revision == revision, "Loss rejection is atomic");
			t.allowDataLoss = true;
			auto edits = preparePatternTransform(doc, {{0,3,9,1,2}}, t);
			check(doc.revision == revision && doc.native() == metadata, "Row preview preserves revision and musical envelopes");
			doc.edit(edits);
			for(int row = 0; row < 64; ++row) for(int channel = 0; channel < 4; ++channel)
				check(doc.cell(0,row,channel) == expected[row * 4 + channel], "Masked row splice matches independent vector insertion/deletion across whole pattern");
			check(doc.native() == metadata, "Row splices retain pattern length and separate musical envelopes");
			Document reopened(doc.snapshotData());
			for(int row = 0; row < 64; ++row) for(int channel = 0; channel < 4; ++channel)
				check(reopened.cell(0,row,channel) == expected[row * 4 + channel], "Five-format row edits survive native module snapshot");
			if(!edits.empty()) {
				doc.undo();
				for(int row = 0; row < 64; ++row) for(int channel = 0; channel < 4; ++channel)
					check(doc.cell(0,row,channel) == original[row * 4 + channel], "One Undo restores entire row splice");
				doc.redo(); check(doc.cell(0,3,1) == expected[13], "Redo restores row splice");
			} else check(doc.revision == revision, "Empty masked splice is a revision-neutral no-op");
			++cases;
		}
	Document empty;
	for(const auto *op : {"insertRows", "deleteRows"}) {
		PatternTransform t; t.operation = op;
		for(double count : {-1., 0., .5, 10., 65536., std::numeric_limits<double>::infinity()}) {
			t.amount = count; rejects([&] { preparePatternTransform(empty, {{0,3,9,1,2}}, t); });
		}
		t.amount = 9; check(preparePatternTransform(empty, {{0,3,9,1,2}}, t).empty(), "A full empty region may shift without loss permission");
	}
	std::cout << "PASS " << cases << " five-format masked row splice references, range/loss guards, exact history and separate envelope preservation\n";
}
}
int main() {
	try {
		rowSplices();
		Document doc;
		put(doc, 0, 0, {49, 1, VOLCMD_VOLUME, 4, CMD_VIBRATO, 0x34});
		put(doc, 2, 0, {51, 2, VOLCMD_VOLUME, 12, 0, 0});
		put(doc, 4, 0, {53, 1, VOLCMD_VOLUME, 20, 0, 0});
		put(doc, 6, 0, {55, 2, VOLCMD_VOLUME, 30, 0, 0});
		put(doc, 7, 0, {NOTE_KEYOFF, 0, 0, 0, 0, 0});
		put(doc, 0, 1, {61, 3, VOLCMD_VOLUME, 32, CMD_TEMPO, 125});
		const auto original = doc.serialize();
		const auto revision = doc.revision;
		PatternTransform ramp; ramp.operation = "interpolate"; ramp.from = 4; ramp.to = 64; ramp.curve = "exponential"; ramp.only = "notes";
		auto preview = preparePatternTransform(doc, {{0, 0, 8, 0, 1}}, ramp);
		check(doc.revision == revision && doc.serialize() == original, "Preview is read-only");
		doc.edit(preview);
		check(doc.cell(0, 0, 0).volume == 4 && doc.cell(0, 2, 0).volume == 10 && doc.cell(0, 4, 0).volume == 25 && doc.cell(0, 6, 0).volume == 64, "Exponential endpoints and intermediate values");
		check(doc.cell(0, 1, 0) == Cell{} && doc.cell(0, 7, 0).note == NOTE_KEYOFF && doc.cell(0, 0, 1).volume == 32 && doc.cell(0, 0, 0).parameter == 0x34, "Only selected pitched notes' volume changes");
		Document reopened(doc.serialize());
		check(reopened.cell(0, 4, 0).volume == 25, "Transformed cells survive save/reopen");
		doc.undo(); check(doc.cell(0, 4, 0).volume == 20 && doc.cell(0, 6, 0).volume == 30, "One undo restores ramp");
		doc.redo(); check(doc.cell(0, 6, 0).volume == 64, "Redo restores ramp"); doc.undo();
		ramp.curve = "linear"; ramp.from = 0; ramp.to = 60;
		apply(doc, ramp); check(doc.cell(0, 2, 0).volume == 20 && doc.cell(0, 4, 0).volume == 40, "Linear interpolation uses row distances"); doc.undo();
		ramp.curve = "logarithmic"; apply(doc, ramp); check(doc.cell(0, 2, 0).volume > 20 && doc.cell(0, 6, 0).volume == 60, "Logarithmic curve keeps endpoints"); doc.undo();
		ramp.curve = "exponential"; rejects([&] { apply(doc, ramp); });
		ramp.from = 10; ramp.to = 50; apply(doc, ramp, {0, 0, 1, 0, 1}); check(doc.cell(0, 0, 0).volume == 10, "Single eligible cell uses start value"); doc.undo();
		PatternTransform t; t.operation = "reverse"; t.fields = PatternNote;
		apply(doc, t); check(doc.cell(0, 0, 0).note == NOTE_KEYOFF && doc.cell(0, 7, 0).note == 49 && doc.cell(0, 0, 0).volume == 4, "Masked reverse preserves unselected fields"); doc.undo();
		t.operation = "rotate"; t.amount = -1; t.fields = PatternAll;
		apply(doc, t); check(doc.cell(0, 7, 0).note == 49 && doc.cell(0, 1, 0).note == 51, "Negative rotation wraps"); doc.undo();
		t.operation = "transpose"; t.amount = 127;
		apply(doc, t); check(doc.cell(0, 0, 0).note == 120 && doc.cell(0, 7, 0).note == NOTE_KEYOFF, "Transpose clamps pitched notes and preserves note-offs"); doc.undo();
		t.operation = "remapInstrument"; t.instrumentFrom = 1; t.instrumentTo = 2; t.swap = true;
		apply(doc, t); check(doc.cell(0, 0, 0).instrument == 2 && doc.cell(0, 2, 0).instrument == 1 && doc.cell(0, 0, 1).instrument == 3, "Instrument swapping is simultaneous and scoped"); doc.undo();
		t = {}; t.operation = "randomize"; t.from = 10; t.to = 50; t.seed = 77; t.only = "notes";
		auto a = preparePatternTransform(doc, {{0, 0, 8, 0, 1}}, t), b = preparePatternTransform(doc, {{0, 0, 8, 0, 1}}, t);
		check(a.size() == b.size(), "Seeded preview count");
		for(size_t i = 0; i < a.size(); ++i) check(a[i].after == b[i].after && a[i].after.volume >= 10 && a[i].after.volume <= 50, "Seeded output reproducible and bounded");
		t.operation = "humanize"; t.amount = 8;
		for(auto e : preparePatternTransform(doc, {{0, 0, 8, 0, 1}}, t)) check(std::abs(int(e.after.volume) - e.before.volume) <= 8 && e.after.note == e.before.note, "Humanize range and field preservation");
		t.operation = "scale"; t.amount = 2; apply(doc, t); check(doc.cell(0, 0, 0).volume == 8 && doc.cell(0, 6, 0).volume == 60, "Volume multiplication"); doc.undo();
		t.operation = "fill"; t.target = "panning"; t.from = 32;
		apply(doc, t); check(doc.cell(0, 2, 0).volumeCommand == VOLCMD_PANNING && doc.cell(0, 2, 0).volume == 32, "Panning command/value stay paired"); doc.undo();
		t = {}; t.operation = "shrink"; t.amount = 2;
		rejects([&] { apply(doc, t); }); // Note-off on row 7 would disappear.
		t.allowDataLoss = true; apply(doc, t);
		check(doc.cell(0, 1, 0).note == 51 && doc.cell(0, 3, 0).note == 55 && doc.cell(0, 4, 0) == Cell{}, "Explicit lossy shrink"); doc.undo();
		t.operation = "expand"; t.allowDataLoss = false; rejects([&] { apply(doc, t); });
		t.allowDataLoss = true; apply(doc, t); check(doc.cell(0, 4, 0).note == 51 && doc.cell(0, 2, 0) == Cell{}, "Explicit expansion"); doc.undo();
		t.operation = "clear"; t.fields = PatternVolume; apply(doc, t);
		check(doc.cell(0, 0, 0).volumeCommand == 0 && doc.cell(0, 0, 0).volume == 0 && doc.cell(0, 0, 0).note == 49, "Masked clear"); doc.undo();
		rejects([&] { preparePatternTransform(doc, {{0, 0, 2, 0, 1}, {0, 1, 2, 0, 1}}, t); });
		rejects([&] { preparePatternTransform(doc, {{0, 63, 2, 0, 1}}, t); });
		t.amount = std::numeric_limits<double>::infinity(); rejects([&] { apply(doc, t); });
		const std::vector<Cell> clipboard{{60, 0, VOLCMD_PANNING, 16, 0, 0}, {62, 4, VOLCMD_VOLUME, 12, CMD_VIBRATO, 0x12}};
		doc.edit(preparePatternPaste(doc, 0, 0, 0, 2, 1, clipboard, PatternAll, "mix", false));
		check(doc.cell(0, 0, 0).note == 49 && doc.cell(0, 0, 0).volumeCommand == VOLCMD_VOLUME && doc.cell(0, 1, 0).note == 62, "Mix fills empty groups without overwriting"); doc.undo();
		doc.edit(preparePatternPaste(doc, 0, 0, 0, 2, 1, clipboard, PatternAll, "merge", false));
		check(doc.cell(0, 0, 0).note == 60 && doc.cell(0, 0, 0).instrument == 1 && doc.cell(0, 0, 0).parameter == 0x34, "Merge skips empty source fields"); doc.undo();
		rejects([&] { preparePatternPaste(doc, 0, 63, 0, 2, 1, clipboard, PatternAll, "overwrite", false); });
		doc.edit(preparePatternPaste(doc, 0, 63, 0, 2, 1, clipboard, PatternNote, "overwrite", true));
		check(doc.cell(0, 63, 0).note == 60 && doc.cell(0, 63, 0).instrument == 0, "Explicit clipped masked paste");
		Document mod(MOD_TYPE_MOD, 4); put(mod, 0, 0, {49, 1, 0, 0, 0, 0});
		t = {}; t.operation = "fill"; t.from = 20; t.only = "notes";
		rejects([&] { apply(mod, t); }); check(mod.cell(0, 0, 0).volumeCommand == 0, "Unsupported legacy field is atomic rejection");
		std::cout << "PASS pattern tools: exact curves, masks, ranges, deterministic generation, data-loss guards, paste modes, format checks, preview/undo/redo/save\n";
		return 0;
	} catch(const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
