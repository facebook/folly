/*
 * Copyright 2012 Facebook, Inc.
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

#ifndef FOLLY_BASE_STRING_H_
#define FOLLY_BASE_STRING_H_

#include <exception>
#include <string>
#include <boost/type_traits.hpp>

#ifdef __GNUC__
# include <ext/hash_set>
# include <ext/hash_map>
#endif

#include "folly/Conv.h"
#include "folly/FBString.h"
#include "folly/FBVector.h"
#include "folly/Range.h"
#include "folly/ScopeGuard.h"

// Compatibility function, to make sure toStdString(s) can be called
// to convert a std::string or fbstring variable s into type std::string
// with very little overhead if s was already std::string
namespace folly {

inline
std::string toStdString(const folly::fbstring& s) {
  return std::string(s.data(), s.size());
}

inline
const std::string& toStdString(const std::string& s) {
  return s;
}

// If called with a temporary, the compiler will select this overload instead
// of the above, so we don't return a (lvalue) reference to a temporary.
inline
std::string&& toStdString(std::string&& s) {
  return std::move(s);
}

/**
 * C-Escape a string, making it suitable for representation as a C string
 * literal.  Appends the result to the output string.
 *
 * Backslashes all occurrences of backslash and double-quote:
 *   "  ->  \"
 *   \  ->  \\
 *
 * Replaces all non-printable ASCII characters with backslash-octal
 * representation:
 *   <ASCII 254> -> \376
 *
 * Note that we use backslash-octal instead of backslash-hex because the octal
 * representation is guaranteed to consume no more than 3 characters; "\3760"
 * represents two characters, one with value 254, and one with value 48 ('0'),
 * whereas "\xfe0" represents only one character (with value 4064, which leads
 * to implementation-defined behavior).
 */
template <class String>
void cEscape(StringPiece str, String& out);

/**
 * Similar to cEscape above, but returns the escaped string.
 */
template <class String>
String cEscape(StringPiece str) {
  String out;
  cEscape(str, out);
  return out;
}

/**
 * C-Unescape a string; the opposite of cEscape above.  Appends the result
 * to the output string.
 *
 * Recognizes the standard C escape sequences:
 *
 * \' \" \? \\ \a \b \f \n \r \t \v
 * \[0-7]+
 * \x[0-9a-fA-F]+
 *
 * In strict mode (default), throws std::invalid_argument if it encounters
 * an unrecognized escape sequence.  In non-strict mode, it leaves
 * the escape sequence unchanged.
 */
template <class String>
void cUnescape(StringPiece str, String& out, bool strict = true);

/**
 * Similar to cUnescape above, but returns the escaped string.
 */
template <class String>
String cUnescape(StringPiece str, bool strict = true) {
  String out;
  cUnescape(str, out, strict);
  return out;
}

/**
 * stringPrintf is much like printf but deposits its result into a
 * string. Two signatures are supported: the first simply returns the
 * resulting string, and the second appends the produced characters to
 * the specified string and returns a reference to it.
 */
std::string stringPrintf(const char* format, ...)
  __attribute__ ((format (printf, 1, 2)));

/** Similar to stringPrintf, with different signiture.
  */
void stringPrintf(std::string* out, const char* fmt, ...)
  __attribute__ ((format (printf, 2, 3)));

std::string& stringAppendf(std::string* output, const char* format, ...)
  __attribute__ ((format (printf, 2, 3)));

/**
 * Backslashify a string, that is, replace non-printable characters
 * with C-style (but NOT C compliant) "\xHH" encoding.  If hex_style
 * is false, then shorthand notations like "\0" will be used instead
 * of "\x00" for the most common backslash cases.
 *
 * There are two forms, one returning the input string, and one
 * creating output in the specified output string.
 *
 * This is mainly intended for printing to a terminal, so it is not
 * particularly optimized.
 *
 * Do *not* use this in situations where you expect to be able to feed
 * the string to a C or C++ compiler, as there are nuances with how C
 * parses such strings that lead to failures.  This is for display
 * purposed only.  If you want a string you can embed for use in C or
 * C++, use cEscape instead.  This function is for display purposes
 * only.
 */
