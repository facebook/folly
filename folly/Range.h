/*
 * Copyright 2013 Facebook, Inc.
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
#include <glog/logging.h>
#include <algorithm>
#include <cstring>
#include <iosfwd>
#include <string>
#include <stdexcept>
#include <type_traits>
#include <boost/operators.hpp>
#include <bits/c++config.h>
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

  // Works only for Range<const char*>
  /* implicit */ Range(Iter str)
      : b_(str), e_(b_ + strlen(str)) {}
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
       std::is_same<OtherIter, const char*>::value), int>::type = 0>
  /* implicit */ Range(const Range<OtherIter>& other)
    : b_(reinterpret_cast<const unsigned char*>(other.begin())),
      e_(reinterpret_cast<const unsigned char*>(other.end())) {
  }

  template <class OtherIter, typename std::enable_if<
      (std::is_same<Iter, const char*>::value &&
       std::is_same<OtherIter, const unsigned char*>::value), int>::type = 0>
  explicit Range(const Range<OtherIter>& other)
    : b_(reinterpret_cast<const char*>(other.begin())),
      e_(reinterpret_cast<const char*>(other.end())) {
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
    CHECK_GT(size(), i);
    return b_[i];
  }

  const value_type& operator[](size_t i) const {
    CHECK_GT(size(), i);
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

  void swap(Range& rhs) {
    std::swap(b_, rhs.b_);
    std::swap(e_, rhs.e_);
  }

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
Range<Iter> makeRange(Iter first, Iter last) {
  return Range<Iter>(first, last);
}

typedef Range<const char*> StringPiece;
typedef Range<const unsigned char*> ByteRange;

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

#if FOLLY_HAVE_EMMINTRIN_H
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

struct AsciiCaseInsensitive {
  bool operator()(char lhs, char rhs) const {
    return toupper(lhs) == toupper(rhs);
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
