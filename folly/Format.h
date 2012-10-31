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

#ifndef FOLLY_FORMAT_H_
#define FOLLY_FORMAT_H_

#include <array>
#include <tuple>
#include <type_traits>
#include <vector>
#include <deque>
#include <map>
#include <unordered_map>

#include <double-conversion.h>

#include "folly/FBVector.h"
#include "folly/Conv.h"
#include "folly/Range.h"
#include "folly/Likely.h"
#include "folly/String.h"
#include "folly/small_vector.h"
#include "folly/FormatArg.h"

namespace folly {

// forward declarations
template <bool containerMode, class... Args> class Formatter;
template <class... Args>
Formatter<false, Args...> format(StringPiece fmt, Args&&... args);
template <class C>
Formatter<true, C> vformat(StringPiece fmt, C&& container);
template <class T, class Enable=void> class FormatValue;

/**
 * Formatter class.
 *
 * Note that this class is tricky, as it keeps *references* to its arguments
 * (and doesn't copy the passed-in format string).  Thankfully, you can't use
 * this directly, you have to use format(...) below.
 */

template <bool containerMode, class... Args>
class Formatter {
  template <class... A>
  friend Formatter<false, A...> format(StringPiece fmt, A&&... arg);
  template <class C>
  friend Formatter<true, C> vformat(StringPiece fmt, C&& container);
 public:
  /**
   * Append to output.  out(StringPiece sp) may be called (more than once)
   */
  template <class Output>
  void operator()(Output& out) const;

  /**
   * Append to a string.
   */
  template <class Str>
  typename std::enable_if<detail::IsSomeString<Str>::value>::type
  appendTo(Str& str) const {
    auto appender = [&str] (StringPiece s) { str.append(s.data(), s.size()); };
    (*this)(appender);
  }

  /**
   * Conversion to string
   */
  std::string str() const {
    std::string s;
    appendTo(s);
    return s;
  }

  /**
   * Conversion to fbstring
   */
  fbstring fbstr() const {
    fbstring s;
    appendTo(s);
    return s;
  }

 private:
  explicit Formatter(StringPiece str, Args&&... args);

  // Not copyable
  Formatter(const Formatter&) = delete;
  Formatter& operator=(const Formatter&) = delete;

  // Movable, but the move constructor and assignment operator are private,
  // for the exclusive use of format() (below).  This way, you can't create
  // a Formatter object, but can handle references to it (for streaming,
  // conversion to string, etc) -- which is good, as Formatter objects are
  // dangerous (they hold references, possibly to temporaries)
  Formatter(Formatter&&) = default;
  Formatter& operator=(Formatter&&) = default;

  typedef std::tuple<FormatValue<
      typename std::decay<Args>::type>...> ValueTuple;
  static constexpr size_t valueCount = std::tuple_size<ValueTuple>::value;

  template <size_t K, class Callback>
  typename std::enable_if<K == valueCount>::type
  doFormatFrom(size_t i, FormatArg& arg, Callback& cb) const {
    arg.error("argument index out of range, max=", i);
  }

  template <size_t K, class Callback>
  typename std::enable_if<(K < valueCount)>::type
  doFormatFrom(size_t i, FormatArg& arg, Callback& cb) const {
    if (i == K) {
      std::get<K>(values_).format(arg, cb);
    } else {
      doFormatFrom<K+1>(i, arg, cb);
    }
  }

  template <class Callback>
  void doFormat(size_t i, FormatArg& arg, Callback& cb) const {
    return doFormatFrom<0>(i, arg, cb);
  }