template <class String1, class String2>
void backslashify(const String1& input, String2& output, bool hex_style=false);

template <class String>
String backslashify(const String& input, bool hex_style=false) {
  String output;
  backslashify(input, output, hex_style);
  return output;
}

/**
 * Take a string and "humanify" it -- that is, make it look better.
 * Since "better" is subjective, caveat emptor.  The basic approach is
 * to count the number of unprintable characters.  If there are none,
 * then the output is the input.  If there are relatively few, or if
 * there is a long "enough" prefix of printable characters, use
 * backslashify.  If it is mostly binary, then simply hex encode.
 *
 * This is an attempt to make a computer smart, and so likely is wrong
 * most of the time.
 */
template <class String1, class String2>
void humanify(const String1& input, String2& output);

template <class String>
String humanify(const String& input) {
  String output;
  humanify(input, output);
  return output;
}

/**
 * Same functionality as Python's binascii.hexlify.  Returns true
 * on successful conversion.
 *
 * If append_output is true, append data to the output rather than
 * replace it.
 */
template<class InputString, class OutputString>
bool hexlify(const InputString& input, OutputString& output,
             bool append=false);

/**
 * Same functionality as Python's binascii.unhexlify.  Returns true
 * on successful conversion.
 */
template<class InputString, class OutputString>
bool unhexlify(const InputString& input, OutputString& output);

/*
 * A pretty-printer for numbers that appends suffixes of units of the
 * given type.  It prints 4 sig-figs of value with the most
 * appropriate unit.
 *
 * If `addSpace' is true, we put a space between the units suffix and
 * the value.
 *
 * Current types are:
 *   PRETTY_TIME         - s, ms, us, ns, etc.
 *   PRETTY_BYTES_METRIC - kB, MB, GB, etc (goes up by 10^3 = 1000 each time)
 *   PRETTY_BYTES        - kB, MB, GB, etc (goes up by 2^10 = 1024 each time)
 *   PRETTY_BYTES_IEC    - KiB, MiB, GiB, etc
 *   PRETTY_UNITS_METRIC - k, M, G, etc (goes up by 10^3 = 1000 each time)
 *   PRETTY_UNITS_BINARY - k, M, G, etc (goes up by 2^10 = 1024 each time)
 *   PRETTY_UNITS_BINARY_IEC - Ki, Mi, Gi, etc
 *
 * @author Mark Rabkin <mrabkin@fb.com>
 */
enum PrettyType {
  PRETTY_TIME,

  PRETTY_BYTES_METRIC,
  PRETTY_BYTES_BINARY,
  PRETTY_BYTES = PRETTY_BYTES_BINARY,
  PRETTY_BYTES_BINARY_IEC,
  PRETTY_BYTES_IEC = PRETTY_BYTES_BINARY_IEC,

  PRETTY_UNITS_METRIC,
  PRETTY_UNITS_BINARY,
  PRETTY_UNITS_BINARY_IEC,

  PRETTY_NUM_TYPES
};

std::string prettyPrint(double val, PrettyType, bool addSpace = true);

/**
 * Write a hex dump of size bytes starting at ptr to out.
 *
 * The hex dump is formatted as follows:
 *
 * for the string "abcdefghijklmnopqrstuvwxyz\x02"
00000000  61 62 63 64 65 66 67 68  69 6a 6b 6c 6d 6e 6f 70  |abcdefghijklmnop|
00000010  71 72 73 74 75 76 77 78  79 7a 02                 |qrstuvwxyz.     |
 *
 * that is, we write 16 bytes per line, both as hex bytes and as printable
 * characters.  Non-printable characters are replaced with '.'
 * Lines are written to out one by one (one StringPiece at a time) without
 * delimiters.
 */
template <class OutIt>
void hexDump(const void* ptr, size_t size, OutIt out);

/**
 * Return the hex dump of size bytes starting at ptr as a string.
 */
std::string hexDump(const void* ptr, size_t size);

/**
 * Return a fbstring containing the description of the given errno value.
 * Takes care not to overwrite the actual system errno, so calling
 * errnoStr(errno) is valid.
 */
