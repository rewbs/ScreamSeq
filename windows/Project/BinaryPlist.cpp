#include "BinaryPlist.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>
#include <string>

namespace ScreamSeq::Project {
namespace {
static_assert(sizeof(std::size_t) == 8, "BinaryPlist requires a 64-bit host");
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
              "BinaryPlist requires IEEE-754 floating point");
using Bytes = std::vector<std::byte>;
[[noreturn]] void fail(const char* reason) { throw std::runtime_error(std::string("Binary plist: ") + reason); }
std::size_t add(std::size_t a, std::size_t b) {
  if (b > std::numeric_limits<std::size_t>::max() - a) fail("length overflow");
  return a + b;
}
std::size_t mul(std::size_t a, std::size_t b) {
  if (b && a > std::numeric_limits<std::size_t>::max() / b) fail("length overflow");
  return a * b;
}
void bounded(std::size_t n, std::size_t cap, const char* reason) { if (n > cap) fail(reason); }
void validLimits(const Limits& l) { if (!l.maxDepth || l.maxDepth > 256) fail("depth limit must be 1..256"); }
struct Budget {
  std::size_t used = 0;
  const Limits& limits;
  void take(std::size_t n) { used = add(used, n); bounded(used, limits.maxAllocationBytes, "allocation budget"); }
};
std::uint8_t byte(std::span<const std::byte> b, std::size_t p) { return std::to_integer<std::uint8_t>(b[p]); }
std::uint64_t read(std::span<const std::byte> b, std::size_t p, std::size_t n) {
  if (n > 8 || p > b.size() || n > b.size() - p) fail("truncated integer");
  std::uint64_t v = 0;
  for (std::size_t i = 0; i < n; ++i) v = (v << 8) | byte(b, p + i);
  return v;
}
void put(Bytes& out, std::uint64_t v, std::size_t n) {
  for (std::size_t i = n; i; --i) out.push_back(std::byte((v >> ((i - 1) * 8)) & 255));
}
std::size_t width(std::uint64_t v) { return v <= 255 ? 1 : v <= 65535 ? 2 : v <= 0xffffffff ? 4 : 8; }
std::uint8_t exponent(std::size_t n) { return static_cast<std::uint8_t>(std::countr_zero(n)); }
std::size_t headerSize(std::size_t n) { return n < 15 ? 1 : 2 + width(n); }
void header(Bytes& out, std::uint8_t tag, std::size_t n) {
  put(out, tag | (n < 15 ? n : 15), 1);
  if (n >= 15) { auto w = width(n); put(out, 0x10 | exponent(w), 1); put(out, n, w); }
}
// UTF-8 validation never relies on locale, wchar_t width, or replacement characters.
std::uint32_t utf8Point(const std::string& s, std::size_t& p) {
  auto c = static_cast<std::uint8_t>(s[p++]);
  if (c < 128) return c;
  unsigned n; std::uint32_t cp, minimum;
  if (c >= 0xc2 && c <= 0xdf) { n = 1; cp = c & 31; minimum = 0x80; }
  else if (c >= 0xe0 && c <= 0xef) { n = 2; cp = c & 15; minimum = 0x800; }
  else if (c >= 0xf0 && c <= 0xf4) { n = 3; cp = c & 7; minimum = 0x10000; }
  else fail("invalid UTF-8");
  if (n > s.size() - p) fail("truncated UTF-8");
  for (unsigned i = 0; i < n; ++i) { auto d = static_cast<std::uint8_t>(s[p++]); if ((d & 0xc0) != 0x80) fail("invalid UTF-8"); cp = (cp << 6) | (d & 63); }
  if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) fail("invalid Unicode scalar");
  return cp;
}
void appendUtf8(std::string& out, std::uint32_t cp) {
  if (cp < 128) out.push_back(static_cast<char>(cp));
  else {
    unsigned extra = cp < 0x800 ? 1 : cp < 0x10000 ? 2 : 3;
    out.push_back(static_cast<char>((extra == 1 ? 0xc0 : extra == 2 ? 0xe0 : 0xf0) | (cp >> (6 * extra))));
    for (unsigned i = extra; i; --i) out.push_back(static_cast<char>(0x80 | ((cp >> (6 * (i - 1))) & 63)));
  }
}
struct Object {
  std::size_t start = 0, body = 0, end = 0, length = 0, utf8Bytes = 0;
  std::size_t expanded = 1, cost = 192, height = 1;
  std::uint8_t tag = 0, state = 0;
};
class Reader {
  std::span<const std::byte> input;
  const Limits& limits;
  Budget budget;
  std::size_t table = 0, refWidth = 0, root = 0;
  std::vector<Object> objects;
  std::size_t reference(std::size_t p) const {
    auto id = read(input, p, refWidth);
    if (id >= objects.size()) fail("object reference out of range");
    return static_cast<std::size_t>(id);
  }
  template<class Emit> void unicode(const Object& o, Emit emit) const {
    for (std::size_t i = 0; i < o.length; ++i) {
      auto cp = static_cast<std::uint32_t>(read(input, o.body + 2 * i, 2));
      if (cp >= 0xd800 && cp <= 0xdbff) {
        if (++i == o.length) fail("unpaired UTF-16 surrogate");
        auto low = static_cast<std::uint32_t>(read(input, o.body + 2 * i, 2));
        if (low < 0xdc00 || low > 0xdfff) fail("unpaired UTF-16 surrogate");
        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
      } else if (cp >= 0xdc00 && cp <= 0xdfff) fail("unpaired UTF-16 surrogate");
      emit(cp);
    }
  }
  std::string string(const Object& o) const {
    std::string out; out.reserve(o.utf8Bytes);
    if (o.tag == 0x50) for (std::size_t i = 0; i < o.length; ++i) out.push_back(static_cast<char>(byte(input, o.body + i)));
    else unicode(o, [&](std::uint32_t cp) { appendUtf8(out, cp); });
    return out;
  }
  void shape(Object& o, std::size_t boundary) {
    auto marker = byte(input, o.start);
    o.tag = marker & 0xf0;
    std::size_t nibble = marker & 15;
    o.body = o.start + 1;
    auto range = [&](std::size_t n) {
      if (o.body > boundary || n > boundary - o.body) fail("overlapping or truncated object");
      o.end = o.body + n;
    };
    if (o.tag == 0) {
      if (marker != 0 && marker != 8 && marker != 9) fail("unsupported simple object");
      o.length = marker; range(0); return;
    }
    if (o.tag == 0x10 || o.tag == 0x20) {
      o.length = std::size_t(1) << nibble; range(o.length);
      if (o.tag == 0x20 && o.length != 4 && o.length != 8) fail("unsupported real width");
      bool opaque = o.tag == 0x10 && o.length > 8 &&
                    (o.length != 16 || read(input, o.body, 8) != 0);
      if (o.tag == 0x20 && o.length == 4) {
        auto bits = static_cast<std::uint32_t>(read(input, o.body, 4));
        opaque = (bits & 0x7f800000) == 0x7f800000 && (bits & 0x7fffff);
      }
      if (opaque) {
        bounded(o.length, limits.maxDataBytes, "opaque data limit"); o.cost = add(o.cost, o.length);
      }
      return;
    }
    if (o.tag == 0x30 || o.tag == 0x80) {
      if (o.tag == 0x30 && marker != 0x33) fail("unsupported date width");
      o.length = o.tag == 0x30 ? 8 : nibble + 1; range(o.length);
      bounded(o.length, limits.maxDataBytes, "opaque data limit"); o.cost = add(o.cost, o.length);
      return;
    }
    if (o.tag != 0x40 && o.tag != 0x50 && o.tag != 0x60 && o.tag != 0xa0 && o.tag != 0xd0) fail("unsupported object marker");
    o.length = nibble;
    if (nibble == 15) {
      if (o.body >= boundary) fail("missing extended length");
      auto m = byte(input, o.body++);
      if ((m & 0xf0) != 0x10 || (m & 15) > 3) fail("invalid extended length");
      auto n = std::size_t(1) << (m & 15);
      range(n); o.length = static_cast<std::size_t>(read(input, o.body, n)); o.body += n;
    }
    auto unit = o.tag == 0x60 ? 2 : o.tag == 0xa0 ? refWidth : o.tag == 0xd0 ? mul(2, refWidth) : 1;
    range(mul(o.length, unit));
    if (o.tag == 0x40) { bounded(o.length, limits.maxDataBytes, "data limit"); o.cost = add(o.cost, o.length); }
    if (o.tag == 0x50 || o.tag == 0x60) {
      bounded(o.length, limits.maxStringBytes, "string limit");
      if (o.tag == 0x50) {
        for (std::size_t i = 0; i < o.length; ++i) if (byte(input, o.body + i) > 127) fail("invalid ASCII");
        o.utf8Bytes = o.length;
      } else unicode(o, [&](std::uint32_t cp) { o.utf8Bytes = add(o.utf8Bytes, cp < 128 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4); });
      bounded(o.utf8Bytes, limits.maxStringBytes, "string limit");
      o.cost = add(o.cost, mul(o.utf8Bytes, 2));
    }
  }
  void inspect(std::size_t id, std::size_t depth) {
    bounded(depth, limits.maxDepth, "depth limit");
    auto& o = objects[id];
    if (o.state == 1) fail("reference cycle");
    if (o.state == 2) { bounded(add(depth, o.height - 1), limits.maxDepth, "depth limit"); return; }
    o.state = 1;
    if (o.tag == 0xa0 || o.tag == 0xd0) {
      auto count = mul(o.length, o.tag == 0xd0 ? 2 : 1);
      bounded(count, limits.maxExpandedValues, "expanded value limit");
      for (std::size_t i = 0; i < count; ++i) {
        auto child = reference(o.body + i * refWidth);
        if (o.tag == 0xd0 && i < o.length && objects[child].tag != 0x50 && objects[child].tag != 0x60) fail("dictionary key is not a string");
        inspect(child, depth + 1);
        o.height = std::max(o.height, add(objects[child].height, 1));
        o.expanded = add(o.expanded, objects[child].expanded);
        o.cost = add(o.cost, objects[child].cost);
        bounded(o.expanded, limits.maxExpandedValues, "expanded value limit");
        bounded(o.cost, limits.maxAllocationBytes, "allocation budget");
      }
    }
    o.state = 2;
  }
  Value materialize(std::size_t id) const {
    const auto& o = objects[id];
    auto opaque = [&](OpaqueType type) {
      std::vector<std::uint8_t> data(o.length);
      for (std::size_t i = 0; i < o.length; ++i) data[i] = byte(input, o.body + i);
      return Value::binary(std::move(data), static_cast<std::uint64_t>(type));
    };
    if (o.tag == 0) { if (o.length == 0) return nullptr; return o.length == 9; }
    if (o.tag == 0x30) return opaque(OpaqueType::Date);
    if (o.tag == 0x80) return opaque(OpaqueType::UID);
    if (o.tag == 0x10) {
      if (o.length > 8) {
        if (o.length == 16 && read(input, o.body, 8) == 0) {
          auto v = read(input, o.body + 8, 8);
          return v <= INT64_MAX ? Value(static_cast<std::int64_t>(v)) : Value(v);
        }
        return opaque(OpaqueType::WideInteger);
      }
      auto v = read(input, o.body, o.length);
      return o.length == 8 ? Value(std::bit_cast<std::int64_t>(v)) : Value(static_cast<std::int64_t>(v));
    }
    if (o.tag == 0x20) {
      if (o.length == 8) return std::bit_cast<double>(read(input, o.body, 8));
      auto bits = static_cast<std::uint32_t>(read(input, o.body, 4));
      if ((bits & 0x7f800000) == 0x7f800000 && (bits & 0x7fffff)) return opaque(OpaqueType::Real32NaN);
      return static_cast<double>(std::bit_cast<float>(bits));
    }
    if (o.tag == 0x50 || o.tag == 0x60) return string(o);
    if (o.tag == 0x40) {
      std::vector<std::uint8_t> data(o.length);
      for (std::size_t i = 0; i < o.length; ++i) data[i] = byte(input, o.body + i);
      return Value::binary(std::move(data));
    }
    if (o.tag == 0xa0) {
      auto out = Value::array(); out.get_ref<Value::array_t&>().reserve(o.length);
      for (std::size_t i = 0; i < o.length; ++i) out.push_back(materialize(reference(o.body + i * refWidth)));
      return out;
    }
    auto out = Value::object();
    for (std::size_t i = 0; i < o.length; ++i) {
      auto key = string(objects[reference(o.body + i * refWidth)]);
      if (out.contains(key)) fail("duplicate dictionary key");
      out.emplace(std::move(key), materialize(reference(o.body + (i + o.length) * refWidth)));
    }
    return out;
  }
public:
  Reader(std::span<const std::byte> b, const Limits& l) : input(b), limits(l), budget{0, l} {}
  Value run() {
    validLimits(limits); bounded(input.size(), limits.maxInputBytes, "input limit");
    if (input.size() < 42) fail("truncated envelope");
    for (std::size_t i = 0; i < 8; ++i) if (byte(input, i) != static_cast<std::uint8_t>("bplist00"[i])) fail("header");
    auto trailer = input.size() - 32;
    if (read(input, trailer, 6)) fail("unsupported trailer");
    auto offWidth = byte(input, trailer + 6); refWidth = byte(input, trailer + 7);
    if (!offWidth || offWidth > 8 || !refWidth || refWidth > 8) fail("invalid table width");
    auto count = static_cast<std::size_t>(read(input, trailer + 8, 8));
    root = static_cast<std::size_t>(read(input, trailer + 16, 8));
    table = static_cast<std::size_t>(read(input, trailer + 24, 8));
    if (!count || root >= count || table < 9 || table > trailer || mul(count, offWidth) != trailer - table) fail("invalid object table");
    bounded(count, limits.maxObjects, "object limit");
    budget.take(add(128, mul(count, sizeof(Object) + sizeof(std::size_t))));
    objects.resize(count); std::vector<std::size_t> order(count);
    for (std::size_t i = 0; i < count; ++i) {
      auto& o = objects[i]; o.start = static_cast<std::size_t>(read(input, table + i * offWidth, offWidth));
      if (o.start < 8 || o.start >= table) fail("object offset out of range");
      order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return objects[a].start < objects[b].start; });
    for (std::size_t i = 1; i < count; ++i)
      if (objects[order[i - 1]].start == objects[order[i]].start) fail("overlapping or duplicate object offsets");
    // Bound each object to the next physical offset BEFORE scanning strings.
    // Aliases/overlaps must not multiply validation work by the object count.
    for (std::size_t i = 0; i < count; ++i)
      shape(objects[order[i]], i + 1 < count ? objects[order[i + 1]].start : table);
    // Validate even unreachable records. No recursion/materialization occurs until
    // table sizes and all per-object byte ranges have been checked.
    inspect(root, 1);
    for (std::size_t i = 0; i < count; ++i) inspect(i, 1);
    bounded(objects[root].expanded, limits.maxExpandedValues, "expanded value limit");
    budget.take(objects[root].cost);
    return materialize(root);
  }
};
struct Node {
  const Value* value = nullptr;
  const std::string* text = nullptr;
  std::vector<std::size_t> refs;
  std::size_t length = 0;
  std::uint8_t tag = 0;
};
class Writer {
  const Limits& limits;
  Budget budget;
  std::deque<Node> nodes;
  std::size_t refWidth = 0;
  std::size_t plan(const Value* value, const std::string* key, std::size_t depth) {
    bounded(depth, limits.maxDepth, "depth limit");
    auto id = nodes.size(); bounded(add(id, 1), limits.maxObjects, "object limit");
    bounded(id + 1, limits.maxExpandedValues, "expanded value limit");
    budget.take(256); nodes.emplace_back(); auto& n = nodes.back(); n.value = value; n.text = key;
    if (key || value->is_string()) {
      if (!key) n.text = &value->get_ref<const std::string&>();
      bounded(n.text->size(), limits.maxStringBytes, "string limit");
      n.tag = 0x50;
      for (std::size_t i = 0; i < n.text->size();) { auto cp = utf8Point(*n.text, i); if (cp >= 128) n.tag = 0x60; n.length = add(n.length, cp >= 0x10000 ? 2 : 1); }
      if (n.tag == 0x50) n.length = n.text->size();
    } else if (value->is_null()) n.tag = 0;
    else if (value->is_boolean()) n.tag = value->get<bool>() ? 9 : 8;
    else if (value->is_number_integer()) { n.tag = 0x10; n.length = value->is_number_unsigned() && value->get<std::uint64_t>() > INT64_MAX ? 16 : 8; }
    else if (value->is_number_float()) { n.tag = 0x20; n.length = 8; }
    else if (value->is_binary()) {
      n.tag = 0x40; n.length = value->get_binary().size(); bounded(n.length, limits.maxDataBytes, "data limit");
      if (value->get_binary().has_subtype()) {
        const auto& data = value->get_binary();
        switch (static_cast<OpaqueType>(data.subtype())) {
        case OpaqueType::Date:
          if (n.length != 8) fail("invalid opaque date"); n.tag = 0x30; break;
        case OpaqueType::UID:
          if (!n.length || n.length > 16) fail("invalid opaque UID"); n.tag = 0x80; break;
        case OpaqueType::WideInteger:
          if (n.length < 16 || n.length > 32768 || !std::has_single_bit(n.length)) fail("invalid opaque integer");
          if (n.length == 16 && std::all_of(data.begin(), data.begin() + 8, [](auto b) { return b == 0; })) fail("representable integer must use uint64");
          n.tag = 0x10; break;
        case OpaqueType::Real32NaN: {
          if (n.length != 4) fail("invalid opaque float");
          std::uint32_t bits = 0; for (auto b : data) bits = (bits << 8) | b;
          if ((bits & 0x7f800000) != 0x7f800000 || !(bits & 0x7fffff)) fail("opaque float must be NaN");
          n.tag = 0x20; break;
        }
        default: fail("unsupported binary subtype");
        }
      }
    } else if (value->is_array() || value->is_object()) {
      n.tag = value->is_array() ? 0xa0 : 0xd0; n.length = value->size();
      auto count = mul(n.length, n.tag == 0xd0 ? 2 : 1);
      bounded(count, limits.maxObjects, "object limit"); budget.take(mul(count, sizeof(std::size_t))); n.refs.reserve(count);
      if (n.tag == 0xd0) for (auto i = value->begin(); i != value->end(); ++i) n.refs.push_back(plan(nullptr, &i.key(), depth + 1));
      for (const auto& child : *value) n.refs.push_back(plan(&child, nullptr, depth + 1));
    } else fail("unsupported JSON value");
    return id;
  }
  std::size_t size(const Node& n) const {
    if (n.tag < 0x10) return 1;
    if (n.tag == 0x10 || n.tag == 0x20 || n.tag == 0x30 || n.tag == 0x80) return 1 + n.length;
    auto payload = n.tag == 0xa0 || n.tag == 0xd0 ? mul(n.refs.size(), refWidth) : mul(n.length, n.tag == 0x60 ? 2 : 1);
    return add(headerSize(n.length), payload);
  }
  void emit(Bytes& out, const Node& n) const {
    if (n.tag < 0x10) { put(out, n.tag, 1); return; }
    if (n.value && n.value->is_binary() && n.value->get_binary().has_subtype()) {
      auto marker = n.tag == 0x30 ? 0x33 : n.tag == 0x80 ? (0x80 | (n.length - 1)) : (n.tag | exponent(n.length));
      put(out, marker, 1);
      for (auto c : n.value->get_binary()) out.push_back(std::byte(c));
      return;
    }
    if (n.tag == 0x10 || n.tag == 0x20) {
      put(out, n.tag | exponent(n.length), 1);
      if (n.tag == 0x20) put(out, std::bit_cast<std::uint64_t>(n.value->get<double>()), 8);
      else {
        if (n.length == 16) put(out, 0, 8);
        put(out, n.value->is_number_unsigned() ? n.value->get<std::uint64_t>() : std::bit_cast<std::uint64_t>(n.value->get<std::int64_t>()), 8);
      }
      return;
    }
    header(out, n.tag, n.length);
    if (n.tag == 0x40) for (auto c : n.value->get_binary()) out.push_back(std::byte(c));
    else if (n.tag == 0x50) for (auto c : *n.text) out.push_back(std::byte(c));
    else if (n.tag == 0x60) {
      for (std::size_t i = 0; i < n.text->size();) {
        auto cp = utf8Point(*n.text, i);
        if (cp < 0x10000) put(out, cp, 2);
        else { cp -= 0x10000; put(out, 0xd800 | (cp >> 10), 2); put(out, 0xdc00 | (cp & 1023), 2); }
      }
    } else for (auto ref : n.refs) put(out, ref, refWidth);
  }
public:
  explicit Writer(const Limits& l) : limits(l), budget{0, l} {}
  Bytes run(const Value& value) {
    validLimits(limits); plan(&value, nullptr, 1); refWidth = width(nodes.size() - 1);
    std::size_t table = 8;
    for (const auto& n : nodes) table = add(table, size(n));
    auto offWidth = width(table - 1);
    auto total = add(add(table, mul(nodes.size(), offWidth)), 32);
    bounded(total, limits.maxOutputBytes, "output limit");
    budget.take(add(total, mul(nodes.size(), sizeof(std::size_t))));
    Bytes out; out.reserve(total); std::vector<std::size_t> offsets; offsets.reserve(nodes.size());
    for (char c : std::string("bplist00")) out.push_back(std::byte(c));
    for (const auto& n : nodes) { offsets.push_back(out.size()); emit(out, n); }
    for (auto offset : offsets) put(out, offset, offWidth);
    put(out, 0, 6); put(out, offWidth, 1); put(out, refWidth, 1);
    put(out, nodes.size(), 8); put(out, 0, 8); put(out, table, 8);
    if (out.size() != total) fail("internal size mismatch");
    return out;
  }
};
} // namespace
std::vector<std::byte> encodePlist(const Value& value) { return encodePlist(value, Limits{}); }
std::vector<std::byte> encodePlist(const Value& value, const Limits& limits) { return Writer(limits).run(value); }
Value decodePlist(std::span<const std::byte> bytes) { return decodePlist(bytes, Limits{}); }
Value decodePlist(std::span<const std::byte> bytes, const Limits& limits) { return Reader(bytes, limits).run(); }
} // namespace ScreamSeq::Project
