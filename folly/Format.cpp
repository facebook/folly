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

#include <folly/Format.h>

#include <array>
#include <cmath>
#include <cstring>

#include <fmt/format.h>

#include <folly/ConstexprMath.h>
#include <folly/Portability.h>
#include <folly/container/Array.h>

namespace folly {
namespace detail {

//  ctor for items in the align table
struct format_table_align_make_item {
  static constexpr std::size_t size = 256;
  constexpr FormatArg::Align operator()(std::size_t index) const {
    // clang-format off
    return
        index == '<' ? FormatArg::Align::LEFT:
        index == '>' ? FormatArg::Align::RIGHT :
        index == '=' ? FormatArg::Align::PAD_AFTER_SIGN :
        index == '^' ? FormatArg::Align::CENTER :
        FormatArg::Align::INVALID;
    // clang-format on
  }
};

//  ctor for items in the conv tables for representing parts of nonnegative
//  integers into ascii digits of length Size, over a given base Base
template <std::size_t Base, std::size_t Size, bool Upper = false>
struct format_table_conv_make_item {
  static_assert(Base <= 36, "Base is unrepresentable");
  struct make_item {
    std::size_t index{};
    constexpr char alpha(std::size_t ord) const {
      return static_cast<char>(
          ord < 10 ? '0' + ord : (Upper ? 'A' : 'a') + (ord - 10));
    }
    constexpr char operator()(std::size_t offset) const {
      return alpha(index / constexpr_pow(Base, Size - offset - 1) % Base);
    }
  };
  constexpr std::array<char, Size> operator()(std::size_t index) const {
    return make_array_with<Size>(make_item{index});
  }
};

//  ctor for items in the sign table
struct format_table_sign_make_item {
  static constexpr std::size_t size = 256;
  constexpr FormatArg::Sign operator()(std::size_t index) const {
    // clang-format off
    return
        index == '+' ? FormatArg::Sign::PLUS_OR_MINUS :
        index == '-' ? FormatArg::Sign::MINUS :
        index == ' ' ? FormatArg::Sign::SPACE_OR_MINUS :
        FormatArg::Sign::INVALID;
    // clang-format on
  }
};

//  the tables
FOLLY_STORAGE_CONSTEXPR auto formatAlignTable =
    make_array_with<256>(format_table_align_make_item{});
FOLLY_STORAGE_CONSTEXPR auto formatSignTable =
    make_array_with<256>(format_table_sign_make_item{});
FOLLY_STORAGE_CONSTEXPR decltype(formatHexLower) formatHexLower =
    make_array_with<256>(format_table_conv_make_item<16, 2, false>{});
FOLLY_STORAGE_CONSTEXPR decltype(formatHexUpper) formatHexUpper =
    make_array_with<256>(format_table_conv_make_item<16, 2, true>{});
FOLLY_STORAGE_CONSTEXPR decltype(formatOctal) formatOctal =
    make_array_with<512>(format_table_conv_make_item<8, 3>{});
FOLLY_STORAGE_CONSTEXPR decltype(formatBinary) formatBinary =
    make_array_with<256>(format_table_conv_make_item<2, 8>{});

} // namespace detail

using namespace folly::detail;

void FormatValue<double>::formatHelper(
    fbstring& piece, int& prefixLen, FormatArg& arg) const {
  arg.validate(FormatArg::Type::FLOAT);

  // Track whether the user wrote bare '{}' (no explicit specifier or precision).
  // In that case we use fmt's shortest round-trip representation, matching the
  // old double-conversion ToShortest behavior.  Any explicit specifier or
  // precision (e.g. '{:g}', '{:.4}') goes through the precision-based path.
  const bool useShortestRoundTrip =
      (arg.presentation == FormatArg::kDefaultPresentation) &&
      (arg.precision == FormatArg::kDefaultPrecision);

  if (arg.presentation == FormatArg::kDefaultPresentation) {
    arg.presentation = 'g';
  }

  if (!useShortestRoundTrip && arg.precision == FormatArg::kDefaultPrecision) {
    arg.precision = 6;
  }

  // Precision bounds from double-conversion v3.4.0:
  //   kMaxFixedDigitsAfterPoint = 100, kMaxExponentialDigits = 120,
  //   kMaxPrecisionDigits = 120, kMinPrecisionDigits = 1.
  static constexpr int kMaxFixedPrecision = 100;
  static constexpr int kMaxExpPrecision = 120;
  static constexpr int kMaxGenPrecision = 120;
  static constexpr int kMinGenPrecision = 1;

  char plusSign;
  switch (arg.sign) {
    case FormatArg::Sign::PLUS_OR_MINUS:
      plusSign = '+';
      break;
    case FormatArg::Sign::SPACE_OR_MINUS:
      plusSign = ' ';
      break;
    case FormatArg::Sign::DEFAULT:
    case FormatArg::Sign::MINUS:
    case FormatArg::Sign::INVALID:
    default:
      plusSign = '\0';
      break;
  }

  double val = val_;
  char convSpec = arg.presentation;
  bool isPercent = false;

  switch (arg.presentation) {
    case '%':
      val *= 100;
      convSpec = 'f';
      isPercent = true;
      [[fallthrough]];
    case 'f':
    case 'F':
      if (arg.precision > kMaxFixedPrecision) {
        arg.precision = kMaxFixedPrecision;
      }
      break;
    case 'e':
    case 'E':
      if (arg.precision > kMaxExpPrecision) {
        arg.precision = kMaxExpPrecision;
      }
      break;
    case 'n': // should be locale-aware, but isn't
      convSpec = 'g';
      [[fallthrough]];
    case 'g':
    case 'G':
      if (!useShortestRoundTrip) {
        if (arg.precision < kMinGenPrecision) {
          arg.precision = kMinGenPrecision;
        } else if (arg.precision > kMaxGenPrecision) {
          arg.precision = kMaxGenPrecision;
        }
      }
      break;
    default:
      arg.error("invalid specifier '", arg.presentation, "'");
  }

  // buf[0] reserved for optional sign; fmt writes into buf[1..] (at most
  // bufLen-2 bytes). Unlike double-conversion, fmt places no inherent bound on
  // fixed-notation output — e.g. DBL_MAX as 'f' with precision 100 exceeds
  // 400 chars. The enforce check below is the actual safety guard; when output
  // would overflow it throws rather than silently overwriting memory.
  constexpr std::size_t bufLen = 256;
  // SAFETY: the enforce check (fmtLen < bufLen - 2) caps fmt output at
  // bufLen - 3 bytes. At most 3 bytes are written post-enforce:
  //   buf[1 + fmtLen]  trailing '.'  (trailingDot, optional)
  //   buf[1 + len]     '%' suffix    (isPercent, optional)
  //   buf[0]           sign prefix   (plusSign, optional, prepend)
  // Together these exactly fill the buffer in the worst case.
  // If post-enforce additions grow or the enforce bound relaxes, this
  // static_assert will catch the mismatch at compile time.
  static_assert(
      (bufLen - 2 - 1) + 1 /* trailing dot */ + 1 /* '%' */ + 1 /* sign */ ==
          bufLen,
      "enforce bound must tighten if post-enforce byte additions increase");
  std::array<char, bufLen> buf{};

  // Dispatch to a compile-time format string based on convSpec.
  // The inner {} in "{:.{}f}" takes precision from the second argument.
  std::size_t fmtSize = 0;
#define FOLLY_FORMAT_HELPER(spec)                                           \
  fmtSize =                                                                 \
      fmt::format_to_n(                                                     \
          buf.data() + 1, bufLen - 2, "{:.{}" spec "}", val, arg.precision) \
          .size
  if (useShortestRoundTrip) {
    // Bare '{}': use fmt's shortest round-trip representation, equivalent to
    // double-conversion's ToShortest (all significant digits, no truncation).
    fmtSize =
        fmt::format_to_n(buf.data() + 1, bufLen - 2, "{}", val).size;
  } else {
    switch (convSpec) {
      case 'f':
        FOLLY_FORMAT_HELPER("f");
        break;
      case 'F':
        FOLLY_FORMAT_HELPER("F");
        break;
      case 'e':
        FOLLY_FORMAT_HELPER("e");
        break;
      case 'E':
        FOLLY_FORMAT_HELPER("E");
        break;
      case 'g':
        FOLLY_FORMAT_HELPER("g");
        break;
      case 'G':
        FOLLY_FORMAT_HELPER("G");
        break;
      default:
        arg.error("invalid specifier '", arg.presentation, "'");
    }
  }
#undef FOLLY_FORMAT_HELPER

  const int fmtLen = static_cast<int>(fmtSize);
  arg.enforce(
      fmtLen > 0 && fmtLen < static_cast<int>(bufLen) - 2,
      "float conversion failed");

  // If trailingDot is requested, append a bare '.' when the result is a
  // whole-number decimal with no decimal point (e.g. "100000" → "100000.").
  // For exponential notation (e.g. "1e+06") the dot is not added.
  // This mirrors double-conversion's EMIT_TRAILING_DECIMAL_POINT without the
  // trailing zeros that fmt's '#' alternative-form flag would produce.
  int trailingDotAdded = 0;
  if (arg.trailingDot && !std::isinf(val) && !std::isnan(val) &&
      !std::memchr(buf.data() + 1, '.', fmtLen) &&
      !std::memchr(buf.data() + 1, 'e', fmtLen) &&
      !std::memchr(buf.data() + 1, 'E', fmtLen)) {
    buf[1 + fmtLen] = '.';
    trailingDotAdded = 1;
  }
  const int len = fmtLen + trailingDotAdded;

  if (isPercent) {
    buf[1 + len] = '%';
  }

  // Add '+' or ' ' sign if needed — anything that's neither negative nor
  // nan/inf already has its sign in the output.
  char* p = buf.data() + 1;
  prefixLen = 0;
  int signLen = 0;
  if (plusSign && (*p != '-' && *p != 'n' && *p != 'N')) {
    *--p = plusSign;
    prefixLen = 1;
    signLen = 1;
  } else if (*p == '-') {
    prefixLen = 1;
  }

  piece = fbstring(p, size_t(len + (isPercent ? 1 : 0) + signLen));
}

void FormatArg::initSlow() {
  auto b = fullArgString.begin();
  auto end = fullArgString.end();

  // Parse key
  auto p = static_cast<const char*>(memchr(b, ':', size_t(end - b)));
  if (!p) {
    key_ = StringPiece(b, end);
    return;
  }
  key_ = StringPiece(b, p);

  if (*p == ':') {
    // parse format spec
    if (++p == end) {
      return;
    }

    // fill/align, or just align
    Align a;
    if (p + 1 != end &&
        (a = formatAlignTable[static_cast<unsigned char>(p[1])]) !=
            Align::INVALID) {
      fill = *p;
      align = a;
      p += 2;
      if (p == end) {
        return;
      }
    } else if (
        (a = formatAlignTable[static_cast<unsigned char>(*p)]) !=
        Align::INVALID) {
      align = a;
      if (++p == end) {
        return;
      }
    }

    Sign s;
    auto uSign = static_cast<unsigned char>(*p);
    if ((s = formatSignTable[uSign]) != Sign::INVALID) {
      sign = s;
      if (++p == end) {
        return;
      }
    }

    if (*p == '#') {
      basePrefix = true;
      if (++p == end) {
        return;
      }
    }

    if (*p == '0') {
      enforce(align == Align::DEFAULT, "alignment specified twice");
      fill = '0';
      align = Align::PAD_AFTER_SIGN;
      if (++p == end) {
        return;
      }
    }

    auto readInt = [&] {
      auto const c = p;
      do {
        ++p;
      } while (p != end && *p >= '0' && *p <= '9');
      return to<int>(StringPiece(c, p));
    };

    if (*p == '*') {
      width = kDynamicWidth;
      ++p;

      if (p == end) {
        return;
      }

      if (*p >= '0' && *p <= '9') {
        widthIndex = readInt();
      }

      if (p == end) {
        return;
      }
    } else if (*p >= '0' && *p <= '9') {
      width = readInt();

      if (p == end) {
        return;
      }
    }

    if (*p == ',') {
      thousandsSeparator = true;
      if (++p == end) {
        return;
      }
    }

    if (*p == '.') {
      auto d = ++p;
      while (p != end && *p >= '0' && *p <= '9') {
        ++p;
      }
      if (p != d) {
        precision = to<int>(StringPiece(d, p));
        if (p != end && *p == '.') {
          trailingDot = true;
          ++p;
        }
      } else {
        trailingDot = true;
      }

      if (p == end) {
        return;
      }
    }

    presentation = *p;
    if (++p == end) {
      return;
    }
  }

  error("extra characters in format string");
}

void FormatArg::validate(Type type) const {
  enforce(keyEmpty(), "index not allowed");
  switch (type) {
    case Type::INTEGER:
      enforce(
          precision == kDefaultPrecision, "precision not allowed on integers");
      break;
    case Type::FLOAT:
      enforce(
          !basePrefix, "base prefix ('#') specifier only allowed on integers");
      enforce(
          !thousandsSeparator,
          "thousands separator (',') only allowed on integers");
      break;
    case Type::OTHER:
      enforce(
          align != Align::PAD_AFTER_SIGN,
          "'='alignment only allowed on numbers");
      enforce(sign == Sign::DEFAULT, "sign specifier only allowed on numbers");
      enforce(
          !basePrefix, "base prefix ('#') specifier only allowed on integers");
      enforce(
          !thousandsSeparator,
          "thousands separator (',') only allowed on integers");
      break;
  }
}

namespace detail {
void insertThousandsGroupingUnsafe(char* start_buffer, char** end_buffer) {
  auto remaining_digits = uint32_t(*end_buffer - start_buffer);
  uint32_t separator_size = (remaining_digits - 1) / 3;
  uint32_t result_size = remaining_digits + separator_size;
  *end_buffer = *end_buffer + separator_size;

  // get the end of the new string with the separators
  uint32_t buffer_write_index = result_size - 1;
  uint32_t buffer_read_index = remaining_digits - 1;
  start_buffer[buffer_write_index + 1] = 0;

  bool done = false;
  uint32_t next_group_size = 3;

  while (!done) {
    uint32_t current_group_size = std::max<uint32_t>(
        1, std::min<uint32_t>(remaining_digits, next_group_size));

    // write out the current group's digits to the buffer index
    for (uint32_t i = 0; i < current_group_size; i++) {
      start_buffer[buffer_write_index--] = start_buffer[buffer_read_index--];
    }

    // if not finished, write the separator before the next group
    if (buffer_write_index < buffer_write_index + 1) {
      start_buffer[buffer_write_index--] = ',';
    } else {
      done = true;
    }

    remaining_digits -= current_group_size;
  }
}
} // namespace detail

FormatKeyNotFoundException::FormatKeyNotFoundException(StringPiece key)
    : std::out_of_range(kMessagePrefix.str() + key.str()) {}

} // namespace folly