fbstring errnoStr(int err);

/**
 * Return the demangled (prettyfied) version of a C++ type.
 *
 * This function tries to produce a human-readable type, but the type name will
 * be returned unchanged in case of error or if demangling isn't supported on
 * your system.
 *
 * Use for debugging -- do not rely on demangle() returning anything useful.
 *
 * This function may allocate memory (and therefore throw).
 */
fbstring demangle(const char* name);
inline fbstring demangle(const std::type_info& type) {
  return demangle(type.name());
}

/**
 * Debug string for an exception: include type and what().
 */
inline fbstring exceptionStr(const std::exception& e) {
  return folly::to<fbstring>(demangle(typeid(e)), ": ", e.what());
}

inline fbstring exceptionStr(std::exception_ptr ep) {
  try {
    std::rethrow_exception(ep);
  } catch (const std::exception& e) {
    return exceptionStr(e);
  } catch (...) {
    return "<unknown exception>";
  }
}

/*
 * Split a string into a list of tokens by delimiter.
 *
 * The split interface here supports different output types, selected
 * at compile time: StringPiece, fbstring, or std::string.  If you are
 * using a vector to hold the output, it detects the type based on
 * what your vector contains.  If the output vector is not empty, split
 * will append to the end of the vector.
 *
 * You can also use splitTo() to write the output to an arbitrary
 * OutputIterator (e.g. std::inserter() on a std::set<>), in which
 * case you have to tell the function the type.  (Rationale:
 * OutputIterators don't have a value_type, so we can't detect the
 * type in splitTo without being told.)
 *
 * Examples:
 *
 *   std::vector<folly::StringPiece> v;
 *   folly::split(":", "asd:bsd", v);
 *
 *   std::set<StringPiece> s;
 *   folly::splitTo<StringPiece>(":", "asd:bsd:asd:csd",
 *    std::inserter(s, s.begin()));
 *
 * Split also takes a flag (ignoreEmpty) that indicates whether adjacent
 * delimiters should be treated as one single separator (ignoring empty tokens)
 * or not (generating empty tokens).
 */

template<class Delim, class String, class OutputType>
void split(const Delim& delimiter,
           const String& input,
           std::vector<OutputType>& out,
           bool ignoreEmpty = false);

template<class Delim, class String, class OutputType>
void split(const Delim& delimiter,
           const String& input,
           folly::fbvector<OutputType>& out,
           bool ignoreEmpty = false);

template<class OutputValueType, class Delim, class String,
         class OutputIterator>
void splitTo(const Delim& delimiter,
             const String& input,
             OutputIterator out,
             bool ignoreEmpty = false);

/*
 * Join list of tokens.
 *
 * Stores a string representation of tokens in the same order with
 * deliminer between each element.
 */

template <class Delim, class Iterator, class String>
void join(const Delim& delimiter,
          Iterator begin,
          Iterator end,
          String& output);

template <class Delim, class Container, class String>
void join(const Delim& delimiter,
          const Container& container,
          String& output) {
  join(delimiter, container.begin(), container.end(), output);
}

template <class Delim, class Container>
std::string join(const Delim& delimiter,
                 const Container& container) {
  std::string output;
  join(delimiter, container.begin(), container.end(), output);
  return output;
}

} // namespace folly

// Hash functions for string and fbstring usable with e.g. hash_map
#ifdef __GNUC__
namespace __gnu_cxx {

template <class C>
struct hash<folly::basic_fbstring<C> > : private hash<const C*> {
  size_t operator()(const folly::basic_fbstring<C> & s) const {
    return hash<const C*>::operator()(s.c_str());
  }
};

template <class C>
struct hash<std::basic_string<C> > : private hash<const C*> {
  size_t operator()(const std::basic_string<C> & s) const {
    return hash<const C*>::operator()(s.c_str());
  }
};

} // namespace __gnu_cxx
#endif

// Hook into boost's type traits
namespace boost {
template <class T>
struct has_nothrow_constructor<folly::basic_fbstring<T> > : true_type {
  enum { value = true };
};
} // namespace boost

#include "folly/String-inl.h"

#endif
