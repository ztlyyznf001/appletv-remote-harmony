// SPDX-License-Identifier: MIT
// Companion OPACK/TLV semantics are ported from protocol/Codec.kt.
// Upstream pyatv MIT attribution: protocol/LICENSE-pyatv.txt.
#include "companion/codec.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>

namespace companion {
namespace {
constexpr std::size_t kMaxNodes = 65536;
void require(bool condition, const char* message) {
  if (!condition) throw std::invalid_argument(message);
}

struct Encoder {
  Bytes out;
  std::size_t nodes = 0;
  void room(std::size_t size) {
    require(size <= kMaxCodecBytes - out.size(), "OPACK exceeds size limit");
  }
  void byte(std::uint8_t b) { room(1); out.push_back(b); }
  void le(std::uint64_t n, unsigned size) {
    room(size);
    for (unsigned i = 0; i < size; ++i) out.push_back(static_cast<std::uint8_t>(n >> (i * 8)));
  }
  void blob(const std::uint8_t* data, std::size_t size, std::uint8_t base) {
    require(size <= kMaxCodecBytes, "OPACK blob exceeds size limit");
    if (size <= 32) byte(static_cast<std::uint8_t>(base + size));
    else if (size <= 255) { byte(base + 33); le(size, 1); }
    else if (size <= 65535) { byte(base + 34); le(size, 2); }
    else { byte(base + 35); le(size, base == 0x40 ? 3 : 4); }
    room(size);
    if (size) out.insert(out.end(), data, data + size);
  }
  void emit(const Value& value, unsigned depth) {
    require(depth < kMaxCodecDepth, "OPACK nesting too deep");
    require(++nodes <= kMaxNodes, "Too many OPACK values");
    if (std::holds_alternative<std::nullptr_t>(value.data)) byte(4);
    else if (const auto* p = std::get_if<bool>(&value.data)) byte(*p ? 1 : 2);
    else if (const auto* p = std::get_if<std::uint64_t>(&value.data)) {
      const auto n = *p;
      if (n < 40) byte(static_cast<std::uint8_t>(n + 8));
      else if (n <= 255) { byte(0x30); le(n, 1); }
      else if (n <= 65535) { byte(0x31); le(n, 2); }
      else if (n <= 0xffffffffULL) { byte(0x32); le(n, 4); }
      else { byte(0x33); le(n, 8); }
    } else if (const auto* p = std::get_if<double>(&value.data)) {
      static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
      std::uint64_t bits; std::memcpy(&bits, p, sizeof(bits)); byte(0x36); le(bits, 8);
    } else if (const auto* p = std::get_if<std::string>(&value.data)) {
      blob(reinterpret_cast<const std::uint8_t*>(p->data()), p->size(), 0x40);
    } else if (const auto* p = std::get_if<Bytes>(&value.data)) {
      blob(p->data(), p->size(), 0x70);
    } else if (const auto* p = std::get_if<Value::Array>(&value.data)) {
      require(p->size() <= kMaxNodes, "Too many OPACK values");
      byte(static_cast<std::uint8_t>(0xd0 + std::min<std::size_t>(p->size(), 15)));
      for (const auto& item : *p) emit(item, depth + 1);
      if (p->size() >= 15) byte(3);
    } else {
      const auto& dict = std::get<Value::Dict>(value.data);
      require(dict.size() <= kMaxNodes / 2, "Too many OPACK dictionary pairs");
      byte(static_cast<std::uint8_t>(0xe0 + std::min<std::size_t>(dict.size(), 15)));
      for (const auto& pair : dict) { emit(Value(pair.first), depth + 1); emit(pair.second, depth + 1); }
      if (dict.size() >= 15) byte(3);
    }
  }
};

struct Decoder {
  const Bytes& input;
  std::size_t pos = 0;
  std::size_t nodes = 0;
  std::size_t expanded_bytes = 0;
  std::vector<Value> objects;
  std::set<std::string> cached;

