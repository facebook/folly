/*
 * Copyright 2015 Facebook, Inc.
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

#ifndef FOLLY_FORMAT_H_
#error This file may only be included from Format.h.
#endif

#include <folly/Exception.h>
#include <folly/Traits.h>

// Ignore -Wformat-nonliteral warnings within this file
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"

namespace folly {

namespace detail {

// Updates the end of the buffer after the comma separators have been added.
void insertThousandsGroupingUnsafe(char* start_buffer, char** end_buffer);

extern const char formatHexUpper[256][2];
extern const char formatHexLower[256][2];
extern const char formatOctal[512][3];
extern const char formatBinary[256][8];

const size_t kMaxHexLength = 2 * sizeof(uintmax_t);
const size_t kMaxOctalLength = 3 * sizeof(uintmax_t);
const size_t kMaxBinaryLength = 8 * sizeof(uintmax_t);

/**
 * Convert an unsigned to hex, using repr (which maps from each possible
 * 2-hex-bytes value to the 2-character representation).
 *
 * Just like folly::detail::uintToBuffer in Conv.h, writes at the *end* of
 * the supplied buffer and returns the offset of the beginning of the string
 * from the start of the buffer.  The formatted string will be in range
 * [buf+begin, buf+bufLen).
 */
template <class Uint>
size_t uintToHex(char* buffer, size_t bufLen, Uint v,
                 const char (&repr)[256][2]) {
  // 'v >>= 7, v >>= 1' is no more than a work around to get rid of shift size
  // warning when Uint = uint8_t (it's false as v >= 256 implies sizeof(v) > 1).
  for (; !less_than<unsigned, 256>(v); v >>= 7, v >>= 1) {
    auto b = v & 0xff;
    bufLen -= 2;
    buffer[bufLen] = repr[b][0];
    buffer[bufLen + 1] = repr[b][1];
  }
  buffer[--bufLen] = repr[v][1];
  if (v >= 16) {
    buffer[--bufLen] = repr[v][0];
  }
  return bufLen;
}

/**
 * Convert an unsigned to hex, using lower-case letters for the digits
 * above 9.  See the comments for uintToHex.
 */
template <class Uint>
inline size_t uintToHexLower(char* buffer, size_t bufLen, Uint v) {
  return uintToHex(buffer, bufLen, v, formatHexLower);
}

/**
 * Convert an unsigned to hex, using upper-case letters for the digits
 * above 9.  See the comments for uintToHex.
 */
template <class Uint>
inline size_t uintToHexUpper(char* buffer, size_t bufLen, Uint v) {
  return uintToHex(buffer, bufLen, v, formatHexUpper);
}

/**
 * Convert an unsigned to octal.
 *
 * Just like folly::detail::uintToBuffer in Conv.h, writes at the *end* of
 * the supplied buffer and returns the offset of the beginning of the string
 * from the start of the buffer.  The formatted string will be in range
 * [buf+begin, buf+bufLen).
 */
template <class Uint>
size_t uintToOctal(char* buffer, size_t bufLen, Uint v) {
  auto& repr = formatOctal;
  // 'v >>= 7, v >>= 2' is no more than a work around to get rid of shift size
  // warning when Uint = uint8_t (it's false as v >= 512 implies sizeof(v) > 1).
  for (; !less_than<unsigned, 512>(v); v >>= 7, v >>= 2) {
    auto b = v & 0x1ff;
    bufLen -= 3;
    buffer[bufLen] = repr[b][0];
    buffer[bufLen + 1] = repr[b][1];
    buffer[bufLen + 2] = repr[b][2];
  }
  buffer[--bufLen] = repr[v][2];
  if (v >= 8) {
    buffer[--bufLen] = repr[v][1];
  }
  if (v >= 64) {
    buffer[--bufLen] = repr[v][0];
  }
  return bufLen;
}

/**
 * Convert an unsigned to binary.
 *
 * Just like folly::detail::uintToBuffer in Conv.h, writes at the *end* of
 * the supplied buffer and returns the offset of the beginning of the string
 * from the start of the buffer.  The formatted string will be in range
 * [buf+begin, buf+bufLen).
 */
template <class Uint>
size_t uintToBinary(char* buffer, size_t bufLen, Uint v) {
  auto& repr = formatBinary;
  if (v == 0) {
    buffer[--bufLen] = '0';
    return bufLen;
  }
  for (; v; v >>= 7, v >>= 1) {
    auto b = v & 0xff;
    bufLen -= 8;
    memcpy(buffer + bufLen, &(repr[b][0]), 8);
  }
  while (buffer[bufLen] == '0') {
    ++bufLen;
  }
  return bufLen;
}

}  // namespace detail

