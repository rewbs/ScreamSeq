#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <nlohmann/json.hpp>

namespace ScreamSeq::Project {
using Value = nlohmann::json;

// Binary-plist v0 codec only: this does not validate a ScreamSeq container's
// version, snapshot, native metadata, musical references, or plugin recipes.
// There is no Python/Foundation/platform dependency. Intended for 64-bit hosts.
//
// The entire ROOT VALUE is retained, including unknown dictionary fields. Data
// is Value::binary(bytes) with NO subtype. Integrators must check has_subtype()
// before accepting a binary value as module/plugin data. Do not serialize this
// Value through JSON text: that would lose binary and non-finite real types.
//
// Numeric mapping follows Apple/Python plist rules: 1/2/4-byte integers are
// unsigned on disk, 8-byte integers signed. Values fitting int64 become signed
// JSON integers; positive 16-byte integers with zero high 64 bits become int64
// or uint64 as needed. Small caller-supplied unsigned JSON integers normalize to
// signed integers. uint64 > INT64_MAX encodes in Apple's 16-byte integer form.
// 32-bit finite/infinite reals widen exactly to double; signed zero survives.
// 64-bit real bits (including NaN payloads) are retained on IEEE-754 hosts.
//
// Not a byte-identical container roundtrip: object/key order, scalar widths,
// ASCII versus UTF-16 encoding, padding, unreachable records and graph sharing
// are not retained. Shared acyclic references expand into independent values.
// An accepted shared graph can therefore require a larger ENCODE object/output
// budget. Arrays retain order. Dates/UIDs/wide integers retain exact raw bytes
// via OpaqueType, without dictionary magic keys that could collide with users.
// Sets/ordered sets, UTF-8 object markers, fill objects, unknown markers/real
// widths and invalid Unicode fail closed. XML/bplist versions other than 00
// are rejected. Duplicate dictionary keys, cycles, offset aliases/overlaps,
// out-of-range references and truncation are errors, never silent repairs.
//
// All calls are synchronous, own their result, and leave input unchanged.
// Invalid/unsupported input and budget violations throw std::runtime_error;
// allocator exhaustion can still throw std::bad_alloc. Never use on the audio
// callback. A caller must not concurrently mutate the supplied bytes/value.
struct Limits {
  std::size_t maxInputBytes = 600ULL * 1024 * 1024;
  std::size_t maxOutputBytes = 600ULL * 1024 * 1024;
  std::size_t maxDataBytes = 512ULL * 1024 * 1024;
  std::size_t maxStringBytes = 32ULL * 1024 * 1024; // decoded UTF-8, per string/key
  std::size_t maxObjects = 250000; // records including keys (not just containers)
  std::size_t maxDepth = 128; // root = 1; configurable 1..256, hard stack guard
  std::size_t maxExpandedValues = 1000000; // repeated references and keys count
  // Conservative cumulative allocation-work ledger, not an exact RSS/allocator
  // cap: table/sort storage + 192 bytes per expanded value + binary payloads +
  // twice UTF-8 string bytes. Writer charges 256 bytes/record + references,
  // offsets and exact output bytes before reserving them. Charges never refund;
  // decode expansion is preflighted before any JSON tree is materialized.
  // Borrowed input/caller tree, allocator headers and exception storage are
  // outside this ledger. Defaults deliberately reject some otherwise-valid
  // huge/deep files; tune per trusted use case, not automatically after failure.
  std::size_t maxAllocationBytes = 768ULL * 1024 * 1024;
};
// These binary subtypes are reserved by the codec, not dictionary magic keys.
// Payloads are exact big-endian on-disk bytes, WITHOUT the plist marker:
// Date: exactly 8 IEEE-754 bytes (seconds since 2001-01-01), uninterpreted.
// UID: 1..16 bytes, preserving leading zeroes and original width.
// WideInteger: power-of-two lengths 16..32768; 16-byte high half must be nonzero
// (otherwise use an ordinary integer). Two's-complement bytes, never narrowed.
// Real32NaN: exactly 4 bytes with all exponent bits set and nonzero mantissa;
// used to preserve float32 signaling/quiet NaN payloads without conversion.
// Other subtype values or malformed opaque payloads are rejected by the writer.
enum class OpaqueType : std::uint64_t {
  Date = 0x53515001, UID = 0x53515002, WideInteger = 0x53515003,
  Real32NaN = 0x53515004
};
std::vector<std::byte> encodePlist(const Value& value);
std::vector<std::byte> encodePlist(const Value& value, const Limits& limits);
Value decodePlist(std::span<const std::byte> bytes);
Value decodePlist(std::span<const std::byte> bytes, const Limits& limits);
} // namespace ScreamSeq::Project
