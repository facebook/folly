/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/json.h>
#include <cassert>
#include <boost/next_prior.hpp>
#include <boost/algorithm/string.hpp>

#include <folly/Conv.h>
#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/String.h>
#include <folly/Unicode.h>
#include <folly/portability/Constexpr.h>

namespace folly {

//////////////////////////////////////////////////////////////////////

namespace json {
namespace {

char32_t decodeUtf8(
    const unsigned char*& p,
    const unsigned char* const e,
    bool skipOnError) {
  /* The following encodings are valid, except for the 5 and 6 byte
   * combinations:
   * 0xxxxxxx
   * 110xxxxx 10xxxxxx
   * 1110xxxx 10xxxxxx 10xxxxxx
   * 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
   * 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
   * 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
   */

  auto skip = [&] { ++p; return U'\ufffd'; };

  if (p >= e) {
    if (skipOnError) return skip();
    throw std::runtime_error("folly::decodeUtf8 empty/invalid string");
  }

  unsigned char fst = *p;
  if (!(fst & 0x80)) {
    // trivial case
    return *p++;
  }

  static const uint32_t bitMask[] = {
    (1 << 7) - 1,
    (1 << 11) - 1,
    (1 << 16) - 1,
    (1 << 21) - 1
  };

  // upper control bits are masked out later
  uint32_t d = fst;

  if ((fst & 0xC0) != 0xC0) {
    if (skipOnError) return skip();
    throw std::runtime_error(to<std::string>("folly::decodeUtf8 i=0 d=", d));
  }

  fst <<= 1;

  for (unsigned int i = 1; i != 3 && p + i < e; ++i) {
    unsigned char tmp = p[i];

    if ((tmp & 0xC0) != 0x80) {
      if (skipOnError) return skip();
      throw std::runtime_error(
        to<std::string>("folly::decodeUtf8 i=", i, " tmp=", (uint32_t)tmp));
    }

    d = (d << 6) | (tmp & 0x3F);
    fst <<= 1;

    if (!(fst & 0x80)) {
      d &= bitMask[i];

      // overlong, could have been encoded with i bytes
      if ((d & ~bitMask[i - 1]) == 0) {
        if (skipOnError) return skip();
        throw std::runtime_error(
          to<std::string>("folly::decodeUtf8 i=", i, " d=", d));
      }

      // check for surrogates only needed for 3 bytes
      if (i == 2) {
        if ((d >= 0xD800 && d <= 0xDFFF) || d > 0x10FFFF) {
          if (skipOnError) return skip();
          throw std::runtime_error(
            to<std::string>("folly::decodeUtf8 i=", i, " d=", d));
        }
      }

      p += i + 1;
      return d;
    }
  }

  if (skipOnError) return skip();
  throw std::runtime_error("folly::decodeUtf8 encoding length maxed out");
}

struct Printer {
  explicit Printer(
      std::string& out,
      unsigned* indentLevel,
      serialization_opts const* opts)
      : out_(out), indentLevel_(indentLevel), opts_(*opts) {}

  void operator()(dynamic const& v) const {
    switch (v.type()) {
    case dynamic::DOUBLE:
      if (!opts_.allow_nan_inf &&
          (std::isnan(v.asDouble()) || std::isinf(v.asDouble()))) {
        throw std::runtime_error("folly::toJson: JSON object value was a "
          "NaN or INF");
      }
      toAppend(v.asDouble(), &out_, opts_.double_mode, opts_.double_num_digits);
      break;
    case dynamic::INT64: {
      auto intval = v.asInt();
      if (opts_.javascript_safe) {
        // Use folly::to to check that this integer can be represented
        // as a double without loss of precision.
        intval = int64_t(to<double>(intval));
      }
      toAppend(intval, &out_);
      break;
    }
    case dynamic::BOOL:
      out_ += v.asBool() ? "true" : "false";
      break;
    case dynamic::NULLT:
      out_ += "null";
      break;
    case dynamic::STRING:
      escapeString(v.asString(), out_, opts_);
      break;
    case dynamic::OBJECT:
      printObject(v);
      break;
    case dynamic::ARRAY:
      printArray(v);
      break;
    default:
      CHECK(0) << "Bad type " << v.type();
    }
  }

private:
  void printKV(const std::pair<const dynamic, dynamic>& p) const {
    if (!opts_.allow_non_string_keys && !p.first.isString()) {
      throw std::runtime_error("folly::toJson: JSON object key was not a "
        "string");
    }
    (*this)(p.first);
    mapColon();
    (*this)(p.second);
  }