template <class Derived, bool containerMode, class... Args>
BaseFormatter<Derived, containerMode, Args...>::BaseFormatter(StringPiece str,
                                                              Args&&... args)
    : str_(str),
      values_(FormatValue<typename std::decay<Args>::type>(
          std::forward<Args>(args))...) {
  static_assert(!containerMode || sizeof...(Args) == 1,
                "Exactly one argument required in container mode");
}

template <class Derived, bool containerMode, class... Args>
void BaseFormatter<Derived, containerMode, Args...>::handleFormatStrError()
    const {
  if (crashOnError_) {
    LOG(FATAL) << "folly::format: bad format string \"" << str_ << "\": " <<
      folly::exceptionStr(std::current_exception());
  }
  throw;
}

template <class Derived, bool containerMode, class... Args>
template <class Output>
void BaseFormatter<Derived, containerMode, Args...>::operator()(Output& out)
    const {
  // Catch BadFormatArg and range_error exceptions, and call
  // handleFormatStrError().
  //
  // These exception types indicate a problem with the format string.  Most
  // format strings are string literals specified by the programmer.  If they
  // have a problem, this is usually a programmer bug.  We want to crash to
  // ensure that these are found early on during development.
  //
  // BadFormatArg is thrown by the Format.h code, while range_error is thrown
  // by Conv.h, which is used in several places in our format string
  // processing.
  //
  // (Note: This behavior is slightly dangerous.  If the Output object throws a
  // BadFormatArg or a range_error, we will also crash the program, even if it
  // wasn't an issue with the format string.  This seems highly unlikely
  // though, and none of our current Output objects can throw these errors.)
  //
  // We also throw out_of_range errors if the format string references an
  // argument that isn't present (or a key that isn't present in one of the
  // argument containers).  However, at the moment we don't crash on these
  // errors, as it is likely that the container is dynamic at runtime.
  try {
    appendOutput(out);
  } catch (const BadFormatArg& ex) {
    handleFormatStrError();
  } catch (const std::range_error& ex) {
    handleFormatStrError();
  }
}

template <class Derived, bool containerMode, class... Args>
template <class Output>
void BaseFormatter<Derived, containerMode, Args...>::appendOutput(Output& out)
    const {

  // Copy raw string (without format specifiers) to output;
  // not as simple as we'd like, as we still need to translate "}}" to "}"
  // and throw if we see any lone "}"
  auto outputString = [&out] (StringPiece s) {
    auto p = s.begin();
    auto end = s.end();
    while (p != end) {
      auto q = static_cast<const char*>(memchr(p, '}', end - p));
      if (!q) {
        out(StringPiece(p, end));
        break;
      }
      ++q;
      out(StringPiece(p, q));
      p = q;

      if (p == end || *p != '}') {
        throw BadFormatArg("folly::format: single '}' in format string");
      }
      ++p;
    }
  };

  auto p = str_.begin();
  auto end = str_.end();

  int nextArg = 0;
  bool hasDefaultArgIndex = false;
  bool hasExplicitArgIndex = false;
  while (p != end) {
    auto q = static_cast<const char*>(memchr(p, '{', end - p));
    if (!q) {
      outputString(StringPiece(p, end));
      break;
    }
    outputString(StringPiece(p, q));
    p = q + 1;

    if (p == end) {
      throw BadFormatArg("folly::format: '}' at end of format string");
    }

    // "{{" -> "{"
    if (*p == '{') {
      out(StringPiece(p, 1));
      ++p;
      continue;
    }

    // Format string
    q = static_cast<const char*>(memchr(p, '}', end - p));
    if (q == nullptr) {
      throw BadFormatArg("folly::format: missing ending '}'");
    }
    FormatArg arg(StringPiece(p, q));
    p = q + 1;

    int argIndex = 0;
    auto piece = arg.splitKey<true>();  // empty key component is okay
    if (containerMode) {  // static
      if (piece.empty()) {
        arg.setNextIntKey(nextArg++);
        hasDefaultArgIndex = true;
      } else {
        arg.setNextKey(piece);
        hasExplicitArgIndex = true;
      }
    } else {
      if (piece.empty()) {
        argIndex = nextArg++;
        hasDefaultArgIndex = true;
      } else {
        try {
          argIndex = to<int>(piece);
        } catch (const std::out_of_range& e) {
          arg.error("argument index must be integer");
        }
        arg.enforce(argIndex >= 0, "argument index must be non-negative");
        hasExplicitArgIndex = true;
      }
    }

    if (hasDefaultArgIndex && hasExplicitArgIndex) {
      throw BadFormatArg(
          "folly::format: may not have both default and explicit arg indexes");
    }

    doFormat(argIndex, arg, out);
  }
}

template <class Derived, bool containerMode, class... Args>
void writeTo(FILE* fp,
             const BaseFormatter<Derived, containerMode, Args...>& formatter) {
  auto writer = [fp] (StringPiece sp) {
    size_t n = fwrite(sp.data(), 1, sp.size(), fp);
    if (n < sp.size()) {
      throwSystemError("Formatter writeTo", "fwrite failed");
    }
  };
  formatter(writer);
}