  StringPiece str_;
  ValueTuple values_;
};

/**
 * Formatter objects can be written to streams.
 */
template<bool containerMode, class... Args>
std::ostream& operator<<(std::ostream& out,
                         const Formatter<containerMode, Args...>& formatter) {
  auto writer = [&out] (StringPiece sp) { out.write(sp.data(), sp.size()); };
  formatter(writer);
  return out;
}

/**
 * Create a formatter object.
 *
 * std::string formatted = format("{} {}", 23, 42).str();
 * LOG(INFO) << format("{} {}", 23, 42);
 */
template <class... Args>
Formatter<false, Args...> format(StringPiece fmt, Args&&... args) {
  return Formatter<false, Args...>(
      fmt, std::forward<Args>(args)...);
}

/**
 * Create a formatter object that takes one argument (of container type)
 * and uses that container to get argument values from.
 *
 * std::map<string, string> map { {"hello", "world"}, {"answer", "42"} };
 *
 * The following are equivalent:
 * format("{0[hello]} {0[answer]}", map);
 *
 * vformat("{hello} {answer}", map);
 *
 * but the latter is cleaner.
 */
template <class Container>
Formatter<true, Container> vformat(StringPiece fmt, Container&& container) {
  return Formatter<true, Container>(
      fmt, std::forward<Container>(container));
}

/**
 * Append formatted output to a string.
 *
 * std::string foo;
 * format(&foo, "{} {}", 42, 23);
 *
 * Shortcut for toAppend(format(...), &foo);
 */
template <class Str, class... Args>
typename std::enable_if<detail::IsSomeString<Str>::value>::type
format(Str* out, StringPiece fmt, Args&&... args) {
  format(fmt, std::forward<Args>(args)...).appendTo(*out);
}

/**
 * Append vformatted output to a string.
 */
template <class Str, class Container>
typename std::enable_if<detail::IsSomeString<Str>::value>::type
vformat(Str* out, StringPiece fmt, Container&& container) {
  vformat(fmt, std::forward<Container>(container)).appendTo(*out);
}

/**
 * Utilities for all format value specializations.
 */
namespace format_value {

/**
 * Format a string in "val", obeying appropriate alignment, padding, width,
 * and precision.  Treats Align::DEFAULT as Align::LEFT, and
 * Align::PAD_AFTER_SIGN as Align::RIGHT; use formatNumber for
 * number-specific formatting.
 */
template <class FormatCallback>
void formatString(StringPiece val, FormatArg& arg, FormatCallback& cb);

/**
 * Format a number in "val"; the first prefixLen characters form the prefix
 * (sign, "0x" base prefix, etc) which must be left-aligned if the alignment
 * is Align::PAD_AFTER_SIGN.  Treats Align::DEFAULT as Align::LEFT.  Ignores
 * arg.precision, as that has a different meaning for numbers (not "maximum
 * field width")
 */
template <class FormatCallback>
void formatNumber(StringPiece val, int prefixLen, FormatArg& arg,
                  FormatCallback& cb);


/**
 * Format a Formatter object recursively.  Behaves just like
 * formatString(fmt.str(), arg, cb); but avoids creating a temporary
 * string if possible.
 */
template <class FormatCallback, bool containerMode, class... Args>
void formatFormatter(const Formatter<containerMode, Args...>& formatter,
                     FormatArg& arg,
                     FormatCallback& cb);

}  // namespace format_value

/*
 * Specialize folly::FormatValue for your type.
 *
 * FormatValue<T> is constructed with a (reference-collapsed) T&&, which is
 * guaranteed to stay alive until the FormatValue object is destroyed, so you
 * may keep a reference (or pointer) to it instead of making a copy.
 *
 * You must define
 *   template <class Callback>
 *   void format(FormatArg& arg, Callback& cb) const;
 * with the following semantics: format the value using the given argument.
 *
 * arg is given by non-const reference for convenience -- it won't be reused,
 * so feel free to modify it in place if necessary.  (For example, wrap an
 * existing conversion but change the default, or remove the "key" when
 * extracting an element from a container)
 *
 * Call the callback to append data to the output.  You may call the callback
 * as many times as you'd like (or not at all, if you want to output an
 * empty string)
 */

}  // namespace folly

#include "folly/Format-inl.h"

#endif /* FOLLY_FORMAT_H_ */

