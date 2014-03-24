/*
 * Copyright 2014 Facebook, Inc.
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

// @author Mark Rabkin (mrabkin@fb.com)
// @author Andrei Alexandrescu (andrei.alexandrescu@fb.com)

#ifndef FOLLY_RANGE_H_
#define FOLLY_RANGE_H_

#include "folly/Portability.h"
#include "folly/FBString.h"
#include <algorithm>
#include <boost/operators.hpp>
#include <cstring>
#include <glog/logging.h>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <type_traits>

// libc++ doesn't provide this header
#if !FOLLY_USE_LIBCPP
// This file appears in two locations: inside fbcode and in the
// libstdc++ source code (when embedding fbstring as std::string).
// To aid in this schizophrenic use, two macros are defined in
// c++config.h:
//   _LIBSTDCXX_FBSTRING - Set inside libstdc++.  This is useful to
//      gate use inside fbcode v. libstdc++
#include <bits/c++config.h>
#endif

#include "folly/CpuId.h"
#include "folly/Traits.h"
#include "folly/Likely.h"

// Ignore shadowing warnings within this file, so includers can use -Wshadow.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

namespace folly {

template <class T> class Range;

/**
 * Finds the first occurrence of needle in haystack. The algorithm is on
 * average faster than O(haystack.size() * needle.size()) but not as fast
 * as Boyer-Moore. On the upside, it does not do any upfront
 * preprocessing and does not allocate memory.
 */
template <class T, class Comp = std::equal_to<typename Range<T>::value_type>>
inline size_t qfind(const Range<T> & haystack,
                    const Range<T> & needle,
                    Comp eq = Comp());

/**
 * Finds the first occurrence of needle in haystack. The result is the
 * offset reported to the beginning of haystack, or string::npos if
 * needle wasn't found.
 */
template <class T>
size_t qfind(const Range<T> & haystack,
             const typename Range<T>::value_type& needle);

/**
 * Finds the last occurrence of needle in haystack. The result is the
 * offset reported to the beginning of haystack, or string::npos if
 * needle wasn't found.
 */
template <class T>
size_t rfind(const Range<T> & haystack,
             const typename Range<T>::value_type& needle);


/**
 * Finds the first occurrence of any element of needle in
 * haystack. The algorithm is O(haystack.size() * needle.size()).
 */
template <class T>
inline size_t qfind_first_of(const Range<T> & haystack,
                             const Range<T> & needle);

/**
 * Small internal helper - returns the value just before an iterator.
 */
namespace detail {

/**
 * For random-access iterators, the value before is simply i[-1].
 */
template <class Iter>
typename std::enable_if<
  std::is_same<typename std::iterator_traits<Iter>::iterator_category,
               std::random_access_iterator_tag>::value,
  typename std::iterator_traits<Iter>::reference>::type
value_before(Iter i) {
  return i[-1];
}

/**
 * For all other iterators, we need to use the decrement operator.
 */
template <class Iter>
typename std::enable_if<
  !std::is_same<typename std::iterator_traits<Iter>::iterator_category,
                std::random_access_iterator_tag>::value,
  typename std::iterator_traits<Iter>::reference>::type
value_before(Iter i) {
  return *--i;
}

} // namespace detail

/**
 * Range abstraction keeping a pair of iterators. We couldn't use
 * boost's similar range abstraction because we need an API identical
 * with the former StringPiece class, which is used by a lot of other
 * code. This abstraction does fulfill the needs of boost's
 * range-oriented algorithms though.
 *
 * (Keep memory lifetime in mind when using this class, since it
 * doesn't manage the data it refers to - just like an iterator
 * wouldn't.)
 */
template <class Iter>
class Range : private boost::totally_ordered<Range<Iter> > {
public:
  typedef std::size_t size_type;
  typedef Iter iterator;
  typedef Iter const_iterator;
  typedef typename std::remove_reference<
    typename std::iterator_traits<Iter>::reference>::type
  value_type;
  typedef typename std::iterator_traits<Iter>::reference reference;
  typedef std::char_traits<typename std::remove_const<value_type>::type>
    traits_type;

  static const size_type npos;

  // Works for all iterators
  Range() : b_(), e_() {
  }

public:
  // Works for all iterators
  Range(Iter start, Iter end) : b_(start), e_(end) {
  }