namespace format_value {

template <class FormatCallback>
void formatString(StringPiece val, FormatArg& arg, FormatCallback& cb) {
  if (arg.width != FormatArg::kDefaultWidth && arg.width < 0) {
    throw BadFormatArg("folly::format: invalid width");
  }
  if (arg.precision != FormatArg::kDefaultPrecision && arg.precision < 0) {
    throw BadFormatArg("folly::format: invalid precision");
  }

  // XXX: clang should be smart enough to not need the two static_cast<size_t>
  // uses below given the above checks. If clang ever becomes that smart, we
  // should remove the otherwise unnecessary warts.

  if (arg.precision != FormatArg::kDefaultPrecision &&
      val.size() > static_cast<size_t>(arg.precision)) {
    val.reset(val.data(), arg.precision);
  }

  constexpr int padBufSize = 128;
  char padBuf[padBufSize];

  // Output padding, no more than padBufSize at once
  auto pad = [&padBuf, &cb, padBufSize] (int chars) {
    while (chars) {
      int n = std::min(chars, padBufSize);
      cb(StringPiece(padBuf, n));
      chars -= n;
    }
  };

  int padRemaining = 0;
  if (arg.width != FormatArg::kDefaultWidth &&
      val.size() < static_cast<size_t>(arg.width)) {
    char fill = arg.fill == FormatArg::kDefaultFill ? ' ' : arg.fill;
    int padChars = static_cast<int> (arg.width - val.size());
    memset(padBuf, fill, std::min(padBufSize, padChars));

    switch (arg.align) {
    case FormatArg::Align::DEFAULT:
    case FormatArg::Align::LEFT:
      padRemaining = padChars;
      break;
    case FormatArg::Align::CENTER:
      pad(padChars / 2);
      padRemaining = padChars - padChars / 2;
      break;
    case FormatArg::Align::RIGHT:
    case FormatArg::Align::PAD_AFTER_SIGN:
      pad(padChars);
      break;
    default:
      abort();
      break;
    }
  }

  cb(val);

  if (padRemaining) {
    pad(padRemaining);
  }
}

template <class FormatCallback>
void formatNumber(StringPiece val, int prefixLen, FormatArg& arg,
                  FormatCallback& cb) {
  // precision means something different for numbers
  arg.precision = FormatArg::kDefaultPrecision;
  if (arg.align == FormatArg::Align::DEFAULT) {
    arg.align = FormatArg::Align::RIGHT;
  } else if (prefixLen && arg.align == FormatArg::Align::PAD_AFTER_SIGN) {
    // Split off the prefix, then do any padding if necessary
    cb(val.subpiece(0, prefixLen));
    val.advance(prefixLen);
    arg.width = std::max(arg.width - prefixLen, 0);
  }
  format_value::formatString(val, arg, cb);
}

template <class FormatCallback,
          class Derived,
          bool containerMode,
          class... Args>
void formatFormatter(
    const BaseFormatter<Derived, containerMode, Args...>& formatter,
    FormatArg& arg,
    FormatCallback& cb) {
  if (arg.width == FormatArg::kDefaultWidth &&
      arg.precision == FormatArg::kDefaultPrecision) {
    // nothing to do
    formatter(cb);
  } else if (arg.align != FormatArg::Align::LEFT &&
             arg.align != FormatArg::Align::DEFAULT) {
    // We can only avoid creating a temporary string if we align left,
    // as we'd need to know the size beforehand otherwise
    format_value::formatString(formatter.fbstr(), arg, cb);
  } else {
    auto fn = [&arg, &cb] (StringPiece sp) mutable {
      int sz = static_cast<int>(sp.size());
      if (arg.precision != FormatArg::kDefaultPrecision) {
        sz = std::min(arg.precision, sz);
        sp.reset(sp.data(), sz);
        arg.precision -= sz;
      }
      if (!sp.empty()) {
        cb(sp);
        if (arg.width != FormatArg::kDefaultWidth) {
          arg.width = std::max(arg.width - sz, 0);
        }
      }
    };
    formatter(fn);
    if (arg.width != FormatArg::kDefaultWidth && arg.width != 0) {
      // Rely on formatString to do appropriate padding
      format_value::formatString(StringPiece(), arg, cb);
    }
  }
}

}  // namespace format_value

// Definitions for default FormatValue classes

