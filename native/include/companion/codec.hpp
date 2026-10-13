// SPDX-License-Identifier: MIT
#pragma once

#include "companion/crypto.hpp"
#include <map>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace companion {
struct Value {
  using Array = std::vector<Value>;
  using Dict = std::map<std::string, Value>;
  using Storage = std::variant<std::nullptr_t, bool, std::uint64_t, double,
                               std::string, Bytes, Array, Dict>;
  Storage data;
  Value() : data(nullptr) {}
  Value(std::nullptr_t) : data(nullptr) {}
  Value(bool v) : data(v) {}
  template <class T, std::enable_if_t<std::is_integral_v<T> &&
                                     !std::is_same_v<T, bool>, int> = 0>
  Value(T v) {
    if constexpr (std::is_signed_v<T>) {
      if (v < 0) throw std::invalid_argument("OPACK integer must be unsigned");
    }
    data = static_cast<std::uint64_t>(v);
  }
  Value(double v) : data(v) {}
  Value(const char* v) : data(std::string(v)) {}
  Value(std::string v) : data(std::move(v)) {}
  Value(Bytes v) : data(std::move(v)) {}
  Value(Array v) : data(std::move(v)) {}
  Value(Dict v) : data(std::move(v)) {}
  const Dict& dict() const { return std::get<Dict>(data); }
  const Array& array() const { return std::get<Array>(data); }
  const std::string& string() const { return std::get<std::string>(data); }
  const Bytes& blob() const { return std::get<Bytes>(data); }
  std::uint64_t uint() const { return std::get<std::uint64_t>(data); }
  const Value* find(const std::string& key) const noexcept;
  bool operator==(const Value& other) const { return data == other.data; }
};
using Dict = Value::Dict;
using Array = Value::Array;
constexpr std::size_t kMaxCodecBytes = 1024 * 1024;
constexpr unsigned kMaxCodecDepth = 48;

Bytes opack_encode(const Value& value);
Value opack_decode(const Bytes& bytes);
using Tlv = std::map<std::uint8_t, Bytes>;
Bytes tlv_encode(const std::vector<std::pair<std::uint8_t, Bytes>>& fields);
Tlv tlv_decode(const Bytes& bytes);
}  // namespace companion