  // Works only for random-access iterators
  Range(Iter start, size_t size)
      : b_(start), e_(start + size) { }

#if FOLLY_HAVE_CONSTEXPR_STRLEN
  // Works only for Range<const char*>
  /* implicit */ constexpr Range(Iter str)
      : b_(str), e_(str + strlen(str)) {}
#else
  // Works only for Range<const char*>
  /* implicit */ Range(Iter str)
      : b_(str), e_(str + strlen(str)) {}
#endif
  // Works only for Range<const char*>
  /* implicit */ Range(const std::string& str)
      : b_(str.data()), e_(b_ + str.size()) {}

  // Works only for Range<const char*>
  Range(const std::string& str, std::string::size_type startFrom) {
    if (UNLIKELY(startFrom > str.size())) {
      throw std::out_of_range("index out of range");
    }
    b_ = str.data() + startFrom;
    e_ = str.data() + str.size();
  }
  // Works only for Range<const char*>
  Range(const std::string& str,
        std::string::size_type startFrom,
        std::string::size_type size) {
    if (UNLIKELY(startFrom > str.size())) {
      throw std::out_of_range("index out of range");
    }
    b_ = str.data() + startFrom;
    if (str.size() - startFrom < size) {
      e_ = str.data() + str.size();
    } else {
      e_ = b_ + size;
    }
  }
  Range(const Range<Iter>& str,
        size_t startFrom,
        size_t size) {
    if (UNLIKELY(startFrom > str.size())) {
      throw std::out_of_range("index out of range");
    }
    b_ = str.b_ + startFrom;
    if (str.size() - startFrom < size) {
      e_ = str.e_;
    } else {
      e_ = b_ + size;
    }
  }
  // Works only for Range<const char*>
  /* implicit */ Range(const fbstring& str)
    : b_(str.data()), e_(b_ + str.size()) { }
  // Works only for Range<const char*>
  Range(const fbstring& str, fbstring::size_type startFrom) {
    if (UNLIKELY(startFrom > str.size())) {
      throw std::out_of_range("index out of range");
    }
    b_ = str.data() + startFrom;
    e_ = str.data() + str.size();
  }
  // Works only for Range<const char*>
  Range(const fbstring& str, fbstring::size_type startFrom,
        fbstring::size_type size) {
    if (UNLIKELY(startFrom > str.size())) {
      throw std::out_of_range("index out of range");
    }
    b_ = str.data() + startFrom;
    if (str.size() - startFrom < size) {
      e_ = str.data() + str.size();
    } else {
      e_ = b_ + size;
    }
  }

  // Allow implicit conversion from Range<const char*> (aka StringPiece) to
  // Range<const unsigned char*> (aka ByteRange), as they're both frequently
  // used to represent ranges of bytes.  Allow explicit conversion in the other
  // direction.
  template <class OtherIter, typename std::enable_if<
      (std::is_same<Iter, const unsigned char*>::value &&
       (std::is_same<OtherIter, const char*>::value ||
        std::is_same<OtherIter, char*>::value)), int>::type = 0>
  /* implicit */ Range(const Range<OtherIter>& other)
    : b_(reinterpret_cast<const unsigned char*>(other.begin())),
      e_(reinterpret_cast<const unsigned char*>(other.end())) {
  }

  template <class OtherIter, typename std::enable_if<
      (std::is_same<Iter, unsigned char*>::value &&
       std::is_same<OtherIter, char*>::value), int>::type = 0>
  /* implicit */ Range(const Range<OtherIter>& other)
    : b_(reinterpret_cast<unsigned char*>(other.begin())),
      e_(reinterpret_cast<unsigned char*>(other.end())) {
  }

  template <class OtherIter, typename std::enable_if<
      (std::is_same<Iter, const char*>::value &&
       (std::is_same<OtherIter, const unsigned char*>::value ||
        std::is_same<OtherIter, unsigned char*>::value)), int>::type = 0>
  explicit Range(const Range<OtherIter>& other)
    : b_(reinterpret_cast<const char*>(other.begin())),
      e_(reinterpret_cast<const char*>(other.end())) {
  }

