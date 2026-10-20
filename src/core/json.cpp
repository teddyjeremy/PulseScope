#include "pulsescope/json.hpp"

#include <cctype>
#include <cmath>
#include <sstream>

namespace pulsescope {
namespace {

struct Parser {
  std::string_view in;
  std::size_t i = 0;
  JsonLimits limits;
  std::size_t elements = 0;
  std::string error;

  explicit Parser(std::string_view s, JsonLimits lim) : in(s), limits(lim) {}

  bool Fail(const char* msg) {
    error = std::string(msg) + " at offset " + std::to_string(i);
    return false;
  }

  void SkipWs() {
    while (i < in.size() && std::isspace(static_cast<unsigned char>(in[i]))) {
      ++i;
    }
  }

  bool Consume(char c) {
    SkipWs();
    if (i < in.size() && in[i] == c) {
      ++i;
      return true;
    }
    return false;
  }

  bool BumpElements() {
    if (++elements > limits.maxElements) {
      return Fail("json element limit exceeded");
    }
    return true;
  }

  bool ParseValue(Json& out, std::size_t depth) {
    if (depth > limits.maxDepth) {
      return Fail("json nesting limit exceeded");
    }
    SkipWs();
    if (i >= in.size()) {
      return Fail("unexpected end of json");
    }
    const char c = in[i];
    if (c == 'n') return ParseLiteral(out, "null", Json::Null());
    if (c == 't') return ParseLiteral(out, "true", Json::Bool(true));
    if (c == 'f') return ParseLiteral(out, "false", Json::Bool(false));
    if (c == '"') return ParseString(out);
    if (c == '[') return ParseArray(out, depth);
    if (c == '{') return ParseObject(out, depth);
    if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
    return Fail("unexpected json token");
  }

  bool ParseLiteral(Json& out, const char* lit, Json value) {
    const std::size_t n = std::char_traits<char>::length(lit);
    if (in.substr(i, n) != lit) {
      return Fail("invalid literal");
    }
    i += n;
    if (!BumpElements()) return false;
    out = std::move(value);
    return true;
  }

  bool ParseString(Json& out) {
    if (i >= in.size() || in[i] != '"') return Fail("expected string");
    ++i;
    std::string s;
    while (i < in.size()) {
      const unsigned char c = static_cast<unsigned char>(in[i++]);
      if (c == '"') {
        if (!BumpElements()) return false;
        out = Json::String(std::move(s));
        return true;
      }
      if (c == '\\') {
        if (i >= in.size()) return Fail("bad escape");
        const char e = in[i++];
        switch (e) {
          case '"':
          case '\\':
          case '/':
            s.push_back(e);
            break;
          case 'b':
            s.push_back('\b');
            break;
          case 'f':
            s.push_back('\f');
            break;
          case 'n':
            s.push_back('\n');
            break;
          case 'r':
            s.push_back('\r');
            break;
          case 't':
            s.push_back('\t');
            break;
          case 'u':
            if (!ParseUnicode(s)) return false;
            break;
          default:
            return Fail("bad escape");
        }
      } else if (c < 0x20) {
        return Fail("raw control character in string");
      } else {
        s.push_back(static_cast<char>(c));
      }
      if (s.size() > limits.maxBytes) return Fail("string too long");
    }
    return Fail("unterminated string");
  }

  bool ParseHex4(unsigned& cp) {
    cp = 0;
    if (i + 4 > in.size()) return Fail("short unicode escape");
    for (int k = 0; k < 4; ++k) {
      const char h = in[i++];
      cp <<= 4;
      if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
      else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
      else return Fail("bad unicode escape");
    }
    return true;
  }