  template <typename Iterator>
  void printKVPairs(Iterator begin, Iterator end) const {
    printKV(*begin);
    for (++begin; begin != end; ++begin) {
      out_ += ',';
      newline();
      printKV(*begin);
    }
  }

  void printObject(dynamic const& o) const {
    if (o.empty()) {
      out_ += "{}";
      return;
    }

    out_ += '{';
    indent();
    newline();
    if (opts_.sort_keys) {
      std::vector<std::pair<dynamic, dynamic>> items(
        o.items().begin(), o.items().end());
      std::sort(items.begin(), items.end());
      printKVPairs(items.begin(), items.end());
    } else {
      printKVPairs(o.items().begin(), o.items().end());
    }
    outdent();
    newline();
    out_ += '}';
  }

  void printArray(dynamic const& a) const {
    if (a.empty()) {
      out_ += "[]";
      return;
    }

    out_ += '[';
    indent();
    newline();
    (*this)(a[0]);
    for (auto& val : range(boost::next(a.begin()), a.end())) {
      out_ += ',';
      newline();
      (*this)(val);
    }
    outdent();
    newline();
    out_ += ']';
  }

private:
  void outdent() const {
    if (indentLevel_) {
      --*indentLevel_;
    }
  }

  void indent() const {
    if (indentLevel_) {
      ++*indentLevel_;
    }
  }

  void newline() const {
    if (indentLevel_) {
      out_ += to<std::string>('\n', std::string(*indentLevel_ * 2, ' '));
    }
  }

  void mapColon() const {
    out_ += indentLevel_ ? " : " : ":";
  }

private:
 std::string& out_;
 unsigned* const indentLevel_;
 serialization_opts const& opts_;
};

  //////////////////////////////////////////////////////////////////////

  struct ParseError : std::runtime_error {
    explicit ParseError(int line)
      : std::runtime_error(to<std::string>("json parse error on line ", line))
    {}

    explicit ParseError(int line, std::string const& context,
        std::string const& expected)
      : std::runtime_error(to<std::string>("json parse error on line ", line,
          !context.empty() ? to<std::string>(" near `", context, '\'')
                          : "",
          ": ", expected))
    {}

    explicit ParseError(std::string const& msg)
      : std::runtime_error("json parse error: " + msg)
    {}
  };

// Wraps our input buffer with some helper functions.
struct Input {
  explicit Input(StringPiece range, json::serialization_opts const* opts)
      : range_(range)
      , opts_(*opts)
      , lineNum_(0)
  {
    storeCurrent();
  }

  Input(Input const&) = delete;
  Input& operator=(Input const&) = delete;

  char const* begin() const { return range_.begin(); }

  // Parse ahead for as long as the supplied predicate is satisfied,
  // returning a range of what was skipped.
  template<class Predicate>
  StringPiece skipWhile(const Predicate& p) {
    std::size_t skipped = 0;
    for (; skipped < range_.size(); ++skipped) {
      if (!p(range_[skipped])) {
        break;
      }
      if (range_[skipped] == '\n') {
        ++lineNum_;
      }
    }
    auto ret = range_.subpiece(0, skipped);
    range_.advance(skipped);
    storeCurrent();
    return ret;
  }

  StringPiece skipDigits() {
    return skipWhile([] (char c) { return c >= '0' && c <= '9'; });
  }

