#pragma once

#include "pulsescope/status.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pulsescope {

enum class JsonType { Null, Bool, Number, String, Array, Object };

// Numbers that are integers in range of uint64 are preserved exactly.
// Other numbers are stored as double and are not used for timestamps.
struct Json {
  JsonType type = JsonType::Null;
  bool b = false;
  double n = 0;
  bool exactUint = false;
  std::uint64_t u = 0;
  std::string s;
  std::vector<Json> a;
  std::vector<std::pair<std::string, Json>> o;

  static Json Null();
  static Json Bool(bool v);
  static Json Uint(std::uint64_t v);
  static Json Number(double v);
  static Json String(std::string v);
  static Json Array(std::vector<Json> v = {});
  static Json Object();

  Json& Set(std::string key, Json value);
  const Json* Find(std::string_view key) const;
  std::optional<std::uint64_t> AsUint() const;
  std::optional<double> AsNumber() const;
  std::optional<bool> AsBool() const;
  const std::string* AsString() const;
  const std::vector<Json>* AsArray() const;
};

struct JsonLimits {
  std::size_t maxBytes = 64u * 1024u * 1024u;
  std::size_t maxDepth = 64;
  std::size_t maxElements = 5'000'000;
};

Status ParseJson(std::string_view input, Json& out, const JsonLimits& limits = {});
std::string StringifyJson(const Json& value, int indent = 2);

}  // namespace pulsescope
