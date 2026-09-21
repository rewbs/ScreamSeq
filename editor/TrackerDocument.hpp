#pragma once
#include "SampleSnap.hpp"
#include "SampleLoopCrossfade.hpp"
#include "common/stdafx.h"
#include "soundlib/Sndfile.h"
#include <atomic>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include "NativeSong.hpp"
#include "PreciseNoteRuntime.hpp"
#include "RecordingClock.hpp"
#include "SampleProcessing.hpp"
#include "SampleWaveform.hpp"
#include "SampleClipboard.hpp"

namespace Tracker
{
using namespace OpenMPT;
struct Cell
{
	uint8_t note{}, instrument{}, volumeCommand{}, volume{}, effect{}, parameter{};
	bool operator==(const Cell &) const = default;
};
struct Edit
{
	uint16_t pattern{}, row{}, channel{};
	Cell before{}, after{};
};
struct SampleLoopSettings
{
	uint32_t start = 0, end = 0;
	bool enabled = false, pingpong = false;
	bool reverse = false;
};
class Document
{
	std::unique_ptr<CSoundFile> song_;
	std::vector<std::byte> originalBytes_;
	std::string sourcePath_;
	NativeSong native_;
	struct SampleUndo
	{
		uint16_t sample = 0;
		uint32_t frames = 0;
		uint8_t bits = 16, channels = 1;
		std::vector<SamplePCMChunk> chunks;
		std::optional<std::pair<SampleEditGeometry, SampleEditGeometry>> geometry;
	};
	using SampleGeometry = SampleEditGeometry;
	struct SampleSpliceUndo {
		uint16_t sample = 0;
		uint32_t first = 0;
		SampleGeometry before, after;
		std::vector<std::byte> removed, inserted;
	};
	struct SampleSlotUndo {
		uint16_t sample = 0, countBefore = 0, countAfter = 0;
		ModSample before, after; // Headers only; never own PCM.
		::OpenMPT::mpt::charbuf<MAX_SAMPLENAME> nameBefore, nameAfter;
		std::vector<std::byte> data;
	};
	struct SampleDeleter { void operator()(void *p) const noexcept { ModSample::FreeSample(p); } };
	using SampleAllocation = std::unique_ptr<void, SampleDeleter>;
	struct UndoEntry
	{
		std::vector<Edit> cells;
		std::vector<std::byte> before, after;
		std::optional<NativeSong> nativeBefore, nativeAfter;
		std::optional<SampleUndo> sample;
		std::optional<SampleSpliceUndo> splice;
		std::optional<SampleSlotUndo> slot;
		SEQUENCEINDEX sequenceBefore = 0, sequenceAfter = 0;
		size_t bytes() const;
	};
	std::vector<UndoEntry> undo_, redo_;
	void trimHistory();
	mutable SampleWaveform waveformCache_;
	mutable int waveformSample_ = 0;
	SamplePCMView samplePCM(int sample) const;
	void validateSampleUndo(const SampleUndo &edit, bool redo) const;
	void applySampleUndo(const SampleUndo &edit, bool redo) noexcept;
	static SampleGeometry sampleGeometry(const ModSample &sample);
	void validateSpliceUndo(const SampleSpliceUndo &edit, bool redo) const;
	SampleAllocation prepareSpliceUndo(const SampleSpliceUndo &edit, bool redo) const;
	void applySpliceUndo(const SampleSpliceUndo &edit, bool redo, SampleAllocation allocation) noexcept;
	void validateSampleSlot(const SampleSlotUndo &edit, bool redo) const;
	SampleAllocation prepareSampleSlot(const SampleSlotUndo &edit, bool redo) const;
	void applySampleSlot(const SampleSlotUndo &edit, bool redo, SampleAllocation allocation) noexcept;
public:
	class PreparedSampleCopy {
		friend class Document;
		const Document *owner_ = nullptr;
		uint64_t revision_ = 0;
		int source_ = 0;
		uint32_t first_ = 0, last_ = 0;
		SampleChannels channels_ = SampleChannels::Both;
		ModSample sourceHeader_;
		UndoEntry entry_;
		SampleAllocation allocation_;
	public:
		int sample() const { return entry_.slot->sample; }
		uint64_t identity() const { return entry_.nativeAfter->samples.at(sample()).id; }
		const ModSample &settings() const { return entry_.slot->after; }
		std::string name() const { return entry_.slot->nameAfter.str(); }
		bool reusesEmptySlot() const { return entry_.slot->countBefore == entry_.slot->countAfter; }
		size_t historyBytes() const { return entry_.bytes(); }
	};
	class PreparedSampleEdit {
		friend class Document;
		const Document *owner_ = nullptr;
		uint64_t revision_ = 0;
		UndoEntry entry_;
		SampleAllocation replacement_;
		SampleSpliceResult result_;
	public:
		const SampleSpliceResult &result() const { return result_; }
		bool hasChanges() const { return entry_.sample.has_value() || entry_.splice.has_value(); }
	};
	class PreparedSampleProcess {
		friend class Document;
		const Document *owner_ = nullptr;
		uint64_t revision_ = 0;
		int sample_ = 0;
		SamplePCMPlan plan_;
		std::optional<std::pair<SampleEditGeometry, SampleEditGeometry>> geometry_;
		std::vector<SamplePCMChunk> guards_; // Original read regions; not retained in history.
	public:
		const SampleProcessResult &result() const { return plan_.result; }
		const auto &geometry() const { return geometry_; }
		bool hasChanges() const { return !plan_.chunks.empty() || (geometry_ && geometry_->first != geometry_->second); }
	};
	uint64_t revision = 0;
	explicit Document(MODTYPE type = MOD_TYPE_MPT, CHANNELINDEX channels = 8);
	explicit Document(const std::vector<std::byte> &bytes);
	static std::unique_ptr<Document> open(const std::string &path);
	static std::unique_ptr<Document> demo(MODTYPE type = MOD_TYPE_MPT);
	CSoundFile &song() { return *song_; }
	const CSoundFile &song() const { return *song_; }
	const NativeSong &native() const { return native_; }
	void restoreNative(NativeSong metadata); // validated project load, no history
	void annotate(const std::function<void(NativeSong &)> &change);
	std::vector<std::byte> serialize(); // Unchecked module base; use save/validateModuleSampleExport for export.
	std::vector<std::byte> snapshotData(); // Module + exact sample/timing/title/pattern/order corrections.
	bool editable() const;
	std::vector<std::byte> playbackData();
	const std::string &sourcePath() const { return sourcePath_; }
	void save(const std::string &path, bool preserveSamples = false);
	void validateModuleSampleExport();
	void validateSamples() const;
	Cell cell(int pattern, int row, int channel) const;
	bool valid(int pattern, int row, int channel) const;
	std::vector<Edit> edit(const std::vector<Edit> &edits);
	void validateEdits(const std::vector<Edit> &edits) const;
	bool editNative(NativeSong metadata, const std::vector<Edit> &edits = {});
	std::vector<Edit> undo();
	std::vector<Edit> redo();
	bool canUndo() const { return !undo_.empty(); }
	bool canRedo() const { return !redo_.empty(); }
	const NativeSong &historyNative(bool redo) const {
		const auto &history = redo ? redo_ : undo_;
		if(history.empty()) return native_;
		const auto &metadata = redo ? history.back().nativeAfter : history.back().nativeBefore;
		return metadata ? *metadata : native_;
	}
	bool undoChangesStructure() const { return !undo_.empty() && (!undo_.back().before.empty() || undo_.back().sample.has_value() || undo_.back().splice.has_value() || undo_.back().slot.has_value()); }
	bool redoChangesStructure() const { return !redo_.empty() && (!redo_.back().before.empty() || redo_.back().sample.has_value() || redo_.back().splice.has_value() || redo_.back().slot.has_value()); }
	bool undoChangesAutomation() const { return !undo_.empty() && undo_.back().nativeBefore && (undo_.back().nativeBefore->automation != native_.automation || undo_.back().nativeBefore->performance != native_.performance || undo_.back().nativeBefore->preciseNotes != native_.preciseNotes); }
	bool redoChangesAutomation() const { return !redo_.empty() && redo_.back().nativeAfter && (redo_.back().nativeAfter->automation != native_.automation || redo_.back().nativeAfter->performance != native_.performance || redo_.back().nativeAfter->preciseNotes != native_.preciseNotes); }
	bool undoChangesMixer() const { return !undo_.empty() && undo_.back().nativeBefore && (undo_.back().nativeBefore->mixer != native_.mixer || !sameSignalProcessing(undo_.back().nativeBefore->signal,native_.signal)); }
	bool redoChangesMixer() const { return !redo_.empty() && redo_.back().nativeAfter && (redo_.back().nativeAfter->mixer != native_.mixer || !sameSignalProcessing(redo_.back().nativeAfter->signal,native_.signal)); }
	void transaction(const std::function<void(CSoundFile &)> &change);
	void transaction(const std::function<void(CSoundFile &, NativeSong &)> &change);
	int importSample(const std::string &path, int slot = 0);
	struct ImportedSample { std::string path; int sample = 0, instrument = 0; };
	std::vector<ImportedSample> importSamples(const std::vector<std::string> &paths, bool instruments, bool dryRun = false);
	struct MultisampleSource { std::string path; int rootNote = 0; };
	struct MultisampleZone { std::string path; int sample = 0, rootNote = 0, lowNote = 0, highNote = 0; };
	struct ImportedMultisample { int instrument = 0; std::vector<MultisampleZone> zones; };
	ImportedMultisample importMultisample(std::vector<MultisampleSource> sources, const std::string &name, bool dryRun = false);
	int importInstrument(const std::string &path, int slot = 0);
	int addPattern(int rows, bool duplicate, int source);
	void setOrder(int index, int pattern);
	void editOrder(int index, int pattern, const std::string &operation);
	void removeOrder(int index);
	void processSample(int sample, const std::string &operation, uint32_t first, uint32_t last);
	SampleProcessResult processSample(int sample, const SampleProcessOptions &options, bool dryRun = false);
	PreparedSampleProcess prepareSampleProcess(int sample, const SampleProcessOptions &options) const;
	PreparedSampleProcess prepareSampleDraw(int sample, const SampleDrawOptions &options) const;
	PreparedSampleProcess prepareSampleCrossfade(int sample, const SampleCrossfadeOptions &options) const;
	PreparedSampleProcess prepareSampleLoops(int sample, const std::optional<SampleLoopSettings> &normal,
	                                        const std::optional<SampleLoopSettings> &sustain) const;
	std::vector<SampleSnapResult> snapSample(int sample, std::span<const uint32_t> positions, const SampleSnapOptions &options) const;
	SampleProcessResult applySampleProcess(PreparedSampleProcess prepared);
	SampleClipboard copySample(int sample, uint32_t first, uint32_t last, SampleChannels channels) const;
	PreparedSampleEdit prepareSamplePaste(int sample, const SampleClipboard &clipboard, const SamplePasteOptions &options) const;
	PreparedSampleEdit prepareSampleErase(int sample, uint32_t first, uint32_t last) const;
	SampleSpliceResult applySampleEdit(PreparedSampleEdit prepared);
	PreparedSampleCopy prepareSampleCopy(int sample, uint32_t first, uint32_t last, SampleChannels channels,
	                                    const std::optional<std::string> &name = std::nullopt) const;
	int applySampleCopy(PreparedSampleCopy prepared);
private:
	PreparedSampleEdit prepareSampleSplice(int sample, SampleSplicePlan plan) const;
public:
	void sampleSettings(int sample, int rate, int volume, int pan, uint32_t start, uint32_t end, bool loop, bool pingpong, const std::optional<std::string> &name = std::nullopt);
	std::vector<float> waveform(int sample, size_t bins) const;
	std::vector<float> waveform(int sample, uint32_t first, uint32_t last, size_t bins, SampleChannels channels) const;
	size_t historyBytes() const;
	uint64_t waveformReadFrames() const { return waveformCache_.readFrames(); }
	static void put(CSoundFile &, const Edit &);
	static void resizeChannels(CSoundFile &, int channels);
};
struct Telemetry
{
	uint32_t order{}, pattern{}, row{}, voices{};
	float left{}, right{};
	uint64_t frames{}, callbacks{}, overruns{};
	double maxMicros{}, p999Micros{};
};
struct PlaybackRegion {
	// pattern == UINT32_MAX follows the song; otherwise endRow is exclusive.
	uint32_t pattern = UINT32_MAX, startRow = 0, endRow = 0, cursorRow = 0;
	bool loop = false;
};
struct PreviewNote
{
	uint8_t note;
	uint16_t instrument;
	uint8_t velocity;
	bool on;
	uint16_t sample = 0;
};
class Renderer
{
	PlaybackRegion region_;
	std::atomic<bool> loop_{false};
	bool regionStarted_ = false;
	uint32_t regionLastRow_ = UINT32_MAX;
	std::unique_ptr<PreciseNoteRuntime> preciseNotes_;
	std::unique_ptr<RecordingClock> recordingClock_ = std::make_unique<RecordingClock>();
	uint64_t renderHostTime_ = 0;
	double hostTicksPerSample_ = 0;
	uint32_t renderOffset_ = 0;
	std::unique_ptr<CSoundFile> song_;
	static constexpr uint32_t queueSize = 8192;
	std::array<Edit, queueSize> edits_{};
	std::array<uint16_t, queueSize> batchSizes_{};
	alignas(64) std::atomic<uint32_t> write_{0};
	alignas(64) std::atomic<uint32_t> read_{0};
	std::atomic<uint32_t> order_{0}, pattern_{0}, row_{0}, voices_{0};
	std::atomic<float> left_{0}, right_{0};
	std::atomic<uint64_t> frames_{0};
	std::array<std::atomic<bool>, 192> mute_{};
	std::atomic<bool> fault_{false};
	std::array<PreviewNote, 128> notes_{};
	std::array<float, 512> nativeTailLeft_{}, nativeTailRight_{};
	std::atomic<uint32_t> noteWrite_{0}, noteRead_{0};
	std::array<uint16_t, 128> noteChannels_{};
	uint16_t nextPreviewChannel_ = 0;
	std::atomic<bool> panic_{false};
public:
	Renderer(const std::vector<std::byte> &bytes, uint32_t sampleRate, uint32_t order = 0, bool preview = false, const std::string &sourcePath = {}, uint32_t sequence = 0, PlaybackRegion region = {});
	void loop(bool value) noexcept { loop_.store(value, std::memory_order_relaxed); }
	CSoundFile &song() { return *song_; }
	void preparePreciseNotes(const NativeSong &native) { preciseNotes_=std::make_unique<PreciseNoteRuntime>(native); }
	void recordingTime(uint64_t hostTime,double ticksPerSample) noexcept { renderHostTime_=hostTime;hostTicksPerSample_=ticksPerSample; }
	const RecordingClock &recordingClock() const { return *recordingClock_; }
	bool enqueue(const std::vector<Edit> &edits);
	bool preview(PreviewNote) noexcept;
	void panic() noexcept { panic_.store(true); }
	uint32_t render(float *interleaved, uint32_t frames) noexcept;
	void processNativeTail(float *interleaved, uint32_t frames) noexcept;
	Telemetry telemetry() const noexcept;
	void mute(uint32_t ch, bool mute) noexcept;
	void applyColumnMutes(const NativeSong &native, const CSoundFile &source) noexcept;
	bool faulted() const noexcept { return fault_.load(); }
};
}  // namespace Tracker
