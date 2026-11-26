#include "editor/UsageAnalytics.hpp"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

void check(bool condition, const char *message) {
	if(!condition) throw std::runtime_error(message);
}

std::vector<Json> readJsonl(const fs::path &path) {
	std::vector<Json> rows;
	std::ifstream in(path, std::ios::binary);
	std::string line;
	while(std::getline(in, line)) {
		if(line.empty()) continue;
		rows.push_back(Json::parse(line));
	}
	return rows;
}

} // namespace

int main() {
	try {
		const fs::path root = fs::temp_directory_path() / "screamseq-usage-analytics-tests";
		std::error_code ec;
		fs::remove_all(root, ec);
		fs::create_directories(root, ec);

		// 1. Strictly suppressed mode writes zero files even if toggled or invoked.
		{
			const fs::path cfg = root / "suppressed-config.json";
			const fs::path log = root / "suppressed.jsonl";
			Tracker::UsageAnalytics suppressed({cfg, log, "windows", true});
			check(suppressed.suppressed(), "Suppressed flag must stay true");
			check(!suppressed.enabled(), "Suppressed analytics must be disabled");
			check(!suppressed.setEnabled(true), "Suppressed analytics must reject setEnabled(true)");
			suppressed.recordAppStart("demo");
			suppressed.recordCommand("cmd.undo", "Edit / Undo", "shortcut", true);
			suppressed.recordBookmark("shortcut", "should not write");
			suppressed.recordAppStop();
			check(!fs::exists(cfg), "Suppressed analytics must never create config file");
			check(!fs::exists(log), "Suppressed analytics must never create log file");
		}

		// 2. Default-off opt-in, persistence across restarts, general usage & all 5 passive friction detectors.
		const fs::path cfg = root / "config.json";
		const fs::path log = root / "events.jsonl";
		{
			Tracker::UsageAnalytics a({cfg, log, "windows", false});
			check(!a.enabled(), "Analytics must default to OFF when config does not exist");
			a.recordAppStart("demo");
			check(!fs::exists(log), "Disabled analytics must not create log file");

			check(a.setEnabled(true), "Enabling analytics should persist config");
			check(a.enabled(), "Analytics should now be enabled");
			check(fs::exists(cfg), "Config file must be created on opt-in");
		}

		// Re-open in a fresh instance to verify opt-in persists across restarts.
		{
			Tracker::UsageAnalytics a({cfg, log, "windows", false});
			check(a.enabled(), "Opt-in must persist across restarts");
			a.setClockOverrideForTesting(1000);
			a.recordAppStart("test-song.screamseq");

			// General usage events.
			a.setClockOverrideForTesting(1200);
			a.recordLayoutChange("Sound design");
			a.setClockOverrideForTesting(1400);
			a.recordTransport("play", true);
			a.setClockOverrideForTesting(1500);
			a.recordDocumentAction("save", "screamseq");

			// Passive detector #3: panel bounce A -> B -> A within 5000ms.
			a.setClockOverrideForTesting(2000);
			a.recordPanelFocus("mixer");
			a.setClockOverrideForTesting(3200);
			a.recordPanelFocus("pattern"); // pattern -> mixer -> pattern in 1200ms!

			// Non-bounce when gap > 5000ms.
			a.setClockOverrideForTesting(10000);
			a.recordPanelFocus("graph");
			a.setClockOverrideForTesting(16500);
			a.recordPanelFocus("pattern"); // 6500ms -> should NOT fire bounce!

			// Passive detector #4: 3+ Undo commands in 5000ms.
			a.setClockOverrideForTesting(20000);
			a.recordCommand("windows.command.122", "Edit / Undo", "shortcut", true);
			a.setClockOverrideForTesting(21000);
			a.recordCommand("windows.command.122", "Edit / Undo", "shortcut", true);
			a.setClockOverrideForTesting(22000);
			a.recordCommand("windows.command.122", "Edit / Undo", "shortcut", true);
			// 4th undo within cooldown should not emit a duplicate burst immediately.
			a.setClockOverrideForTesting(22500);
			a.recordCommand("windows.command.122", "Edit / Undo", "shortcut", true);

			// Passive detector #1: rejected edit & Draft held + dedupe within 1500ms.
			a.setClockOverrideForTesting(25000);
			a.recordStatusMessage("Draft held · Reload saved state to continue", "mixer");
			a.setClockOverrideForTesting(25500);
			a.recordStatusMessage("Draft held · Reload saved state to continue", "mixer"); // deduped!
			a.setClockOverrideForTesting(27000);
			a.recordStatusMessage("Finish the current edit before changing buses", "graph");

			// Passive detector #2: Command Palette zero-result & abandoned query.
			a.setClockOverrideForTesting(30000);
			a.recordPaletteOpen();
			a.recordPaletteQuery("quantum reverb", 0);
			a.recordPaletteClose("quantum reverb", 0, false);

			// Command Palette execution (general usage).
			a.setClockOverrideForTesting(31000);
			a.recordPaletteOpen();
			a.recordPaletteQuery("mixer", 3);
			a.recordPaletteExecute("mixer", "Mix / Open mixer controls and routing", 3);
			a.recordPaletteClose("mixer", 3, true);

			// Passive detector #5: deep menu / More... invocation.
			a.setClockOverrideForTesting(32000);
			a.recordCommand("windows.command.663", "Pattern / Header actions", "deep_menu", false, "Pattern / More / Header actions");

			// Explicit bookmark + follow-up note (with JSON special chars).
			a.setClockOverrideForTesting(33000);
			const std::string bid = a.recordBookmark("shortcut");
			check(!bid.empty(), "Bookmark must return non-empty ID");
			a.setClockOverrideForTesting(34000);
			a.recordBookmarkNote(bid, "lost \"cursor\" after\tpaste\\undo");

			a.setClockOverrideForTesting(35000);
			a.recordAppStop();
		}

		const auto rows = readJsonl(log);
		check(!rows.empty(), "JSONL log must contain recorded events");

		int bounces = 0, undoBursts = 0, draftHelds = 0, rejectedEdits = 0;
		int zeroResults = 0, abandoned = 0, deepMenus = 0, bookmarks = 0, bookmarkNotes = 0;
		bool sawLast10OnFriction = true;
		for(const auto &row : rows) {
			check(row.at("v").get<int>() == 1, "Schema version must be 1");
			check(row.at("platform").get<std::string>() == "windows", "Platform must match");
			const auto cat = row.at("category").get<std::string>();
			const auto ev = row.at("event").get<std::string>();
			if(cat == "friction") {
				if(!row.contains("last10Actions") || !row.at("last10Actions").is_array()) {
					sawLast10OnFriction = false;
				}
			}
			if(ev == "panel_bounce") ++bounces;
			else if(ev == "undo_burst") ++undoBursts;
			else if(ev == "draft_held") ++draftHelds;
			else if(ev == "rejected_edit") ++rejectedEdits;
			else if(ev == "palette_zero_result") ++zeroResults;
			else if(ev == "palette_abandoned") ++abandoned;
			else if(ev == "deep_menu") ++deepMenus;
			else if(ev == "bookmark") ++bookmarks;
			else if(ev == "bookmark_note") {
				++bookmarkNotes;
				check(row.at("detail").at("note").get<std::string>() == "lost \"cursor\" after\tpaste\\undo",
					"JSON escaping roundtrip failed");
			}
		}

		check(sawLast10OnFriction, "Every friction event must include last10Actions");
		check(bounces == 1, "Expected exactly 1 panel_bounce event");
		check(undoBursts == 1, "Expected exactly 1 undo_burst event (cooldown applied)");
		check(draftHelds == 1, "Expected exactly 1 draft_held event (deduped within 1500ms)");
		check(rejectedEdits == 1, "Expected exactly 1 rejected_edit event");
		check(zeroResults == 1, "Expected exactly 1 palette_zero_result event");
		check(abandoned == 1, "Expected exactly 1 palette_abandoned event");
		check(deepMenus == 1, "Expected exactly 1 deep_menu event");
		check(bookmarks == 1, "Expected exactly 1 bookmark event");
		check(bookmarkNotes == 1, "Expected exactly 1 bookmark_note event");

		fs::remove_all(root, ec);
		std::cout << "PASS UsageAnalyticsTests (" << rows.size() << " events validated)\n";
		return 0;
	} catch(const std::exception &e) {
		std::cerr << "FAIL " << e.what() << "\n";
		return 1;
	}
}
