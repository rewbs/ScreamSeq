#pragma once
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Tracker {

// Main-thread-only local usage & friction analytics recorder.
// Never call from an audio callback or realtime DSP thread.
// Opt-in (disabled by default) and strictly suppressed during inspection,
// automation, and automated test sessions.
class UsageAnalytics {
public:
	struct Config {
		std::filesystem::path configPath;
		std::filesystem::path logPath;
		std::string platform = "windows";
		bool suppressed = true;
	};

	struct Field {
		std::string key;
		std::string value;
		bool rawJson = false;
	};

	UsageAnalytics() = default;
	explicit UsageAnalytics(Config config);

	void configure(Config config);

	bool suppressed() const noexcept { return config_.suppressed; }
	bool enabled() const noexcept { return !config_.suppressed && enabled_; }
	const std::filesystem::path &configPath() const noexcept { return config_.configPath; }
	const std::filesystem::path &logPath() const noexcept { return config_.logPath; }
	const std::string &sessionId() const noexcept { return sessionId_; }

	// Persists opt-in toggle to configPath (no-op when suppressed).
	bool setEnabled(bool enabled);
	bool toggleEnabled() { return setEnabled(!enabled_); }

	// Context tracking (called on main thread).
	void setContext(std::string panel, std::string layout, bool playing);
	void setPlaying(bool playing);
	const std::string &panel() const noexcept { return panel_; }
	const std::string &layout() const noexcept { return layout_; }
	bool playing() const noexcept { return playing_; }

	// Lifecycle events.
	void recordAppStart(const std::string &document = {});
	void recordAppStop();

	// General usage events (and passive detector triggers).
	void recordCommand(const std::string &commandId, const std::string &label,
		const std::string &source = "shortcut", bool isUndo = false,
		const std::string &deepMenuPath = {});
	void recordPanelFocus(const std::string &nextPanel);
	void recordLayoutChange(const std::string &nextLayout);
	void recordTransport(const std::string &action, bool nowPlaying);
	void recordDocumentAction(const std::string &action, const std::string &detail = {});
	void recordEditorOpen(const std::string &editorName);

	// Command Palette session tracking (passive detectors #2: zero-result & abandoned).
	void recordPaletteOpen();
	void recordPaletteQuery(const std::string &query, size_t matchCount);
	void recordPaletteExecute(const std::string &query, const std::string &commandLabel, size_t matchCount);
	void recordPaletteClose(const std::string &finalQuery, size_t finalMatchCount, bool executed);

	// Passive detector #1: rejected edits / "Draft held" / retained-field blocks.
	void recordStatusMessage(const std::string &message, const std::string &context = {});
	void recordRejectedEdit(const std::string &reason, const std::string &context = {}, bool draftHeld = false);

	// Passive detector #5: explicit deep menu / "More..." invocation.
	void recordDeepMenu(const std::string &menuPath, const std::string &commandLabel);

	// Explicit friction bookmark (Ctrl+Shift+. / Cmd+Shift+. / Palette).
	// Returns the bookmarkId so an optional follow-up note can link to it.
	std::string recordBookmark(const std::string &trigger = "shortcut", const std::string &note = {});
	void recordBookmarkNote(const std::string &bookmarkId, const std::string &note);

	// Deterministic test clock override (0 = use steady_clock).
	void setClockOverrideForTesting(uint64_t monoMs) noexcept { testClockMs_ = monoMs; useTestClock_ = true; }
	void clearClockOverrideForTesting() noexcept { useTestClock_ = false; }

	// Helpers exposed for unit testing.
	static bool looksLikeFrictionStatus(const std::string &message, bool &isDraftHeld);
	static std::string escapeJson(const std::string &text);

private:
	uint64_t nowMonoMs() const noexcept;
	static std::string nowIso8601Utc();
	static std::string generateId(uint64_t counter);
	void pushAction(const std::string &summary);
	void loadConfig();
	bool saveConfig() const;
	void writeEvent(const char *category, const char *eventName, const std::vector<Field> &detail, bool includeActions);

	Config config_;
	bool enabled_ = false;
	std::string sessionId_;
	uint64_t startMonoMs_ = 0;
	uint64_t eventCounter_ = 0;

	std::string panel_ = "pattern";
	std::string layout_ = "Compose";
	bool playing_ = false;

	std::deque<std::string> last10Actions_;

	// Passive detector #3: panel bounce A -> B -> A within 5,000 ms.
	struct PanelVisit { std::string panel; uint64_t ms = 0; };
	std::deque<PanelVisit> panelHistory_;
	uint64_t lastBounceLogMs_ = 0;

	// Passive detector #4: 3+ Undo commands within 5,000 ms.
	std::deque<uint64_t> undoTimestamps_;
	uint64_t lastUndoBurstLogMs_ = 0;

	// Passive detector #1: dedupe repeated status messages within 1,500 ms.
	std::string lastStatusFriction_;
	uint64_t lastStatusFrictionMs_ = 0;

	// Passive detector #2: Command Palette zero-result tracking per open session.
	std::string lastPaletteZeroQuery_;

	bool useTestClock_ = false;
	uint64_t testClockMs_ = 0;
};

} // namespace Tracker
