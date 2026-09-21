#include "NativeMetadata.hpp"
#include "soundlib/NativeNoteEffects.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace ScreamSeq::Project {
namespace {
using namespace Tracker;
void need(bool ok, const char *why) { if (!ok) throw std::invalid_argument(why); }
const Json &object(const Json &v) { need(v.is_object(), "Expected metadata dictionary"); return v; }
const Json &array(const Json &v, size_t maximum) { need(v.is_array() && v.size() <= maximum, "Invalid metadata array or capacity"); return v; }
const Json &field(const Json &v, const char *key) { object(v); need(v.contains(key), "Missing required metadata field"); return v.at(key); }
Json optional(const Json &v, const char *key, Json fallback) { object(v); return v.contains(key) ? v.at(key) : std::move(fallback); }
uint64_t integer(const Json &v, uint64_t lo, uint64_t hi) {
  need(v.is_number_integer(), "Expected metadata integer");
  if (v.is_number_unsigned()) { const auto n = v.get<uint64_t>(); need(n >= lo && n <= hi, "Metadata integer out of range"); return n; }
  const auto n = v.get<int64_t>(); need(n >= 0 && uint64_t(n) >= lo && uint64_t(n) <= hi, "Metadata integer out of range"); return uint64_t(n);
}
double number(const Json &v, double lo, double hi) { need(v.is_number(), "Expected metadata number"); const auto n = v.get<double>(); need(std::isfinite(n) && n >= lo && n <= hi, "Metadata number out of range"); return n; }
bool boolean(const Json &v) { need(v.is_boolean(), "Expected metadata boolean"); return v.get<bool>(); }
std::string text(const Json &v, size_t maximum) {
  need(v.is_string(), "Expected metadata string"); const auto s = v.get<std::string>();
  // Foundation limits NSString.length (UTF-16 units), not UTF-8 byte count.
  size_t units = 0;
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i++]); uint32_t cp = c; size_t extra = 0;
    if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; extra = 1; }
    else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; extra = 2; }
    else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; extra = 3; }
    else need(c > 0 && c < 0x80, "Invalid UTF-8 or NUL in metadata string");
    need(i + extra <= s.size(), "Truncated UTF-8 metadata string");
    for (size_t j = 0; j < extra; ++j) { const auto d = static_cast<unsigned char>(s[i++]); need((d & 0xc0) == 0x80, "Invalid UTF-8 continuation"); cp = (cp << 6) | (d & 63); }
    need((extra != 1 || cp >= 0x80) && (extra != 2 || cp >= 0x800) && (extra != 3 || cp >= 0x10000) && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff), "Invalid UTF-8 code point");
    units += cp > 0xffff ? 2 : 1; need(units <= maximum, "Metadata string exceeds its limit");
  }
  return s;
}
std::string nativeID(uint64_t id) { need(id > 0 && id < NativeSong::maximumID, "Invalid native identity"); return "n" + std::to_string(id); }
uint64_t decodeID(const Json &v) {
  const auto s = text(v, 32); need(s.size() > 1 && s[0] == 'n' && s[1] != '0', "Invalid native identity");
  uint64_t id = 0;
  for (size_t i = 1; i < s.size(); ++i) { need(s[i] >= '0' && s[i] <= '9' && id < NativeSong::maximumID / 10, "Invalid native identity"); id = id * 10 + s[i] - '0'; }
  need(nativeID(id) == s, "Noncanonical native identity"); return id;
}
Json entity(const NativeEntity &e) { return {{"id", nativeID(e.id)}, {"name", e.name}, {"annotation", e.annotation}, {"color", e.color}}; }
NativeEntity decodeEntity(const Json &v) { return {decodeID(field(v,"id")), text(field(v,"name"),256), text(field(v,"annotation"),4096), uint32_t(integer(field(v,"color"),0,0xffffff))}; }
#include "NativeMetadataAutomation.inc"
#include "NativeMetadataMixer.inc"
#include "NativeMetadataSignal.inc"
#include "NativeMetadataIntegrity.inc"
}
Tracker::NativeSong decodeNativeMetadata(const Json &v) {
  const auto version = unsigned(integer(field(v,"version"),1,14));
  NativeSong n; n.nextID = integer(field(v,"nextID"),1,NativeSong::maximumID);
  auto map = [&](const char *key, auto &out) { for (const auto &item : array(field(v,key),65536)) { const auto &p = array(item,2); need(p.size() == 2,"Invalid indexed metadata"); need(out.emplace(uint16_t(integer(p[0],0,65535)),decodeEntity(p[1])).second,"Duplicate metadata index"); } };
  map("patterns",n.patterns); map("tracks",n.tracks); map("samples",n.samples); map("instruments",n.instruments);
  for (const auto &s : array(field(v,"sequences"),256)) { NativeSequence seq{decodeEntity(field(s,"info")),{}}; for (const auto &o : array(field(s,"orders"),65536)) seq.orders.push_back(decodeEntity(o)); n.sequences.push_back(std::move(seq)); }
  auto gate = [&](unsigned first, const char *key) { if (version < first) need(!v.contains(key),"Legacy metadata contains a forbidden known field"); return version >= first; };
  if (gate(2,"automation")) for (const auto &l : items(v,"automation",256)) n.automation.push_back(decodeLane(l,version));
  if (gate(3,"mixer")) n.mixer = decodeMixer(field(v,"mixer"),version);
  if (gate(5,"noteTracks")) for (const auto &t : items(v,"noteTracks",127)) { NativeNoteTrack track{decodeID(field(t,"bus")),{}}; for (const auto &c : items(t,"columns",127)) track.columns.push_back(decodeID(c)); n.noteTracks.push_back(std::move(track)); }
  if (gate(5,"columnMutes")) for (const auto &m : items(v,"columnMutes",127)) { array(m,2); need(m.size() == 2,"Invalid column mute override"); need(n.columnMutes.emplace(decodeID(m[0]),boolean(m[1])).second,"Duplicate column mute override"); }
  if (gate(7,"performance")) n.performance = decodePerformance(field(v,"performance"),version);
  if (gate(9,"preciseNotes")) for (const auto &p : items(v,"preciseNotes",maximumPreciseNotes)) {
    need(version >= 12 || (!p.contains("effect") && !p.contains("parameter")),"Legacy metadata cannot contain per-note effects");
    n.preciseNotes.push_back({decodeID(field(p,"pattern")),decodeID(field(p,"track")),u32(p,"position"),uint16_t(u32(p,"instrument",0,255)),uint8_t(u32(p,"note",1,255)),uint8_t(u32(p,"velocity",1,127)),uint8_t(integer(optional(p,"effect",0),0,255)),uint8_t(integer(optional(p,"parameter",0),0,255))});
  }
  if (gate(10,"signalGraph")) n.signal = decodeSignal(field(v,"signalGraph"),version);
  if (gate(14,"envelopeBank")) decodeBank(n,field(v,"envelopeBank"));
  validateMetadataReferences(n);
  return n;
}
Json encodeNativeMetadata(const Tracker::NativeSong &n) {
  auto map = [](const auto &items) { Json a = Json::array(); for (const auto &[i,e] : items) a.push_back(Json::array({i,entity(e)})); return a; };
  Json sequences = Json::array();
  for (const auto &s : n.sequences) { Json orders = Json::array(); for (const auto &e : s.orders) orders.push_back(entity(e)); sequences.push_back({{"info",entity(s.info)},{"orders",orders}}); }
  Json automation = Json::array(), tracks = Json::array(), mutes = Json::array(), notes = Json::array();
  for (const auto &l : n.automation) automation.push_back(lane(l));
  for (const auto &t : n.noteTracks) { Json columns = Json::array(); for (auto id : t.columns) columns.push_back(nativeID(id)); tracks.push_back({{"bus",nativeID(t.bus)},{"columns",columns}}); }
  for (auto [id,muted] : n.columnMutes) mutes.push_back(Json::array({nativeID(id),muted}));
  for (const auto &p : n.preciseNotes) { Json j{{"pattern",nativeID(p.pattern)},{"track",nativeID(p.track)},{"position",p.position},{"instrument",p.instrument},{"note",p.note},{"velocity",p.velocity}}; if (p.effect || p.parameter) { j["effect"] = p.effect; j["parameter"] = p.parameter; } notes.push_back(std::move(j)); }
  Json encoded{{"version",14},{"nextID",n.nextID},{"patterns",map(n.patterns)},{"tracks",map(n.tracks)},{"samples",map(n.samples)},{"instruments",map(n.instruments)},{"sequences",sequences},
    {"automation",automation},{"mixer",mixer(n.mixer)},{"noteTracks",tracks},{"columnMutes",mutes},{"preciseNotes",notes},{"performance",performance(n.performance)},{"signalGraph",signal(n.signal)},{"envelopeBank",bank(n)}};
  // Never silently discard non-default data in conditionally encoded members,
  // or serialize a model the reader cannot accept. Snapshot validation remains
  // the owner's Document::restoreNative responsibility, not a synthetic song.
  need(decodeNativeMetadata(encoded) == n,"Native model cannot roundtrip through metadata 14");
  return encoded;
}
} // namespace ScreamSeq::Project
