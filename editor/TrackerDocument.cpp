#include "TrackerDocument.hpp"
#include "SampleArchive.hpp"
#include "SongTiming.hpp"
#include "TrackLayout.hpp"
#include "common/FileReader.h"
#include "soundlib/Mixer.h"
#include "soundlib/plugins/PlugInterface.h"
#include "soundlib/ModSample.h"
#include "soundlib/mod_specifications.h"
#include "tracklib/SampleEdit.h"
#include "common/version.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cmath>
#include <cstring>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <cerrno>
#include <system_error>

namespace Tracker
{
namespace
{
class MemoryWriter final : public std::streambuf
{
	size_t position_ = 0;
public:
	std::vector<std::byte> data;
protected:
	std::streamsize xsputn(const char *source, std::streamsize n) override
	{
		if(n < 0 || uint64_t(n) > 512 * 1024 * 1024 || position_ + size_t(n) > 512 * 1024 * 1024) return 0;
		if(position_ + size_t(n) > data.size()) data.resize(position_ + size_t(n));
		std::memcpy(data.data() + position_, source, size_t(n));
		position_ += size_t(n);
		return n;
	}
	int_type overflow(int_type value) override
	{
		if(traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
		char c = traits_type::to_char_type(value);
		return xsputn(&c, 1) == 1 ? value : traits_type::eof();
	}
	pos_type seekoff(off_type offset, std::ios_base::seekdir direction, std::ios_base::openmode) override
	{
		int64_t next = (direction == std::ios_base::beg ? 0 : direction == std::ios_base::cur ? int64_t(position_)
																							  : int64_t(data.size()))
					 + offset;
		if(next < 0 || next > 512 * 1024 * 1024) return pos_type(off_type(-1));
		position_ = size_t(next);
		return pos_type(position_);
	}
	pos_type seekpos(pos_type position, std::ios_base::openmode mode) override { return seekoff(off_type(position), std::ios_base::beg, mode); }
};
std::unique_ptr<CSoundFile> load(const std::vector<std::byte> &bytes, const std::string &path = {})
{
	if(isSongSnapshot(bytes))
	{
		const auto parts = splitSongSnapshot(bytes);
		if(isSongSnapshot(parts.module)) throw std::runtime_error("Nested native song snapshots are not supported");
		auto song = load(std::vector<std::byte>(parts.module.begin(), parts.module.end()), path);
		restoreSampleArchive(*song, parts.samples);
		restoreTimingArchive(*song, parts.timing);
		return song;
	}
	auto song = std::make_unique<CSoundFile>();
	if(bytes.empty() || !song->Create(FileReader(::mpt::as_span(bytes), path.empty() ? FileReader::shared_filename_type{} : std::make_shared<::OpenMPT::mpt::PathString>(::OpenMPT::mpt::PathString::FromUTF8(path))), CSoundFile::loadNoPluginInstance))
		throw std::runtime_error("This file could not be read as a tracker module.");
	return song;
}
class FloatTarget : public IAudioTarget
{
	float *output_;
	uint32_t position_ = 0;
public:
	explicit FloatTarget(float *p)
		: output_(p)
	{
	}
	void Process(::mpt::audio_span_interleaved<MixSampleInt> b) override
	{
		for(size_t i = 0; i < b.size_frames(); ++i)
			for(size_t c = 0; c < 2; ++c)
				output_[(position_ + i) * 2 + c] = b(c, i) / float(MIXING_SCALEF);
		position_ += b.size_frames();
	}
	void Process(::mpt::audio_span_interleaved<MixSampleFloat> b) override
	{
		for(size_t i = 0; i < b.size_frames(); ++i)
			for(size_t c = 0; c < 2; ++c)
				output_[(position_ + i) * 2 + c] = b(c, i);
		position_ += b.size_frames();
	}
};
Cell from(const ModCommand &m)
{
	return {m.note, m.instr, uint8_t(m.volcmd), m.vol, uint8_t(m.command), m.param};
}
}  // namespace
Document::Document(MODTYPE type, CHANNELINDEX channels)
	: song_(std::make_unique<CSoundFile>())
{
	song_->Create(type, channels);
	song_->Patterns.Insert(0, 64);
	song_->Order().assign(1, 0);
	song_->m_songName = "Untitled";
	song_->m_dwCreatedWithVersion = Version::Current();
	song_->m_dwLastSavedWithVersion = Version::Current();
	native_.reconcile(*song_);
}
Document::Document(const std::vector<std::byte> &bytes)
	: song_(load(bytes))
{
	native_.reconcile(*song_);
}
std::unique_ptr<Document> Document::open(const std::string &path)
{
	std::ifstream f(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
	if(!f || f.tellg() <= 0 || f.tellg() > 512 * 1024 * 1024) throw std::runtime_error("Cannot open this file (maximum 512 MB).");
	std::vector<std::byte> bytes(size_t(f.tellg()));
	f.seekg(0);
	f.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
	if(!f) throw std::runtime_error("The file could not be completely read.");
	auto doc = std::make_unique<Document>();
	doc->song_ = load(bytes, path);
	doc->originalBytes_ = bytes;
	doc->sourcePath_ = path;
	doc->native_ = {};
	doc->native_.reconcile(*doc->song_);
	// Resolve external assets at import; subsequent saves embed the loaded PCM.
	for(SAMPLEINDEX i = 1; i <= doc->song_->GetNumSamples(); ++i)
		if(doc->song_->GetSample(i).HasSampleData()) doc->song_->GetSample(i).uFlags.reset(SMP_KEEPONDISK);
	return doc;
}
std::unique_ptr<Document> Document::demo(MODTYPE type)
{
	auto doc = std::make_unique<Document>(type);
	auto &s = doc->song();
	s.m_songName = "Midnight Circuit";
	s.Order().SetDefaultTempoInt(124);
	s.m_nSamples = 4;
	const char *names[] = {"", "Warm pulse", "Sub bass", "Soft kick", "Closed hat"};
	for(int index = 1; index <= 4; ++index)
	{
		auto &sample = s.GetSample(index);
		sample.Initialize(MOD_TYPE_MPT);
		sample.uFlags.set(CHN_16BIT);
		sample.nLength = index <= 2 ? 256 : 12000;
		sample.nC5Speed = index <= 2 ? 256 * 261.6256 : 24000;
		sample.nVolume = 128;
		sample.nGlobalVol = 64;
		if(index <= 2)
		{
			sample.uFlags.set(CHN_LOOP);
			sample.nLoopStart = 0;
			sample.nLoopEnd = 256;
		}
		if(!sample.AllocateSample()) throw std::bad_alloc();
		uint32_t random = 12345;
		for(uint32_t i = 0; i < sample.nLength; ++i)
		{
			double t = double(i) / sample.nLength, v = 0;
			if(index == 1) v = (sin(t * 6.2831853) + 0.28 * sin(t * 18.849556)) * 0.45;
			if(index == 2) v = sin(t * 6.2831853) * 0.65;
			if(index == 3)
			{
				double seconds = double(i) / 24000;
				v = sin(6.2831853 * (48 * seconds + 1.3 * (1 - exp(-seconds * 35)))) * exp(-seconds * 16) * 0.8;
			}
			if(index == 4)
			{
				random = random * 1664525u + 1013904223u;
				v = (double(int32_t(random)) / 2147483648.0) * exp(-t * 35) * 0.3;
			}
			sample.sample16()[i] = int16_t(v * 32767);
		}
		s.m_szNames[index] = names[index];
	}
	const int notes[] = {49, 56, 61, 56, 46, 53, 58, 53, 44, 51, 56, 51, 48, 55, 60, 55};
	for(int r = 0; r < 64; ++r)
	{
		auto &p = s.Patterns[0];
		if(r % 4 == 0)
		{
			auto &m = *p.GetpModCommand(r, 0);
			m.note = notes[r / 4];
			m.instr = 1;
			m.volcmd = VOLCMD_VOLUME;
			m.vol = 38;
		}
		if(r % 16 == 0)
		{
			auto &m = *p.GetpModCommand(r, 1);
			m.note = notes[r / 4] - 12;
			m.instr = 2;
			m.volcmd = VOLCMD_VOLUME;
			m.vol = 42;
		}
		if(r % 8 == 0)
		{
			auto &m = *p.GetpModCommand(r, 2);
			m.note = 61;
			m.instr = 3;
		}
		if(r % 4 == 2)
		{
			auto &m = *p.GetpModCommand(r, 3);
			m.note = 61;
			m.instr = 4;
		}
	}
	s.PrecomputeSampleLoops();
	doc->native_.reconcile(s);
	return doc;
}
bool Document::editable() const
{
	return bool(song_->GetType() & (MOD_TYPE_MOD | MOD_TYPE_XM | MOD_TYPE_S3M | MOD_TYPE_IT | MOD_TYPE_MPT));
}
std::vector<std::byte> Document::playbackData()
{
	return editable() ? snapshotData() : originalBytes_;
}
std::vector<std::byte> Document::serialize()
{

	MemoryWriter storage;
	std::ostream stream(&storage);
	bool ok = false;
	switch(song_->GetType())
	{
		case MOD_TYPE_MOD: ok = song_->SaveMod(stream); break;
		case MOD_TYPE_XM: ok = song_->SaveXM(stream); break;
		case MOD_TYPE_S3M: ok = song_->SaveS3M(stream); break;
		case MOD_TYPE_IT:
		case MOD_TYPE_MPT: ok = song_->SaveIT(stream, {}); break;
		default: throw std::runtime_error("This legacy format is currently read-only. Open MOD, XM, S3M, IT or MPTM to edit.");
	}
	if(!ok || !stream) throw std::runtime_error("Module serialization failed.");
	if(storage.data.size() > 512 * 1024 * 1024) throw std::runtime_error("Module exceeds the 512 MB save/reopen limit.");
	return std::move(storage.data);
}
std::vector<std::byte> Document::snapshotData()
{
	validateSamples();
	auto module = editable() ? serialize() : originalBytes_;
	auto base = load(module);
	auto samples = encodeSampleArchive(*song_, *base);
	auto timing = encodeTimingArchive(*song_, *base);
	return packSongSnapshot(module, samples, timing);
}
void Document::validateSamples() const
{
	for(SAMPLEINDEX i = 1; i <= song_->GetNumSamples(); ++i)
		if(song_->SampleHasPath(i) && !song_->GetSample(i).HasSampleData()) throw std::runtime_error("An external sample is missing. Replace it before saving or rendering.");
}
void Document::validateModuleSampleExport()
{
	validateSamples();
	auto converted = load(serialize());
	validateSongStructureExport(*song_, *converted);
	validateSampleExport(*song_, *converted);
	validateTimingExport(*song_, *converted);
}
void Document::save(const std::string &path, bool preserveSamples)
{
	validateSamples();
	auto bytes = serialize();
	auto converted = load(bytes);
	validateSongStructureExport(*song_, *converted);
	if(preserveSamples) {
		validateSampleExport(*song_, *converted);
		validateTimingExport(*song_, *converted);
	}
#if defined(_WIN32)
	// Same-directory exclusive creation and replacement preserve the original on
	// failure. UTF-8 document paths must not pass through the Windows ANSI codepage.
	const auto destination = std::filesystem::u8path(path).wstring();
	static std::atomic<uint64_t> saveSerial{0};
	std::wstring temporary;
	HANDLE file = INVALID_HANDLE_VALUE;
	for(unsigned attempt = 0; attempt < 64; ++attempt)
	{
		temporary = destination + L".writing." + std::to_wstring(GetCurrentProcessId()) + L"."
			+ std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(saveSerial.fetch_add(1));
		file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if(file != INVALID_HANDLE_VALUE) break;
		const auto error = GetLastError();
		if(error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
			throw std::system_error(error, std::system_category(), "Cannot create save file");
	}
	if(file == INVALID_HANDLE_VALUE)
		throw std::system_error(ERROR_FILE_EXISTS, std::system_category(), "Cannot create unique save file");
	try
	{
		size_t written = 0;
		while(written < bytes.size())
		{
			DWORD count = 0;
			const auto chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - written, MAXDWORD));
			if(!WriteFile(file, bytes.data() + written, chunk, &count, nullptr))
				throw std::system_error(GetLastError(), std::system_category(), "Saving failed; original retained");
			if(!count) throw std::runtime_error("Saving made no progress; original retained");
			written += count;
		}
		if(!FlushFileBuffers(file)) throw std::system_error(GetLastError(), std::system_category(), "Cannot flush save file");
		if(!CloseHandle(file)) throw std::system_error(GetLastError(), std::system_category(), "Cannot close save file");
		file = INVALID_HANDLE_VALUE;
		if(!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			throw std::system_error(GetLastError(), std::system_category(), "Cannot replace save file; original retained");
	} catch(...)
	{
		if(file != INVALID_HANDLE_VALUE) CloseHandle(file);
		DeleteFileW(temporary.c_str());
		throw;
	}
#else
	std::string temporary = path + ".writing.XXXXXX";
	int fd = mkstemp(temporary.data());
	if(fd < 0) throw std::system_error(errno, std::generic_category(), "Cannot create save file");
	try
	{
		size_t written = 0;
		while(written < bytes.size())
		{
			auto n = ::write(fd, bytes.data() + written, bytes.size() - written);
			if(n < 0 && errno == EINTR) continue;
			if(n <= 0) throw std::system_error(errno, std::generic_category(), "Saving failed; original retained");
			written += size_t(n);
		}
		if(fsync(fd)) throw std::system_error(errno, std::generic_category(), "Cannot flush save file");
		if(close(fd))
		{
			fd = -1;
			throw std::system_error(errno, std::generic_category(), "Cannot close save file");
		}
		fd = -1;
		std::filesystem::rename(temporary, path);
	} catch(...)
	{
		if(fd >= 0) close(fd);
		unlink(temporary.c_str());
		throw;
	}
#endif
}
bool Document::valid(int p, int r, int c) const
{
	return p >= 0 && song_->Patterns.IsValidPat(p) && r >= 0 && r < song_->Patterns[p].GetNumRows() && c >= 0 && c < song_->GetNumChannels();
}
Cell Document::cell(int p, int r, int c) const
{
	return valid(p, r, c) ? from(*song_->Patterns[p].GetpModCommand(r, c)) : Cell{};
}
void Document::put(CSoundFile &s, const Edit &e)
{
	auto &m = *s.Patterns[e.pattern].GetpModCommand(e.row, e.channel);
	m.note = e.after.note;
	m.instr = e.after.instrument;
	m.volcmd = VolumeCommand(e.after.volumeCommand);
	m.vol = e.after.volume;
	m.command = EffectCommand(e.after.effect);
	m.param = e.after.parameter;
}
void Document::validateEdits(const std::vector<Edit> &input) const
{
	if(!editable()) throw std::runtime_error("This legacy format is preview-only. Editing supports MOD, XM, S3M, IT, and MPTM.");
	for(auto &e : input)
	{
		if(!valid(e.pattern, e.row, e.channel)) throw std::out_of_range("Cell outside pattern");
		const auto &spec = song_->GetModSpecifications();
		if(e.after.note && !spec.HasNote(e.after.note)) throw std::invalid_argument("This note is outside the module format's range.");
		if(e.after.effect && !spec.HasCommand(EffectCommand(e.after.effect))) throw std::invalid_argument("This format does not support that effect.");
		if(e.after.volumeCommand && !spec.HasVolCommand(VolumeCommand(e.after.volumeCommand))) throw std::invalid_argument("This format does not support that volume-column command.");
		if((e.after.note > 128 && e.after.note < NOTE_MIN_SPECIAL) || e.after.effect >= MAX_EFFECTS || e.after.volumeCommand >= MAX_VOLCMDS)
			throw std::invalid_argument("Invalid note or effect command");
	}
}
std::vector<Edit> Document::edit(const std::vector<Edit> &input)
{
	validateEdits(input);
	std::optional<NativeSong> metadata;
	for(const auto &e:input) {
		const auto before=cell(e.pattern,e.row,e.channel);
		if(before.effect==e.after.effect && before.parameter==e.after.parameter) continue;
		const auto pattern=native_.patterns.at(e.pattern).id,track=native_.tracks.at(e.channel).id;
		auto matches=[&](const auto &c){return c.pattern==pattern&&c.track==track&&!c.column&&c.position/performanceUnitsPerRow==e.row;};
		if(std::any_of(native_.performance.commands.begin(),native_.performance.commands.end(),matches)) {
			if(!metadata) metadata=native_;
			std::erase_if(metadata->performance.commands,matches);
		}
	}
	if(metadata) {
		std::vector<Edit> changes;
		for(auto e:input){e.before=cell(e.pattern,e.row,e.channel);if(e.before!=e.after)changes.push_back(e);}
		editNative(std::move(*metadata),changes);return changes;
	}
	std::vector<Edit> edits;
	edits.reserve(input.size());
	UndoEntry entry;
	entry.cells.reserve(input.size());
	undo_.reserve(undo_.size() + 1);
	for(auto e : input)
	{
		e.before = cell(e.pattern, e.row, e.channel);
		if(e.before != e.after)
		{
			edits.push_back(e);
			entry.cells.push_back(e);
			put(*song_, e);
		}
	}
	if(!edits.empty())
	{
		undo_.push_back(std::move(entry));
		redo_.clear();
		++revision;
		trimHistory();
	}
	return edits;
}
bool Document::editNative(NativeSong metadata, const std::vector<Edit> &input)
{
	validateEdits(input);metadata.validate(*song_);
	UndoEntry entry;
	entry.cells.reserve(input.size());
	std::set<std::tuple<uint16_t,uint16_t,uint16_t>> positions;
	for(auto edit:input) {
		if(!positions.emplace(edit.pattern,edit.row,edit.channel).second)throw std::invalid_argument("Duplicate cell in native edit");
		edit.before=cell(edit.pattern,edit.row,edit.channel);
		if(edit.before!=edit.after)entry.cells.push_back(edit);
	}
	if(metadata==native_&&entry.cells.empty())return false;
	entry.nativeBefore=native_;entry.nativeAfter=metadata;
	undo_.reserve(undo_.size()+1);
	for(const auto &edit:entry.cells)put(*song_,edit);
	native_=std::move(metadata);undo_.push_back(std::move(entry));redo_.clear();++revision;trimHistory();
	return true;
}
std::vector<Edit> Document::undo()
{
	if(undo_.empty()) return {};
	const auto &pending = undo_.back();
	if(pending.sample) validateSampleUndo(*pending.sample, false);
	SampleAllocation sampleReplacement;
	if(pending.splice) sampleReplacement = prepareSpliceUndo(*pending.splice, false);
	if(pending.slot) sampleReplacement = prepareSampleSlot(*pending.slot, false);
	std::unique_ptr<CSoundFile> replacement;
	if(!pending.before.empty()) { replacement = load(pending.before); replacement->Order.SetSequence(pending.sequenceBefore); }
	auto metadata = pending.nativeBefore;
	if(metadata) metadata->nextID = std::max(metadata->nextID, native_.nextID);
	std::vector<Edit> inverse;
	inverse.reserve(pending.cells.size());
	redo_.reserve(redo_.size() + 1);
	for(auto i = pending.cells.rbegin(); i != pending.cells.rend(); ++i)
	{
		auto e = *i;
		std::swap(e.before, e.after);
		inverse.push_back(e);
	}
	if(replacement)
	{
		song_ = std::move(replacement);
		waveformCache_.invalidate();
	}
	else
		for(auto &e : inverse)
			put(*song_, e);
	if(pending.sample) applySampleUndo(*pending.sample, false);
	if(pending.splice) applySpliceUndo(*pending.splice, false, std::move(sampleReplacement));
	if(pending.slot) applySampleSlot(*pending.slot, false, std::move(sampleReplacement));
	if(metadata) native_ = std::move(*metadata);
	redo_.push_back(std::move(undo_.back()));
	undo_.pop_back();
	++revision;
	return inverse;
}
std::vector<Edit> Document::redo()
{
	if(redo_.empty()) return {};
	const auto &pending = redo_.back();
	if(pending.sample) validateSampleUndo(*pending.sample, true);
	SampleAllocation sampleReplacement;
	if(pending.splice) sampleReplacement = prepareSpliceUndo(*pending.splice, true);
	if(pending.slot) sampleReplacement = prepareSampleSlot(*pending.slot, true);
	std::unique_ptr<CSoundFile> replacement;
	if(!pending.after.empty()) { replacement = load(pending.after); replacement->Order.SetSequence(pending.sequenceAfter); }
	auto metadata = pending.nativeAfter;
	if(metadata) metadata->nextID = std::max(metadata->nextID, native_.nextID);
	auto cells = pending.cells;
	undo_.reserve(undo_.size() + 1);
	if(replacement)
	{
		song_ = std::move(replacement);
		waveformCache_.invalidate();
	}
	else
		for(auto &e : cells)
			put(*song_, e);
	if(pending.sample) applySampleUndo(*pending.sample, true);
	if(pending.splice) applySpliceUndo(*pending.splice, true, std::move(sampleReplacement));
	if(pending.slot) applySampleSlot(*pending.slot, true, std::move(sampleReplacement));
	if(metadata) native_ = std::move(*metadata);
	undo_.push_back(std::move(redo_.back()));
	redo_.pop_back();
	++revision;
	return cells;
}
void Document::trimHistory()
{
	size_t bytes = 0;
	for(const auto &entry : undo_) bytes += entry.bytes();
	while(bytes > 128 * 1024 * 1024 && undo_.size() > 1)
	{
		bytes -= undo_.front().bytes();
		undo_.erase(undo_.begin());
	}
}
void Document::restoreNative(NativeSong metadata)
{
	metadata.validate(*song_);
	native_ = std::move(metadata);
}
void Document::annotate(const std::function<void(NativeSong &)> &change)
{
    annotate(change,{});
}
void Document::annotate(const std::function<void(NativeSong &)> &change,const std::function<void()> &beforeCommit)
{
	if(!editable()) throw std::runtime_error("This document is read-only.");
	auto next = native_;
	change(next);
	next.validate(*song_);
	if(next == native_) return;
	UndoEntry entry;
	entry.nativeBefore = native_;
	entry.nativeAfter = next;
	undo_.reserve(undo_.size() + 1);
    static_assert(std::is_nothrow_move_assignable_v<NativeSong>);
	undo_.push_back(std::move(entry));
    // MSVC map move construction may allocate its sentinel. Stage the history
    // entry before publishing, and roll it back if the live queue refuses it.
    try { if(beforeCommit)beforeCommit(); }
    catch(...) { undo_.pop_back();throw; }
	native_ = std::move(next);
	redo_.clear();
	++revision;
	trimHistory();
}
void Document::transaction(const std::function<void(CSoundFile &)> &change)
{
	transaction([&](CSoundFile &song, NativeSong &) { change(song); });
}
void Document::transaction(const std::function<void(CSoundFile &, NativeSong &)> &change)
{
	auto before = snapshotData();
	auto nativeBefore = native_;
	const auto sequenceBefore = song_->Order.GetCurrentSequenceIndex();
	undo_.reserve(undo_.size() + 1);
	try
	{
		waveformCache_.invalidate();
		change(*song_, native_);
		native_.reconcile(*song_);
		native_.validate(*song_);
		auto after = snapshotData();
		UndoEntry entry;
		entry.before = before;
		entry.after = std::move(after);
		entry.nativeBefore = nativeBefore;
		entry.nativeAfter = native_;
		entry.sequenceBefore = sequenceBefore;
		entry.sequenceAfter = song_->Order.GetCurrentSequenceIndex();
		undo_.push_back(std::move(entry));
		redo_.clear();
		++revision;
		trimHistory();
	} catch(...)
	{
		song_ = load(before);
		song_->Order.SetSequence(sequenceBefore);
		native_ = std::move(nativeBefore);
		throw;
	}
}
void Document::resizeChannels(CSoundFile &song, int channels)
{
	const auto &spec = song.GetModSpecifications();
	if(channels < spec.channelsMin || channels > spec.channelsMax) throw std::runtime_error("Channel count is outside the format limits");
	if(channels == song.GetNumChannels()) return;
	std::vector<std::pair<PATTERNINDEX, std::vector<ModCommand>>> patterns;
	for(PATTERNINDEX index = 0; index < song.Patterns.Size(); ++index)
	{
		if(!song.Patterns.IsValidPat(index)) continue;
		auto &pattern = song.Patterns[index];
		std::vector<ModCommand> cells(pattern.GetNumRows() * channels);
		for(ROWINDEX row = 0; row < pattern.GetNumRows(); ++row)
			for(int ch = 0; ch < std::min(channels, int(song.GetNumChannels())); ++ch)
				cells[row * channels + ch] = *pattern.GetpModCommand(row, ch);
		patterns.emplace_back(index, std::move(cells));
	}
	song.ChnSettings.resize(channels);
	for(auto &pattern : patterns)
		song.Patterns[pattern.first].SetData(std::move(pattern.second));
}
int Document::importSample(const std::string &path, int slot)
{
	std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
	if(!file || file.tellg() <= 0 || file.tellg() > 256 * 1024 * 1024) throw std::runtime_error("Cannot import sample (maximum 256 MB).");
	std::vector<std::byte> bytes(size_t(file.tellg()));
	file.seekg(0);
	file.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
	if(!file) throw std::runtime_error("Cannot read sample.");
	if(!slot) slot = song_->GetNumSamples() + 1;
	if(slot <= 0 || slot > song_->GetModSpecifications().samplesMax || slot >= MAX_SAMPLES) throw std::runtime_error("Sample slots are full.");
	transaction([&](CSoundFile &s)
	{s.m_nSamples=std::max(s.m_nSamples,SAMPLEINDEX(slot));FileReader reader(::mpt::as_span(bytes));if(!s.ReadSampleFromFile(slot,reader,false))throw std::runtime_error("Unsupported sample file. Try WAV or AIFF.");s.m_szNames[slot]=::OpenMPT::mpt::ToCharset(s.GetCharsetInternal(), ::OpenMPT::mpt::Charset::UTF8, ::OpenMPT::mpt::PathString::FromUTF8(path).GetFilenameBase().ToUTF8());s.GetSample(slot).PrecomputeLoops(s,false); });
	return slot;
}
int Document::importInstrument(const std::string &path, int slot)
{
	std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
	if(!file || file.tellg() <= 0 || file.tellg() > 256 * 1024 * 1024) throw std::runtime_error("Cannot import instrument (maximum 256 MB).");
	std::vector<std::byte> bytes(size_t(file.tellg()));
	file.seekg(0);
	file.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
	if(!file) throw std::runtime_error("Cannot read instrument.");
	int result = slot;
	transaction([&](CSoundFile &song)
	{
		const auto maximum = song.GetModSpecifications().instrumentsMax;
		if(!maximum) throw std::runtime_error("This module format does not support instruments.");
		if(!song.GetNumInstruments() && song.GetNumSamples())
		{
			if(song.GetNumSamples() >= maximum) throw std::runtime_error("Matching the existing sample slots leaves no free instrument slot.");
			for(SAMPLEINDEX i = 1; i <= song.GetNumSamples(); ++i)
			{
				song.Instruments[i] = new ModInstrument(i);
				song.Instruments[i]->name = song.GetSampleName(i);
				song.m_nInstruments = i;
			}
		}
		if(!result) result = song.GetNumInstruments() + 1;
		if(result <= 0 || result > maximum || result >= MAX_INSTRUMENTS) throw std::runtime_error("Instrument slot is outside the format limits.");
		FileReader reader(::mpt::as_span(bytes), std::make_shared<::OpenMPT::mpt::PathString>(::OpenMPT::mpt::PathString::FromUTF8(path)));
		if(!song.ReadInstrumentFromFile(INSTRUMENTINDEX(result), reader, false)) throw std::runtime_error("Unsupported instrument file. Try ITI or XI.");
		for(SAMPLEINDEX i = 1; i <= song.GetNumSamples(); ++i)
		{
			auto &sample = song.GetSample(i);
			if(sample.HasSampleData())
				sample.uFlags.reset(SMP_KEEPONDISK);
			else if(song.SampleHasPath(i))
				throw std::runtime_error("The instrument refers to a missing external sample.");
		}
	});
	return result;
}
int Document::addPattern(int rows, bool duplicate, int source)
{
	int index = 0;
	while(song_->Patterns.IsValidPat(index))
		++index;
	transaction([&](CSoundFile &s, NativeSong &native)
	{
		const auto &spec = s.GetModSpecifications();
		if(rows < spec.patternRowsMin || rows > spec.patternRowsMax) throw std::runtime_error("This row count is outside the format limits.");
		if(index >= spec.patternsMax || s.Order().size() >= spec.ordersMax) throw std::runtime_error("The format has no more pattern or order slots.");
		if(!s.Patterns.Insert(index, rows)) throw std::runtime_error("Could not allocate pattern.");
		if(duplicate && s.Patterns.IsValidPat(source))
		{
			auto &from = s.Patterns[source];
			for(int r = 0; r < std::min(rows, int(from.GetNumRows())); ++r)
				for(int c = 0; c < s.GetNumChannels(); ++c) *s.Patterns[index].GetpModCommand(r, c) = *from.GetpModCommand(r, c);
			auto entity = native.patterns.at(source);
			entity.id = native.makeEntity().id;
			native.clonePatternAutomation(native.patterns.at(source).id, entity.id);
			native.patterns[index] = std::move(entity);
		}
		s.Order().push_back(index);
	});
	return index;
}
void Document::setOrder(int index, int pattern) { editOrder(index, pattern, "assign"); }
void Document::editOrder(int index, int pattern, const std::string &operation)
{
	transaction([&](CSoundFile &s, NativeSong &native)
	{
		auto &sequence = s.Order();
		auto &slots = native.sequences[s.Order.GetCurrentSequenceIndex()].orders;
		if(index < 0 || index >= sequence.size()) throw std::invalid_argument("Select a valid order.");
		if(operation == "assign" || operation == "before" || operation == "after")
		{
			if(pattern < 0 || !s.Patterns.IsValidPat(pattern)) throw std::invalid_argument("Select a valid pattern.");
			if(operation == "assign") sequence[index] = PATTERNINDEX(pattern);
			else
			{
				if(sequence.size() >= s.GetModSpecifications().ordersMax) throw std::invalid_argument("The order list is full.");
				const auto position = index + (operation == "after");
				auto entity = native.makeEntity();
				if(sequence.insert(ORDERINDEX(position), 1, PATTERNINDEX(pattern)) != 1) throw std::runtime_error("Could not insert order.");
				slots.insert(slots.begin() + position, std::move(entity));
			}
		} else if(operation == "up" || operation == "down")
		{
			const auto target = index + (operation == "up" ? -1 : 1);
			if(target < 0 || target >= sequence.size()) throw std::invalid_argument("This order is at the edge of the list.");
			std::swap(sequence[index], sequence[target]);
			std::swap(slots[index], slots[target]);
		} else if(operation == "remove")
		{
			if(sequence.size() <= 1) throw std::invalid_argument("Keep at least one order.");
			sequence.Remove(index, index);
			slots.erase(slots.begin() + index);
		} else throw std::invalid_argument("Unknown order operation.");
	});
}
void Document::removeOrder(int index) { editOrder(index, 0, "remove"); }
void Document::processSample(int index, const std::string &operation, uint32_t first, uint32_t last)
{
	if(index < 1 || index > song_->GetNumSamples()) throw std::runtime_error("Select a sample first.");
	const auto length = song_->GetSample(index).nLength;
	if(!last) last = length; // Legacy native editor's whole-sample sentinel.
	if(operation != "trim")
	{
		SampleProcessOptions options; options.operation = operation; options.first = first; options.last = last;
		processSample(index, options);
		return;
	}
	if(first >= last || last > length) throw std::invalid_argument("Select a nonempty range inside the sample.");
	if(first == 0 && last == length) return;
	transaction([&](CSoundFile &s)
	{
		auto &sample = s.GetSample(index);
		if(last < sample.nLength) SampleEdit::RemoveRange(sample, last, sample.nLength, SampleChannelSelection::Both, s);
		if(first) SampleEdit::RemoveRange(sample, 0, first, SampleChannelSelection::Both, s);
		sample.PrecomputeLoops(s, false);
	});
}
void Document::sampleSettings(int index, int rate, int volume, int pan, uint32_t start, uint32_t end, bool loop, bool pingpong, const std::optional<std::string> &name, SampleSettingsFields fields)
{
	if(index < 1 || index > song_->GetNumSamples()) throw std::runtime_error("Select a sample first.");
	transaction([&](CSoundFile &s)
	{
		auto &sample=s.GetSample(index);
		if(name)s.m_szNames[index]=::OpenMPT::mpt::ToCharset(s.GetCharsetInternal(), ::OpenMPT::mpt::Charset::UTF8,*name);
		if(fields.rate) {
			sample.nC5Speed=std::clamp(rate,100,192000);
			if(s.GetType()&(MOD_TYPE_MOD|MOD_TYPE_XM))sample.FrequencyToTranspose();
		}
		if(fields.volume)sample.nVolume=std::clamp(volume,0,64)*4;
		// An omitted pan field must preserve channel/instrument panning.
		// Explicit pan (even its current numeric value) enables sample panning.
		if(fields.pan){sample.nPan=std::clamp(pan,0,256);sample.uFlags.set(CHN_PANNING);}
		if(fields.loops) {
			if(loop&&(start>=end || end>sample.nLength))throw std::runtime_error("Loop end must follow its start and lie inside the sample.");
			sample.SetLoop(start,end,loop,pingpong,s);
		}
	});
}

Renderer::Renderer(const std::vector<std::byte> &bytes, uint32_t rate, uint32_t order, bool preview, const std::string &sourcePath, uint32_t sequence, PlaybackRegion region, const NativeSong *native)
	: region_(region), loop_(region.loop), song_(load(bytes, sourcePath))
{
	if(native) native->prepareEffects(*song_);
	for(INSTRUMENTINDEX i=1;i<=song_->GetNumInstruments();++i) if(song_->Instruments[i]) instrumentIndices_[song_->Instruments[i]]=i;

	for(SAMPLEINDEX i = 1; i <= song_->GetNumSamples(); ++i)
		if(song_->SampleHasPath(i) && !song_->GetSample(i).HasSampleData()) throw std::runtime_error("An external sample is missing. Replace it before saving or rendering.");
	if(sequence == UINT32_MAX) sequence = song_->Order.GetCurrentSequenceIndex();
	if(sequence >= song_->Order.GetNumSequences()) throw std::runtime_error("Select a valid sequence.");
	song_->Order.SetSequence(SEQUENCEINDEX(sequence));
	auto settings = song_->m_MixerSettings;
	settings.gdwMixingFreq = rate;
	settings.gnChannels = 2;
	settings.m_nMaxMixChannels = 256;
	song_->SetMixerSettings(settings);
	auto resampler = song_->m_Resampler.m_Settings;
	resampler.SrcMode = SRCMODE_SINC8LP;
	song_->SetResamplerSettings(resampler);
	song_->SetRepeatCount(0);
	song_->ResetPlayPos();
	if(region_.pattern != UINT32_MAX) {
		if(!song_->Patterns.IsValidPat(PATTERNINDEX(region_.pattern)) || region_.pattern > UINT16_MAX || region_.startRow >= region_.endRow || region_.endRow > song_->Patterns[region_.pattern].GetNumRows() || region_.cursorRow < region_.startRow || region_.cursorRow >= region_.endRow)
			throw std::runtime_error("Invalid playback row range.");
	}
	if(order || region_.cursorRow)
	{
		if(order >= song_->Order().size() || !song_->Order().IsValidPat(order)) throw std::runtime_error("Select a playable order.");
		song_->m_PlayState.m_nCurrentOrder = ORDERINDEX(order);
		song_->SetCurrentOrder(ORDERINDEX(order));
		song_->m_PlayState.m_nNextRow = ROWINDEX(region_.cursorRow);
		song_->m_PlayState.m_nTickCount = CSoundFile::TICKS_ROW_FINISHED;
		auto target = song_->GetLength(eAdjustSamplePositions, GetLengthTarget(ORDERINDEX(order), ROWINDEX(region_.cursorRow)));
		if(!target.empty()) frames_.store(uint64_t(target.back().duration * rate));
	}
	song_->InitPlayer(true);
	song_->m_SongFlags.reset(SONG_PLAYALLSONGS);
	song_->PrepareRealtime();
	if(region_.pattern != UINT32_MAX) {
		song_->m_PlayState.m_flags.set(SONG_PATTERNLOOP);
		song_->m_PlayState.m_nPattern = PATTERNINDEX(region_.pattern);
		song_->m_PlayState.m_nNextRow = ROWINDEX(region_.cursorRow);
		song_->m_PlayState.m_nTickCount = CSoundFile::TICKS_ROW_FINISHED;
	}
	song_->nativeTransportContext = this;
	song_->nativeTransportRow = [](void *context) noexcept {
		auto &renderer = *static_cast<Renderer *>(context); auto &s = *renderer.song_; auto &state = s.m_PlayState;
		const bool loop = renderer.loop_.load(std::memory_order_relaxed);
		s.SetRepeatCount(loop ? -1 : 0);
		const auto &range = renderer.region_;
		if(range.pattern != UINT32_MAX) {
			if(renderer.regionStarted_ && (state.m_nRow < range.startRow || state.m_nRow >= range.endRow || (state.m_nRow == 0 && renderer.regionLastRow_ + 1 == s.Patterns[range.pattern].GetNumRows()))) {
				if(!loop) return false;
				state.m_nRow = ROWINDEX(range.startRow);
			}
			// Pattern commands may request an order jump; a bounded audition stays here.
			state.m_nPattern = PATTERNINDEX(range.pattern);
			renderer.regionStarted_ = true; renderer.regionLastRow_ = state.m_nRow;
		}
		return true;
	};
	song_->nativePrepareContext=this;
	song_->nativePrepareMix=[](void *context,uint32_t count) noexcept {
		auto &renderer=*static_cast<Renderer *>(context);auto &song=*renderer.song_;
		if(renderer.preciseNotes_)count=renderer.preciseNotes_->prepare(song,count);
		const auto &state=song.m_PlayState;
		if(renderer.renderHostTime_&&renderer.hostTicksPerSample_>0&&!state.m_flags[SONG_PAUSED|SONG_FADINGSONG]&&state.m_nSamplesPerTick&&state.TicksOnRow()) {
			const double units=double(performanceUnitsPerRow)/(double(state.TicksOnRow())*state.m_nSamplesPerTick);
			const double position=double(state.m_nRow)*performanceUnitsPerRow+double(state.m_nTickCount)*performanceUnitsPerRow/state.TicksOnRow()+state.SamplesIntoTick()*units;
			const auto start=renderer.renderHostTime_+uint64_t(std::llround(renderer.renderOffset_*renderer.hostTicksPerSample_));
			const auto end=renderer.renderHostTime_+uint64_t(std::llround((renderer.renderOffset_+count)*renderer.hostTicksPerSample_));
			renderer.recordingClock_->publish(start,end,{song.Order.GetCurrentSequenceIndex(),state.m_nCurrentOrder,state.m_nPattern,0},position,units/renderer.hostTicksPerSample_);
		}
		renderer.renderOffset_+=count;return count;
	};
	noteChannels_.fill(CHANNELINDEX_INVALID);
	nextPreviewChannel_ = song_->GetNumChannels();
	if(preview) song_->m_PlayState.m_flags.set(SONG_PAUSED);
	for(size_t ch = 0; ch < song_->GetNumChannels(); ++ch)
		mute_[ch].store(song_->ChnSettings[ch].dwFlags[CHN_MUTE]);
}
bool Renderer::enqueue(const std::vector<Edit> &edits)
{
	auto w = write_.load(std::memory_order_relaxed), r = read_.load(std::memory_order_acquire);
	if(edits.empty()) return true;
	if(edits.size() > 512 || edits.size() > queueSize - (w - r)) return false;
	batchSizes_[w % queueSize] = uint16_t(edits.size());
	for(auto &e : edits)
		edits_[(w++) % queueSize] = e;
	write_.store(w, std::memory_order_release);
	return true;
}
bool Renderer::preview(PreviewNote note) noexcept
{
	auto w = noteWrite_.load(std::memory_order_relaxed), r = noteRead_.load(std::memory_order_acquire);
	if(w - r >= notes_.size())
	{
		panic();
		return false;
	}
	notes_[w % notes_.size()] = {note, panicEpoch_.load(std::memory_order_acquire)};
	noteWrite_.store(w + 1, std::memory_order_release);
	return true;
}
void Renderer::mute(uint32_t ch, bool value) noexcept
{
	if(ch < mute_.size()) mute_[ch].store(value, std::memory_order_relaxed);
}
void Renderer::applyColumnMutes(const NativeSong &native, const CSoundFile &source) noexcept
{
  for (uint16_t c = 0; c < source.GetNumChannels(); ++c) mute(c, effectiveColumnMute(native, source, c));
}
uint32_t Renderer::render(float *out, uint32_t frames) noexcept
{
	std::fill(out, out + frames * 2, 0.0f);
	auto pluginNote = [&](CHANNELINDEX channel, uint16 note, uint16 volume) {
		const auto *instrument = song_->m_PlayState.Chn[channel].pModInstrument;
		if(instrument && instrument->nMixPlug && instrument->nMixPlug <= MAX_MIXPLUGINS)
			if(auto *plugin = song_->m_MixPlugins[instrument->nMixPlug - 1].pMixPlugin)
				plugin->MidiCommand(*instrument, note, volume, channel);
	};
	auto releasePreview = [&](ModChannel &chn, bool cut) {
		const auto increment = chn.increment;
		const auto volume = chn.nVolume;
		song_->NoteChange(chn, cut ? NOTE_NOTECUT : NOTE_KEYOFF);
		if(cut) {
			// Like native NC, keep the sample moving while ramping it down.
			// Legacy IT cut freezes its increment and leaves a long DC tail.
			// Preview releases must also take effect between tracker ticks.
			chn.increment = increment;
			// A zero channel volume retires background channels at the next
			// tick, possibly before this ramp finishes. Fade-to-zero keeps the
			// moving sample alive until the mixer has completed its ramp.
			chn.nVolume = std::max(1, volume);
			chn.nFadeOutVol = 0;
			chn.dwFlags.set(CHN_NOTEFADE);
			chn.newLeftVol = chn.newRightVol = 0;
			chn.dwFlags.set(CHN_FASTVOLRAMP | CHN_VOLUMERAMP);
			song_->ProcessRamping(chn);
		}
	};
	auto synchronizePanic = [&] {
		const auto epoch = panicEpoch_.load(std::memory_order_acquire);
		if(epoch == previewEpoch_) return;
		for(auto &chn : song_->m_PlayState.BackgroundChannels(*song_))
			if(chn.isPreviewNote) { pluginNote(static_cast<CHANNELINDEX>(&chn-song_->m_PlayState.Chn.data()), NOTE_KEYOFF, 0); releasePreview(chn, true); }
		noteChannels_.fill(CHANNELINDEX_INVALID);
		previewEpoch_ = epoch;
	};
	synchronizePanic();
	auto noteRead = noteRead_.load(std::memory_order_relaxed), noteWrite = noteWrite_.load(std::memory_order_acquire);
	for(int consumed = 0; noteRead != noteWrite && consumed < 32; ++noteRead)
	{
		const auto queued = notes_[noteRead % notes_.size()];
		// A Panic can precede this event, including one arriving after the
		// callback's initial epoch read. Never flush newer accepted notes.
		if(queued.epoch != previewEpoch_) synchronizePanic();
		if(queued.epoch != previewEpoch_) continue;
		++consumed; // At most 32 active events; at most 128 total queue slots.
		const auto event = queued.note;
		if(event.note < 1 || event.note > 120) continue;
		if(!event.on || !event.velocity)
		{
			auto channel = noteChannels_[event.note];
			if(channel < MAX_CHANNELS)
			{
				pluginNote(channel, NOTE_KEYOFF, 0);
				releasePreview(song_->m_PlayState.Chn[channel], event.sample != 0);
				noteChannels_[event.note] = CHANNELINDEX_INVALID;
			}
			continue;
		}
		if(noteChannels_[event.note] < MAX_CHANNELS) {
			auto &previous = song_->m_PlayState.Chn[noteChannels_[event.note]];
			pluginNote(noteChannels_[event.note], NOTE_KEYOFF, 0);
			releasePreview(previous, previous.pModInstrument == nullptr);
		}
		CHANNELINDEX channel = nextPreviewChannel_++;
		pluginNote(channel, NOTE_KEYOFF, 0);
		if(nextPreviewChannel_ >= MAX_CHANNELS) nextPreviewChannel_ = song_->GetNumChannels();
		for(auto &mapped : noteChannels_)
			if(mapped == channel) mapped = CHANNELINDEX_INVALID;
		auto &chn = song_->m_PlayState.Chn[channel];
		chn.Reset(ModChannel::resetTotal, *song_, CHANNELINDEX_INVALID, CHN_MUTE);
		chn.nNewNote = chn.nLastNote = event.note;
		chn.nVolume = 256;
		chn.ResetEnvelopes();
		if(event.sample && event.sample <= song_->GetNumSamples()) {
			chn.pModInstrument = nullptr;
			chn.pModSample = &song_->GetSample(event.sample);
			chn.nC5Speed = chn.pModSample->nC5Speed;
			chn.nFineTune = chn.pModSample->nFineTune;
			chn.nTranspose = chn.pModSample->RelativeTone;
			chn.nInsVol = chn.pModSample->nGlobalVol;
			if(chn.pModSample->uFlags[CHN_PANNING]) chn.nPan = chn.pModSample->nPan;
			chn.nNewIns = 0;
		} else song_->InstrumentChange(song_->m_PlayState, channel, event.instrument);
		chn.nFadeOutVol = 0x10000;
		chn.isPreviewNote = true;
		chn.nMasterChn = 0;
		song_->NoteChange(chn, event.note, false, true, true, channel);
		chn.nVolume = event.velocity * 256 / 127;
		pluginNote(channel, event.note, static_cast<uint16>(chn.nVolume));
		noteChannels_[event.note] = channel;
		auto begin = std::begin(song_->m_PlayState.ChnMix);
		auto end = std::remove(begin, begin + song_->m_nMixChannels, channel);
		song_->m_nMixChannels = CHANNELINDEX(std::distance(begin, end));
	}
	noteRead_.store(noteRead, std::memory_order_release);
	auto r = read_.load(std::memory_order_relaxed), w = write_.load(std::memory_order_acquire);
	uint32_t processed = 0;
	while(r != w)
	{
		auto size = batchSizes_[r % queueSize];
		if(!size || processed + size > 512) break;
		for(uint32_t n = 0; n < size; ++n, ++r)
			Document::put(*song_, edits_[r % queueSize]);
		processed += size;
	}
	read_.store(r, std::memory_order_release);
	std::array<bool, 192> changedMutes{};
	bool muteChanged = false;
	for(size_t c = 0; c < song_->GetNumChannels(); ++c)
	{
		auto &channel = song_->m_PlayState.Chn[c];
		const bool muted = mute_[c].load(std::memory_order_relaxed);
		changedMutes[c] = channel.dwFlags[CHN_MUTE | CHN_SYNCMUTE] != muted;
		muteChanged |= changedMutes[c];
		if(muted && changedMutes[c]) pluginNote(static_cast<CHANNELINDEX>(c), NOTE_KEYOFF, 0);
		channel.dwFlags.reset(CHN_SYNCMUTE);
		channel.dwFlags.set(CHN_MUTE, muted);
		// S3M compatibility also consults channel settings when processing events.
		song_->ChnSettings[c].dwFlags.set(CHN_MUTE, muted);
	}
	if(muteChanged) for(auto &voice : song_->m_PlayState.BackgroundChannels(*song_))
	{
		if(voice.isPreviewNote || !voice.nMasterChn || voice.nMasterChn > song_->GetNumChannels()) continue;
		const auto parent = voice.nMasterChn - 1;
		if(!changedMutes[parent]) continue;
		const bool muted = song_->m_PlayState.Chn[parent].dwFlags[CHN_MUTE];
		if(muted && !voice.dwFlags[CHN_MUTE])
			pluginNote(static_cast<CHANNELINDEX>(&voice - song_->m_PlayState.Chn.data()), NOTE_KEYOFF, 0);
		voice.dwFlags.reset(CHN_SYNCMUTE);
		voice.dwFlags.set(CHN_MUTE, muted);
	}
	song_->ResetMixStat();
	FloatTarget target(out);
	renderOffset_=0;
	uint32_t count = song_->Read(frames, target);
	if(song_->RealtimeCapacityExceeded())
	{
		fault_ = true;
		std::fill(out, out + frames * 2, 0.0f);
		return 0;
	}
	float left = 0, right = 0;
	for(uint32_t i = 0; i < count; ++i)
	{
		left = std::max(left, std::abs(out[i * 2]));
		right = std::max(right, std::abs(out[i * 2 + 1]));
	}
	order_.store(song_->GetCurrentOrder(), std::memory_order_relaxed);
	pattern_.store(song_->GetCurrentPattern(), std::memory_order_relaxed);
	row_.store(song_->m_PlayState.m_nRow, std::memory_order_relaxed);
	voices_.store(song_->GetMixStat(), std::memory_order_relaxed);
	left_.store(left, std::memory_order_relaxed);
	right_.store(right, std::memory_order_relaxed);
	frames_.fetch_add(count, std::memory_order_relaxed);
 publishVoices();
	return count;
}
void Renderer::processNativeTail(float *interleaved, uint32_t frames) noexcept
{
	for(uint32_t position = 0; position < frames;)
	{
		const uint32_t count = std::min<uint32_t>(frames - position, nativeTailLeft_.size());
		for(uint32_t i = 0; i < count; ++i)
		{
			nativeTailLeft_[i] = interleaved[(position + i) * 2];
			nativeTailRight_[i] = interleaved[(position + i) * 2 + 1];
		}
		FloatTarget target(interleaved + position * 2);
		song_->ProcessNativeTail(nativeTailLeft_.data(), nativeTailRight_.data(), count, target);
		position += count;
	}
}
void Renderer::publishVoices() noexcept {
 voiceSequence_.fetch_add(1); // Odd while writing; all fields remain atomic.
 uint32_t count=0;
 const auto &channels=song_->m_PlayState.Chn;
 for(size_t channel=0;channel<channels.size() && count<publishedVoices_.size();++channel) {
   const auto &voice=channels[channel];
   if(!voice.pCurrentSample || !voice.nLength || voice.dwFlags[CHN_MUTE | CHN_SYNCMUTE]) continue;
   const auto sampleAddress=reinterpret_cast<uintptr_t>(voice.pModSample),first=reinterpret_cast<uintptr_t>(&song_->GetSample(1)),last=reinterpret_cast<uintptr_t>(&song_->GetSample(0))+(song_->GetNumSamples()+1)*sizeof(ModSample);
   if(sampleAddress<first || sampleAddress>=last) continue;
   const auto sample=uint32_t((sampleAddress-reinterpret_cast<uintptr_t>(&song_->GetSample(0)))/sizeof(ModSample));
   const auto found=instrumentIndices_.find(voice.pModInstrument);
   const auto instrument=found==instrumentIndices_.end()?0:found->second;
   const auto offset=song_->m_playBehaviour[kITEnvelopePositionHandling]?1u:0u;
   auto tick=[&](const ModChannel::EnvInfo &env){return env.nEnvPosition>offset?env.nEnvPosition-offset:0u;};
   auto &entry=publishedVoices_[count++];
   entry.words[0].store(uint64_t(channel) | (uint64_t(sample)<<16) | (uint64_t(instrument)<<32));
   entry.words[1].store(uint64_t(voice.position.GetUInt()));
   entry.words[2].store(voice.nativeNoteGeneration);
   entry.words[3].store(uint64_t(tick(voice.VolEnv)) | (uint64_t(tick(voice.PanEnv))<<32));
   entry.words[4].store(tick(voice.PitchEnv));
 }
 publishedVoiceCount_.store(count);voiceSequence_.fetch_add(1);
}
std::vector<VoicePosition> Renderer::voicePositions() const {
 std::vector<VoicePosition> result;result.reserve(publishedVoices_.size());
 for(int attempt=0;attempt<3;++attempt) {
   const auto before=voiceSequence_.load();if(before&1)continue;
   result.clear();const auto count=std::min<size_t>(publishedVoiceCount_.load(),publishedVoices_.size());
   for(size_t i=0;i<count;++i) {
     const auto &entry=publishedVoices_[i];const auto ids=entry.words[0].load(),env=entry.words[3].load();
     result.push_back({uint32_t(ids&0xffff),uint32_t((ids>>16)&0xffff),uint32_t(ids>>32),uint32_t(entry.words[1].load()),entry.words[2].load(),{uint32_t(env),uint32_t(env>>32),uint32_t(entry.words[4].load())}});
   }
   if(before==voiceSequence_.load())return result;
 }
 return {}; // Never wait for the audio thread.
}
Telemetry Renderer::telemetry() const noexcept
{
	return {order_.load(), pattern_.load(), row_.load(), voices_.load(), left_.load(), right_.load(), frames_.load()};
}
}  // namespace Tracker