// Integral types (except bool)
template <class T>
class FormatValue<
  T, typename std::enable_if<
    std::is_integral<T>::value &&
    !std::is_same<T, bool>::value>::type>
  {
 public:
  explicit FormatValue(T val) : val_(val) { }
  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    arg.validate(FormatArg::Type::INTEGER);
    doFormat(arg, cb);
  }

  template <class FormatCallback>
  void doFormat(FormatArg& arg, FormatCallback& cb) const {
    char presentation = arg.presentation;
    if (presentation == FormatArg::kDefaultPresentation) {
      presentation = std::is_same<T, char>::value ? 'c' : 'd';
    }

    // Do all work as unsigned, we'll add the prefix ('0' or '0x' if necessary)
    // and sign ourselves.
    typedef typename std::make_unsigned<T>::type UT;
    UT uval;
    char sign;
    if (std::is_signed<T>::value) {
      if (folly::is_negative(val_)) {
        uval = static_cast<UT>(-val_);
        sign = '-';
      } else {
        uval = static_cast<UT>(val_);
        switch (arg.sign) {
        case FormatArg::Sign::PLUS_OR_MINUS:
          sign = '+';
          break;
        case FormatArg::Sign::SPACE_OR_MINUS:
          sign = ' ';
          break;
        default:
          sign = '\0';
          break;
        }
      }
    } else {
      uval = val_;
      sign = '\0';

      arg.enforce(arg.sign == FormatArg::Sign::DEFAULT,
                  "sign specifications not allowed for unsigned values");
    }

    // max of:
    // #x: 0x prefix + 16 bytes = 18 bytes
    // #o: 0 prefix + 22 bytes = 23 bytes
    // #b: 0b prefix + 64 bytes = 65 bytes
    // ,d: 26 bytes (including thousands separators!)
    // + nul terminator
    // + 3 for sign and prefix shenanigans (see below)
    constexpr size_t valBufSize = 69;
    char valBuf[valBufSize];
    char* valBufBegin = nullptr;
    char* valBufEnd = nullptr;

    int prefixLen = 0;
    switch (presentation) {
    case 'n':
      arg.enforce(!arg.basePrefix,
                  "base prefix not allowed with '", presentation,
                  "' specifier");

      arg.enforce(!arg.thousandsSeparator,
                  "cannot use ',' with the '", presentation,
                  "' specifier");

      valBufBegin = valBuf + 3;  // room for sign and base prefix
      valBufEnd = valBufBegin + sprintf(valBufBegin, "%'ju",
                                        static_cast<uintmax_t>(uval));
      break;
    case 'd':
      arg.enforce(!arg.basePrefix,
                  "base prefix not allowed with '", presentation,
                  "' specifier");
      valBufBegin = valBuf + 3;  // room for sign and base prefix

      // Use uintToBuffer, faster than sprintf
      valBufEnd = valBufBegin + uint64ToBufferUnsafe(uval, valBufBegin);
      if (arg.thousandsSeparator) {
        detail::insertThousandsGroupingUnsafe(valBufBegin, &valBufEnd);
      }
      break;
    case 'c':
      arg.enforce(!arg.basePrefix,
                  "base prefix not allowed with '", presentation,
                  "' specifier");
      arg.enforce(!arg.thousandsSeparator,
                  "thousands separator (',') not allowed with '",
                  presentation, "' specifier");
      valBufBegin = valBuf + 3;
      *valBufBegin = static_cast<char>(uval);
      valBufEnd = valBufBegin + 1;
      break;
    case 'o':
    case 'O':
      arg.enforce(!arg.thousandsSeparator,
                  "thousands separator (',') not allowed with '",
                  presentation, "' specifier");
      valBufEnd = valBuf + valBufSize - 1;
      valBufBegin = valBuf + detail::uintToOctal(valBuf, valBufSize - 1, uval);
      if (arg.basePrefix) {
        *--valBufBegin = '0';
        prefixLen = 1;
      }
      break;
    case 'x':
      arg.enforce(!arg.thousandsSeparator,
                  "thousands separator (',') not allowed with '",
                  presentation, "' specifier");
      valBufEnd = valBuf + valBufSize - 1;
      valBufBegin = valBuf + detail::uintToHexLower(valBuf, valBufSize - 1,
                                                    uval);
      if (arg.basePrefix) {
        *--valBufBegin = 'x';
        *--valBufBegin = '0';
        prefixLen = 2;
      }
      break;
    case 'X':
      arg.enforce(!arg.thousandsSeparator,
                  "thousands separator (',') not allowed with '",
                  presentation, "' specifier");
      valBufEnd = valBuf + valBufSize - 1;
      valBufBegin = valBuf + detail::uintToHexUpper(valBuf, valBufSize - 1,
                                                    uval);
      if (arg.basePrefix) {
        *--valBufBegin = 'X';
        *--valBufBegin = '0';
        prefixLen = 2;
      }
      break;
    case 'b':
    case 'B':
      arg.enforce(!arg.thousandsSeparator,
                  "thousands separator (',') not allowed with '",
                  presentation, "' specifier");
      valBufEnd = valBuf + valBufSize - 1;
      valBufBegin = valBuf + detail::uintToBinary(valBuf, valBufSize - 1,
                                                  uval);
      if (arg.basePrefix) {
        *--valBufBegin = presentation;  // 0b or 0B
        *--valBufBegin = '0';
        prefixLen = 2;
      }
      break;
    default:
      arg.error("invalid specifier '", presentation, "'");
    }

    if (sign) {
      *--valBufBegin = sign;
      ++prefixLen;
    }

    format_value::formatNumber(StringPiece(valBufBegin, valBufEnd), prefixLen,
                               arg, cb);
  }

 private:
  T val_;
};