  explicit Decoder(const Bytes& source) : input(source) {}

  void available(std::size_t n) const {
    require(n <= input.size() - pos, "Truncated OPACK");
  }
  std::uint8_t byte() { available(1); return input[pos++]; }
  std::uint64_t le(unsigned n) {
    available(n); std::uint64_t result = 0;
    for (unsigned i = 0; i < n; ++i) result |= static_cast<std::uint64_t>(input[pos++]) << (8 * i);
    return result;
  }
  std::size_t length(unsigned n) {
    const auto size = le(n);
    require(size <= kMaxCodecBytes, "OPACK length exceeds size limit");
    return static_cast<std::size_t>(size);
  }
  void account(std::size_t n) {
    require(n <= kMaxCodecBytes - expanded_bytes, "Expanded OPACK exceeds size limit");
    expanded_bytes += n;
  }
  Value blob(std::size_t n, bool text) {
    available(n); account(n);
    const auto start = input.data() + pos; pos += n;
    if (text) return std::string(reinterpret_cast<const char*>(start), n);
    return Bytes(start, start + n);
  }
  // Only scalars enter the reference table. Canonical keys preserve the source
  // float width, including distinct Float/Double entries, as in the Kotlin port.
  std::string cache_key(const Value& v, std::uint8_t tag) {
    std::string key;
    if (const auto* p = std::get_if<std::uint64_t>(&v.data)) {
      key.push_back('i');
      for (unsigned i = 0; i < 8; ++i) key.push_back(static_cast<char>(*p >> (8 * i)));
    } else if (const auto* p = std::get_if<double>(&v.data)) {
      key.push_back(tag == 0x35 ? 'f' : 'd');
      std::uint64_t bits;
      if (std::isnan(*p)) bits = 0x7ff8000000000000ULL;
      else std::memcpy(&bits, p, sizeof(bits));
      for (unsigned i = 0; i < 8; ++i) key.push_back(static_cast<char>(bits >> (8 * i)));
    } else if (const auto* p = std::get_if<std::string>(&v.data)) {
      key.push_back('s'); key += *p;
    } else if (const auto* p = std::get_if<Bytes>(&v.data)) {
      key.push_back('b'); key.append(p->begin(), p->end());
    }
    return key;
  }
  Value reference(std::size_t index) {
    require(index < objects.size(), "Invalid OPACK reference");
    const auto& v = objects[index];
    if (const auto* p = std::get_if<std::string>(&v.data)) account(p->size());
    if (const auto* p = std::get_if<Bytes>(&v.data)) account(p->size());
    return v;
  }
  Value read(unsigned depth) {
    require(depth < kMaxCodecDepth, "OPACK nesting too deep");
    require(++nodes <= kMaxNodes, "Too many OPACK values");
    const auto tag = byte(); bool cache = true; Value value;
    if (tag == 1 || tag == 2) { value = Value(tag == 1); cache = false; }
    else if (tag == 4) { value = Value(); cache = false; }
    else if (tag == 5) value = blob(16, false);
    else if (tag == 6) value = Value(le(8));
    else if (tag >= 8 && tag <= 47) { value = Value(tag - 8); cache = false; }
    else if (tag >= 0x30 && tag <= 0x33) value = Value(le(1U << (tag - 0x30)));
    else if (tag == 0x35) {
      static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
      const auto bits = static_cast<std::uint32_t>(le(4)); float f; std::memcpy(&f, &bits, sizeof(f));
      value = Value(static_cast<double>(f));
    } else if (tag == 0x36) {
      const auto bits = le(8); double d; std::memcpy(&d, &bits, sizeof(d)); value = Value(d);
    } else if (tag >= 0x40 && tag <= 0x60) value = blob(tag - 0x40, true);
    else if (tag >= 0x61 && tag <= 0x64) value = blob(length(tag - 0x60), true);
    else if (tag >= 0x70 && tag <= 0x90) value = blob(tag - 0x70, false);
    else if (tag >= 0x91 && tag <= 0x94) value = blob(length(1U << (tag - 0x91)), false);
    else if (tag >= 0xa0 && tag <= 0xc0) { value = reference(tag - 0xa0); cache = false; }
    else if (tag >= 0xc1 && tag <= 0xc4) { value = reference(length(tag - 0xc0)); cache = false; }
    else if (tag >= 0xd0 && tag <= 0xdf) {
      cache = false; Value::Array array; const unsigned count = tag & 15;
      if (count < 15) { for (unsigned i = 0; i < count; ++i) array.push_back(read(depth + 1)); }
      else {
        while (true) { available(1); if (input[pos] == 3) { ++pos; break; } array.push_back(read(depth + 1)); }
      }
      value = Value(std::move(array));
    } else if (tag >= 0xe0 && tag <= 0xef) {
      cache = false; Value::Dict dict; const unsigned count = tag & 15;
      auto pair = [&]() {
        Value key = read(depth + 1);
        require(std::holds_alternative<std::string>(key.data), "Invalid OPACK dictionary key");
        auto item = read(depth + 1);
        require(dict.emplace(key.string(), std::move(item)).second, "Duplicate OPACK dictionary key");
      };
      if (count < 15) { for (unsigned i = 0; i < count; ++i) pair(); }
      else { while (true) { available(1); if (input[pos] == 3) { ++pos; break; } pair(); } }
      value = Value(std::move(dict));
    } else throw std::invalid_argument("Unsupported OPACK tag");
    if (cache && cached.insert(cache_key(value, tag)).second) objects.push_back(value);
    return value;
  }
};
}  // namespace

const Value* Value::find(const std::string& key) const noexcept {
  const auto* p = std::get_if<Dict>(&data);
  if (!p) return nullptr;
  auto it = p->find(key); return it == p->end() ? nullptr : &it->second;
}
Bytes opack_encode(const Value& value) { Encoder encoder; encoder.emit(value, 0); return std::move(encoder.out); }
Value opack_decode(const Bytes& input) {
  require(!input.empty() && input.size() <= kMaxCodecBytes, "Invalid OPACK size");
  Decoder decoder{input}; Value result = decoder.read(0);
  require(decoder.pos == input.size(), "Trailing OPACK bytes");
  return result;
}
Bytes tlv_encode(const std::vector<std::pair<std::uint8_t, Bytes>>& fields) {
  Bytes out;
  for (const auto& field : fields) {
    const auto& value = field.second;
    require(value.size() <= kMaxCodecBytes, "TLV value exceeds size limit");
    const auto chunks = std::max<std::size_t>(1, (value.size() + 254) / 255);
    require(value.size() + chunks * 2 <= kMaxCodecBytes - out.size(), "TLV exceeds size limit");
    if (value.empty()) { out.push_back(field.first); out.push_back(0); }
    for (std::size_t offset = 0; offset < value.size();) {
      const auto count = std::min<std::size_t>(255, value.size() - offset);
      out.push_back(field.first); out.push_back(static_cast<std::uint8_t>(count));
      out.insert(out.end(), value.begin() + offset, value.begin() + offset + count); offset += count;
    }
  }
  return out;
}
Tlv tlv_decode(const Bytes& input) {
  require(input.size() <= kMaxCodecBytes, "TLV exceeds size limit");
  Tlv result; std::size_t pos = 0;
  while (pos < input.size()) {
    require(input.size() - pos >= 2, "Truncated TLV header");
    const auto tag = input[pos++]; const auto count = input[pos++];
    require(count <= input.size() - pos, "Truncated TLV value");
    auto& value = result[tag];
    value.insert(value.end(), input.begin() + pos, input.begin() + pos + count); pos += count;
  }
  return result;
}
}  // namespace companion