  template <class OtherIter, typename std::enable_if<
      (std::is_same<Iter, char*>::value &&
       std::is_same<OtherIter, unsigned char*>::value), int>::type = 0>
  explicit Range(const Range<OtherIter>& other)
    : b_(reinterpret_cast<char*>(other.begin())),
      e_(reinterpret_cast<char*>(other.end())) {
  }

  // Allow implicit conversion from Range<From> to Range<To> if From is
  // implicitly convertible to To.
  template <class OtherIter, typename std::enable_if<
     (!std::is_same<Iter, OtherIter>::value &&
      std::is_convertible<OtherIter, Iter>::value), int>::type = 0>
  /* implicit */ Range(const Range<OtherIter>& other)
    : b_(other.begin()),
      e_(other.end()) {
  }

  // Allow explicit conversion from Range<From> to Range<To> if From is
  // explicitly convertible to To.
  template <class OtherIter, typename std::enable_if<
    (!std::is_same<Iter, OtherIter>::value &&
     !std::is_convertible<OtherIter, Iter>::value &&
     std::is_constructible<Iter, const OtherIter&>::value), int>::type = 0>
  explicit Range(const Range<OtherIter>& other)
    : b_(other.begin()),
      e_(other.end()) {
  }

  void clear() {
    b_ = Iter();
    e_ = Iter();
  }

  void assign(Iter start, Iter end) {
    b_ = start;
    e_ = end;
  }

  void reset(Iter start, size_type size) {
    b_ = start;
    e_ = start + size;
  }

  // Works only for Range<const char*>
  void reset(const std::string& str) {
    reset(str.data(), str.size());
  }

  size_type size() const {
    assert(b_ <= e_);
    return e_ - b_;
  }
  size_type walk_size() const {
    assert(b_ <= e_);
    return std::distance(b_, e_);
  }
  bool empty() const { return b_ == e_; }
  Iter data() const { return b_; }
  Iter start() const { return b_; }
  Iter begin() const { return b_; }
  Iter end() const { return e_; }
  Iter cbegin() const { return b_; }
  Iter cend() const { return e_; }
  value_type& front() {
    assert(b_ < e_);
    return *b_;
  }
  value_type& back() {
    assert(b_ < e_);
    return detail::value_before(e_);
  }
  const value_type& front() const {
    assert(b_ < e_);
    return *b_;
  }
  const value_type& back() const {
    assert(b_ < e_);
    return detail::value_before(e_);
  }
  // Works only for Range<const char*>
  std::string str() const { return std::string(b_, size()); }
  std::string toString() const { return str(); }
  // Works only for Range<const char*>
  fbstring fbstr() const { return fbstring(b_, size()); }
  fbstring toFbstring() const { return fbstr(); }

  // Works only for Range<const char*>
  int compare(const Range& o) const {
    const size_type tsize = this->size();
    const size_type osize = o.size();
    const size_type msize = std::min(tsize, osize);
    int r = traits_type::compare(data(), o.data(), msize);
    if (r == 0) r = tsize - osize;
    return r;
  }

  value_type& operator[](size_t i) {
    DCHECK_GT(size(), i);
    return b_[i];
  }

  const value_type& operator[](size_t i) const {
    DCHECK_GT(size(), i);
    return b_[i];
  }

  value_type& at(size_t i) {
    if (i >= size()) throw std::out_of_range("index out of range");
    return b_[i];
  }

  const value_type& at(size_t i) const {
    if (i >= size()) throw std::out_of_range("index out of range");
    return b_[i];
  }

  // Works only for Range<const char*>
  uint32_t hash() const {
    // Taken from fbi/nstring.h:
    //    Quick and dirty bernstein hash...fine for short ascii strings
    uint32_t hash = 5381;
    for (size_t ix = 0; ix < size(); ix++) {
      hash = ((hash << 5) + hash) + b_[ix];
    }
    return hash;
  }

  void advance(size_type n) {
    if (UNLIKELY(n > size())) {
      throw std::out_of_range("index out of range");
    }
    b_ += n;
  }

  void subtract(size_type n) {
    if (UNLIKELY(n > size())) {
      throw std::out_of_range("index out of range");
    }
    e_ -= n;
  }

  void pop_front() {
    assert(b_ < e_);
    ++b_;
  }

