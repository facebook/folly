/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/json/json.h>

#include <algorithm>
#include <functional>
#include <iterator>
#include <sstream>
#include <type_traits>

#include <boost/algorithm/string.hpp>
#include <glog/logging.h>

#include <folly/Conv.h>
#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/Unicode.h>
#include <folly/Utility.h>
#include <folly/lang/Bits.h>
#include <folly/portability/Constexpr.h>

namespace folly {

//////////////////////////////////////////////////////////////////////

namespace json {

namespace {

parse_error make_parse_error(
    unsigned int line,
    std::string const& context,
    std::string const& expected) {
  return parse_error(to<std::string>(
      "json parse error on line ",
      line,
      !context.empty() ? to<std::string>(" near `", context, '\'') : "",
      ": ",
      expected));
}

struct Printer {
  // Context class is allows to restore the path to element that we are about to
  // print so that if error happens we can throw meaningful exception.
  class Context {
   public:
    Context(const Context* parent_context, const dynamic& key)
        : parent_context_(parent_context), key_(key), is_key_(false) {}
    Context(const Context* parent_context, const dynamic& key, bool is_key)
        : parent_context_(parent_context), key_(key), is_key_(is_key) {}

    // Return location description of a context as a chain of keys
    // ex., '"outherKey"->"innerKey"'.
    std::string locationDescription() const {
      std::vector<std::string> keys;
      const Context* ptr = parent_context_;
      while (ptr) {
        keys.push_back(ptr->getName());
        ptr = ptr->parent_context_;
      }
      keys.push_back(getName());
      std::ostringstream stream;
      std::reverse_copy(
          keys.begin(),
          keys.end() - 1,
          std::ostream_iterator<std::string>(stream, "->"));

      // Add current key.
      stream << keys.back();
      return stream.str();
    }
    std::string getName() const {
      return Printer::toStringOr(key_, "<unprintable>");
    }
    std::string typeDescription() const { return is_key_ ? "key" : "value"; }

   private:
    const Context* const parent_context_;
    const dynamic& key_;
    bool is_key_;
  };

  explicit Printer(
      std::string& out, unsigned* indentLevel, serialization_opts const* opts)
      : out_(out), indentLevel_(indentLevel), opts_(*opts) {}

  void operator()(dynamic const& v, const Context& context) const {
    (*this)(v, &context);
  }
  void operator()(dynamic const& v, const Context* context) const {
    switch (v.type()) {
      case dynamic::DOUBLE:
        if (!opts_.allow_nan_inf) {
          if (std::isnan(v.asDouble())) {
            throw json::print_error(
                "folly::toJson: JSON object value was a NaN when serializing " +
                contextDescription(context));
          }
          if (std::isinf(v.asDouble())) {
            throw json::print_error(
                "folly::toJson: JSON object value was an INF when serializing " +
                contextDescription(context));
          }
        }
        toAppend(
            v.asDouble(),
            &out_,
            opts_.double_mode,
            opts_.double_num_digits,
            opts_.double_flags);
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
        escapeString(v.stringPiece(), out_, opts_);
        break;
      case dynamic::OBJECT:
        printObject(v, context);
        break;
      case dynamic::ARRAY:
        printArray(v, context);
        break;
      default:
        CHECK(0) << "Bad type " << v.type();
    }
  }

 private:
  void printKV(
      dynamic const& o,
      const std::pair<const dynamic, dynamic>& p,
      const Context* context) const {
    if (opts_.convert_int_keys && p.first.isInt()) {
      auto strKey = p.first.asString();
      if (o.count(strKey)) {
        throw json::print_error(folly::to<std::string>(
            "folly::toJson: Source object has integer and string keys "
            "representing the same value: ",
            p.first.asInt()));
      }
      (*this)(p.first.asString(), Context(context, p.first, true));
    } else if (!opts_.allow_non_string_keys && !p.first.isString()) {
      throw json::print_error(
          "folly::toJson: JSON object key " +
          toStringOr(p.first, "<unprintable key>") + " was not a string " +
          (opts_.convert_int_keys ? "or integer " : "") +
          "when serializing key at " +
          Context(context, p.first, true).locationDescription());
    } else {
      (*this)(p.first, Context(context, p.first, true)); // Key
    }
    mapColon();
    (*this)(p.second, Context(context, p.first, false)); // Value
  }

  template <typename Iterator>
  void printKVPairs(
      dynamic const& o,
      Iterator begin,
      Iterator end,
      const Context* context) const {
    printKV(o, *begin, context);
    for (++begin; begin != end; ++begin) {
      out_ += ',';
      newline();
      printKV(o, *begin, context);
    }
  }

