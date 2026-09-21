#include "BinaryPlist.hpp"
#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cmath>
#include <limits>
#include <vector>
#include <functional>
#include <fstream>
#include <filesystem>
#include <random>
using namespace ScreamSeq::Project;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void signedIntegerRoundTrip() {
  auto bytes = encodePlist(Value(std::int64_t(-918273645)));
  check(bytes.size() >= 42, "missing binary plist envelope");
  auto decoded = decodePlist(bytes);
  check(decoded.type() == Value::value_t::number_integer, "signed integer type lost");
  check(decoded.get<std::int64_t>() == -918273645, "signed integer changed");
}
void typedTreeRoundTrip() {
  Value tree = {{"unicode", "音楽 😀 café"}, {"nul", std::string("a\0b", 3)},
    {"unknown", {{"__plist_type", "ordinary user key"}, {"nested", Value::array({true, false, nullptr, -7, 1.25})}}},
    {"data", Value::binary({0, 1, 127, 128, 255})}, {"emptyData", Value::binary({})},
    {"emptyArray", Value::array()}, {"emptyDict", Value::object()},
    {"uint64", std::numeric_limits<std::uint64_t>::max()}, {"negativeZero", -0.0}};
  auto decoded = decodePlist(encodePlist(tree));
  check(decoded == tree, "nested typed values changed");
  check(decoded["negativeZero"].is_number_float() && std::signbit(decoded["negativeZero"].get<double>()), "signed zero lost");
  check(decoded["uint64"].is_number_unsigned(), "uint64 type lost");
  check(!decoded["data"].get_binary().has_subtype(), "data got an opaque subtype");
  check(decoded["unknown"]["nested"][0].is_boolean(), "bool became integer");
}
using Bytes = std::vector<std::byte>;
void put(Bytes& bytes, std::uint64_t v, std::size_t width) {
  for (std::size_t i = width; i; --i) bytes.push_back(std::byte((v >> ((i - 1) * 8)) & 255));
}
Bytes fixture(const std::vector<std::vector<std::uint8_t>>& objects, std::size_t root = 0) {
  Bytes out; for (char c : std::string("bplist00")) out.push_back(std::byte(c));
  std::vector<std::size_t> offsets;
  for (const auto& o : objects) { offsets.push_back(out.size()); for (auto b : o) out.push_back(std::byte(b)); }
  auto table = out.size(); for (auto offset : offsets) put(out, offset, 8);
  put(out, 0, 6); put(out, 8, 1); put(out, 1, 1); put(out, objects.size(), 8); put(out, root, 8); put(out, table, 8);
  return out;
}
void opaqueTypesRoundTrip() {
  auto subtype = [](OpaqueType type) { return static_cast<std::uint64_t>(type); };
  const std::vector<std::uint8_t> date{0x80,0,0,0,0,0,0,0}; // exact date -0 seconds
  std::vector<std::uint8_t> wide(16, 0xff); wide.back() = 0xfe;
  const std::vector<std::uint8_t> nan32{0x7f,0xa1,0x23,0x45};
  Value tree = {{"date", Value::binary(date, subtype(OpaqueType::Date))},
    {"uid", Value::binary({0, 0, 9}, subtype(OpaqueType::UID))},
    {"wide", Value::binary(wide, subtype(OpaqueType::WideInteger))},
    {"nan32", Value::binary(nan32, subtype(OpaqueType::Real32NaN))}};
  check(decodePlist(encodePlist(tree)) == tree, "opaque type or raw bytes lost");
  auto dateObject = date; dateObject.insert(dateObject.begin(), 0x33);
  check(decodePlist(fixture({dateObject})) == tree["date"], "date marker not decoded");
  for (std::size_t n = 1; n <= 16; ++n) {
    std::vector<std::uint8_t> o(n, 0xa5); auto expected = Value::binary(o, subtype(OpaqueType::UID));
    o.insert(o.begin(), static_cast<std::uint8_t>(0x80 | (n - 1)));
    check(decodePlist(fixture({o})) == expected, "UID width lost");
    check(decodePlist(encodePlist(expected)) == expected, "UID reencoding changed");
  }
  for (unsigned power = 4; power <= 15; ++power) {
    std::vector<std::uint8_t> o(std::size_t(1) << power, 0xaa);
    auto expected = Value::binary(o, subtype(OpaqueType::WideInteger));
    o.insert(o.begin(), static_cast<std::uint8_t>(0x10 | power));
    check(decodePlist(fixture({o})) == expected, "wide integer width lost");
    check(decodePlist(encodePlist(expected)) == expected, "wide integer reencoding changed");
  }
  for (auto bits : {0x8000000000000000ULL, 0x7ff0000000000000ULL, 0xfff0000000000000ULL, 0x7ff123456789abcdULL}) {
    auto number = std::bit_cast<double>(bits);
    auto decoded = decodePlist(encodePlist(Value(number)));
    check(decoded.is_number_float() && std::bit_cast<std::uint64_t>(decoded.get<double>()) == bits, "double bits changed");
  }
  auto scalar = decodePlist(fixture({{0x22,0x80,0,0,0}}));
  check(scalar.is_number_float() && std::signbit(scalar.get<double>()), "float32 signed zero lost");
}
void canonicalIntegerWidths() {
  for (const auto& object : std::vector<std::vector<std::uint8_t>>{{0x10,0xff}, {0x11,0xff,0xff}, {0x12,0xff,0xff,0xff,0xff}}) {
    auto v = decodePlist(fixture({object}));
    check(v.type() == Value::value_t::number_integer && v.get<std::int64_t>() > 0, "small integers are unsigned on disk");
  }
  std::vector<std::uint8_t> wide(17, 0); wide[0] = 0x14; wide.back() = 2;
  auto v = decodePlist(fixture({wide}));
  check(v.type() == Value::value_t::number_integer && v == 2, "representable 16-byte integer must normalize consistently");
  for (auto n : {INT64_MIN, std::int64_t(-1), std::int64_t(0), INT64_MAX})
    check(decodePlist(encodePlist(Value(n))) == n, "int64 boundary changed");
  for (auto n : {std::uint64_t(INT64_MAX) + 1, UINT64_MAX}) {
    auto decoded = decodePlist(encodePlist(Value(n)));
    check(decoded.is_number_unsigned() && decoded.get<std::uint64_t>() == n, "uint64 boundary changed");
  }
}
std::size_t rejectionCases = 0;
void rejects(const std::function<void()>& action, const char* fragment = "") {
  try { action(); }
  catch (const std::runtime_error& e) {
    if (std::string(e.what()).find(fragment) == std::string::npos)
      throw std::runtime_error(std::string("expected '") + fragment + "', got: " + e.what());
    ++rejectionCases; return;
  }
  throw std::runtime_error("hostile input unexpectedly accepted");
}
void replaceInteger(Bytes& b, std::size_t at, std::uint64_t v, std::size_t n = 8) {
  Bytes encoded; put(encoded, v, n);
  for (std::size_t i = 0; i < n; ++i) b.at(at + i) = encoded[i];
}
void malformedTables() {
  auto good = fixture({{0x09}}); auto trailer = good.size() - 32;
  for (std::size_t n = 0; n < good.size(); ++n) rejects([&] { decodePlist(std::span(good).first(n)); });
  for (auto at : {std::size_t(0), std::size_t(7), trailer}) {
    auto b = good; b[at] = std::byte{1}; rejects([&] { decodePlist(b); });
  }
  for (auto at : {trailer + 6, trailer + 7}) for (auto value : {0, 9, 255}) {
    auto b = good; b[at] = std::byte(value); rejects([&] { decodePlist(b); }, "width");
  }
  for (auto count : {std::uint64_t(0), std::uint64_t(2), UINT64_MAX}) {
    auto b = good; replaceInteger(b, trailer + 8, count); rejects([&] { decodePlist(b); });
  }
  for (auto root : {std::uint64_t(1), UINT64_MAX}) {
    auto b = good; replaceInteger(b, trailer + 16, root); rejects([&] { decodePlist(b); });
  }
  for (auto table : {std::uint64_t(0), std::uint64_t(8), UINT64_MAX}) {
    auto b = good; replaceInteger(b, trailer + 24, table); rejects([&] { decodePlist(b); });
  }
  for (auto offset : {std::uint64_t(0), std::uint64_t(7), std::uint64_t(9), UINT64_MAX}) {
    auto b = good; replaceInteger(b, 9, offset); rejects([&] { decodePlist(b); }, "offset");
  }
  auto duplicate = fixture({{0x09}, {0x08}}); replaceInteger(duplicate, 18, 8);
  rejects([&] { decodePlist(duplicate); }, "overlapping");
  auto overlap = fixture({{0x42, 0x09, 0x08}, {0x09}}); replaceInteger(overlap, 20, 9);
  rejects([&] { decodePlist(overlap); }, "overlapping");
  // Offset aliases must be rejected BEFORE scanning any payload. Otherwise many
  // aliases to a long string can cause object_count * string_length CPU work.
  auto aliasedString = fixture({{0x51, 0xff}, {0x09}});
  replaceInteger(aliasedString, 19, 8);
  rejects([&] { decodePlist(aliasedString); }, "overlapping");
}
void malformedObjects() {
  for (const auto& objects : std::vector<std::vector<std::vector<std::uint8_t>>>{
      {{0xa1, 0}}, {{0xa1, 1}, {0xa1, 0}}, {{0x09}, {0xa1, 1}}, // cycles, including unreachable
      {{0xa1, 1}}, {{0xd1, 1, 1}, {0x09}}, // OOB and non-string key
      {{0xd2, 1, 1, 2, 2}, {0x51, 'a'}, {0x09}}, // repeated key ref
      {{0xd2, 1, 2, 3, 3}, {0x51, 'a'}, {0x61, 0, 'a'}, {0x09}}}) {
    auto b = fixture(objects); rejects([&] { decodePlist(b); });
  }
  for (const auto& object : std::vector<std::vector<std::uint8_t>>{
      {0x0f}, {0x01}, {0x07}, {0xff}, {0xb0}, {0xc0}, {0x70}, {0x21, 0, 0},
      {0x24,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, {0x32},
      {0x4f}, {0x4f,0x20}, {0x4f,0x14}, {0x4f,0x13,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
      {0xaf,0x13,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff}, {0x42, 1},
      {0x51, 0x80}, {0x61,0xd8,0}, {0x61,0xdc,0}, {0x62,0xd8,0,0,65}}) {
    auto b = fixture({object}); rejects([&] { decodePlist(b); });
  }
  for (const auto& bad : std::vector<std::string>{"\x80", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xf0\x90", "\xe2\x28\xa1"}) {
    rejects([&] { encodePlist(Value(bad)); });
    rejects([&] { encodePlist(Value::object({{bad, 1}})); });
  }
}
void invalidOpaqueValues() {
  auto rejectBinary = [](std::vector<std::uint8_t> data, std::uint64_t subtype) {
    auto value = Value::binary(std::move(data), subtype); rejects([&] { encodePlist(value); });
  };
  rejectBinary({}, 123);
  rejectBinary({}, static_cast<std::uint64_t>(OpaqueType::UID));
  rejectBinary(std::vector<std::uint8_t>(17), static_cast<std::uint64_t>(OpaqueType::UID));
  rejectBinary(std::vector<std::uint8_t>(7), static_cast<std::uint64_t>(OpaqueType::Date));
  for (auto n : {8, 16, 17, 65536}) rejectBinary(std::vector<std::uint8_t>(n), static_cast<std::uint64_t>(OpaqueType::WideInteger));
  rejectBinary(std::vector<std::uint8_t>(4), static_cast<std::uint64_t>(OpaqueType::Real32NaN));
  rejectBinary(std::vector<std::uint8_t>(3), static_cast<std::uint64_t>(OpaqueType::Real32NaN));
  rejects([&] { encodePlist(Value(Value::value_t::discarded)); }, "unsupported JSON");
}
void resourceLimits() {
  auto tree = Value::array({1, 2, 3}); auto bytes = encodePlist(tree); Limits l;
  l.maxInputBytes = bytes.size(); check(decodePlist(bytes, l) == tree, "exact input boundary");
  --l.maxInputBytes; rejects([&] { decodePlist(bytes, l); }, "input limit");
  l = Limits{}; l.maxOutputBytes = bytes.size(); check(encodePlist(tree, l) == bytes, "exact output boundary");
  --l.maxOutputBytes; rejects([&] { encodePlist(tree, l); }, "output limit");
  auto data = Value::binary({1,2,3,4}); auto dataBytes = encodePlist(data);
  l = Limits{}; l.maxDataBytes = 4; check(decodePlist(dataBytes, l) == data, "exact data boundary");
  check(encodePlist(data, l) == dataBytes, "exact write data boundary");
  --l.maxDataBytes; rejects([&] { decodePlist(dataBytes, l); }, "data limit"); rejects([&] { encodePlist(data, l); }, "data limit");
  auto text = Value("😀"); auto textBytes = encodePlist(text);
  l = Limits{}; l.maxStringBytes = 4; check(decodePlist(textBytes, l) == text, "UTF-8 budget boundary");
  --l.maxStringBytes; rejects([&] { decodePlist(textBytes, l); }, "string limit"); rejects([&] { encodePlist(text, l); }, "string limit");
  l = Limits{}; l.maxObjects = 4; check(decodePlist(bytes, l) == tree && encodePlist(tree, l) == bytes, "object boundary");
  --l.maxObjects; rejects([&] { decodePlist(bytes, l); }, "object limit"); rejects([&] { encodePlist(tree, l); }, "object limit");
  l = Limits{}; l.maxExpandedValues = 4; check(decodePlist(bytes, l) == tree, "expansion boundary");
  --l.maxExpandedValues; rejects([&] { decodePlist(bytes, l); }, "expanded"); rejects([&] { encodePlist(tree, l); }, "expanded");
  l = Limits{}; l.maxDepth = 2; check(decodePlist(bytes, l) == tree && encodePlist(tree, l) == bytes, "depth boundary");
  l.maxDepth = 1; rejects([&] { decodePlist(bytes, l); }, "depth limit"); rejects([&] { encodePlist(tree, l); }, "depth limit");
  l.maxDepth = 257; rejects([&] { decodePlist(bytes, l); }, "1..256"); rejects([&] { encodePlist(tree, l); }, "1..256");
  l = Limits{}; l.maxDataBytes = 0;
  check(decodePlist(encodePlist(Value(UINT64_MAX), l), l) == Value(UINT64_MAX), "numeric width must not consume data limit");
  check(decodePlist(fixture({{0x22,0x3f,0x80,0,0}}), l) == Value(1.0), "finite float32 must not consume data limit");
  l = Limits{}; l.maxAllocationBytes = 1;
  rejects([&] { decodePlist(bytes, l); }, "allocation"); rejects([&] { encodePlist(tree, l); }, "allocation");
  // Repeated references must charge each resulting copy, not just unique objects.
  std::vector<std::uint8_t> payload(102, 1); payload[0] = 0x4f; payload[1] = 0x10; payload.insert(payload.begin() + 2, 100);
  auto shared = fixture({{0xa4,1,1,1,1}, payload});
  l = Limits{}; l.maxAllocationBytes = 1400; rejects([&] { decodePlist(shared, l); }, "allocation");
  l.maxAllocationBytes = 3000; auto copies = decodePlist(shared, l);
  check(copies.size() == 4 && copies[0].get_binary().size() == 100, "bounded shared data decode");
  // Tiny on-disk DAG expands exponentially if cached costs are ignored.
  std::vector<std::vector<std::uint8_t>> bomb;
  for (std::uint8_t i = 0; i < 30; ++i) bomb.push_back({0xa2, static_cast<std::uint8_t>(i+1), static_cast<std::uint8_t>(i+1)});
  bomb.push_back({0}); auto bombBytes = fixture(bomb);
  rejects([&] { decodePlist(bombBytes); }, "expanded");
  std::vector<std::vector<std::uint8_t>> chain;
  for (unsigned i = 0; i < 255; ++i) chain.push_back({0xa1, static_cast<std::uint8_t>(i+1)});
  chain.push_back({0}); auto deepBytes = fixture(chain);
  rejects([&] { decodePlist(deepBytes); }, "depth limit");
  l = Limits{}; l.maxDepth = 256; auto deep = decodePlist(deepBytes, l); auto encoded = encodePlist(deep, l);
  check(decodePlist(encoded, l) == deep, "maximum safe depth roundtrip");
  rejects([&] { encodePlist(deep); }, "depth limit");
}
bool typedEqual(const Value& a, const Value& b) {
  if (a.type() != b.type()) return false;
  if (a.is_number_float()) return std::bit_cast<std::uint64_t>(a.get<double>()) == std::bit_cast<std::uint64_t>(b.get<double>());
  if (a.is_array()) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (!typedEqual(a[i], b[i])) return false;
    return true;
  }
  if (a.is_object()) {
    if (a.size() != b.size()) return false;
    for (auto i = a.begin(); i != a.end(); ++i) if (!b.contains(i.key()) || !typedEqual(i.value(), b.at(i.key()))) return false;
    return true;
  }
  return a == b;
}
void tableWidthBoundaries() {
  for (auto n : {14,15,16,255,256,257,65535,65536}) {
    auto data = Value::binary(std::vector<std::uint8_t>(n, 0x81));
    check(typedEqual(decodePlist(encodePlist(data)), data), "extended data length or offset width");
  }
  auto array = Value::array(); for (unsigned i = 0; i < 300; ++i) array.push_back(i);
  auto value = decodePlist(encodePlist(array));
  check(value == array && typedEqual(decodePlist(encodePlist(value)), value), "multi-byte object references");
  auto shared = fixture({{0xa2,1,1}, {0xd1,2,3}, {0x51,'x'}, {0x09}});
  check(decodePlist(shared) == Value::array({{{"x", true}}, {{"x", true}}}), "shared subtree lost");
  check(decodePlist(fixture({{0x08}, {0x09}}, 1)) == true, "nonzero top object");
}
void deterministicMutationCorpus() {
  std::mt19937 random(0x51ee);
  auto seed = encodePlist(Value{{"data", Value::binary({0,1,2,255})}, {"text", "音楽"}, {"a", Value::array({true, -1, -0.0})}});
  Limits l; l.maxObjects = 2048; l.maxExpandedValues = 4096; l.maxAllocationBytes = 1024 * 1024; l.maxDepth = 32;
  std::size_t accepted = 0, rejected = 0;
  for (unsigned i = 0; i < 6000; ++i) {
    auto bytes = seed;
    if (i % 3 == 0) bytes.resize(random() % (seed.size() + 1));
    else for (unsigned j = 0, n = 1 + random() % 4; j < n; ++j) bytes[random() % bytes.size()] = std::byte(random() & 255);
    Value decoded;
    try { decoded = decodePlist(bytes, l); }
    catch (const std::runtime_error&) { ++rejected; continue; }
    ++accepted;
    check(typedEqual(decoded, decodePlist(encodePlist(decoded, l), l)), "accepted mutation changed typed values");
  }
  check(accepted > 0 && rejected > 0, "mutation corpus exercised no valid/invalid cases");
  std::cout << "mutation_cases=6000 accepted=" << accepted << " rejected=" << rejected << '\n';
}
Bytes readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate); check(bool(in), "cannot open fixture");
  auto length = in.tellg(); check(length >= 0 && length <= std::streamoff(Limits{}.maxInputBytes), "fixture size limit");
  Bytes bytes(static_cast<std::size_t>(length)); in.seekg(0);
  in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); check(bool(in), "fixture read failure"); return bytes;
}
void repack(const char* input, const char* output) {
  auto path = [](const char* text) { return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text))); };
  auto source = path(input), target = path(output);
  check(!std::filesystem::exists(target), "test output already exists");
  auto bytes = readFile(source); auto tree = decodePlist(bytes); auto encoded = encodePlist(tree);
  check(typedEqual(tree, decodePlist(encoded)), "external typed tree mismatch");
  { std::ofstream out(target, std::ios::binary); out.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size())); check(bool(out), "test output write failed"); }
  check(typedEqual(tree, decodePlist(readFile(target))), "external disk reopen mismatch");
  check(readFile(source) == bytes, "source fixture changed");
  std::cout << "PASS external fixture typed roundtrip input_bytes=" << bytes.size() << " output_bytes=" << encoded.size() << '\n';
}
}
int main(int argc, char** argv) {
  try {
    if (argc == 4 && std::string(argv[1]) == "--repack") { repack(argv[2], argv[3]); return 0; }
    check(argc == 1, "usage: BinaryPlistTests [--repack INPUT NEW_OUTPUT]");
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
      {"signedIntegerRoundTrip", signedIntegerRoundTrip}, {"typedTreeRoundTrip", typedTreeRoundTrip},
      {"opaqueTypesRoundTrip", opaqueTypesRoundTrip}, {"canonicalIntegerWidths", canonicalIntegerWidths},
      {"malformedTables", malformedTables}, {"malformedObjects", malformedObjects},
      {"invalidOpaqueValues", invalidOpaqueValues}, {"resourceLimits", resourceLimits},
      {"tableWidthBoundaries", tableWidthBoundaries}, {"deterministicMutationCorpus", deterministicMutationCorpus}};
    for (const auto& [name, action] : tests) { action(); std::cout << "PASS " << name << '\n'; }
    std::cout << "PASS test_groups=" << tests.size() << " explicit_rejections=" << rejectionCases << '\n';
  }
  catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