  void pop_back() {
    assert(b_ < e_);
    --e_;
  }

  Range subpiece(size_type first,
                 size_type length = std::string::npos) const {
    if (UNLIKELY(first > size())) {
      throw std::out_of_range("index out of range");
    }
    return Range(b_ + first,
                 std::min<std::string::size_type>(length, size() - first));
  }

  // string work-alike functions
  size_type find(Range str) const {
    return qfind(*this, str);
  }

  size_type find(Range str, size_t pos) const {
    if (pos > size()) return std::string::npos;
    size_t ret = qfind(subpiece(pos), str);
    return ret == npos ? ret : ret + pos;
  }

  size_type find(Iter s, size_t pos, size_t n) const {
    if (pos > size()) return std::string::npos;
    size_t ret = qfind(pos ? subpiece(pos) : *this, Range(s, n));
    return ret == npos ? ret : ret + pos;
  }

  // Works only for Range<const (unsigned) char*> which have Range(Iter) ctor
  size_type find(const Iter s) const {
    return qfind(*this, Range(s));
  }

  // Works only for Range<const (unsigned) char*> which have Range(Iter) ctor
  size_type find(const Iter s, size_t pos) const {
    if (pos > size()) return std::string::npos;
    size_type ret = qfind(subpiece(pos), Range(s));
    return ret == npos ? ret : ret + pos;
  }

  size_type find(value_type c) const {
    return qfind(*this, c);
  }

  size_type rfind(value_type c) const {
    return folly::rfind(*this, c);
  }

  size_type find(value_type c, size_t pos) const {
    if (pos > size()) return std::string::npos;
    size_type ret = qfind(subpiece(pos), c);
    return ret == npos ? ret : ret + pos;
  }

  size_type find_first_of(Range needles) const {
    return qfind_first_of(*this, needles);
  }

  size_type find_first_of(Range needles, size_t pos) const {
    if (pos > size()) return std::string::npos;
    size_type ret = qfind_first_of(subpiece(pos), needles);
    return ret == npos ? ret : ret + pos;
  }

  // Works only for Range<const (unsigned) char*> which have Range(Iter) ctor
  size_type find_first_of(Iter needles) const {
    return find_first_of(Range(needles));
  }

  // Works only for Range<const (unsigned) char*> which have Range(Iter) ctor
  size_type find_first_of(Iter needles, size_t pos) const {
    return find_first_of(Range(needles), pos);
  }

  size_type find_first_of(Iter needles, size_t pos, size_t n) const {
    return find_first_of(Range(needles, n), pos);
  }

  size_type find_first_of(value_type c) const {
    return find(c);
  }

  size_type find_first_of(value_type c, size_t pos) const {
    return find(c, pos);
  }

  /**
   * Determine whether the range contains the given subrange or item.
   *
   * Note: Call find() directly if the index is needed.
   */
  bool contains(const Range& other) const {
    return find(other) != std::string::npos;
  }

  bool contains(const value_type& other) const {
    return find(other) != std::string::npos;
  }

  void swap(Range& rhs) {
    std::swap(b_, rhs.b_);
    std::swap(e_, rhs.e_);
  }

  /**
   * Does this Range start with another range?
   */
  bool startsWith(const Range& other) const {
    return size() >= other.size() && subpiece(0, other.size()) == other;
  }
  bool startsWith(value_type c) const {
    return !empty() && front() == c;
  }

  /**
   * Does this Range end with another range?
   */
  bool endsWith(const Range& other) const {
    return size() >= other.size() && subpiece(size() - other.size()) == other;
  }
  bool endsWith(value_type c) const {
    return !empty() && back() == c;
  }

  /**
   * Remove the given prefix and return true if the range starts with the given
   * prefix; return false otherwise.
   */
  bool removePrefix(const Range& prefix) {
    return startsWith(prefix) && (b_ += prefix.size(), true);
  }
  bool removePrefix(value_type prefix) {
    return startsWith(prefix) && (++b_, true);
  }

  /**
   * Remove the given suffix and return true if the range ends with the given
   * suffix; return false otherwise.
   */
  bool removeSuffix(const Range& suffix) {
    return endsWith(suffix) && (e_ -= suffix.size(), true);
  }
  bool removeSuffix(value_type suffix) {
    return endsWith(suffix) && (--e_, true);
  }

