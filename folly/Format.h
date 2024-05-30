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

//
// Docs: https://fburl.com/fbcref_format
//

/**
 * folly::format has been superseded by
 * [fmt](https://fmt.dev/latest/index.html). `#include <fmt/core.h>`
 *
 * format() performs text-formatting, similar to Python's str.format. The full
 * specification is on github:
 * https://github.com/facebook/folly/blob/main/folly/docs/Format.md
 *
 * @refcode folly/docs/examples/folly/Format.cpp
 * @file Format.h
 */

#pragma once
#define FOLLY_FORMAT_H_

#include <cstdio>
#include <ios>
#include <stdexcept>
#include <tuple>
#include <type_traits>

#include <folly/CPortability.h>
#include <folly/Conv.h>
#include <folly/FormatArg.h>
#include <folly/Range.h>
#include <folly/String.h>
#include <folly/Traits.h>

// Ignore shadowing warnings within this file, so includers can use -Wshadow.
FOLLY_PUSH_WARNING
FOLLY_GNU_DISABLE_WARNING("-Wshadow")

namespace folly {

// forward declarations
template <bool containerMode, class... Args>
class Formatter;
template <class... Args>
Formatter<false, Args...> format(StringPiece fmt, Args&&... args);
template <class C>
std::string svformat(StringPiece fmt, C&& container);
template <class T, class Enable = void>
class FormatValue;

// meta-attribute to identify formatters in this sea of template weirdness
namespace detail {
class FormatterTag {};

struct BaseFormatterBase {
  template <class Callback>
  using DoFormatFn = void(const BaseFormatterBase&, FormatArg&, Callback&);

  StringPiece str_;

