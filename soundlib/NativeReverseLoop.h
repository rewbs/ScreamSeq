/* Native sampler traversal. Legacy module playback never enters this path. */
#pragma once
#include "Snd_defs.h"
#include <algorithm>

OPENMPT_NAMESPACE_BEGIN

// Position increases in traversal order, so interpolation always receives the
// chronological PCM sequence, including the first turn and subsequent wraps.
// The loop's last frame is repeated once at the initial forward/reverse turn.
struct NativeReverseLoopState
{
	const void *sample = nullptr;
	SmpLength start = 0, end = 0;
	SamplePosition lastPosition;
	static constexpr uint32 HistoryFrames = 32;
	uint32 historyFrames = 0; // Prior reverse cycles retained until all interpolation taps are periodic.
	bool reversed = false, sustain = false;

	void Reset() noexcept { *this = {}; }
	void Attach(const void *identity, SmpLength first, SmpLength last, bool held, SamplePosition position) noexcept
	{
		if(sample != identity || start != first || end != last || sustain != held || lastPosition != position)
		{
			Reset(); sample = identity; start = first; end = last; sustain = held;
		}
		lastPosition = position;
	}
	SamplePosition Advance(SamplePosition position) noexcept
	{
		const auto boundary = SamplePosition(end, 0).GetRaw();
		const auto length = SamplePosition(end - start, 0).GetRaw();
		if(length > 0 && position.GetRaw() >= boundary)
		{
			const auto excess = position.GetRaw() - boundary;
			const auto cycles = excess / length + (reversed ? 1 : 0);
			historyFrames = uint32(std::min<int64>(HistoryFrames, historyFrames + cycles * (end - start)));
			reversed = true;
			position = SamplePosition(SamplePosition(start, 0).GetRaw() + excess % length);
		}
		lastPosition = position;
		return position;
	}
	SmpLength SourceFrame(int64 position, SmpLength frames) const noexcept
	{
		const int64 length = int64(end) - start;
		if(!reversed && position < end)
			return SmpLength(std::clamp<int64>(position, 0, frames - 1));
		if(reversed && historyFrames < HistoryFrames && int64(historyFrames) + position < start)
			return SmpLength(std::clamp<int64>(int64(end) + historyFrames + position - start, 0, frames - 1));
		const int64 offset = position - (reversed ? start : end);
		const int64 phase = (offset % length + length) % length;
		return SmpLength(int64(end) - 1 - phase);
	}
	SamplePosition PhysicalPosition(SamplePosition position) const noexcept
	{
		if(!reversed || position != lastPosition) return position;
		return std::max(SamplePosition{}, SamplePosition(start + end - 1, 0) - position);
	}
};

OPENMPT_NAMESPACE_END
