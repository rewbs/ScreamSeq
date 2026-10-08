#pragma once
#include "HostClock.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace ScreamSeq {
struct MidiInputEvent {
  std::uint64_t hostTime=0,generation=0;
  std::uint8_t status=0,data1=0,data2=0;
};
struct MidiInputBatch {
  std::array<MidiInputEvent,1024> events{};
  std::size_t count=0;
  bool panic=false;
  std::uint64_t generation=0,lost=0;
};
struct MidiInputBoundary {std::uint64_t watermark=0,generation=0;bool resume=false;};
// Same callback queue in real WinMM and explicitly private injected tests.
// begin is serialized with drain and requires stopped producers. end cancels
// producers atomically; lifecycle code then stops/joins the native callbacks.
class MidiInputBuffer {
  friend struct MidiInputBufferTestAccess;
  struct Raw {std::uint64_t generation=0,epoch=0;std::uint32_t packed=0,milliseconds=0;};
  struct Slot {std::atomic<std::uint64_t> sequence{0};Raw value;};
  static constexpr std::size_t capacity=4096;
  std::array<Slot,capacity> slots_{};
  std::atomic<std::uint64_t> head_{0},tail_{0},generation_{0},anchor_{0},uncertainty_{0},epoch_{1};
  std::atomic<std::uint64_t> lost_{0},late_{0},driverErrors_{0},timestampErrors_{0},overflow_{0};
  std::atomic<bool> running_{false},accepting_{false},quarantine_{false};
  std::uint64_t acceptedEpoch_=1;
  bool pop(Raw &) noexcept;
  void loss(std::uint64_t count=1) noexcept;
  MidiInputBatch drainTo(std::uint64_t now,std::uint64_t watermark) noexcept;
public:
  struct Counters {std::uint64_t generation=0,lost=0,late=0,driverErrors=0,timestampErrors=0,overflow=0,anchorUncertainty100ns=0;};
  MidiInputBuffer() noexcept;
  bool begin(std::uint64_t generation,std::uint64_t anchorHostTime,std::uint64_t uncertainty100ns=0) noexcept;
  // Consumer paused; producer only queues raw milliseconds, so a completed
  // midiInStart bracket can refine its anchor without retiming queued data.
  void anchor(std::uint64_t hostTime,std::uint64_t uncertainty100ns) noexcept;
  void end() noexcept;
  void quarantine() noexcept; // Transition/loss: discard queued input before panic.
  // Called by a driver only. driverLoss covers MIM_ERROR/MIM_MOREDATA. Valid
  // channel voice messages are retained, including ordinary CC for graph input.
  bool push(std::uint64_t generation,std::uint32_t packed,std::uint32_t driverMilliseconds,bool driverLoss=false) noexcept;
  MidiInputBatch drain(std::uint64_t now=hostTime100ns()) noexcept;
  // One consumer. Pause admission, snapshot the finite reserved head, drain at
  // most four batches, then end even when an uncommitted reservation stalls it.
  // A pre-watermark unpublished reservation reports loss; Finish must retain
  // the take. End excludes later transaction input without manufacturing loss.
  MidiInputBoundary beginBoundary() noexcept;
  MidiInputBatch drainBoundary(const MidiInputBoundary &,std::uint64_t now=hostTime100ns()) noexcept;
  void endBoundary(const MidiInputBoundary &) noexcept;
  Counters counters() const noexcept;
};
class MidiInput final {
public:
  struct Source {std::wstring id,name;};
  struct Status : MidiInputBuffer::Counters {bool connected=false;std::wstring id,name;std::string error;};
  MidiInput();
  ~MidiInput();
  MidiInput(const MidiInput &)=delete;
  MidiInput &operator=(const MidiInput &)=delete;
  static std::vector<Source> sources(); // Driver calls: use a control worker.
  // Lifecycle/status/drain have one serialized owner. Stage a new input before
  // replacing the old one; failed connection leaves the old input available.
  void connect(const std::wstring &opaqueInterfaceID);
  void disconnect();
  Status status() const;
  MidiInputBatch drain() noexcept;
  MidiInputBoundary beginBoundary() noexcept;
  MidiInputBatch drainBoundary(const MidiInputBoundary &,std::uint64_t now=hostTime100ns()) noexcept;
  void endBoundary(const MidiInputBoundary &) noexcept;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