  /**
   * Splits this `Range` `[b, e)` in the position `i` dictated by the next
   * occurence of `delimiter`.
   *
   * Returns a new `Range` `[b, i)` and adjusts this range to start right after
   * the delimiter's position. This range will be empty if the delimiter is not
   * found. If called on an empty `Range`, both this and the returned `Range`
   * will be empty.
   *
   * Example:
   *
   *  folly::StringPiece s("sample string for split_next");
   *  auto p = s.split_step(' ');
   *
   *  // prints "sample"
   *  cout << s << endl;
   *
   *  // prints "string for split_next"
   *  cout << p << endl;
   *
   * Example 2:
   *
   *  void tokenize(StringPiece s, char delimiter) {
   *    while (!s.empty()) {
   *      cout << s.split_step(delimiter);
   *    }
   *  }
   *
   * @author: Marcelo Juchem <marcelo@fb.com>
   */
  Range split_step(value_type delimiter) {
    auto i = std::find(b_, e_, delimiter);
    Range result(b_, i);

    b_ = i == e_ ? e_ : std::next(i);

    return result;
  }

  Range split_step(Range delimiter) {
    auto i = find(delimiter);
    Range result(b_, i == std::string::npos ? size() : i);

    b_ = result.end() == e_ ? e_ : std::next(result.end(), delimiter.size());

    return result;
  }

  /**
   * Convenience method that calls `split_step()` and passes the result to a
   * functor, returning whatever the functor does.
   *
   * Say you have a functor with this signature:
   *
   *  Foo fn(Range r) { }
   *
   * `split_step()`'s return type will be `Foo`. It works just like:
   *
   *  auto result = fn(myRange.split_step(' '));
   *
   * A functor returning `void` is also supported.
   *
   * Example:
   *
   *  void do_some_parsing(folly::StringPiece s) {
   *    auto version = s.split_step(' ', [&](folly::StringPiece x) {
   *      if (x.empty()) {
   *        throw std::invalid_argument("empty string");
   *      }
   *      return std::strtoull(x.begin(), x.end(), 16);
   *    });
   *
   *    // ...
   *  }
   *
   * @author: Marcelo Juchem <marcelo@fb.com>
   */
  template <typename TProcess>
  auto split_step(value_type delimiter, TProcess &&process)
    -> decltype(process(std::declval<Range>()))
  { return process(split_step(delimiter)); }

  template <typename TProcess>
  auto split_step(Range delimiter, TProcess &&process)
    -> decltype(process(std::declval<Range>()))
  { return process(split_step(delimiter)); }

private:
  Iter b_, e_;
};

template <class Iter>
const typename Range<Iter>::size_type Range<Iter>::npos = std::string::npos;

template <class T>
void swap(Range<T>& lhs, Range<T>& rhs) {
  lhs.swap(rhs);
}

/**
 * Create a range from two iterators, with type deduction.
 */
template <class Iter>
Range<Iter> range(Iter first, Iter last) {
  return Range<Iter>(first, last);
}

/*
 * Creates a range to reference the contents of a contiguous-storage container.
 */
// Use pointers for types with '.data()' member
template <class Collection,
          class T = typename std::remove_pointer<
              decltype(std::declval<Collection>().data())>::type>
Range<T*> range(Collection&& v) {
  return Range<T*>(v.data(), v.data() + v.size());
}

template <class T, size_t n>
Range<T*> range(T (&array)[n]) {
  return Range<T*>(array, array + n);
}

typedef Range<const char*> StringPiece;
typedef Range<char*> MutableStringPiece;
typedef Range<const unsigned char*> ByteRange;
typedef Range<unsigned char*> MutableByteRange;

std::ostream& operator<<(std::ostream& os, const StringPiece& piece);

/**
 * Templated comparison operators
 */

template <class T>
inline bool operator==(const Range<T>& lhs, const Range<T>& rhs) {
  return lhs.size() == rhs.size() && lhs.compare(rhs) == 0;
}

template <class T>
inline bool operator<(const Range<T>& lhs, const Range<T>& rhs) {
  return lhs.compare(rhs) < 0;
}

/**
 * Specializations of comparison operators for StringPiece
 */