  static std::false_type recordUsedArg(const BaseFormatterBase&, size_t) {
    return {};
  }
};

// BaseFormatterTuple suffices and is faster to compile than is std::tuple
template <size_t I, typename A>
struct BaseFormatterTupleIndexedValue {
  A value;
};
template <typename, typename...>
struct BaseFormatterTuple;
template <size_t... I, typename... A>
struct BaseFormatterTuple<std::index_sequence<I...>, A...>
    : BaseFormatterTupleIndexedValue<I, A>... {
  explicit BaseFormatterTuple(std::in_place_t, A&&... a)
      : BaseFormatterTupleIndexedValue<I, A>{static_cast<A&&>(a)}... {}
};

template <typename Str>
struct BaseFormatterAppendToString {
  Str& str;
  void operator()(StringPiece s) const { str.append(s.data(), s.size()); }
};

inline void formatCheckIndex(size_t i, const FormatArg& arg, size_t max) {
  arg.enforce(i < max, "argument index out of range, max=", max);
}
} // namespace detail

/**
 * Formatter class.
 *
 * Note that this class is tricky, as it keeps *references* to its lvalue
 * arguments (while it takes ownership of the temporaries), and it doesn't
 * copy the passed-in format string. Thankfully, you can't use this
 * directly, you have to use format(...) below.
 */

/* BaseFormatter class.
 * Overridable behaviors:
 * You may override the actual formatting of positional parameters in
 * `doFormatArg`. The Formatter class provides the default implementation.
 *
 * You may also override `recordUsedArg`. This override point was added to
 * permit static analysis of format strings, when it is inconvenient or
 * impossible to instantiate a BaseFormatter with the correct storage. If
 * overriding, the return type must be std::true_type.
 */
template <class Derived, bool containerMode, class Indices, class... Args>
class BaseFormatterImpl;
template <class Derived, bool containerMode, size_t... I, class... Args>
class BaseFormatterImpl<
    Derived,
    containerMode,
    std::index_sequence<I...>,
    Args...> : public detail::BaseFormatterBase {
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
  typename std::enable_if<IsSomeString<Str>::value>::type appendTo(
      Str& str) const {
    detail::BaseFormatterAppendToString<Str> out{str};
    (*this)(out);
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
   * Metadata to identify generated children of BaseFormatter
   */
  typedef detail::FormatterTag IsFormatter;

 private:
  template <typename T, typename D = typename std::decay<T>::type>
  using IsSizeable = std::bool_constant<
      std::is_integral<D>::value && !std::is_same<D, bool>::value>;

  template <class Callback>
  static constexpr c_array<DoFormatFn<Callback>*, sizeof...(Args) + 1>
  getDoFormatFnArray() {
    return {{Derived::template doFormatArg<I, Callback>..., nullptr}};
  }

  template <size_t, typename>
  constexpr int getSizeArgAt(std::false_type) const {
    return -1;
  }
  template <size_t K, typename T>
  int getSizeArgAt(std::true_type) const {
    using V = detail::BaseFormatterTupleIndexedValue<K, T>;
    return static_cast<int>(static_cast<const V&>(values_).value);
  }
  void getSizeArg(int* out) const {
    using _ = int[];
    void(_{(out[I] = getSizeArgAt<I, Args>(IsSizeable<Args>{}))..., 0});
  }

 protected:
  explicit BaseFormatterImpl(StringPiece str, Args&&... args)
      : detail::BaseFormatterBase{str},
        values_(std::in_place, static_cast<Args&&>(args)...) {}

  // Not copyable
  BaseFormatterImpl(const BaseFormatterImpl&) = delete;
  BaseFormatterImpl& operator=(const BaseFormatterImpl&) = delete;

  // Movable, but the move constructor and assignment operator are private,
  // for the exclusive use of format() (below).  This way, you can't create
  // a Formatter object, but can handle references to it (for streaming,
  // conversion to string, etc) -- which is good, as Formatter objects are
  // dangerous (they may hold references).
  BaseFormatterImpl(BaseFormatterImpl&&) = default;
  BaseFormatterImpl& operator=(BaseFormatterImpl&&) = default;

  template <size_t K, typename T = type_pack_element_t<K, Args...>>
  FormatValue<typename std::decay<T>::type> getFormatValue() const {
    using V = detail::BaseFormatterTupleIndexedValue<K, T>;
    auto& v = static_cast<const V&>(values_);
    return FormatValue<typename std::decay<T>::type>(v.value);
  }

  detail::BaseFormatterTuple<std::index_sequence<I...>, Args...> values_;
};
template <class Derived, bool containerMode, class... Args>
using BaseFormatter = BaseFormatterImpl<
    Derived,
    containerMode,
    std::index_sequence_for<Args...>,
    Args...>;

template <bool containerMode, class... Args>
class Formatter : public BaseFormatter<
                      Formatter<containerMode, Args...>,
                      containerMode,
                      Args...> {
  using self = Formatter<containerMode, Args...>;
  using base = BaseFormatter<self, containerMode, Args...>;

  static_assert(
      !containerMode || sizeof...(Args) == 1,
      "Exactly one argument required in container mode");

 private:
  using base::base;

  template <size_t K, class Callback>
  static void doFormatArg(
      const detail::BaseFormatterBase& obj, FormatArg& arg, Callback& cb) {
    auto& d = static_cast<const Formatter&>(obj);
    d.template getFormatValue<K>().format(arg, cb);
  }

  friend base;

  template <class... A>
  friend Formatter<false, A...> format(StringPiece fmt, A&&... arg);
  template <class Str, class... A>
  friend typename std::enable_if<IsSomeString<Str>::value>::type format(
      Str* out, StringPiece fmt, A&&... args);
  template <class... A>
  friend std::string sformat(StringPiece fmt, A&&... arg);
  template <class C>
  friend std::string svformat(StringPiece fmt, C&& container);
};

namespace detail {
template <typename Out>
struct FormatterOstreamInsertionWriterFn {
  Out& out;
  void operator()(StringPiece sp) const {
    out.write(sp.data(), std::streamsize(sp.size()));
  }
};
} // namespace detail

/**
 * Formatter objects can be written to streams.
 */
template <class C, class CT, bool containerMode, class... Args>
std::ostream& operator<<(
    std::basic_ostream<C, CT>& out,
    const Formatter<containerMode, Args...>& formatter) {
  using out_t = std::basic_ostream<C, CT>;
  auto writer = detail::FormatterOstreamInsertionWriterFn<out_t>{out};
  formatter(writer);
  return out;
}

/**
 * Create a formatter object.
 *
 * std::string formatted = format("{} {}", 23, 42).str();
 * LOG(INFO) << format("{} {}", 23, 42);
 * writeTo(stdout, format("{} {}", 23, 42));
 */
template <class... Args>
[[deprecated(
    "Use fmt::format instead of folly::format for better performance, build "
    "times and compatibility with std::format")]] //
Formatter<false, Args...>
format(StringPiece fmt, Args&&... args) {
  return Formatter<false, Args...>(fmt, static_cast<Args&&>(args)...);
}

/**
 * Like format(), but immediately returns the formatted string instead of an
 * intermediate format object.
 */
template <class... Args>
inline std::string sformat(StringPiece fmt, Args&&... args) {
  return Formatter<false, Args...>(fmt, static_cast<Args&&>(args)...).str();
}

/**
 * Create a formatter object that takes one argument (of container type)
 * and uses that container to get argument values from.
 *
 * std::map<string, string> map { {"hello", "world"}, {"answer", "42"} };
 *
 * The following are equivalent:
 * sformat("{0[hello]} {0[answer]}", map);
 *
 * svformat("{hello} {answer}", map);
 *
 * but the latter is cleaner.
 */
template <class Container>
[[deprecated(
    "Use fmt::format instead of folly::svformat for better performance, build "
    "times and compatibility with std::format")]] //
inline std::string
svformat(StringPiece fmt, Container&& container) {
  return Formatter<true, Container>(fmt, static_cast<Container&&>(container))
      .str();
}

/**
 * Exception class thrown when a format key is not found in the given
 * associative container keyed by strings. We inherit std::out_of_range for
 * compatibility with callers that expect exception to be thrown directly
 * by std::map or std::unordered_map.
 *
 * Having the key be at the end of the message string, we can access it by
 * simply adding its offset to what(). Not storing separate std::string key
 * makes the exception type small and noexcept-copyable like std::out_of_range,
 * and therefore able to fit in-situ in exception_wrapper.
 */
class FOLLY_EXPORT FormatKeyNotFoundException : public std::out_of_range {
 public:
  explicit FormatKeyNotFoundException(StringPiece key);

  char const* key() const noexcept { return what() + kMessagePrefix.size(); }

 private:
  static constexpr StringPiece const kMessagePrefix = "format key not found: ";
};

/**
 * Wrap a sequence or associative container so that out-of-range lookups
 * return a default value rather than throwing an exception.
 *
 * Usage:
 * format("[no_such_key"], defaulted(map, 42))  -> 42
 */
namespace detail {
template <class Container, class Value>
struct DefaultValueWrapper {
  DefaultValueWrapper(const Container& container, const Value& defaultValue)
      : container(container), defaultValue(defaultValue) {}

  const Container& container;
  const Value& defaultValue;
};
} // namespace detail

template <class Container, class Value>
detail::DefaultValueWrapper<Container, Value> defaulted(
    const Container& c, const Value& v) {
  return detail::DefaultValueWrapper<Container, Value>(c, v);
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
typename std::enable_if<IsSomeString<Str>::value>::type format(
    Str* out, StringPiece fmt, Args&&... args) {
  Formatter<false, Args...>(fmt, static_cast<Args&&>(args)...).appendTo(*out);
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
void formatNumber(
    StringPiece val, int prefixLen, FormatArg& arg, FormatCallback& cb);

/**
 * Format a Formatter object recursively.  Behaves just like
 * formatString(fmt.str(), arg, cb); but avoids creating a temporary
 * string if possible.
 */
template <class FormatCallback, bool containerMode, class... Args>
void formatFormatter(
    const Formatter<containerMode, Args...>& formatter,
    FormatArg& arg,
    FormatCallback& cb);

} // namespace format_value

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

namespace detail {

template <class T, class Enable = void>
struct IsFormatter : public std::false_type {};

template <class T>
struct IsFormatter<
    T,
    typename std::enable_if<
        std::is_same<typename T::IsFormatter, detail::FormatterTag>::value>::
        type> : public std::true_type {};
} // namespace detail

} // namespace folly

#include <folly/Format-inl.h>

FOLLY_POP_WARNING
