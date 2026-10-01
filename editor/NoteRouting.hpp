#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Tracker {
enum class NoteSourceKind : uint8_t { Channel, Instrument };
struct NoteRoute {
  uint64_t id=0;
  NoteSourceKind sourceKind=NoteSourceKind::Channel;
  uint64_t source=0;
  std::string plugin;
  uint8_t midiChannel=0; // 0 preserves the instrument channel; 1..16 overrides it.
  bool enabled=true;
  bool operator==(const NoteRoute &) const = default;
};
struct NoteRouting {
  std::vector<NoteRoute> routes;
  // A disconnected implicit assignment must stay disconnected. The instrument
  // still generates events in the engine; its graph cables own delivery.
  std::vector<uint64_t> suppressedAssignments;
  bool operator==(const NoteRouting &) const = default;
  bool empty() const { return routes.empty() && suppressedAssignments.empty(); }
  size_t bytes() const;
  void validate(std::span<const uint64_t> tracks,std::span<const uint64_t> instruments) const;
};
struct NoteEndpoint {
  void *context=nullptr;
  bool (*send)(void *,uint8_t,uint8_t,uint8_t) noexcept=nullptr;
  bool (*schedule)(void *,uint8_t,uint8_t,uint8_t,uint64_t) noexcept=nullptr;
  bool operator==(const NoteEndpoint &) const = default;
};
struct NoteEndpointInfo { std::string plugin; NoteEndpoint endpoint; };
struct NoteAssignment { uint64_t instrument=0; std::string plugin; };
struct NoteSource {
  // Origin identifies the upstream MIDI generator, not the final destination.
  // A raw voice can move to an NNA channel while retaining track/instrument IDs.
  const void *origin=nullptr;
  uint16_t voice=0;
  uint64_t track=0,instrument=0;
};
struct NoteRouteKey {
  uint64_t id=0;
  bool implicit=false;
  bool operator==(const NoteRouteKey &) const = default;
};
// Stable, producer-owned identity plus independent lock-free audio counters.
// A route can stop owning a held note without emitting a duplicate note-off.
struct NoteRouteActivity {
  uint64_t token=0,copy=0;
  NoteRouteKey key;
  NoteSourceKind kind=NoteSourceKind::Channel;
  uint64_t source=0;
  std::string plugin;
  NoteEndpoint endpoint;
  uint8_t midiChannel=0;
  std::atomic<uint64_t> events{0},noteOns{0},noteOffs{0},failures{0},routingReleases{0},lastFrame{0},adoptedGeneration{0};
  std::atomic<uint32_t> heldNotes{0},heldPedals{0};
  std::atomic<bool> member{false};
  void event(uint8_t status,uint8_t a,uint8_t b,uint64_t frame,bool accepted) noexcept;
  void acquire(bool pedal) noexcept;
  void release(bool pedal,uint64_t frame,bool routing) noexcept;
};
struct NoteRouteActivityReading {
  uint64_t token=0,copy=0,route=0,source=0,events=0,noteOns=0,noteOffs=0,failures=0,routingReleases=0,lastFrame=0,adoptedGeneration=0;
  NoteSourceKind sourceKind=NoteSourceKind::Channel;
  std::string plugin;
  uint32_t heldNotes=0,heldPedals=0;
  uint8_t midiChannel=0;
  bool implicit=false,member=false,current=false;
};
struct NoteActivitySnapshot {
  uint64_t engine=0,requestedGeneration=0,adoptedGeneration=0;
  double sampleRate=0;
  bool available=false,fresh=false;
  std::vector<NoteRouteActivityReading> routes;
};
class NoteRoutingPlan {
public:
  struct Route {
    NoteRouteKey key;
    NoteSourceKind kind;
    uint64_t source;
    NoteEndpoint endpoint;
    uint8_t midiChannel;
    std::shared_ptr<NoteRouteActivity> activity;
    bool matches(const NoteSource &) const noexcept;
  };
  std::vector<Route> routes;
  NoteRoutingPlan(const NoteRouting &,std::span<const NoteAssignment>,std::span<const NoteEndpointInfo>,std::span<const std::shared_ptr<NoteRouteActivity>> previous={});
};
// Audio-owned bounded state. Plans/endpoints are prepared and retained by the
// host; adopt/send/release never allocate, free, lock or replay past note-ons.
class NoteRouteLedger {
public:
  static constexpr size_t maximumNotes=4096,maximumDeliveries=16384,maximumOwners=5;
  struct Statistics { uint32_t heldNotes=0,deliveries=0; uint64_t noteOns=0,noteOffs=0,routeReleases=0,overflows=0; };
  NoteRouteLedger();
  static size_t storageBytes() noexcept;
  ~NoteRouteLedger();
  NoteRouteLedger(const NoteRouteLedger &)=delete;
  NoteRouteLedger &operator=(const NoteRouteLedger &)=delete;
  bool adopt(const NoteRoutingPlan &,uint64_t frame=0,uint64_t generation=1) noexcept;
  bool send(const NoteSource &,uint8_t status,uint8_t a,uint8_t b,uint64_t frame=0) noexcept;
  bool scheduleControl(const NoteSource &,uint8_t status,uint8_t a,uint8_t b,uint64_t frame) noexcept;
  bool release(const void *origin=nullptr,uint64_t frame=0) noexcept;
  bool releaseInstrument(uint64_t instrument,uint64_t frame) noexcept;
  void moveVoice(const void *origin,uint16_t from,uint16_t to) noexcept;
  Statistics statistics() const noexcept { return stats_; } // audio owner only
private:
  static constexpr uint16_t none=UINT16_MAX;
  struct Note {
    NoteSource source;
    uint64_t serial=0;
    uint16_t first=none;
    uint8_t channel=0,pitch=0;
    bool used=false,pedal=false;
  };
  struct Delivery {
    NoteEndpoint endpoint;
    std::array<NoteRouteKey,maximumOwners> owners{};
    std::array<NoteRouteActivity *,maximumOwners> activity{};
    uint16_t next=none;
    uint8_t channel=0,count=0;
  };
  struct ControlDelivery {const NoteEndpoint *endpoint;NoteRouteActivity *activity[maximumOwners];uint8_t channel,count;};
  // Single audio owner; endpoint callbacks cannot re-enter the ledger.
  std::unique_ptr<std::array<Delivery,512+255>> selected_;
  std::unique_ptr<std::array<ControlDelivery,512+255>> controls_;
  std::unique_ptr<std::array<Note,maximumNotes>> notes_;
  std::unique_ptr<std::array<Delivery,maximumDeliveries>> deliveries_;
  std::array<uint16_t,maximumNotes> freeNotes_{};
  std::array<uint16_t,maximumDeliveries> freeDeliveries_{};
  size_t availableNotes_=maximumNotes,availableDeliveries_=maximumDeliveries;
  const NoteRoutingPlan *plan_=nullptr;
  std::array<NoteRouteActivity *,512+255> activeActivity_{};
  size_t activeActivityCount_=0;
  uint64_t serial_=0;
  Statistics stats_;
  bool finish(uint16_t note,bool routingChange,uint64_t frame,uint8_t velocity=0) noexcept;
  bool emitOff(const Note &,const Delivery &,uint64_t frame,uint8_t velocity=0) noexcept;
  bool otherPedal(const Note &,const Delivery &) const noexcept;
};
}