  StringPiece skipMinusAndDigits() {
    bool firstChar = true;
    return skipWhile([&firstChar] (char c) {
        bool result = (c >= '0' && c <= '9') || (firstChar && c == '-');
        firstChar = false;
        return result;
      });
  }

  void skipWhitespace() {
    range_ = folly::skipWhitespace(range_);
    storeCurrent();
  }

  void expect(char c) {
    if (**this != c) {
      throw ParseError(lineNum_, context(),
        to<std::string>("expected '", c, '\''));
    }
    ++*this;
  }

  std::size_t size() const {
    return range_.size();
  }

  int operator*() const {
    return current_;
  }

  void operator++() {
    range_.pop_front();
    storeCurrent();
  }

  template<class T>
  T extract() {
    try {
      return to<T>(&range_);
    } catch (std::exception const& e) {
      error(e.what());
    }
  }

  bool consume(StringPiece str) {
    if (boost::starts_with(range_, str)) {
      range_.advance(str.size());
      storeCurrent();
      return true;
    }
    return false;
  }

  std::string context() const {
    return range_.subpiece(0, 16 /* arbitrary */).toString();
  }

  dynamic error(char const* what) const {
    throw ParseError(lineNum_, context(), what);
  }

  json::serialization_opts const& getOpts() {
    return opts_;
  }

  void incrementRecursionLevel() {
    if (currentRecursionLevel_ > opts_.recursion_limit) {
      error("recursion limit exceeded");
    }
    currentRecursionLevel_++;
  }

  void decrementRecursionLevel() {
    currentRecursionLevel_--;
  }

 private:
  void storeCurrent() {
    current_ = range_.empty() ? EOF : range_.front();
  }

private:
  StringPiece range_;
  json::serialization_opts const& opts_;
  unsigned lineNum_;
  int current_;
  unsigned int currentRecursionLevel_{0};
};

class RecursionGuard {
 public:
  explicit RecursionGuard(Input& in) : in_(in) {
    in_.incrementRecursionLevel();
  }

  ~RecursionGuard() {
    in_.decrementRecursionLevel();
  }