// Bool
template <>
class FormatValue<bool> {
 public:
  explicit FormatValue(bool val) : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    if (arg.presentation == FormatArg::kDefaultPresentation) {
      arg.validate(FormatArg::Type::OTHER);
      format_value::formatString(val_ ? "true" : "false", arg, cb);
    } else {  // number
      FormatValue<int>(val_).format(arg, cb);
    }
  }

 private:
  bool val_;
};

// double
template <>
class FormatValue<double> {
 public:
  explicit FormatValue(double val) : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    using ::double_conversion::DoubleToStringConverter;
    using ::double_conversion::StringBuilder;

    arg.validate(FormatArg::Type::FLOAT);

    if (arg.presentation == FormatArg::kDefaultPresentation) {
      arg.presentation = 'g';
    }

    const char* infinitySymbol = isupper(arg.presentation) ? "INF" : "inf";
    const char* nanSymbol = isupper(arg.presentation) ? "NAN" : "nan";
    char exponentSymbol = isupper(arg.presentation) ? 'E' : 'e';

    if (arg.precision == FormatArg::kDefaultPrecision) {
      arg.precision = 6;
    }

    // 2+: for null terminator and optional sign shenanigans.
    char buf[2 + std::max({
        (2 + DoubleToStringConverter::kMaxFixedDigitsBeforePoint +
         DoubleToStringConverter::kMaxFixedDigitsAfterPoint),
        (8 + DoubleToStringConverter::kMaxExponentialDigits),
        (7 + DoubleToStringConverter::kMaxPrecisionDigits)})];
    StringBuilder builder(buf + 1, static_cast<int> (sizeof(buf) - 1));

    char plusSign;
    switch (arg.sign) {
    case FormatArg::Sign::PLUS_OR_MINUS:
      plusSign = '+';
      break;
    case FormatArg::Sign::SPACE_OR_MINUS:
      plusSign = ' ';
      break;
    default:
      plusSign = '\0';
      break;
    };

    auto flags =
        DoubleToStringConverter::EMIT_POSITIVE_EXPONENT_SIGN |
        (arg.trailingDot ? DoubleToStringConverter::EMIT_TRAILING_DECIMAL_POINT
                         : 0);

    double val = val_;
    switch (arg.presentation) {
    case '%':
      val *= 100;
    case 'f':
    case 'F':
      {
        if (arg.precision >
            DoubleToStringConverter::kMaxFixedDigitsAfterPoint) {
          arg.precision = DoubleToStringConverter::kMaxFixedDigitsAfterPoint;
        }
        DoubleToStringConverter conv(flags,
                                     infinitySymbol,
                                     nanSymbol,
                                     exponentSymbol,
                                     -4,
                                     arg.precision,
                                     0,
                                     0);
        arg.enforce(conv.ToFixed(val, arg.precision, &builder),
                    "fixed double conversion failed");
      }
      break;
    case 'e':
    case 'E':
      {
        if (arg.precision > DoubleToStringConverter::kMaxExponentialDigits) {
          arg.precision = DoubleToStringConverter::kMaxExponentialDigits;
        }

        DoubleToStringConverter conv(flags,
                                     infinitySymbol,
                                     nanSymbol,
                                     exponentSymbol,
                                     -4,
                                     arg.precision,
                                     0,
                                     0);
        arg.enforce(conv.ToExponential(val, arg.precision, &builder));
      }
      break;
    case 'n':  // should be locale-aware, but isn't
    case 'g':
    case 'G':
      {
        if (arg.precision < DoubleToStringConverter::kMinPrecisionDigits) {
          arg.precision = DoubleToStringConverter::kMinPrecisionDigits;
        } else if (arg.precision >
                   DoubleToStringConverter::kMaxPrecisionDigits) {
          arg.precision = DoubleToStringConverter::kMaxPrecisionDigits;
        }
        DoubleToStringConverter conv(flags,
                                     infinitySymbol,
                                     nanSymbol,
                                     exponentSymbol,
                                     -4,
                                     arg.precision,
                                     0,
                                     0);
        arg.enforce(conv.ToShortest(val, &builder));
      }
      break;
    default:
      arg.error("invalid specifier '", arg.presentation, "'");
    }

    int len = builder.position();
    builder.Finalize();
    DCHECK_GT(len, 0);

    // Add '+' or ' ' sign if needed
    char* p = buf + 1;
    // anything that's neither negative nor nan
    int prefixLen = 0;
    if (plusSign && (*p != '-' && *p != 'n' && *p != 'N')) {
      *--p = plusSign;
      ++len;
      prefixLen = 1;
    } else if (*p == '-') {
      prefixLen = 1;
    }