  void printObject(dynamic const& o, const Context* context) const {
    if (o.empty()) {
      out_ += "{}";
      return;
    }

    out_ += '{';
    indent();
    newline();
    if (opts_.sort_keys || opts_.sort_keys_by) {
      using ref = std::reference_wrapper<decltype(o.items())::value_type const>;
      auto sort_keys_by = [&](auto begin, auto end, const auto& comp) {
        std::sort(begin, end, [&](ref a, ref b) {
          // Only compare keys.  No ordering among identical keys.
          return comp(a.get().first, b.get().first);
        });
      };
      std::vector<ref> refs(o.items().begin(), o.items().end());
      if (opts_.sort_keys_by) {
        sort_keys_by(refs.begin(), refs.end(), opts_.sort_keys_by);
      } else {
        sort_keys_by(refs.begin(), refs.end(), std::less<>());
      }
      printKVPairs(o, refs.cbegin(), refs.cend(), context);
    } else {
      printKVPairs(o, o.items().begin(), o.items().end(), context);
    }
    outdent();
    newline();
    out_ += '}';
  }

  static std::string toStringOr(dynamic const& v, const char* placeholder) {
    try {
      std::string result;
      unsigned indentLevel = 0;
      serialization_opts opts;
      opts.allow_nan_inf = true;
      opts.allow_non_string_keys = true;
      Printer printer(result, &indentLevel, &opts);
      printer(v, nullptr);
      return result;
    } catch (...) {
      return placeholder;
    }
  }

  static std::string contextDescription(const Context* context) {
    if (!context) {
      return "<undefined location>";
    }
    return context->typeDescription() + " at " + context->locationDescription();
  }

  void printArray(dynamic const& a, const Context* context) const {
    if (a.empty()) {
      out_ += "[]";
      return;
    }

    out_ += '[';
    indent();
    newline();
    (*this)(a[0], Context(context, dynamic(0)));
    for (auto it = std::next(a.begin()); it != a.end(); ++it) {
      out_ += ',';
      newline();
      (*this)(*it, Context(context, dynamic(std::distance(a.begin(), it))));
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
      auto indent = *indentLevel_ * opts_.pretty_formatting_indent_width;
      out_ += to<std::string>('\n', std::string(indent, ' '));
    }
  }

  void mapColon() const { out_ += indentLevel_ ? ": " : ":"; }

 private:
  std::string& out_;
  unsigned* const indentLevel_;
  serialization_opts const& opts_;
};

//////////////////////////////////////////////////////////////////////

// Wraps our input buffer with some helper functions.
struct Input {
  explicit Input(StringPiece range, json::serialization_opts const* opts)
      : range_(range), opts_(*opts), lineNum_(0) {
    storeCurrent();
  }

  Input(Input const&) = delete;
  Input& operator=(Input const&) = delete;

  char const* begin() const { return range_.begin(); }

  unsigned getLineNum() const { return lineNum_; }

  // Parse ahead for as long as the supplied predicate is satisfied,
  // returning a range of what was skipped.
  template <class Predicate>
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
    return skipWhile([](char c) { return c >= '0' && c <= '9'; });
  }

  StringPiece skipMinusAndDigits() {
    bool firstChar = true;
    return skipWhile([&firstChar](char c) {
      bool result = (c >= '0' && c <= '9') || (firstChar && c == '-');
      firstChar = false;
      return result;
    });
  }

  void skipWhitespace() {
    unsigned index = 0;
    while (true) {
      while (index < range_.size() && range_[index] == ' ') {
        index++;
      }
      if (index < range_.size()) {
        if (range_[index] == '\n') {
          index++;
          ++lineNum_;
          continue;
        }
        if (range_[index] == '\t' || range_[index] == '\r') {
          index++;
          continue;
        }
      }
      break;
    }
    range_.advance(index);
    storeCurrent();
  }

  void expect(char c) {
    if (**this != c) {
      throw json::make_parse_error(
          lineNum_, context(), to<std::string>("expected '", c, '\''));
    }
    ++*this;
  }

  std::size_t size() const { return range_.size(); }

  int operator*() const { return current_; }

  void operator++() {
    range_.pop_front();
    storeCurrent();
  }

  template <class T>
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

  [[noreturn]] void error(char const* what) const {
    throw json::make_parse_error(lineNum_, context(), what);
  }
  template <typename R>
  R error(char const* what) const {
    error(what);
  }