namespace detail {

template <class A, class B>
struct ComparableAsStringPiece {
  enum {
    value =
    (std::is_convertible<A, StringPiece>::value
     && std::is_same<B, StringPiece>::value)
    ||
    (std::is_convertible<B, StringPiece>::value
     && std::is_same<A, StringPiece>::value)
  };
};

} // namespace detail

/**
 * operator== through conversion for Range<const char*>
 */
template <class T, class U>
typename
std::enable_if<detail::ComparableAsStringPiece<T, U>::value, bool>::type
operator==(const T& lhs, const U& rhs) {
  return StringPiece(lhs) == StringPiece(rhs);
}

/**
 * operator< through conversion for Range<const char*>
 */
template <class T, class U>
typename
std::enable_if<detail::ComparableAsStringPiece<T, U>::value, bool>::type
operator<(const T& lhs, const U& rhs) {
  return StringPiece(lhs) < StringPiece(rhs);
}

/**
 * operator> through conversion for Range<const char*>
 */
template <class T, class U>
typename
std::enable_if<detail::ComparableAsStringPiece<T, U>::value, bool>::type
operator>(const T& lhs, const U& rhs) {
  return StringPiece(lhs) > StringPiece(rhs);
}

/**
 * operator< through conversion for Range<const char*>
 */
template <class T, class U>
typename
std::enable_if<detail::ComparableAsStringPiece<T, U>::value, bool>::type
operator<=(const T& lhs, const U& rhs) {
  return StringPiece(lhs) <= StringPiece(rhs);
}

/**
 * operator> through conversion for Range<const char*>
 */
template <class T, class U>
typename
std::enable_if<detail::ComparableAsStringPiece<T, U>::value, bool>::type
operator>=(const T& lhs, const U& rhs) {
  return StringPiece(lhs) >= StringPiece(rhs);
}

struct StringPieceHash {
  std::size_t operator()(const StringPiece& str) const {
    return static_cast<std::size_t>(str.hash());
  }
};

/**
 * Finds substrings faster than brute force by borrowing from Boyer-Moore
 */
template <class T, class Comp>
size_t qfind(const Range<T>& haystack,
             const Range<T>& needle,
             Comp eq) {
  // Don't use std::search, use a Boyer-Moore-like trick by comparing
  // the last characters first
  auto const nsize = needle.size();
  if (haystack.size() < nsize) {
    return std::string::npos;
  }
  if (!nsize) return 0;
  auto const nsize_1 = nsize - 1;
  auto const lastNeedle = needle[nsize_1];

  // Boyer-Moore skip value for the last char in the needle. Zero is
  // not a valid value; skip will be computed the first time it's
  // needed.
  std::string::size_type skip = 0;

  auto i = haystack.begin();
  auto iEnd = haystack.end() - nsize_1;

  while (i < iEnd) {
    // Boyer-Moore: match the last element in the needle
    while (!eq(i[nsize_1], lastNeedle)) {
      if (++i == iEnd) {
        // not found
        return std::string::npos;
      }
    }
    // Here we know that the last char matches
    // Continue in pedestrian mode
    for (size_t j = 0; ; ) {
      assert(j < nsize);
      if (!eq(i[j], needle[j])) {
        // Not found, we can skip
        // Compute the skip value lazily
        if (skip == 0) {
          skip = 1;
          while (skip <= nsize_1 && !eq(needle[nsize_1 - skip], lastNeedle)) {
            ++skip;
          }
        }
        i += skip;
        break;
      }
      // Check if done searching
      if (++j == nsize) {
        // Yay
        return i - haystack.begin();
      }
    }
  }
  return std::string::npos;
}

namespace detail {

size_t qfind_first_byte_of_nosse(const StringPiece& haystack,
                                 const StringPiece& needles);

#if FOLLY_HAVE_EMMINTRIN_H && __GNUC_PREREQ(4, 6)
size_t qfind_first_byte_of_sse42(const StringPiece& haystack,
                                 const StringPiece& needles);

inline size_t qfind_first_byte_of(const StringPiece& haystack,
                                  const StringPiece& needles) {
  static auto const qfind_first_byte_of_fn =
    folly::CpuId().sse42() ? qfind_first_byte_of_sse42
                           : qfind_first_byte_of_nosse;
  return qfind_first_byte_of_fn(haystack, needles);
}

#else
inline size_t qfind_first_byte_of(const StringPiece& haystack,
                                  const StringPiece& needles) {
  return qfind_first_byte_of_nosse(haystack, needles);
}
#endif // FOLLY_HAVE_EMMINTRIN_H

} // namespace detail