    format_value::formatNumber(StringPiece(p, len), prefixLen, arg, cb);
  }

 private:
  double val_;
};

// float (defer to double)
template <>
class FormatValue<float> {
 public:
  explicit FormatValue(float val) : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    FormatValue<double>(val_).format(arg, cb);
  }

 private:
  float val_;
};

// Sring-y types (implicitly convertible to StringPiece, except char*)
template <class T>
class FormatValue<
  T, typename std::enable_if<
      (!std::is_pointer<T>::value ||
       !std::is_same<char, typename std::decay<
          typename std::remove_pointer<T>::type>::type>::value) &&
      std::is_convertible<T, StringPiece>::value>::type>
  {
 public:
  explicit FormatValue(StringPiece val) : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    if (arg.keyEmpty()) {
      arg.validate(FormatArg::Type::OTHER);
      arg.enforce(arg.presentation == FormatArg::kDefaultPresentation ||
                  arg.presentation == 's',
                  "invalid specifier '", arg.presentation, "'");
      format_value::formatString(val_, arg, cb);
    } else {
      FormatValue<char>(val_.at(arg.splitIntKey())).format(arg, cb);
    }
  }

 private:
  StringPiece val_;
};

// Null
template <>
class FormatValue<std::nullptr_t> {
 public:
  explicit FormatValue(std::nullptr_t) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    arg.validate(FormatArg::Type::OTHER);
    arg.enforce(arg.presentation == FormatArg::kDefaultPresentation,
                "invalid specifier '", arg.presentation, "'");
    format_value::formatString("(null)", arg, cb);
  }
};

// Partial specialization of FormatValue for char*
template <class T>
class FormatValue<
  T*,
  typename std::enable_if<
      std::is_same<char, typename std::decay<T>::type>::value>::type>
  {
 public:
  explicit FormatValue(T* val) : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    if (arg.keyEmpty()) {
      if (!val_) {
        FormatValue<std::nullptr_t>(nullptr).format(arg, cb);
      } else {
        FormatValue<StringPiece>(val_).format(arg, cb);
      }
    } else {
      FormatValue<typename std::decay<T>::type>(
          val_[arg.splitIntKey()]).format(arg, cb);
    }
  }

 private:
  T* val_;
};

// Partial specialization of FormatValue for void*
template <class T>
class FormatValue<
  T*,
  typename std::enable_if<
      std::is_same<void, typename std::decay<T>::type>::value>::type>
  {
 public:
  explicit FormatValue(T* val) : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    if (!val_) {
      FormatValue<std::nullptr_t>(nullptr).format(arg, cb);
    } else {
      // Print as a pointer, in hex.
      arg.validate(FormatArg::Type::OTHER);
      arg.enforce(arg.presentation == FormatArg::kDefaultPresentation,
                  "invalid specifier '", arg.presentation, "'");
      arg.basePrefix = true;
      arg.presentation = 'x';
      if (arg.align == FormatArg::Align::DEFAULT) {
        arg.align = FormatArg::Align::LEFT;
      }
      FormatValue<uintptr_t>(
          reinterpret_cast<uintptr_t>(val_)).doFormat(arg, cb);
    }
  }

 private:
  T* val_;
};

template <class T, class = void>
class TryFormatValue {
 public:
  template <class FormatCallback>
  static void formatOrFail(T& value, FormatArg& arg, FormatCallback& cb) {
    arg.error("No formatter available for this type");
  }
};

template <class T>
class TryFormatValue<
  T,
  typename std::enable_if<
      0 < sizeof(FormatValue<typename std::decay<T>::type>)>::type>
  {
 public:
  template <class FormatCallback>
  static void formatOrFail(T& value, FormatArg& arg, FormatCallback& cb) {
    FormatValue<typename std::decay<T>::type>(value).format(arg, cb);
  }
};

// Partial specialization of FormatValue for other pointers
template <class T>
class FormatValue<
  T*,
  typename std::enable_if<
      !std::is_same<char, typename std::decay<T>::type>::value &&
      !std::is_same<void, typename std::decay<T>::type>::value>::type>
  {
 public:
  explicit FormatValue(T* val) : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    if (arg.keyEmpty()) {
      FormatValue<void*>((void*)val_).format(arg, cb);
    } else {
      TryFormatValue<T>::formatOrFail(val_[arg.splitIntKey()], arg, cb);
    }
  }
 private:
  T* val_;
};

namespace detail {

// Shortcut, so we don't have to use enable_if everywhere
struct FormatTraitsBase {
  typedef void enabled;
};

// Traits that define enabled, value_type, and at() for anything
// indexable with integral keys: pointers, arrays, vectors, and maps
// with integral keys
template <class T, class Enable=void> struct IndexableTraits;

// Base class for sequences (vectors, deques)
template <class C>
struct IndexableTraitsSeq : public FormatTraitsBase {
  typedef C container_type;
  typedef typename C::value_type value_type;
  static const value_type& at(const C& c, int idx) {
    return c.at(idx);
  }