  json::serialization_opts const& getOpts() { return opts_; }

  void incrementRecursionLevel() {
    if (currentRecursionLevel_ > opts_.recursion_limit) {
      error("recursion limit exceeded");
    }
    currentRecursionLevel_++;
  }

  void decrementRecursionLevel() { currentRecursionLevel_--; }

 private:
  void storeCurrent() { current_ = range_.empty() ? EOF : range_.front(); }

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

  ~RecursionGuard() { in_.decrementRecursionLevel(); }

 private:
  Input& in_;
};

dynamic parseValue(Input& in, json::metadata_map* map);
std::string parseString(Input& in);
dynamic parseNumber(Input& in);

void parseObjectKeyValue(
    Input& in,
    dynamic& ret,
    dynamic&& key,
    json::metadata_map* map,
    bool distinct) {
  auto keyLineNumber = in.getLineNum();
  in.skipWhitespace();
  in.expect(':');
  in.skipWhitespace();
  auto valueLineNumber = in.getLineNum();
  auto value = parseValue(in, map);
  auto [it, inserted] = ret.try_emplace(std::move(key), std::move(value));
  if (!inserted) {
    if (distinct) {
      in.error("duplicate key inserted");
    }
    it->second = std::move(value);
  }
  if (map) {
    map->emplace(
        &it->second,
        json::parse_metadata{{{keyLineNumber}}, {{valueLineNumber}}});
  }
}

dynamic parseObject(Input& in, json::metadata_map* map) {
  DCHECK_EQ(*in, '{');
  ++in;

  dynamic ret = dynamic::object;

  in.skipWhitespace();
  if (*in == '}') {
    ++in;
    return ret;
  }

  const auto& opts = in.getOpts();
  const bool distinct = opts.validate_keys || opts.convert_int_keys;
  for (;;) {
    if (opts.allow_trailing_comma && *in == '}') {
      break;
    }
    dynamic key = parseValue(in, map);
    if (opts.convert_int_keys && key.isInt()) {
      key = key.asString();
    } else if (!opts.allow_non_string_keys && !key.isString()) {
      in.error(
          opts.convert_int_keys ? "expected string or integer for object key"
                                : "expected string for object key");
    }
    parseObjectKeyValue(in, ret, std::move(key), map, distinct);

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

dynamic parseArray(Input& in, json::metadata_map* map) {
  DCHECK_EQ(*in, '[');
  ++in;

  dynamic ret = dynamic::array;

  in.skipWhitespace();
  if (*in == ']') {
    ++in;
    return ret;
  }

  std::vector<uint32_t> lineNumbers;
  for (;;) {
    if (in.getOpts().allow_trailing_comma && *in == ']') {
      break;
    }
    ret.push_back(parseValue(in, map));
    if (map) {
      lineNumbers.push_back(in.getLineNum());
    }
    in.skipWhitespace();
    if (*in != ',') {
      break;
    }
    ++in;
    in.skipWhitespace();
  }
  if (map) {
    for (size_t i = 0, e = ret.size(); i < e; i++) {
      map->emplace(&ret[i], json::parse_metadata{{{0}}, {{lineNumbers[i]}}});
    }
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

  if (*in != '.' && !wasE && in.getOpts().parse_numbers_as_strings) {
    return integral;
  }

  constexpr const char* maxIntStr = "9223372036854775807";
  constexpr const char* minIntStr = "-9223372036854775808";
  constexpr auto maxIntLen = constexpr_strlen(maxIntStr);
  constexpr auto minIntLen = constexpr_strlen(minIntStr);
  auto extremaLen = negative ? minIntLen : maxIntLen;
  auto extremaStr = negative ? minIntStr : maxIntStr;
  if (*in != '.' && !wasE) {
    if (FOLLY_LIKELY(
            !in.getOpts().double_fallback || integral.size() < extremaLen) ||
        (integral.size() == extremaLen && integral <= extremaStr)) {
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

void decodeUnicodeEscape(Input& in, std::string& out) {
  auto hexVal = [&](int c) -> uint16_t {
    // clang-format off
    return uint16_t(
        c >= '0' && c <= '9' ? c - '0' :
        c >= 'a' && c <= 'f' ? c - 'a' + 10 :
        c >= 'A' && c <= 'F' ? c - 'A' + 10 :
        in.error<uint16_t>("invalid hex digit"));
    // clang-format on
  };

  auto readHex = [&]() -> uint16_t {
    if (in.size() < 4) {
      in.error("expected 4 hex digits");
    }

    auto ret = uint16_t(hexVal(*in) * 4096);
    ++in;
    ret += hexVal(*in) * 256;
    ++in;
    ret += hexVal(*in) * 16;
    ++in;
    ret += hexVal(*in);
    ++in;
    return ret;
  };

  //  If the value encoded is in the surrogate pair range, we need to make
  //  sure there is another escape that we can use also.
  //
  //  See the explanation in folly/Unicode.h.
  uint16_t prefix = readHex();
  char32_t codePoint = prefix;
  if (utf16_code_unit_is_high_surrogate(prefix)) {
    if (!in.consume("\\u")) {
      in.error(
          "expected another unicode escape for second half of "
          "surrogate pair");
    }
    uint16_t suffix = readHex();
    if (!utf16_code_unit_is_low_surrogate(suffix)) {
      in.error("second character in surrogate pair is invalid");
    }
    codePoint = unicode_code_point_from_utf16_surrogate_pair(prefix, suffix);
  } else if (!utf16_code_unit_is_bmp(prefix)) {
    in.error("invalid unicode code point (in range [0xdc00,0xdfff])");
  }

  appendCodePointToUtf8(codePoint, out);
}

std::string parseString(Input& in) {
  DCHECK_EQ(*in, '\"');
  ++in;

  std::string ret;
  for (;;) {
    auto range = in.skipWhile([](char c) { return c != '\"' && c != '\\'; });
    ret.append(range.begin(), range.end());

    if (*in == '\"') {
      ++in;
      break;
    }
    if (*in == '\\') {
      ++in;
      switch (*in) {
        // clang-format off
        case '\"':    ret.push_back('\"'); ++in; break;
        case '\\':    ret.push_back('\\'); ++in; break;
        case '/':     ret.push_back('/');  ++in; break;
        case 'b':     ret.push_back('\b'); ++in; break;
        case 'f':     ret.push_back('\f'); ++in; break;
        case 'n':     ret.push_back('\n'); ++in; break;
        case 'r':     ret.push_back('\r'); ++in; break;
        case 't':     ret.push_back('\t'); ++in; break;
        case 'u':     ++in; decodeUnicodeEscape(in, ret); break;
        // clang-format on
        default:
          in.error(
              to<std::string>("unknown escape ", *in, " in string").c_str());
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

    ret.push_back(char(*in));
    ++in;
  }

  return ret;
}

dynamic parseValue(Input& in, json::metadata_map* map) {
  RecursionGuard guard(in);

  in.skipWhitespace();
  // clang-format off
  return
      *in == '[' ? parseArray(in, map) :
      *in == '{' ? parseObject(in, map) :
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
      in.error<dynamic>("expected json value");
  // clang-format on
}

} // namespace

//////////////////////////////////////////////////////////////////////

std::array<uint64_t, 2> buildExtraAsciiToEscapeBitmap(StringPiece chars) {
  std::array<uint64_t, 2> escapes{{0, 0}};
  for (auto b : ByteRange(chars)) {
    if (b >= 0x20 && b < 0x80) {
      escapes[b / 64] |= uint64_t(1) << (b % 64);
    }
  }
  return escapes;
}

std::string serialize(dynamic const& dyn, serialization_opts const& opts) {
  std::string ret;
  unsigned indentLevel = 0;
  Printer p(ret, opts.pretty_formatting ? &indentLevel : nullptr, &opts);
  p(dyn, nullptr);
  return ret;
}

// Fast path to determine the longest prefix that can be left
// unescaped in a string of sizeof(T) bytes packed in an integer of
// type T.
template <bool EnableExtraAsciiEscapes, class T>
size_t firstEscapableInWord(T s, const serialization_opts& opts) {
  static_assert(std::is_unsigned<T>::value, "Unsigned integer required");
  static constexpr T kOnes = ~T() / 255; // 0x...0101
  static constexpr T kMsbs = kOnes * 0x80; // 0x...8080

  // Sets the MSB of bytes < b. Precondition: b < 128.
  auto isLess = [](T w, uint8_t b) {
    // A byte is < b iff subtracting b underflows, so we check that
    // the MSB wasn't set before and it's set after the subtraction.
    return (w - kOnes * b) & ~w & kMsbs;
  };

  auto isChar = [&](uint8_t c) {
    // A byte is == c iff it is 0 if xor'd with c.
    return isLess(s ^ (kOnes * c), 1);
  };

  // The following masks have the MSB set for each byte of the word
  // that satisfies the corresponding condition.
  auto isHigh = s & kMsbs; // >= 128
  auto isLow = isLess(s, 0x20); // <= 0x1f
  auto needsEscape = isHigh | isLow | isChar('\\') | isChar('"');

  if /* constexpr */ (EnableExtraAsciiEscapes) {
    // Deal with optional bitmap for unicode escapes. Escapes can optionally be
    // set for ascii characters 32 - 127, so the inner loop may run up to 96
    // times. However, for the case where 0 or a handful of bits are set,
    // looping will be minimal through use of findFirstSet.
    for (size_t i = 0, e = opts.extra_ascii_to_escape_bitmap.size(); i < e;
         ++i) {
      const auto offset = i * 64;
      // Clear first 32 characters if this is the first index, since those are
      // always escaped.
      auto bitmap = opts.extra_ascii_to_escape_bitmap[i] &
          (i == 0 ? uint64_t(-1) << 32 : ~0ULL);
      while (bitmap) {
        auto bit = folly::findFirstSet(bitmap);
        needsEscape |= isChar(static_cast<uint8_t>(offset + bit - 1));
        bitmap &= bitmap - 1;
      }
    }
  }

  if (!needsEscape) {
    return sizeof(T);
  }

  if (folly::kIsLittleEndian) {
    return folly::findFirstSet(needsEscape) / 8 - 1;
  } else {
    return sizeof(T) - folly::findLastSet(needsEscape) / 8;
  }
}

// Escape a string so that it is legal to print it in JSON text.
template <bool EnableExtraAsciiEscapes>
void escapeStringImpl(
    StringPiece input, std::string& out, const serialization_opts& opts) {
  auto hexDigit = [](uint8_t c) -> char {
    return c < 10 ? c + '0' : c - 10 + 'a';
  };

  out.push_back('\"');

  auto* p = reinterpret_cast<const unsigned char*>(input.begin());
  auto* q = reinterpret_cast<const unsigned char*>(input.begin());
  auto* e = reinterpret_cast<const unsigned char*>(input.end());

  while (p < e) {
    // Find the longest prefix that does not need escaping, and copy
    // it literally into the output string.
    auto firstEsc = p;
    while (firstEsc < e) {
      auto avail = to_unsigned(e - firstEsc);
      uint64_t word = 0;
      if (avail >= 8) {
        word = folly::loadUnaligned<uint64_t>(firstEsc);
      } else {
        word = folly::partialLoadUnaligned<uint64_t>(firstEsc, avail);
      }
      auto prefix = firstEscapableInWord<EnableExtraAsciiEscapes>(word, opts);
      DCHECK_LE(prefix, avail);
      firstEsc += prefix;
      if (prefix < 8) {
        break;
      }
    }
    if (firstEsc > p) {
      out.append(reinterpret_cast<const char*>(p), firstEsc - p);
      p = firstEsc;
      // We can't be in the middle of a multibyte sequence, so we can reset q.
      q = p;
      if (p == e) {
        break;
      }
    }

    // Handle the next byte that may need escaping.

    // Since non-ascii encoding inherently does utf8 validation
    // we explicitly validate utf8 only if non-ascii encoding is disabled.
    if ((opts.validate_utf8 || opts.skip_invalid_utf8) &&
        !opts.encode_non_ascii) {
      // To achieve better spatial and temporal coherence
      // we do utf8 validation progressively along with the
      // string-escaping instead of two separate passes.

      // As the encoding progresses, q will stay at or ahead of p.
      CHECK_GE(q, p);

      // As p catches up with q, move q forward.
      if (q == p) {
        // calling utf8_decode has the side effect of
        // checking that utf8 encodings are valid
        char32_t v = utf8ToCodePoint(q, e, opts.skip_invalid_utf8);
        if (opts.skip_invalid_utf8 && v == U'\ufffd') {
          out.append(reinterpret_cast<const char*>(u8"\ufffd"));
          p = q;
          continue;
        }
      }
    }

    auto encodeUnicode = opts.encode_non_ascii && (*p & 0x80);
    if /* constexpr */ (EnableExtraAsciiEscapes) {
      encodeUnicode = encodeUnicode ||
          (*p >= 0x20 && *p < 0x80 &&
           (opts.extra_ascii_to_escape_bitmap[*p / 64] &
            (uint64_t(1) << (*p % 64))));
    }

    if (encodeUnicode) {
      // note that this if condition captures utf8 chars
      // with value > 127, so size > 1 byte (or they are whitelisted for
      // Unicode encoding).
      // NOTE: char32_t / char16_t are both unsigned.
      char32_t cp = utf8ToCodePoint(p, e, opts.skip_invalid_utf8);
      auto writeHex = [&](char16_t v) {
        char buf[] = "\\u\0\0\0\0";
        buf[2] = hexDigit((v >> 12) & 0x0f);
        buf[3] = hexDigit((v >> 8) & 0x0f);
        buf[4] = hexDigit((v >> 4) & 0x0f);
        buf[5] = hexDigit(v & 0x0f);
        out.append(buf, 6);
      };
      // From the ECMA-404 The JSON Data Interchange Syntax 2nd Edition Dec 2017
      if (cp < 0x10000u) {
        // If the code point is in the Basic Multilingual Plane (U+0000 through
        // U+FFFF), then it may be represented as a six-character sequence:
        // a reverse solidus, followed by the lowercase letter u, followed by
        // four hexadecimal digits that encode the code point.
        writeHex(static_cast<char16_t>(cp));
      } else {
        // To escape a code point that is not in the Basic Multilingual Plane,
        // the character may be represented as a twelve-character sequence,
        // encoding the UTF-16 surrogate pair corresponding to the code point.
        writeHex(static_cast<char16_t>(
            0xd800u + (((cp - 0x10000u) >> 10) & 0x3ffu)));
        writeHex(static_cast<char16_t>(0xdc00u + ((cp - 0x10000u) & 0x3ffu)));
      }
    } else if (*p == '\\' || *p == '\"') {
      char buf[] = "\\\0";
      buf[1] = char(*p++);
      out.append(buf, 2);
    } else if (*p <= 0x1f) {
      switch (*p) {
        // clang-format off
        case '\b': out.append("\\b"); p++; break;
        case '\f': out.append("\\f"); p++; break;
        case '\n': out.append("\\n"); p++; break;
        case '\r': out.append("\\r"); p++; break;
        case '\t': out.append("\\t"); p++; break;
        // clang-format on
        default:
          // Note that this if condition captures non readable chars
          // with value < 32, so size = 1 byte (e.g control chars).
          char buf[] = "\\u00\0\0";
          buf[4] = hexDigit(uint8_t((*p & 0xf0) >> 4));
          buf[5] = hexDigit(uint8_t(*p & 0xf));
          out.append(buf, 6);
          p++;
      }
    } else {
      out.push_back(char(*p++));
    }
  }

  out.push_back('\"');
}

void escapeString(
    StringPiece input, std::string& out, const serialization_opts& opts) {
  if (FOLLY_UNLIKELY(
          opts.extra_ascii_to_escape_bitmap[0] ||
          opts.extra_ascii_to_escape_bitmap[1])) {
    escapeStringImpl<true>(input, out, opts);
  } else {
    escapeStringImpl<false>(input, out, opts);
  }
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
          if (FOLLY_UNLIKELY(s.size() == 1)) {
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
        if (s.startsWith('\n')) {
          // preserve the line break to preserve the line count
          result.push_back(s[0]);
        } else if (s.startsWith("*/")) {
          state = State::None;
          ++i;
        }
        break;
      case State::LineComment:
        if (s[0] == '\n') {
          // preserve the line break to preserve the line count
          result.push_back(s[0]);
          state = State::None;
        }
        break;
      default:
        throw std::logic_error("Unknown comment state");
    }
  }
  return result;
}

} // namespace json

//////////////////////////////////////////////////////////////////////

dynamic parseJsonWithMetadata(StringPiece range, json::metadata_map* map) {
  return parseJsonWithMetadata(range, json::serialization_opts(), map);
}

dynamic parseJsonWithMetadata(
    StringPiece range,
    json::serialization_opts const& opts,
    json::metadata_map* map) {
  json::Input in(range, &opts);

  uint32_t n = in.getLineNum();
  auto ret = parseValue(in, map);
  if (map) {
    map->emplace(&ret, json::parse_metadata{{{0}}, {{n}}});
  }

  in.skipWhitespace();
  if (in.size() && *in != '\0') {
    in.error("parsing didn't consume all input");
  }
  return ret;
}

dynamic parseJson(StringPiece range) {
  return parseJson(range, json::serialization_opts());
}

dynamic parseJson(StringPiece range, json::serialization_opts const& opts) {
  json::Input in(range, &opts);

  auto ret = parseValue(in, nullptr);
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
  opts.sort_keys = true;
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

} // namespace folly