 private:
  Input& in_;
};

dynamic parseValue(Input& in);
std::string parseString(Input& in);
dynamic parseNumber(Input& in);

dynamic parseObject(Input& in) {
  assert(*in == '{');
  ++in;

  dynamic ret = dynamic::object;

  in.skipWhitespace();
  if (*in == '}') {
    ++in;
    return ret;
  }

  for (;;) {
    if (in.getOpts().allow_trailing_comma && *in == '}') {
      break;
    }
    if (*in == '\"') { // string
      auto key = parseString(in);
      in.skipWhitespace();
      in.expect(':');
      in.skipWhitespace();
      ret.insert(std::move(key), parseValue(in));
    } else if (!in.getOpts().allow_non_string_keys) {
      in.error("expected string for object key name");
    } else {
      auto key = parseValue(in);
      in.skipWhitespace();
      in.expect(':');
      in.skipWhitespace();
      ret.insert(std::move(key), parseValue(in));
    }

    in.skipWhitespace();
    if (*in != ',') {
      break;
    }
    ++in;
    in.skipWhitespace();
  }
  in.expect('}');

  return ret;
}

dynamic parseArray(Input& in) {
  assert(*in == '[');
  ++in;

  dynamic ret = dynamic::array;

  in.skipWhitespace();
  if (*in == ']') {
    ++in;
    return ret;
  }

  for (;;) {
    if (in.getOpts().allow_trailing_comma && *in == ']') {
      break;
    }
    ret.push_back(parseValue(in));
    in.skipWhitespace();
    if (*in != ',') {
      break;
    }
    ++in;
    in.skipWhitespace();
  }
  in.expect(']');

  return ret;
}

dynamic parseNumber(Input& in) {
  bool const negative = (*in == '-');
  if (negative && in.consume("-Infinity")) {
    if (in.getOpts().parse_numbers_as_strings) {
      return "-Infinity";
    } else {
      return -std::numeric_limits<double>::infinity();
    }
  }

  auto integral = in.skipMinusAndDigits();
  if (negative && integral.size() < 2) {
    in.error("expected digits after `-'");
  }

  auto const wasE = *in == 'e' || *in == 'E';

  constexpr const char* maxInt = "9223372036854775807";
  constexpr const char* minInt = "-9223372036854775808";
  constexpr auto maxIntLen = constexpr_strlen(maxInt);
  constexpr auto minIntLen = constexpr_strlen(minInt);

  if (*in != '.' && !wasE && in.getOpts().parse_numbers_as_strings) {
    return integral;
  }

  if (*in != '.' && !wasE) {
    if (LIKELY(!in.getOpts().double_fallback || integral.size() < maxIntLen) ||
        (!negative && integral.size() == maxIntLen && integral <= maxInt) ||
        (negative && integral.size() == minIntLen && integral <= minInt)) {
      auto val = to<int64_t>(integral);
      in.skipWhitespace();
      return val;
    } else {
      auto val = to<double>(integral);
      in.skipWhitespace();
      return val;
    }
  }

  auto end = !wasE ? (++in, in.skipDigits().end()) : in.begin();
  if (*in == 'e' || *in == 'E') {
    ++in;
    if (*in == '+' || *in == '-') {
      ++in;
    }
    auto expPart = in.skipDigits();
    end = expPart.end();
  }
  auto fullNum = range(integral.begin(), end);
  if (in.getOpts().parse_numbers_as_strings) {
    return fullNum;
  }
  auto val = to<double>(fullNum);
  return val;
}

std::string decodeUnicodeEscape(Input& in) {
  auto hexVal = [&] (char c) -> unsigned {
    return c >= '0' && c <= '9' ? c - '0' :
           c >= 'a' && c <= 'f' ? c - 'a' + 10 :
           c >= 'A' && c <= 'F' ? c - 'A' + 10 :
           (in.error("invalid hex digit"), 0);
  };

  auto readHex = [&]() -> uint16_t {
    if (in.size() < 4) {
      in.error("expected 4 hex digits");
    }

    uint16_t ret = hexVal(*in) * 4096;
    ++in;
    ret += hexVal(*in) * 256;
    ++in;
    ret += hexVal(*in) * 16;
    ++in;
    ret += hexVal(*in);
    ++in;
    return ret;
  };

  /*
   * If the value encoded is in the surrogate pair range, we need to
   * make sure there is another escape that we can use also.
   */
  uint32_t codePoint = readHex();
  if (codePoint >= 0xd800 && codePoint <= 0xdbff) {
    if (!in.consume("\\u")) {
      in.error("expected another unicode escape for second half of "
        "surrogate pair");
    }
    uint16_t second = readHex();
    if (second >= 0xdc00 && second <= 0xdfff) {
      codePoint = 0x10000 + ((codePoint & 0x3ff) << 10) +
                  (second & 0x3ff);
    } else {
      in.error("second character in surrogate pair is invalid");
    }
  } else if (codePoint >= 0xdc00 && codePoint <= 0xdfff) {
    in.error("invalid unicode code point (in range [0xdc00,0xdfff])");
  }

  return codePointToUtf8(codePoint);
}

std::string parseString(Input& in) {
  assert(*in == '\"');
  ++in;

  std::string ret;
  for (;;) {
    auto range = in.skipWhile(
      [] (char c) { return c != '\"' && c != '\\'; }
    );
    ret.append(range.begin(), range.end());

    if (*in == '\"') {
      ++in;
      break;
    }
    if (*in == '\\') {
      ++in;
      switch (*in) {
      case '\"':    ret.push_back('\"'); ++in; break;
      case '\\':    ret.push_back('\\'); ++in; break;
      case '/':     ret.push_back('/');  ++in; break;
      case 'b':     ret.push_back('\b'); ++in; break;
      case 'f':     ret.push_back('\f'); ++in; break;
      case 'n':     ret.push_back('\n'); ++in; break;
      case 'r':     ret.push_back('\r'); ++in; break;
      case 't':     ret.push_back('\t'); ++in; break;
      case 'u':     ++in; ret += decodeUnicodeEscape(in); break;
      default:
        in.error(to<std::string>("unknown escape ", *in, " in string").c_str());
      }
      continue;
    }
    if (*in == EOF) {
      in.error("unterminated string");
    }
    if (!*in) {
      /*
       * Apparently we're actually supposed to ban all control
       * characters from strings.  This seems unnecessarily
       * restrictive, so we're only banning zero bytes.  (Since the
       * string is presumed to be UTF-8 encoded it's fine to just
       * check this way.)
       */
      in.error("null byte in string");
    }

    ret.push_back(*in);
    ++in;
  }

  return ret;
}

dynamic parseValue(Input& in) {
  RecursionGuard guard(in);

  in.skipWhitespace();
  return *in == '[' ? parseArray(in) :
         *in == '{' ? parseObject(in) :
         *in == '\"' ? parseString(in) :
         (*in == '-' || (*in >= '0' && *in <= '9')) ? parseNumber(in) :
         in.consume("true") ? true :
         in.consume("false") ? false :
         in.consume("null") ? nullptr :
         in.consume("Infinity") ?
          (in.getOpts().parse_numbers_as_strings ? (dynamic)"Infinity" :
            (dynamic)std::numeric_limits<double>::infinity()) :
         in.consume("NaN") ?
           (in.getOpts().parse_numbers_as_strings ? (dynamic)"NaN" :
             (dynamic)std::numeric_limits<double>::quiet_NaN()) :
         in.error("expected json value");
}

}

//////////////////////////////////////////////////////////////////////

std::string serialize(dynamic const& dyn, serialization_opts const& opts) {
  std::string ret;
  unsigned indentLevel = 0;
  Printer p(ret, opts.pretty_formatting ? &indentLevel : nullptr, &opts);
  p(dyn);
  return ret;
}

// Escape a string so that it is legal to print it in JSON text.
void escapeString(
    StringPiece input,
    std::string& out,
    const serialization_opts& opts) {
  auto hexDigit = [] (int c) -> char {
    return c < 10 ? c + '0' : c - 10 + 'a';
  };

  out.push_back('\"');

  auto* p = reinterpret_cast<const unsigned char*>(input.begin());
  auto* q = reinterpret_cast<const unsigned char*>(input.begin());
  auto* e = reinterpret_cast<const unsigned char*>(input.end());

  while (p < e) {
    // Since non-ascii encoding inherently does utf8 validation
    // we explicitly validate utf8 only if non-ascii encoding is disabled.
    if ((opts.validate_utf8 || opts.skip_invalid_utf8)
        && !opts.encode_non_ascii) {
      // to achieve better spatial and temporal coherence
      // we do utf8 validation progressively along with the
      // string-escaping instead of two separate passes

      // as the encoding progresses, q will stay at or ahead of p
      CHECK(q >= p);

      // as p catches up with q, move q forward
      if (q == p) {
        // calling utf8_decode has the side effect of
        // checking that utf8 encodings are valid
        char32_t v = decodeUtf8(q, e, opts.skip_invalid_utf8);
        if (opts.skip_invalid_utf8 && v == U'\ufffd') {
          out.append(u8"\ufffd");
          p = q;
          continue;
        }
      }
    }
    if (opts.encode_non_ascii && (*p & 0x80)) {
      // note that this if condition captures utf8 chars
      // with value > 127, so size > 1 byte
      char32_t v = decodeUtf8(p, e, opts.skip_invalid_utf8);
      out.append("\\u");
      out.push_back(hexDigit(v >> 12));
      out.push_back(hexDigit((v >> 8) & 0x0f));
      out.push_back(hexDigit((v >> 4) & 0x0f));
      out.push_back(hexDigit(v & 0x0f));
    } else if (*p == '\\' || *p == '\"') {
      out.push_back('\\');
      out.push_back(*p++);
    } else if (*p <= 0x1f) {
      switch (*p) {
        case '\b': out.append("\\b"); p++; break;
        case '\f': out.append("\\f"); p++; break;
        case '\n': out.append("\\n"); p++; break;
        case '\r': out.append("\\r"); p++; break;
        case '\t': out.append("\\t"); p++; break;
        default:
          // note that this if condition captures non readable chars
          // with value < 32, so size = 1 byte (e.g control chars).
          out.append("\\u00");
          out.push_back(hexDigit((*p & 0xf0) >> 4));
          out.push_back(hexDigit(*p & 0xf));
          p++;
      }
    } else {
      out.push_back(*p++);
    }
  }

  out.push_back('\"');
}

std::string stripComments(StringPiece jsonC) {
  std::string result;
  enum class State {
    None,
    InString,
    InlineComment,
    LineComment
  } state = State::None;

  for (size_t i = 0; i < jsonC.size(); ++i) {
    auto s = jsonC.subpiece(i);
    switch (state) {
      case State::None:
        if (s.startsWith("/*")) {
          state = State::InlineComment;
          ++i;
          continue;
        } else if (s.startsWith("//")) {
          state = State::LineComment;
          ++i;
          continue;
        } else if (s[0] == '\"') {
          state = State::InString;
        }
        result.push_back(s[0]);
        break;
      case State::InString:
        if (s[0] == '\\') {
          if (UNLIKELY(s.size() == 1)) {
            throw std::logic_error("Invalid JSONC: string is not terminated");
          }
          result.push_back(s[0]);
          result.push_back(s[1]);
          ++i;
          continue;
        } else if (s[0] == '\"') {
          state = State::None;
        }
        result.push_back(s[0]);
        break;
      case State::InlineComment:
        if (s.startsWith("*/")) {
          state = State::None;
          ++i;
        }
        break;
      case State::LineComment:
        if (s[0] == '\n') {
          // skip the line break. It doesn't matter.
          state = State::None;
        }
        break;
      default:
        throw std::logic_error("Unknown comment state");
    }
  }
  return result;
}

}

//////////////////////////////////////////////////////////////////////

dynamic parseJson(StringPiece range) {
  return parseJson(range, json::serialization_opts());
}

dynamic parseJson(
    StringPiece range,
    json::serialization_opts const& opts) {

  json::Input in(range, &opts);

  auto ret = parseValue(in);
  in.skipWhitespace();
  if (in.size() && *in != '\0') {
    in.error("parsing didn't consume all input");
  }
  return ret;
}

std::string toJson(dynamic const& dyn) {
  return json::serialize(dyn, json::serialization_opts());
}

std::string toPrettyJson(dynamic const& dyn) {
  json::serialization_opts opts;
  opts.pretty_formatting = true;
  return json::serialize(dyn, opts);
}

//////////////////////////////////////////////////////////////////////
// dynamic::print_as_pseudo_json() is implemented here for header
// ordering reasons (most of the dynamic implementation is in
// dynamic-inl.h, which we don't want to include json.h).

void dynamic::print_as_pseudo_json(std::ostream& out) const {
  json::serialization_opts opts;
  opts.allow_non_string_keys = true;
  opts.allow_nan_inf = true;
  out << json::serialize(*this, opts);
}

void PrintTo(const dynamic& dyn, std::ostream* os) {
  json::serialization_opts opts;
  opts.allow_nan_inf = true;
  opts.allow_non_string_keys = true;
  opts.pretty_formatting = true;
  opts.sort_keys = true;
  *os << json::serialize(dyn, opts);
}

//////////////////////////////////////////////////////////////////////

}