  static const value_type& at(const C& c, int idx,
                              const value_type& dflt) {
    return (idx >= 0 && size_t(idx) < c.size()) ? c.at(idx) : dflt;
  }
};

// Base class for associative types (maps)
template <class C>
struct IndexableTraitsAssoc : public FormatTraitsBase {
  typedef typename C::value_type::second_type value_type;
  static const value_type& at(const C& c, int idx) {
    return c.at(static_cast<typename C::key_type>(idx));
  }
  static const value_type& at(const C& c, int idx,
                              const value_type& dflt) {
    auto pos = c.find(static_cast<typename C::key_type>(idx));
    return pos != c.end() ? pos->second : dflt;
  }
};

// std::array
template <class T, size_t N>
struct IndexableTraits<std::array<T, N>>
  : public IndexableTraitsSeq<std::array<T, N>> {
};

// std::vector
template <class T, class A>
struct IndexableTraits<std::vector<T, A>>
  : public IndexableTraitsSeq<std::vector<T, A>> {
};

// std::deque
template <class T, class A>
struct IndexableTraits<std::deque<T, A>>
  : public IndexableTraitsSeq<std::deque<T, A>> {
};

// fbvector
template <class T, class A>
struct IndexableTraits<fbvector<T, A>>
  : public IndexableTraitsSeq<fbvector<T, A>> {
};

// small_vector
template <class T, size_t M, class A, class B, class C>
struct IndexableTraits<small_vector<T, M, A, B, C>>
  : public IndexableTraitsSeq<small_vector<T, M, A, B, C>> {
};

// std::map with integral keys
template <class K, class T, class C, class A>
struct IndexableTraits<
  std::map<K, T, C, A>,
  typename std::enable_if<std::is_integral<K>::value>::type>
  : public IndexableTraitsAssoc<std::map<K, T, C, A>> {
};

// std::unordered_map with integral keys
template <class K, class T, class H, class E, class A>
struct IndexableTraits<
  std::unordered_map<K, T, H, E, A>,
  typename std::enable_if<std::is_integral<K>::value>::type>
  : public IndexableTraitsAssoc<std::unordered_map<K, T, H, E, A>> {
};

}  // namespace detail

// Partial specialization of FormatValue for integer-indexable containers
template <class T>
class FormatValue<
  T,
  typename detail::IndexableTraits<T>::enabled> {
 public:
  explicit FormatValue(const T& val) : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    FormatValue<typename std::decay<
      typename detail::IndexableTraits<T>::value_type>::type>(
        detail::IndexableTraits<T>::at(
            val_, arg.splitIntKey())).format(arg, cb);
  }

 private:
  const T& val_;
};

template <class Container, class Value>
class FormatValue<
  detail::DefaultValueWrapper<Container, Value>,
  typename detail::IndexableTraits<Container>::enabled> {
 public:
  explicit FormatValue(const detail::DefaultValueWrapper<Container, Value>& val)
    : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    FormatValue<typename std::decay<
      typename detail::IndexableTraits<Container>::value_type>::type>(
          detail::IndexableTraits<Container>::at(
              val_.container,
              arg.splitIntKey(),
              val_.defaultValue)).format(arg, cb);
  }

 private:
  const detail::DefaultValueWrapper<Container, Value>& val_;
};

namespace detail {

// Define enabled, key_type, convert from StringPiece to the key types
// that we support
template <class T> struct KeyFromStringPiece;

// std::string
template <>
struct KeyFromStringPiece<std::string> : public FormatTraitsBase {
  typedef std::string key_type;
  static std::string convert(StringPiece s) {
    return s.toString();
  }
  typedef void enabled;
};

// fbstring
template <>
struct KeyFromStringPiece<fbstring> : public FormatTraitsBase {
  typedef fbstring key_type;
  static fbstring convert(StringPiece s) {
    return s.toFbstring();
  }
};

// StringPiece
template <>
struct KeyFromStringPiece<StringPiece> : public FormatTraitsBase {
  typedef StringPiece key_type;
  static StringPiece convert(StringPiece s) {
    return s;
  }
};

// Base class for associative types keyed by strings
template <class T> struct KeyableTraitsAssoc : public FormatTraitsBase {
  typedef typename T::key_type key_type;
  typedef typename T::value_type::second_type value_type;
  static const value_type& at(const T& map, StringPiece key) {
    return map.at(KeyFromStringPiece<key_type>::convert(key));
  }
  static const value_type& at(const T& map, StringPiece key,
                              const value_type& dflt) {
    auto pos = map.find(KeyFromStringPiece<key_type>::convert(key));
    return pos != map.end() ? pos->second : dflt;
  }
};

// Define enabled, key_type, value_type, at() for supported string-keyed
// types
template <class T, class Enabled=void> struct KeyableTraits;

// std::map with string key
template <class K, class T, class C, class A>
struct KeyableTraits<
  std::map<K, T, C, A>,
  typename KeyFromStringPiece<K>::enabled>
  : public KeyableTraitsAssoc<std::map<K, T, C, A>> {
};

// std::unordered_map with string key
template <class K, class T, class H, class E, class A>
struct KeyableTraits<
  std::unordered_map<K, T, H, E, A>,
  typename KeyFromStringPiece<K>::enabled>
  : public KeyableTraitsAssoc<std::unordered_map<K, T, H, E, A>> {
};

}  // namespace detail

