#include "UsageAnalytics.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>

namespace Tracker {

namespace {

std::string toLowerAscii(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return s;
}

std::string trimAscii(const std::string &s) {
	size_t b = 0;
	while(b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
	size_t e = s.size();
	while(e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
	return s.substr(b, e - b);
}

std::string truncateUtf8Safe(const std::string &s, size_t maxBytes) {
	if(s.size() <= maxBytes) return s;
	size_t cut = maxBytes;
	while(cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
	return s.substr(0, cut);
}

} // namespace

UsageAnalytics::UsageAnalytics(Config config) {
	configure(std::move(config));
}

void UsageAnalytics::configure(Config config) {
	config_ = std::move(config);
	enabled_ = false;
	startMonoMs_ = nowMonoMs();
	sessionId_ = generateId(0);
	last10Actions_.clear();
	panelHistory_.clear();
	undoTimestamps_.clear();
	lastBounceLogMs_ = 0;
	lastUndoBurstLogMs_ = 0;
	lastStatusFriction_.clear();
	lastStatusFrictionMs_ = 0;
	lastPaletteZeroQuery_.clear();
	if(!config_.suppressed) {
		loadConfig();
	}
}

uint64_t UsageAnalytics::nowMonoMs() const noexcept {
	if(useTestClock_) return testClockMs_;
	const auto now = std::chrono::steady_clock::now().time_since_epoch();
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::string UsageAnalytics::nowIso8601Utc() {
	using namespace std::chrono;
	const auto now = system_clock::now();
	const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
	const std::time_t tt = system_clock::to_time_t(now);
	std::tm utc{};
#if defined(_WIN32)
	gmtime_s(&utc, &tt);
#else
	gmtime_r(&tt, &utc);
#endif
	char buf[32]{};
	std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
		utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
		utc.tm_hour, utc.tm_min, utc.tm_sec,
		static_cast<int>(ms.count() < 0 ? 0 : ms.count()));
	return std::string(buf);
}

std::string UsageAnalytics::generateId(uint64_t counter) {
	std::random_device rd;
	const uint64_t a = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
	const uint64_t b = (static_cast<uint64_t>(rd()) << 32) ^ counter;
	char buf[36]{};
	std::snprintf(buf, sizeof(buf), "%016llx-%08llx",
		static_cast<unsigned long long>(a),
		static_cast<unsigned long long>(b & 0xFFFFFFFFULL));
	return std::string(buf);
}

std::string UsageAnalytics::escapeJson(const std::string &text) {
	std::string out;
	out.reserve(text.size() + 8);
	for(unsigned char c : text) {
		switch(c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if(c < 0x20) {
				char esc[8]{};
				std::snprintf(esc, sizeof(esc), "\\u%04x", unsigned(c));
				out += esc;
			} else {
				out.push_back(static_cast<char>(c));
			}
			break;
		}
	}
	return out;
}

void UsageAnalytics::loadConfig() {
	if(config_.suppressed || config_.configPath.empty()) return;
	std::error_code ec;
	if(!std::filesystem::exists(config_.configPath, ec)) {
		enabled_ = false;
		return;
	}
	std::ifstream in(config_.configPath, std::ios::binary);
	if(!in) {
		enabled_ = false;
		return;
	}
	std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	const auto keyPos = content.find("\"enabled\"");
	if(keyPos == std::string::npos) {
		enabled_ = false;
		return;
	}
	const auto colon = content.find(':', keyPos + 9);
	if(colon == std::string::npos) {
		enabled_ = false;
		return;
	}
	size_t valPos = colon + 1;
	while(valPos < content.size() && std::isspace(static_cast<unsigned char>(content[valPos]))) ++valPos;
	enabled_ = content.compare(valPos, 4, "true") == 0;
}

bool UsageAnalytics::saveConfig() const {
	if(config_.suppressed || config_.configPath.empty()) return false;
	std::error_code ec;
	if(config_.configPath.has_parent_path()) {
		std::filesystem::create_directories(config_.configPath.parent_path(), ec);
	}
	auto tmpPath = config_.configPath;
	tmpPath += ".tmp";
	{
		std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
		if(!out) return false;
		out << "{\"v\":1,\"enabled\":" << (enabled_ ? "true" : "false") << "}\n";
		out.flush();
		if(!out) return false;
	}
	std::filesystem::rename(tmpPath, config_.configPath, ec);
	if(ec) {
		std::filesystem::remove(config_.configPath, ec);
		ec.clear();
		std::filesystem::rename(tmpPath, config_.configPath, ec);
		if(ec) {
			std::filesystem::remove(tmpPath, ec);
			return false;
		}
	}
	return true;
}

bool UsageAnalytics::setEnabled(bool enabled) {
	if(config_.suppressed) return false;
	const bool wasEnabled = enabled_;
	enabled_ = enabled;
	const bool saved = saveConfig();
	if(enabled_ && !wasEnabled) {
		writeEvent("usage", "analytics_enabled", {{"configPath", config_.configPath.u8string().empty() ? "" : std::string(reinterpret_cast<const char *>(config_.configPath.u8string().data()), config_.configPath.u8string().size())}}, false);
	} else if(!enabled_ && wasEnabled) {
		// Record opt-out before stopping writes.
		const bool prev = enabled_;
		enabled_ = true;
		writeEvent("usage", "analytics_disabled", {}, false);
		enabled_ = prev;
	}
	return saved;
}

void UsageAnalytics::setContext(std::string panel, std::string layout, bool playing) {
	if(!panel.empty()) panel_ = std::move(panel);
	if(!layout.empty()) layout_ = std::move(layout);
	playing_ = playing;
}

void UsageAnalytics::setPlaying(bool playing) {
	playing_ = playing;
}

void UsageAnalytics::pushAction(const std::string &summary) {
	if(summary.empty()) return;
	last10Actions_.push_back(truncateUtf8Safe(summary, 120));
	while(last10Actions_.size() > 10) last10Actions_.pop_front();
}

void UsageAnalytics::writeEvent(const char *category, const char *eventName,
	const std::vector<Field> &detail, bool includeActions) {
	if(config_.suppressed || !enabled_ || config_.logPath.empty()) return;

	std::error_code ec;
	if(config_.logPath.has_parent_path()) {
		std::filesystem::create_directories(config_.logPath.parent_path(), ec);
	}
	std::ofstream out(config_.logPath, std::ios::binary | std::ios::app);
	if(!out) return;

	const uint64_t mono = nowMonoMs();
	const uint64_t elapsed = mono >= startMonoMs_ ? (mono - startMonoMs_) : 0;

	std::ostringstream line;
	line << "{\"v\":1"
		<< ",\"ts\":\"" << nowIso8601Utc() << "\""
		<< ",\"monoMs\":" << elapsed
		<< ",\"session\":\"" << escapeJson(sessionId_) << "\""
		<< ",\"platform\":\"" << escapeJson(config_.platform) << "\""
		<< ",\"category\":\"" << category << "\""
		<< ",\"event\":\"" << eventName << "\""
		<< ",\"panel\":\"" << escapeJson(panel_) << "\""
		<< ",\"layout\":\"" << escapeJson(layout_) << "\""
		<< ",\"playing\":" << (playing_ ? "true" : "false")
		<< ",\"detail\":{";
	for(size_t i = 0; i < detail.size(); ++i) {
		if(i > 0) line << ',';
		line << '"' << escapeJson(detail[i].key) << "\":";
		if(detail[i].rawJson) line << detail[i].value;
		else line << '"' << escapeJson(detail[i].value) << '"';
	}
	line << '}';
	if(includeActions) {
		line << ",\"last10Actions\":[";
		for(size_t i = 0; i < last10Actions_.size(); ++i) {
			if(i > 0) line << ',';
			line << '"' << escapeJson(last10Actions_[i]) << '"';
		}
		line << ']';
	}
	line << "}\n";

	const std::string payload = line.str();
	out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
	out.flush();
}

void UsageAnalytics::recordAppStart(const std::string &document) {
	if(config_.suppressed) return;
	startMonoMs_ = nowMonoMs();
	panelHistory_.clear();
	panelHistory_.push_back({panel_, startMonoMs_});
	if(!enabled_) return;
	writeEvent("usage", "app_start", {{"document", truncateUtf8Safe(document, 120)}}, false);
}

void UsageAnalytics::recordAppStop() {
	if(config_.suppressed || !enabled_) return;
	const uint64_t mono = nowMonoMs();
	const uint64_t duration = mono >= startMonoMs_ ? (mono - startMonoMs_) : 0;
	writeEvent("usage", "app_stop", {{"durationMs", std::to_string(duration), true}}, false);
}

void UsageAnalytics::recordCommand(const std::string &commandId, const std::string &label,
	const std::string &source, bool isUndo, const std::string &deepMenuPath) {
	if(config_.suppressed) return;
	const std::string cleanLabel = truncateUtf8Safe(label.empty() ? commandId : label, 100);
	pushAction("cmd:" + cleanLabel);
	if(!enabled_) return;

	writeEvent("usage", "command", {
		{"id", commandId},
		{"label", cleanLabel},
		{"source", source}
	}, false);

	if(!deepMenuPath.empty() || source == "deep_menu") {
		recordDeepMenu(deepMenuPath.empty() ? cleanLabel : deepMenuPath, cleanLabel);
	}

	if(isUndo) {
		const uint64_t now = nowMonoMs();
		undoTimestamps_.push_back(now);
		while(!undoTimestamps_.empty() && now >= undoTimestamps_.front() && (now - undoTimestamps_.front()) > 5000) {
			undoTimestamps_.pop_front();
		}
		if(undoTimestamps_.size() >= 3 && (lastUndoBurstLogMs_ == 0 || now >= lastUndoBurstLogMs_ + 5000)) {
			const uint64_t windowMs = now >= undoTimestamps_.front() ? (now - undoTimestamps_.front()) : 0;
			lastUndoBurstLogMs_ = now;
			writeEvent("friction", "undo_burst", {
				{"count", std::to_string(undoTimestamps_.size()), true},
				{"windowMs", std::to_string(windowMs), true}
			}, true);
		}
	}
}

void UsageAnalytics::recordPanelFocus(const std::string &nextPanel) {
	if(config_.suppressed || nextPanel.empty() || nextPanel == panel_) return;
	const std::string previous = panel_;
	panel_ = nextPanel;
	pushAction("panel:" + previous + "->" + nextPanel);

	const uint64_t now = nowMonoMs();
	if(panelHistory_.empty()) {
		panelHistory_.push_back({previous, now});
	}
	panelHistory_.push_back({nextPanel, now});
	while(panelHistory_.size() > 6) panelHistory_.pop_front();

	if(!enabled_) return;
	writeEvent("usage", "panel_focus", {{"from", previous}, {"to", nextPanel}}, false);

	// Passive detector #3: panel bounce loop A -> B -> A within 5,000 ms.
	if(panelHistory_.size() >= 3) {
		const auto &a1 = panelHistory_[panelHistory_.size() - 3];
		const auto &b = panelHistory_[panelHistory_.size() - 2];
		const auto &a2 = panelHistory_[panelHistory_.size() - 1];
		if(a1.panel == a2.panel && a1.panel != b.panel && a2.ms >= a1.ms && (a2.ms - a1.ms) <= 5000) {
			if(lastBounceLogMs_ == 0 || now >= lastBounceLogMs_ + 1500) {
				lastBounceLogMs_ = now;
				writeEvent("friction", "panel_bounce", {
					{"panelA", a1.panel},
					{"panelB", b.panel},
					{"elapsedMs", std::to_string(a2.ms - a1.ms), true}
				}, true);
			}
		}
	}
}

void UsageAnalytics::recordLayoutChange(const std::string &nextLayout) {
	if(config_.suppressed || nextLayout.empty() || nextLayout == layout_) return;
	const std::string previous = layout_;
	layout_ = nextLayout;
	pushAction("layout:" + nextLayout);
	if(!enabled_) return;
	writeEvent("usage", "layout_change", {{"from", previous}, {"to", nextLayout}}, false);
}

void UsageAnalytics::recordTransport(const std::string &action, bool nowPlaying) {
	if(config_.suppressed) return;
	playing_ = nowPlaying;
	pushAction("transport:" + action);
	if(!enabled_) return;
	writeEvent("usage", "transport", {{"action", action}}, false);
}

void UsageAnalytics::recordDocumentAction(const std::string &action, const std::string &detail) {
	if(config_.suppressed) return;
	pushAction("doc:" + action);
	if(!enabled_) return;
	std::vector<Field> fields = {{"action", action}};
	if(!detail.empty()) fields.push_back({"detail", truncateUtf8Safe(detail, 120)});
	writeEvent("usage", "document_action", fields, false);
}

void UsageAnalytics::recordEditorOpen(const std::string &editorName) {
	if(config_.suppressed || editorName.empty()) return;
	pushAction("editor:" + editorName);
	if(!enabled_) return;
	writeEvent("usage", "editor_open", {{"editor", truncateUtf8Safe(editorName, 80)}}, false);
}

void UsageAnalytics::recordPaletteOpen() {
	if(config_.suppressed) return;
	lastPaletteZeroQuery_.clear();
	pushAction("palette:open");
	if(!enabled_) return;
	writeEvent("usage", "palette_open", {}, false);
}

void UsageAnalytics::recordPaletteQuery(const std::string &query, size_t matchCount) {
	if(config_.suppressed) return;
	const std::string trimmed = trimAscii(query);
	if(trimmed.empty()) {
		lastPaletteZeroQuery_.clear();
		return;
	}
	if(matchCount == 0) {
		lastPaletteZeroQuery_ = truncateUtf8Safe(trimmed, 100);
	}
}

void UsageAnalytics::recordPaletteExecute(const std::string &query, const std::string &commandLabel, size_t matchCount) {
	if(config_.suppressed) return;
	const std::string trimmed = truncateUtf8Safe(trimAscii(query), 100);
	pushAction("palette:run:" + truncateUtf8Safe(commandLabel, 80));
	if(!enabled_) {
		lastPaletteZeroQuery_.clear();
		return;
	}
	if(!lastPaletteZeroQuery_.empty()) {
		writeEvent("friction", "palette_zero_result", {
			{"query", lastPaletteZeroQuery_},
			{"recovered", "true", true}
		}, true);
		lastPaletteZeroQuery_.clear();
	}
	writeEvent("usage", "palette_execute", {
		{"query", trimmed},
		{"command", truncateUtf8Safe(commandLabel, 100)},
		{"resultCount", std::to_string(matchCount), true}
	}, false);
}

void UsageAnalytics::recordPaletteClose(const std::string &finalQuery, size_t finalMatchCount, bool executed) {
	if(config_.suppressed) return;
	const std::string trimmed = truncateUtf8Safe(trimAscii(finalQuery), 100);
	if(!executed && !trimmed.empty()) {
		pushAction("palette:abandoned:" + trimmed);
	}
	if(!enabled_) {
		lastPaletteZeroQuery_.clear();
		return;
	}
	if(!executed) {
		if(finalMatchCount == 0 && !trimmed.empty()) {
			writeEvent("friction", "palette_zero_result", {
				{"query", trimmed},
				{"recovered", "false", true}
			}, true);
		} else if(!lastPaletteZeroQuery_.empty()) {
			writeEvent("friction", "palette_zero_result", {
				{"query", lastPaletteZeroQuery_},
				{"recovered", "false", true}
			}, true);
		}
		if(!trimmed.empty()) {
			writeEvent("friction", "palette_abandoned", {
				{"query", trimmed},
				{"matchCount", std::to_string(finalMatchCount), true}
			}, true);
		}
	}
	lastPaletteZeroQuery_.clear();
}

bool UsageAnalytics::looksLikeFrictionStatus(const std::string &message, bool &isDraftHeld) {
	isDraftHeld = false;
	if(message.empty()) return false;
	const std::string lower = toLowerAscii(message);
	if(lower.find("draft held") != std::string::npos ||
	   lower.find("fields kept") != std::string::npos ||
	   lower.find("edit kept") != std::string::npos ||
	   lower.find("retained") != std::string::npos ||
	   lower.find("reload saved state") != std::string::npos) {
		isDraftHeld = true;
		return true;
	}
	static const char *const kRejectedTokens[] = {
		"finish the current",
		"finish or reload",
		"menu target changed",
		"unavailable",
		"failed",
		"cannot ",
		"can't ",
		"rejected",
		"invalid ",
		"document is busy",
		"wait for the",
		"outside its allowed range",
		"must be ",
		"focus the pattern",
		"select a plugin",
		"no song edit to"
	};
	for(const char *token : kRejectedTokens) {
		if(lower.find(token) != std::string::npos) return true;
	}
	return false;
}

void UsageAnalytics::recordStatusMessage(const std::string &message, const std::string &context) {
	if(config_.suppressed || message.empty()) return;
	bool draftHeld = false;
	if(!looksLikeFrictionStatus(message, draftHeld)) return;
	recordRejectedEdit(message, context, draftHeld);
}

void UsageAnalytics::recordRejectedEdit(const std::string &reason, const std::string &context, bool draftHeld) {
	if(config_.suppressed || reason.empty()) return;
	const uint64_t now = nowMonoMs();
	const std::string clipped = truncateUtf8Safe(trimAscii(reason), 160);
	if(clipped == lastStatusFriction_ && lastStatusFrictionMs_ > 0 && now >= lastStatusFrictionMs_ && (now - lastStatusFrictionMs_) < 1500) {
		return;
	}
	lastStatusFriction_ = clipped;
	lastStatusFrictionMs_ = now;
	pushAction(std::string(draftHeld ? "draft_held:" : "rejected:") + truncateUtf8Safe(clipped, 60));
	if(!enabled_) return;
	writeEvent("friction", draftHeld ? "draft_held" : "rejected_edit", {
		{"reason", clipped},
		{"context", context.empty() ? panel_ : truncateUtf8Safe(context, 80)}
	}, true);
}

void UsageAnalytics::recordDeepMenu(const std::string &menuPath, const std::string &commandLabel) {
	if(config_.suppressed || !enabled_) return;
	writeEvent("friction", "deep_menu", {
		{"menuPath", truncateUtf8Safe(menuPath, 120)},
		{"command", truncateUtf8Safe(commandLabel, 100)}
	}, true);
}

std::string UsageAnalytics::recordBookmark(const std::string &trigger, const std::string &note) {
	if(config_.suppressed) return {};
	const std::string id = generateId(++eventCounter_);
	const std::string cleanNote = truncateUtf8Safe(trimAscii(note), 200);
	if(enabled_) {
		writeEvent("friction", "bookmark", {
			{"bookmarkId", id},
			{"trigger", trigger},
			{"note", cleanNote}
		}, true);
	}
	pushAction("bookmark:" + (cleanNote.empty() ? id.substr(0, 8) : truncateUtf8Safe(cleanNote, 40)));
	return id;
}

void UsageAnalytics::recordBookmarkNote(const std::string &bookmarkId, const std::string &note) {
	if(config_.suppressed || bookmarkId.empty()) return;
	const std::string cleanNote = truncateUtf8Safe(trimAscii(note), 200);
	if(cleanNote.empty()) return;
	pushAction("bookmark_note:" + truncateUtf8Safe(cleanNote, 40));
	if(!enabled_) return;
	writeEvent("friction", "bookmark_note", {
		{"bookmarkId", bookmarkId},
		{"note", cleanNote}
	}, true);
}

} // namespace Tracker