  void AppendUtf8(std::string& s, unsigned cp) {
    if (cp <= 0x7F) {
      s.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  bool ParseUnicode(std::string& s) {
    unsigned cp = 0;
    if (!ParseHex4(cp)) return false;
    if (cp >= 0xD800 && cp <= 0xDBFF) {
      if (i + 2 > in.size() || in[i] != '\\' || in[i + 1] != 'u') {
        return Fail("lonely surrogate");
      }
      i += 2;
      unsigned lo = 0;
      if (!ParseHex4(lo)) return false;
      if (lo < 0xDC00 || lo > 0xDFFF) return Fail("bad surrogate pair");
      cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
      return Fail("lonely surrogate");
    }
    AppendUtf8(s, cp);
    return true;
  }

  bool ParseNumber(Json& out) {
    const std::size_t start = i;
    if (in[i] == '-') ++i;
    if (i >= in.size() || !std::isdigit(static_cast<unsigned char>(in[i]))) {
      return Fail("bad number");
    }
    if (in[i] == '0') {
      ++i;
    } else {
      while (i < in.size() && std::isdigit(static_cast<unsigned char>(in[i]))) ++i;
    }
    bool isFloat = false;
    if (i < in.size() && in[i] == '.') {
      isFloat = true;
      ++i;
      if (i >= in.size() || !std::isdigit(static_cast<unsigned char>(in[i]))) return Fail("bad number");
      while (i < in.size() && std::isdigit(static_cast<unsigned char>(in[i]))) ++i;
    }
    if (i < in.size() && (in[i] == 'e' || in[i] == 'E')) {
      isFloat = true;
      ++i;
      if (i < in.size() && (in[i] == '+' || in[i] == '-')) ++i;
      if (i >= in.size() || !std::isdigit(static_cast<unsigned char>(in[i]))) return Fail("bad number");
      while (i < in.size() && std::isdigit(static_cast<unsigned char>(in[i]))) ++i;
    }
    const std::string_view slice = in.substr(start, i - start);
    if (!BumpElements()) return false;
    if (!isFloat) {
      std::uint64_t v = 0;
      bool neg = false;
      std::size_t p = 0;
      if (!slice.empty() && slice[0] == '-') {
        neg = true;
        p = 1;
      }
      bool overflow = false;
      for (; p < slice.size(); ++p) {
        const unsigned digit = static_cast<unsigned>(slice[p] - '0');
        if (v > (UINT64_MAX - digit) / 10) {
          overflow = true;
          break;
        }
        v = v * 10 + digit;
      }
      if (!overflow && !neg) {
        out = Json::Uint(v);
        return true;
      }
      if (!overflow && neg && v == 0) {
        out = Json::Number(0);
        return true;
      }
    }
    try {
      std::size_t used = 0;
      const double d = std::stod(std::string(slice), &used);
      if (used != slice.size() || !std::isfinite(d)) return Fail("bad number");
      out = Json::Number(d);
      return true;
    } catch (...) {
      return Fail("bad number");
    }
  }

  bool ParseArray(Json& out, std::size_t depth) {
    ++i;
    if (!BumpElements()) return false;
    Json arr = Json::Array();
    SkipWs();
    if (Consume(']')) {
      out = std::move(arr);
      return true;
    }
    while (true) {
      Json item;
      if (!ParseValue(item, depth + 1)) return false;
      arr.a.push_back(std::move(item));
      SkipWs();
      if (Consume(']')) {
        out = std::move(arr);
        return true;
      }
      if (!Consume(',')) return Fail("expected comma in array");
    }
  }

  bool ParseObject(Json& out, std::size_t depth) {
    ++i;
    if (!BumpElements()) return false;
    Json obj = Json::Object();
    SkipWs();
    if (Consume('}')) {
      out = std::move(obj);
      return true;
    }
    while (true) {
      SkipWs();
      Json key;
      if (!ParseString(key)) return false;
      if (!Consume(':')) return Fail("expected colon");
      Json val;
      if (!ParseValue(val, depth + 1)) return false;
      obj.o.emplace_back(std::move(key.s), std::move(val));
      SkipWs();
      if (Consume('}')) {
        out = std::move(obj);
        return true;
      }
      if (!Consume(',')) return Fail("expected comma in object");
    }
  }
};

void Escape(std::ostream& os, std::string_view s) {
  os << '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        os << "\\\"";
        break;
      case '\\':
        os << "\\\\";
        break;
      case '\b':
        os << "\\b";
        break;
      case '\f':
        os << "\\f";
        break;
      case '\n':
        os << "\\n";
        break;
      case '\r':
        os << "\\r";
        break;
      case '\t':
        os << "\\t";
        break;
      default:
        if (c < 0x20) {
          static const char* hex = "0123456789abcdef";
          os << "\\u00" << hex[c >> 4] << hex[c & 0xF];
        } else {
          os << static_cast<char>(c);
        }
    }
  }
  os << '"';
}

void Write(std::ostream& os, const Json& v, int indent, int level) {
  switch (v.type) {
    case JsonType::Null:
      os << "null";
      break;
    case JsonType::Bool:
      os << (v.b ? "true" : "false");
      break;
    case JsonType::Number:
      if (v.exactUint) {
        os << v.u;
      } else {
        os << v.n;
      }
      break;
    case JsonType::String:
      Escape(os, v.s);
      break;
    case JsonType::Array:
      if (v.a.empty()) {
        os << "[]";
        break;
      }
      os << "[";
      if (indent > 0) os << "\n";
      for (std::size_t i = 0; i < v.a.size(); ++i) {
        if (indent > 0) os << std::string(static_cast<std::size_t>((level + 1) * indent), ' ');
        Write(os, v.a[i], indent, level + 1);
        if (i + 1 != v.a.size()) os << ",";
        if (indent > 0) os << "\n";
      }
      if (indent > 0) os << std::string(static_cast<std::size_t>(level * indent), ' ');
      os << "]";
      break;
    case JsonType::Object:
      if (v.o.empty()) {
        os << "{}";
        break;
      }
      os << "{";
      if (indent > 0) os << "\n";
      for (std::size_t i = 0; i < v.o.size(); ++i) {
        if (indent > 0) os << std::string(static_cast<std::size_t>((level + 1) * indent), ' ');
        Escape(os, v.o[i].first);
        os << (indent > 0 ? ": " : ":");
        Write(os, v.o[i].second, indent, level + 1);
        if (i + 1 != v.o.size()) os << ",";
        if (indent > 0) os << "\n";
      }
      if (indent > 0) os << std::string(static_cast<std::size_t>(level * indent), ' ');
      os << "}";
      break;
  }
}

}  // namespace

Json Json::Null() { return Json{}; }
Json Json::Bool(bool v) {
  Json j;
  j.type = JsonType::Bool;
  j.b = v;
  return j;
}
Json Json::Uint(std::uint64_t v) {
  Json j;
  j.type = JsonType::Number;
  j.exactUint = true;
  j.u = v;
  j.n = static_cast<double>(v);
  return j;
}
Json Json::Number(double v) {
  Json j;
  j.type = JsonType::Number;
  j.n = v;
  return j;
}
Json Json::String(std::string v) {
  Json j;
  j.type = JsonType::String;
  j.s = std::move(v);
  return j;
}
Json Json::Array(std::vector<Json> v) {
  Json j;
  j.type = JsonType::Array;
  j.a = std::move(v);
  return j;
}
Json Json::Object() {
  Json j;
  j.type = JsonType::Object;
  return j;
}

Json& Json::Set(std::string key, Json value) {
  for (auto& kv : o) {
    if (kv.first == key) {
      kv.second = std::move(value);
      return *this;
    }
  }
  o.emplace_back(std::move(key), std::move(value));
  return *this;
}

const Json* Json::Find(std::string_view key) const {
  for (const auto& kv : o) {
    if (kv.first == key) return &kv.second;
  }
  return nullptr;
}

std::optional<std::uint64_t> Json::AsUint() const {
  if (type != JsonType::Number) return std::nullopt;
  if (exactUint) return u;
  if (n < 0 || !std::isfinite(n)) return std::nullopt;
  const double trunc = std::floor(n);
  if (trunc != n) return std::nullopt;
  if (n > static_cast<double>(UINT64_MAX)) return std::nullopt;
  return static_cast<std::uint64_t>(n);
}

std::optional<double> Json::AsNumber() const {
  if (type != JsonType::Number) return std::nullopt;
  if (exactUint) return static_cast<double>(u);
  return n;
}

std::optional<bool> Json::AsBool() const {
  if (type != JsonType::Bool) return std::nullopt;
  return b;
}

const std::string* Json::AsString() const {
  if (type != JsonType::String) return nullptr;
  return &s;
}

const std::vector<Json>* Json::AsArray() const {
  if (type != JsonType::Array) return nullptr;
  return &a;
}

Status ParseJson(std::string_view input, Json& out, const JsonLimits& limits) {
  if (input.size() > limits.maxBytes) {
    return Status{ErrorCode::LimitExceeded, "json exceeds the configured byte limit"};
  }
  if (input.size() >= 3 && static_cast<unsigned char>(input[0]) == 0xEF &&
      static_cast<unsigned char>(input[1]) == 0xBB && static_cast<unsigned char>(input[2]) == 0xBF) {
    input.remove_prefix(3);
  }
  Parser p(input, limits);
  Json value;
  if (!p.ParseValue(value, 0)) {
    return Status{ErrorCode::MalformedInput, p.error.empty() ? "malformed json" : p.error};
  }
  p.SkipWs();
  if (p.i != input.size()) {
    return Status{ErrorCode::MalformedInput, "trailing data after json value"};
  }
  out = std::move(value);
  return Status::Ok();
}

std::string StringifyJson(const Json& value, int indent) {
  std::ostringstream os;
  os.setf(std::ios::fmtflags(0), std::ios::floatfield);
  os.precision(15);
  Write(os, value, indent, 0);
  if (indent > 0) os << "\n";
  return os.str();
}

}  // namespace pulsescope