// Partial specialization of FormatValue for string-keyed containers
template <class T>
class FormatValue<
  T,
  typename detail::KeyableTraits<T>::enabled> {
 public:
  explicit FormatValue(const T& val) : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    FormatValue<typename std::decay<
      typename detail::KeyableTraits<T>::value_type>::type>(
        detail::KeyableTraits<T>::at(
            val_, arg.splitKey())).format(arg, cb);
  }

 private:
  const T& val_;
};

template <class Container, class Value>
class FormatValue<
  detail::DefaultValueWrapper<Container, Value>,
  typename detail::KeyableTraits<Container>::enabled> {
 public:
  explicit FormatValue(const detail::DefaultValueWrapper<Container, Value>& val)
    : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    FormatValue<typename std::decay<
      typename detail::KeyableTraits<Container>::value_type>::type>(
          detail::KeyableTraits<Container>::at(
              val_.container,
              arg.splitKey(),
              val_.defaultValue)).format(arg, cb);
  }

 private:
  const detail::DefaultValueWrapper<Container, Value>& val_;
};

// Partial specialization of FormatValue for pairs
template <class A, class B>
class FormatValue<std::pair<A, B>> {
 public:
  explicit FormatValue(const std::pair<A, B>& val) : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    int key = arg.splitIntKey();
    switch (key) {
    case 0:
      FormatValue<typename std::decay<A>::type>(val_.first).format(arg, cb);
      break;
    case 1:
      FormatValue<typename std::decay<B>::type>(val_.second).format(arg, cb);
      break;
    default:
      arg.error("invalid index for pair");
    }
  }

 private:
  const std::pair<A, B>& val_;
};

// Partial specialization of FormatValue for tuples
template <class... Args>
class FormatValue<std::tuple<Args...>> {
  typedef std::tuple<Args...> Tuple;
 public:
  explicit FormatValue(const Tuple& val) : val_(val) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    int key = arg.splitIntKey();
    arg.enforce(key >= 0, "tuple index must be non-negative");
    doFormat(key, arg, cb);
  }

 private:
  static constexpr size_t valueCount = std::tuple_size<Tuple>::value;

  template <size_t K, class Callback>
  typename std::enable_if<K == valueCount>::type
  doFormatFrom(size_t i, FormatArg& arg, Callback& cb) const {
    arg.enforce("tuple index out of range, max=", i);
  }

  template <size_t K, class Callback>
  typename std::enable_if<(K < valueCount)>::type
  doFormatFrom(size_t i, FormatArg& arg, Callback& cb) const {
    if (i == K) {
      FormatValue<typename std::decay<
        typename std::tuple_element<K, Tuple>::type>::type>(
          std::get<K>(val_)).format(arg, cb);
    } else {
      doFormatFrom<K+1>(i, arg, cb);
    }
  }

  template <class Callback>
  void doFormat(size_t i, FormatArg& arg, Callback& cb) const {
    return doFormatFrom<0>(i, arg, cb);
  }

  const Tuple& val_;
};

// Partial specialization of FormatValue for nested Formatters
template <bool containerMode, class... Args,
          template <bool, class...> class F>
class FormatValue<F<containerMode, Args...>,
                  typename std::enable_if<detail::IsFormatter<
                      F<containerMode, Args...>>::value>::type> {
  typedef typename F<containerMode, Args...>::BaseType FormatterValue;

 public:
  explicit FormatValue(const FormatterValue& f) : f_(f) { }

  template <class FormatCallback>
  void format(FormatArg& arg, FormatCallback& cb) const {
    format_value::formatFormatter(f_, arg, cb);
  }
 private:
  const FormatterValue& f_;
};

/**
 * Formatter objects can be appended to strings, and therefore they're
 * compatible with folly::toAppend and folly::to.
 */
template <class Tgt, class Derived, bool containerMode, class... Args>
typename std::enable_if<IsSomeString<Tgt>::value>::type toAppend(
    const BaseFormatter<Derived, containerMode, Args...>& value, Tgt* result) {
  value.appendTo(*result);
}

}  // namespace folly

#pragma GCC diagnostic pop