template <class T, class Comp>
size_t qfind_first_of(const Range<T> & haystack,
                      const Range<T> & needles,
                      Comp eq) {
  auto ret = std::find_first_of(haystack.begin(), haystack.end(),
                                needles.begin(), needles.end(),
                                eq);
  return ret == haystack.end() ? std::string::npos : ret - haystack.begin();
}

struct AsciiCaseSensitive {
  bool operator()(char lhs, char rhs) const {
    return lhs == rhs;
  }
};

/**
 * Check if two ascii characters are case insensitive equal.
 * The difference between the lower/upper case characters are the 6-th bit.
 * We also check they are alpha chars, in case of xor = 32.
 */
struct AsciiCaseInsensitive {
  bool operator()(char lhs, char rhs) const {
    char k = lhs ^ rhs;
    if (k == 0) return true;
    if (k != 32) return false;
    k = lhs | rhs;
    return (k >= 'a' && k <= 'z');
  }
};

extern const AsciiCaseSensitive asciiCaseSensitive;
extern const AsciiCaseInsensitive asciiCaseInsensitive;

template <class T>
size_t qfind(const Range<T>& haystack,
             const typename Range<T>::value_type& needle) {
  auto pos = std::find(haystack.begin(), haystack.end(), needle);
  return pos == haystack.end() ? std::string::npos : pos - haystack.data();
}

template <class T>
size_t rfind(const Range<T>& haystack,
             const typename Range<T>::value_type& needle) {
  for (auto i = haystack.size(); i-- > 0; ) {
    if (haystack[i] == needle) {
      return i;
    }
  }
  return std::string::npos;
}

// specialization for StringPiece
template <>
inline size_t qfind(const Range<const char*>& haystack, const char& needle) {
  auto pos = static_cast<const char*>(
    ::memchr(haystack.data(), needle, haystack.size()));
  return pos == nullptr ? std::string::npos : pos - haystack.data();
}

#if FOLLY_HAVE_MEMRCHR
template <>
inline size_t rfind(const Range<const char*>& haystack, const char& needle) {
  auto pos = static_cast<const char*>(
    ::memrchr(haystack.data(), needle, haystack.size()));
  return pos == nullptr ? std::string::npos : pos - haystack.data();
}
#endif

// specialization for ByteRange
template <>
inline size_t qfind(const Range<const unsigned char*>& haystack,
                    const unsigned char& needle) {
  auto pos = static_cast<const unsigned char*>(
    ::memchr(haystack.data(), needle, haystack.size()));
  return pos == nullptr ? std::string::npos : pos - haystack.data();
}

#if FOLLY_HAVE_MEMRCHR
template <>
inline size_t rfind(const Range<const unsigned char*>& haystack,
                    const unsigned char& needle) {
  auto pos = static_cast<const unsigned char*>(
    ::memrchr(haystack.data(), needle, haystack.size()));
  return pos == nullptr ? std::string::npos : pos - haystack.data();
}
#endif

template <class T>
size_t qfind_first_of(const Range<T>& haystack,
                      const Range<T>& needles) {
  return qfind_first_of(haystack, needles, asciiCaseSensitive);
}

// specialization for StringPiece
template <>
inline size_t qfind_first_of(const Range<const char*>& haystack,
                             const Range<const char*>& needles) {
  return detail::qfind_first_byte_of(haystack, needles);
}

// specialization for ByteRange
template <>
inline size_t qfind_first_of(const Range<const unsigned char*>& haystack,
                             const Range<const unsigned char*>& needles) {
  return detail::qfind_first_byte_of(StringPiece(haystack),
                                     StringPiece(needles));
}
}  // !namespace folly

#pragma GCC diagnostic pop

FOLLY_ASSUME_FBVECTOR_COMPATIBLE_1(folly::Range);

#endif // FOLLY_RANGE_H_
