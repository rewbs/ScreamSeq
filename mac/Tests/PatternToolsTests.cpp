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
void nativeColumns() {
	// Precise notes, graph lanes and curves follow the same row mapping as cells.
	const uint32_t unit = performanceUnitsPerRow;
	Document doc(MOD_TYPE_MPT, 4);
	put(doc, 2, 1, {61, 1, VOLCMD_VOLUME, 30, 0, 0});
	doc.annotate([&](NativeSong &n) {
		const auto p = n.patterns.at(0).id, t = n.tracks.at(1).id, other = n.tracks.at(2).id;
		n.preciseNotes = {{p, t, 2 * unit + 100, 1, 61, 100}, {p, t, 5 * unit + 7, 0, 255, 127}, {p, other, 2 * unit + 100, 1, 62, 100}, {p, t, 20 * unit, 1, 63, 100}};
		const auto master = n.masterID;
		for(const auto &[index, track] : n.tracks) n.mixer.buses.push_back({track.id, master, MixerBusKind::Track, "Track"});
		n.mixer.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
		n.signal.lanes[t] = 1; n.signal.lanes[other] = 1; n.signal.lanes[master] = 1;
		n.signal.commands = {{p, t, 0, 3 * unit + 9, 0, SignalCommandKind::Clear}, {p, other, 0, 3 * unit + 9, 0, SignalCommandKind::Clear},
			{p, t, 0, 20 * unit, 0, SignalCommandKind::Clear}, {p, master, 0, 3 * unit, 0, SignalCommandKind::Clear}};
		n.automation.push_back({n.makeEntity().id, p, "unresolved-fixture", 7, true,
			{{2 * 256 + 128, .2, AutomationCurve::Linear}, {5 * 256, .8, AutomationCurve::Step}, {20 * 256, .5, AutomationCurve::Linear}}});
		n.automation.push_back({n.makeEntity().id, p, "unresolved-fixture", 8, true, {{4 * 256, .4, AutomationCurve::Linear}}});
	});
	const auto before = doc.native();
	const auto revision = doc.revision;
	auto notes = [](const NativeSong &n) { std::vector<std::pair<int, uint32_t>> v; for(const auto &e : n.preciseNotes) v.emplace_back(e.note, e.position); std::sort(v.begin(), v.end()); return v; };
	// 0 is the master bus, 1 the selected column and 2 another column.
	auto graph = [&](const NativeSong &n) { std::vector<std::pair<int, uint32_t>> v; for(const auto &c : n.signal.commands) v.emplace_back(c.target == before.tracks.at(1).id ? 1 : c.target == before.tracks.at(2).id ? 2 : 0, c.position); std::sort(v.begin(), v.end()); return v; };
	auto points = [](const NativeSong &n, size_t lane) { std::vector<std::pair<uint32_t, double>> v; for(const auto &point : n.automation.at(lane).points) v.emplace_back(point.position, point.value); return v; };
	using Notes = std::vector<std::pair<int, uint32_t>>; using Graph = std::vector<std::pair<int, uint32_t>>; using Points = std::vector<std::pair<uint32_t, double>>;
	const PatternRegion column{0, 0, 8, 1, 1}, rows{0, 0, 8, 0, 4}, wide{0, 0, 16, 0, 4};
	PatternTransform t; t.operation = "insertRows"; t.amount = 2;
	auto next = prepareEffectTransform(doc, {column}, t);
	check(doc.revision == revision && doc.native() == before, "Native column preview is read-only");
	check(notes(next) == Notes{{61, 4 * unit + 100}, {62, 2 * unit + 100}, {63, 20 * unit}, {255, 7 * unit + 7}}, "Row insertion moves the selected column's precise notes and keeps their row offsets");
	check(graph(next) == Graph{{0, 3 * unit}, {1, 5 * unit + 9}, {1, 20 * unit}, {2, 3 * unit + 9}}, "Row insertion moves the selected column's graph lane");
	check(next.automation == before.automation, "Pattern-wide curves stay when only some columns move");
	t.fields = PatternEffect; next = prepareEffectTransform(doc, {column}, t);
	check(next.preciseNotes == before.preciseNotes && next.signal == before.signal, "An FX-only transform leaves notes and graph lanes");
	t.fields = PatternNote | PatternInstrument; next = prepareEffectTransform(doc, {column}, t);
	check(notes(next)[0].second == 4 * unit + 100 && next.signal == before.signal && next.automation == before.automation, "A note transform moves precise notes only");
	t.fields = PatternAll; next = prepareEffectTransform(doc, {rows}, t);
	check(notes(next) == Notes{{61, 4 * unit + 100}, {62, 4 * unit + 100}, {63, 20 * unit}, {255, 7 * unit + 7}} && graph(next) == Graph{{0, 5 * unit}, {1, 5 * unit + 9}, {1, 20 * unit}, {2, 5 * unit + 9}}, "Whole rows move every column");
	check(points(next, 0) == Points{{4 * 256 + 128, .2}, {7 * 256, .8}, {20 * 256, .5}} && points(next, 1) == Points{{6 * 256, .4}}, "Whole rows move pattern curves");
	const auto edits = preparePatternTransform(doc, {rows}, t);
	check(doc.editNative(next, edits) && doc.cell(0, 4, 1).note == 61 && doc.native() == next, "Cells and native columns commit together");
	Document reopened(doc.snapshotData());
	check(reopened.cell(0, 4, 1).note == 61, "Moved cells survive a snapshot");
	doc.undo(); check(doc.native() == before && doc.cell(0, 2, 1).note == 61 && doc.cell(0, 4, 1) == Cell{}, "One Undo restores cells and native columns");
	t.amount = 3;
	rejects([&] { prepareEffectTransform(doc, {column}, t); });
	t.allowDataLoss = true; next = prepareEffectTransform(doc, {rows}, t);
	check(notes(next) == Notes{{61, 5 * unit + 100}, {62, 5 * unit + 100}, {63, 20 * unit}} && points(next, 0) == Points{{5 * 256 + 128, .2}, {20 * 256, .5}}, "Explicit loss discards events pushed out of the region");
	t = {}; t.operation = "deleteRows"; t.amount = 3;
	rejects([&] { prepareEffectTransform(doc, {column}, t); });
	t.allowDataLoss = true; next = prepareEffectTransform(doc, {rows}, t);
	check(notes(next) == Notes{{63, 20 * unit}, {255, 2 * unit + 7}} && graph(next) == Graph{{0, 0}, {1, 9}, {1, 20 * unit}, {2, 9}} && points(next, 0) == Points{{2 * 256, .8}, {20 * 256, .5}} && points(next, 1) == Points{{256, .4}}, "Row deletion moves later events up");
	t = {}; t.operation = "reverse"; next = prepareEffectTransform(doc, {rows}, t);
	check(notes(next) == Notes{{61, 5 * unit + 100}, {62, 5 * unit + 100}, {63, 20 * unit}, {255, 2 * unit + 7}} && points(next, 0) == Points{{2 * 256, .8}, {5 * 256 + 128, .2}, {20 * 256, .5}}, "Reverse keeps curves ordered");
	t.operation = "rotate"; t.amount = 3; next = prepareEffectTransform(doc, {rows}, t);
	check(notes(next) == Notes{{61, 5 * unit + 100}, {62, 5 * unit + 100}, {63, 20 * unit}, {255, 7}} && points(next, 0) == Points{{0, .8}, {5 * 256 + 128, .2}, {20 * 256, .5}}, "Rotation wraps native columns");
	t.operation = "expand"; t.amount = 2;
	rejects([&] { prepareEffectTransform(doc, {rows}, t); });
	next = prepareEffectTransform(doc, {wide}, t);
	check(notes(next) == Notes{{61, 4 * unit + 100}, {62, 4 * unit + 100}, {63, 20 * unit}, {255, 10 * unit + 7}} && points(next, 0) == Points{{5 * 256, .2}, {10 * 256, .8}, {20 * 256, .5}}, "Expansion stretches curves and moves events to their cells' rows");
	t.operation = "shrink";
	rejects([&] { prepareEffectTransform(doc, {rows}, t); }); // Events on odd rows have no retained cell.
	t.allowDataLoss = true; next = prepareEffectTransform(doc, {rows}, t);
	check(notes(next) == Notes{{61, unit + 100}, {62, unit + 100}, {63, 20 * unit}} && graph(next) == Graph{{1, 20 * unit}} && points(next, 0) == Points{{256 + 64, .2}, {2 * 256 + 128, .8}, {20 * 256, .5}}, "Shrink compresses curves and keeps events of retained rows");
	t = {}; t.operation = "clear"; next = prepareEffectTransform(doc, {rows}, t);
	check(notes(next) == Notes{{63, 20 * unit}} && graph(next) == Graph{{1, 20 * unit}}, "Clear removes precise notes and graph commands inside the region");
	check(next.automation == before.automation && next.envelopeLinks == before.envelopeLinks, "Clear never edits pattern-wide curves");
	next = prepareEffectTransform(doc, {column}, t);
	check(notes(next) == Notes{{62, 2 * unit + 100}, {63, 20 * unit}} && graph(next) == Graph{{0, 3 * unit}, {1, 20 * unit}, {2, 3 * unit + 9}}, "Clear is limited to the selected columns");
	t.fields = PatternEffect; check(prepareEffectTransform(doc, {rows}, t) == before, "An FX-only clear without FX commands changes nothing");
	t.fields = PatternAll;
	check(prepareEffectTransform(doc, {{0, 8, 8, 0, 4}}, t) == before && prepareEffectTransform(doc, {{0, 32, 8, 0, 4}}, t) == before, "Clearing plain cells leaves native data identical, so playback continues");
	{
		// A linked curve belongs to its template: Delete, reverse and rotate leave it alone.
		Document linked(MOD_TYPE_MPT, 4);
		linked.annotate([&](NativeSong &n) {
			const auto p = n.patterns.at(0).id, t = n.tracks.at(1).id;
			EnvelopeTemplate shape{n.makeEntity().id, "Ramp", {64 * 256, 4, {{0, 0, AutomationCurve::Linear}, {32 * 256, 1, AutomationCurve::Linear}}}};
			const auto lane = n.makeEntity().id;
			n.automation.push_back({lane, p, "unresolved-fixture", 7, true, fitEnvelope(shape.shape, 64 * 256)});
			n.automation.push_back({n.makeEntity().id, p, "unresolved-fixture", 8, true, {{256, .4, AutomationCurve::Linear}}});
			n.envelopeLinks.push_back({{EnvelopeTargetKind::Parameter, lane, 0}, shape.id, 64 * 256});
			n.envelopeBank.push_back(std::move(shape));
			n.preciseNotes = {{p, t, 2 * unit + 100, 1, 61, 100}};
		});
		const auto source = linked.native();
		const PatternRegion all{0, 0, 64, 0, 4};
		PatternTransform edit; edit.operation = "clear";
		auto result = prepareEffectTransform(linked, {all}, edit);
		check(result.automation == source.automation && result.envelopeLinks == source.envelopeLinks && result.preciseNotes.empty(), "Delete over a whole pattern keeps linked and unlinked curves");
		for(const auto *operation : {"reverse", "rotate"}) {
			edit.operation = operation; edit.amount = 1;
			result = prepareEffectTransform(linked, {all}, edit);
			check(result.automation.at(0) == source.automation.at(0) && result.automation.at(1) != source.automation.at(1) && result.preciseNotes != source.preciseNotes, "Reverse and rotate skip linked curves and still move everything else");
		}
		edit.operation = "insertRows"; edit.amount = 1;
		rejects([&] { prepareEffectTransform(linked, {all}, edit); });
		edit.allowDataLoss = true; result = prepareEffectTransform(linked, {all}, edit);
		check(result.automation.at(0) == source.automation.at(0) && result.automation.at(1).points.at(0).position == 2 * 256, "Row insertion with explicit loss keeps the linked curve and moves the others");
	}
	t.operation = "transpose"; check(prepareEffectTransform(doc, {rows}, t) == before, "Value transforms leave native columns");
	std::cout << "PASS native columns: precise notes, graph lanes and curves follow row transforms with masks, loss guards and one Undo\n";
}
}
void preciseClipboard() {
  Document doc(MOD_TYPE_MPT,4);
  auto original=doc.native();const auto pattern=original.patterns.at(0).id,track=original.tracks.at(0).id;
  original.preciseNotes={{pattern,track,1234,1,49,100,0,0},{pattern,original.tracks.at(1).id,8192,2,61,90,0,0}};
  doc.editNative(original,{});
  PatternTransform clear;clear.operation="clear";const std::vector<PatternRegion> regions{{0,0,1,0,1}};
  auto next=prepareEffectTransform(doc,regions,clear);
  check(next.preciseNotes.size()==1&&next.preciseNotes[0].track!=track,"Cut clears only selected precise events");
  doc.editNative(next,preparePatternTransform(doc,regions,clear));doc.undo();check(doc.native()==original,"One Undo restores precise cut");doc.redo();
  next=doc.native();const ClipboardNote note{0,{0,0,1234,1,49,100,0,0}};
  preparePreciseNotePaste(doc,next,{0,2,1,2,1},{note},PatternAll,"overwrite",false);
  check(next.preciseNotes.size()==2&&next.preciseNotes.back().position==2*65536+1234&&next.preciseNotes.back().track==next.tracks.at(2).id,"Paste translates precise channel and fractional timing");
  doc.editNative(next,{});doc.undo();check(doc.native().preciseNotes.size()==1,"Undo removes pasted precise hits");doc.redo();check(doc.native()==next,"Redo restores pasted precise hits");
  auto invalid=doc.native();rejects([&]{preparePreciseNotePaste(doc,invalid,{0,2,1,2,1},{note,note},PatternAll,"overwrite",false);});check(doc.native()==next,"Duplicate note paste rejection never changes document");
  auto clean=doc.native();preparePreciseNotePaste(doc,clean,{0,2,1,2,1},{},PatternAll,"overwrite",false);check(clean.preciseNotes.size()==1,"Empty source clears destination precise hits");
  clear.fields=PatternEffect;check(prepareEffectTransform(doc,regions,clear).preciseNotes==next.preciseNotes,"Effect-only clear preserves precise notes");
}
void cursorFieldAndChannelDelete() {
  Document doc(MOD_TYPE_MPT,4);
  const uint32_t unit=performanceUnitsPerRow;
  for(int row : {2,3,63})for(int channel : {1,2})put(doc,row,channel,{uint8_t(49+row%12),uint8_t(channel+1),VOLCMD_VOLUME,32,CMD_VIBRATO,0x47});
  doc.annotate([&](NativeSong &n) {
    const auto pattern=n.patterns.at(0).id,track=n.tracks.at(1).id,other=n.tracks.at(2).id;
    n.preciseNotes={{pattern,track,2*unit+100,2,61,100},{pattern,track,3*unit+8192,2,64,90},{pattern,other,3*unit+8192,3,65,90}};
    for(auto target : {track,other}) {
      n.performance.columns[target]=8;
      for(uint8_t column=1;column<8;++column) {
        PatternCommand command;command.pattern=pattern;command.track=target;command.position=3*unit+4096;command.column=column;
        command.kind=PatternCommandKind::NudgeForward;command.value=.75;command.durationBeats=.125;
        n.performance.commands.push_back(command);
      }
      n.signal.lanes[target]=1;n.signal.commands.push_back({pattern,target,0,3*unit+31,0,SignalCommandKind::Clear});
    }
    n.automation.push_back({n.makeEntity().id,pattern,"unresolved-fixture",7,true,{{3*256,.5,AutomationCurve::Linear}}});
  });
  const auto before=doc.native();const auto cell=doc.cell(0,2,1);
  PatternTransform clear;clear.operation="clear";clear.fields=PatternNote;
  auto next=prepareEffectTransform(doc,{{0,2,1,1,1}},clear);
  check(next.preciseNotes.size()==2&&next.performance==before.performance&&next.signal==before.signal&&next.automation==before.automation,
    "Cursor note clear removes only that row's precise notes, retaining all FX, graph lanes and automation");
  doc.editNative(next,preparePatternTransform(doc,{{0,2,1,1,1}},clear));
  auto cleared=cell;cleared.note=0;
  check(doc.cell(0,2,1)==cleared&&doc.cell(0,2,2).note!=0,"Cursor note clear retains instrument, volume, effect and neighbouring channel");
  doc.undo();check(doc.native()==before&&doc.cell(0,2,1)==cell,"One Undo restores cursor note and precise hits");
  PatternTransform remove;remove.operation="deleteRows";remove.amount=1;remove.allowDataLoss=true;
  const PatternRegion tail{0,2,62,1,1};next=prepareEffectTransform(doc,{tail},remove);
  const auto track=before.tracks.at(1).id;
  auto expected=before;
  std::erase_if(expected.preciseNotes,[&](const auto &note){return note.track==track&&note.position/unit==2;});
  for(auto &note:expected.preciseNotes)if(note.track==track)note.position-=unit;
  for(auto &command:expected.performance.commands)if(command.track==track)command.position-=unit;
  for(auto &command:expected.signal.commands)if(command.target==track)command.position-=unit;
  check(next==expected,"Channel row deletion moves every extra FX, precise offset and channel graph command without touching other channels or envelopes");
  const auto following=doc.cell(0,3,1),other=doc.cell(0,3,2),last=doc.cell(0,63,1);
  doc.editNative(next,preparePatternTransform(doc,{tail},remove));
  check(doc.cell(0,2,1)==following&&doc.cell(0,3,2)==other&&doc.cell(0,62,1)==last&&doc.cell(0,63,1)==Cell{},"Channel row deletion shifts ordinary fields and empties exactly that channel's tail");
  doc.undo();check(doc.native()==before&&doc.cell(0,2,1)==cell&&doc.cell(0,63,1)==last,"One Undo restores the complete channel row edit");
  doc.redo();check(doc.native()==expected&&doc.cell(0,2,1)==following,"Redo reapplies all native and ordinary fields together");
}
int main() {
	try {
		rowSplices();preciseClipboard();cursorFieldAndChannelDelete();
		nativeColumns();
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
