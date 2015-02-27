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

// @author: Andrei Alexandrescu (aalexandre)
// String type.

#ifndef FOLLY_BASE_FBSTRING_H_
#define FOLLY_BASE_FBSTRING_H_

#include <atomic>
#include <limits>
#include <type_traits>

// This file appears in two locations: inside fbcode and in the
// libstdc++ source code (when embedding fbstring as std::string).
// To aid in this schizophrenic use, _LIBSTDCXX_FBSTRING is defined in
// libstdc++'s c++config.h, to gate use inside fbcode v. libstdc++.
#ifdef _LIBSTDCXX_FBSTRING

#pragma GCC system_header

// Handle the cases where the fbcode version (folly/Malloc.h) is included
// either before or after this inclusion.
#ifdef FOLLY_MALLOC_H_
#undef FOLLY_MALLOC_H_
#include "basic_fbstring_malloc.h" // nolint
#else
#include "basic_fbstring_malloc.h" // nolint
#undef FOLLY_MALLOC_H_
#endif

#else // !_LIBSTDCXX_FBSTRING

#include <folly/Portability.h>

// libc++ doesn't provide this header, nor does msvc
#ifdef FOLLY_HAVE_BITS_CXXCONFIG_H
#include <bits/c++config.h>
#endif

#include <string>
#include <cstring>
#include <cassert>
#include <algorithm>

#include <folly/Traits.h>
#include <folly/Malloc.h>
#include <folly/Hash.h>
#include <folly/ScopeGuard.h>

#if FOLLY_HAVE_DEPRECATED_ASSOC
#ifdef _GLIBCXX_SYMVER
#include <ext/hash_set>
#include <ext/hash_map>
#endif
#endif

#endif

// We defined these here rather than including Likely.h to avoid
// redefinition errors when fbstring is imported into libstdc++.
#if defined(__GNUC__) && __GNUC__ >= 4
#define FBSTRING_LIKELY(x)   (__builtin_expect((x), 1))
#define FBSTRING_UNLIKELY(x) (__builtin_expect((x), 0))
#else
#define FBSTRING_LIKELY(x)   (x)
#define FBSTRING_UNLIKELY(x) (x)
#endif

// Ignore shadowing warnings within this file, so includers can use -Wshadow.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// FBString cannot use throw when replacing std::string, though it may still
// use std::__throw_*
// nolint
#define throw FOLLY_FBSTRING_MAY_NOT_USE_THROW

#ifdef _LIBSTDCXX_FBSTRING
namespace std _GLIBCXX_VISIBILITY(default) {
_GLIBCXX_BEGIN_NAMESPACE_VERSION
#else
namespace folly {
#endif

// Different versions of gcc/clang support different versions of
// the address sanitizer attribute.  Unfortunately, this attribute
// has issues when inlining is used, so disable that as well.
#if defined(__clang__)
# if __has_feature(address_sanitizer)
#  if __has_attribute(__no_address_safety_analysis__)
#   define FBSTRING_DISABLE_ADDRESS_SANITIZER \
      __attribute__((__no_address_safety_analysis__, __noinline__))
#  elif __has_attribute(__no_sanitize_address__)
#   define FBSTRING_DISABLE_ADDRESS_SANITIZER \
      __attribute__((__no_sanitize_address__, __noinline__))
#  endif
# endif
#elif defined (__GNUC__) && \
      (__GNUC__ == 4) && \
      (__GNUC_MINOR__ >= 8) && \
      __SANITIZE_ADDRESS__
# define FBSTRING_DISABLE_ADDRESS_SANITIZER \
    __attribute__((__no_address_safety_analysis__, __noinline__))
#endif
#ifndef FBSTRING_DISABLE_ADDRESS_SANITIZER
# define FBSTRING_DISABLE_ADDRESS_SANITIZER
#endif

namespace fbstring_detail {

template <class InIt, class OutIt>
inline
OutIt copy_n(InIt b,
             typename std::iterator_traits<InIt>::difference_type n,
             OutIt d) {
  for (; n != 0; --n, ++b, ++d) {
    *d = *b;
  }
  return d;
}

template <class Pod, class T>
inline void pod_fill(Pod* b, Pod* e, T c) {
  assert(b && e && b <= e);
  /*static*/ if (sizeof(T) == 1) {
    memset(b, c, e - b);
  } else {
    auto const ee = b + ((e - b) & ~7u);
    for (; b != ee; b += 8) {
      b[0] = c;
      b[1] = c;
      b[2] = c;
      b[3] = c;
      b[4] = c;
      b[5] = c;
      b[6] = c;
      b[7] = c;
    }
    // Leftovers
    for (; b != e; ++b) {
      *b = c;
    }
  }
}

/*
 * Lightly structured memcpy, simplifies copying PODs and introduces
 * some asserts. Unfortunately using this function may cause
 * measurable overhead (presumably because it adjusts from a begin/end
 * convention to a pointer/size convention, so it does some extra
 * arithmetic even though the caller might have done the inverse
 * adaptation outside).
 */
template <class Pod>
inline void pod_copy(const Pod* b, const Pod* e, Pod* d) {
  assert(e >= b);
  assert(d >= e || d + (e - b) <= b);
  memcpy(d, b, (e - b) * sizeof(Pod));
}

/*
 * Lightly structured memmove, simplifies copying PODs and introduces
 * some asserts
 */
template <class Pod>
inline void pod_move(const Pod* b, const Pod* e, Pod* d) {
  assert(e >= b);
  memmove(d, b, (e - b) * sizeof(*b));
}

} // namespace fbstring_detail

/**
 * Defines a special acquisition method for constructing fbstring
 * objects. AcquireMallocatedString means that the user passes a
 * pointer to a malloc-allocated string that the fbstring object will
 * take into custody.
 */
enum class AcquireMallocatedString {};

/*
 * fbstring_core_model is a mock-up type that defines all required
 * signatures of a fbstring core. The fbstring class itself uses such
 * a core object to implement all of the numerous member functions
 * required by the standard.
 *
 * If you want to define a new core, copy the definition below and
 * implement the primitives. Then plug the core into basic_fbstring as
 * a template argument.

template <class Char>
class fbstring_core_model {
public:
  fbstring_core_model();
  fbstring_core_model(const fbstring_core_model &);
  ~fbstring_core_model();
  // Returns a pointer to string's buffer (currently only contiguous
  // strings are supported). The pointer is guaranteed to be valid
  // until the next call to a non-const member function.
  const Char * data() const;
  // Much like data(), except the string is prepared to support
  // character-level changes. This call is a signal for
  // e.g. reference-counted implementation to fork the data. The
  // pointer is guaranteed to be valid until the next call to a
  // non-const member function.
  Char * mutable_data();
  // Returns a pointer to string's buffer and guarantees that a
  // readable '\0' lies right after the buffer. The pointer is
  // guaranteed to be valid until the next call to a non-const member
  // function.
  const Char * c_str() const;
  // Shrinks the string by delta characters. Asserts that delta <=
  // size().
  void shrink(size_t delta);
  // Expands the string by delta characters (i.e. after this call
  // size() will report the old size() plus delta) but without
  // initializing the expanded region. Returns a pointer to the memory
  // to be initialized (the beginning of the expanded portion). The
  // caller is expected to fill the expanded area appropriately.
  Char* expand_noinit(size_t delta);
  // Expands the string by one character and sets the last character
  // to c.
  void push_back(Char c);
  // Returns the string's size.
  size_t size() const;
  // Returns the string's capacity, i.e. maximum size that the string
  // can grow to without reallocation. Note that for reference counted
  // strings that's technically a lie - even assigning characters
  // within the existing size would cause a reallocation.
  size_t capacity() const;
  // Returns true if the data underlying the string is actually shared
  // across multiple strings (in a refcounted fashion).
  bool isShared() const;
  // Makes sure that at least minCapacity characters are available for
  // the string without reallocation. For reference-counted strings,
  // it should fork the data even if minCapacity < size().
  void reserve(size_t minCapacity);
private:
  // Do not implement
  fbstring_core_model& operator=(const fbstring_core_model &);
};
*/

/**
 * gcc-4.7 throws what appears to be some false positive uninitialized
 * warnings for the members of the MediumLarge struct.  So, mute them here.
 */
#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wuninitialized"
#endif

/**
 * This is the core of the string. The code should work on 32- and
 * 64-bit architectures and with any Char size. Porting to big endian
 * architectures would require some changes.
 *
 * The storage is selected as follows (assuming we store one-byte
 * characters on a 64-bit machine): (a) "small" strings between 0 and
 * 23 chars are stored in-situ without allocation (the rightmost byte
 * stores the size); (b) "medium" strings from 24 through 254 chars
 * are stored in malloc-allocated memory that is copied eagerly; (c)
 * "large" strings of 255 chars and above are stored in a similar
 * structure as medium arrays, except that the string is
 * reference-counted and copied lazily. the reference count is
 * allocated right before the character array.
 *
 * The discriminator between these three strategies sits in the two
 * most significant bits of the rightmost char of the storage. If
 * neither is set, then the string is small (and its length sits in
 * the lower-order bits of that rightmost character). If the MSb is
 * set, the string is medium width. If the second MSb is set, then the
 * string is large.
 */
template <class Char> class fbstring_core {
public:
  fbstring_core() noexcept {
    // Only initialize the tag, will set the MSBs (i.e. the small
    // string size) to zero too
    ml_.capacity_ = maxSmallSize << (8 * (sizeof(size_t) - sizeof(Char)));
    // or: setSmallSize(0);
    writeTerminator();
    assert(category() == Category::isSmall && size() == 0);
  }

  fbstring_core(const fbstring_core & rhs) {
    assert(&rhs != this);
    // Simplest case first: small strings are bitblitted
    if (rhs.category() == Category::isSmall) {
      static_assert(offsetof(MediumLarge, data_) == 0,
          "fbstring layout failure");
      static_assert(offsetof(MediumLarge, size_) == sizeof(ml_.data_),
          "fbstring layout failure");
      static_assert(offsetof(MediumLarge, capacity_) == 2 * sizeof(ml_.data_),
          "fbstring layout failure");
      const size_t size = rhs.smallSize();
      if (size == 0) {
        ml_.capacity_ = rhs.ml_.capacity_;
        writeTerminator();
      } else {
        // Just write the whole thing, don't look at details. In
        // particular we need to copy capacity anyway because we want
        // to set the size (don't forget that the last character,
        // which stores a short string's length, is shared with the
        // ml_.capacity field).
        ml_ = rhs.ml_;
      }
      assert(category() == Category::isSmall && this->size() == rhs.size());
    } else if (rhs.category() == Category::isLarge) {
      // Large strings are just refcounted
      ml_ = rhs.ml_;
      RefCounted::incrementRefs(ml_.data_);
      assert(category() == Category::isLarge && size() == rhs.size());
    } else {
      // Medium strings are copied eagerly. Don't forget to allocate
      // one extra Char for the null terminator.
      auto const allocSize =
           goodMallocSize((1 + rhs.ml_.size_) * sizeof(Char));
      ml_.data_ = static_cast<Char*>(checkedMalloc(allocSize));
      fbstring_detail::pod_copy(rhs.ml_.data_,
                                // 1 for terminator
                                rhs.ml_.data_ + rhs.ml_.size_ + 1,
                                ml_.data_);
      // No need for writeTerminator() here, we copied one extra
      // element just above.
      ml_.size_ = rhs.ml_.size_;
      ml_.capacity_ = (allocSize / sizeof(Char) - 1)
                      | static_cast<category_type>(Category::isMedium);
      assert(category() == Category::isMedium);
    }
    assert(size() == rhs.size());
    assert(memcmp(data(), rhs.data(), size() * sizeof(Char)) == 0);
  }

  fbstring_core(fbstring_core&& goner) noexcept {
    if (goner.category() == Category::isSmall) {
      // Just copy, leave the goner in peace
      new(this) fbstring_core(goner.small_, goner.smallSize());
    } else {
      // Take goner's guts
      ml_ = goner.ml_;
      // Clean goner's carcass
      goner.setSmallSize(0);
    }
  }

  // NOTE(agallagher): The word-aligned copy path copies bytes which are
  // outside the range of the string, and makes address sanitizer unhappy,
  // so just disable it on this function.
  fbstring_core(const Char *const data, const size_t size)
      FBSTRING_DISABLE_ADDRESS_SANITIZER {
#ifndef NDEBUG
#ifndef _LIBSTDCXX_FBSTRING
    SCOPE_EXIT {
      assert(this->size() == size);
      assert(memcmp(this->data(), data, size * sizeof(Char)) == 0);
    };
#endif
#endif

    // Simplest case first: small strings are bitblitted
    if (size <= maxSmallSize) {
      // Layout is: Char* data_, size_t size_, size_t capacity_
      static_assert(sizeof(*this) == sizeof(Char*) + 2 * sizeof(size_t),
          "fbstring has unexpected size");
      static_assert(sizeof(Char*) == sizeof(size_t),
          "fbstring size assumption violation");
      // sizeof(size_t) must be a power of 2
      static_assert((sizeof(size_t) & (sizeof(size_t) - 1)) == 0,
          "fbstring size assumption violation");

      // If data is aligned, use fast word-wise copying. Otherwise,
      // use conservative memcpy.
      if (reinterpret_cast<size_t>(data) & (sizeof(size_t) - 1)) {
        fbstring_detail::pod_copy(data, data + size, small_);
      } else {
        // Copy one word (64 bits) at a time
        const size_t byteSize = size * sizeof(Char);
        if (byteSize > 2 * sizeof(size_t)) {
          // Copy three words
          ml_.capacity_ = reinterpret_cast<const size_t*>(data)[2];
          copyTwo:
          ml_.size_ = reinterpret_cast<const size_t*>(data)[1];
          copyOne:
          ml_.data_ = *reinterpret_cast<Char**>(const_cast<Char*>(data));
        } else if (byteSize > sizeof(size_t)) {
          // Copy two words
          goto copyTwo;
        } else if (size > 0) {
          // Copy one word
          goto copyOne;
        }
      }
      setSmallSize(size);
      return;
    } else if (size <= maxMediumSize) {
      // Medium strings are allocated normally. Don't forget to
      // allocate one extra Char for the terminating null.
      auto const allocSize = goodMallocSize((1 + size) * sizeof(Char));
      ml_.data_ = static_cast<Char*>(checkedMalloc(allocSize));
      fbstring_detail::pod_copy(data, data + size, ml_.data_);
      ml_.size_ = size;
      ml_.capacity_ = (allocSize / sizeof(Char) - 1)
                      | static_cast<category_type>(Category::isMedium);
    } else {
      // Large strings are allocated differently
      size_t effectiveCapacity = size;
      auto const newRC = RefCounted::create(data, & effectiveCapacity);
      ml_.data_ = newRC->data_;
      ml_.size_ = size;
      ml_.capacity_ = effectiveCapacity
                      | static_cast<category_type>(Category::isLarge);
    }
    writeTerminator();
  }

  ~fbstring_core() noexcept {
    auto const c = category();
    if (c == Category::isSmall) {
      return;
    }
    if (c == Category::isMedium) {
      free(ml_.data_);
      return;
    }
    RefCounted::decrementRefs(ml_.data_);
  }

  // Snatches a previously mallocated string. The parameter "size"
  // is the size of the string, and the parameter "allocatedSize"
  // is the size of the mallocated block.  The string must be
  // \0-terminated, so allocatedSize >= size + 1 and data[size] == '\0'.
  //
  // So if you want a 2-character string, pass malloc(3) as "data",
  // pass 2 as "size", and pass 3 as "allocatedSize".
  fbstring_core(Char * const data,
                const size_t size,
                const size_t allocatedSize,
                AcquireMallocatedString) {
    if (size > 0) {
      assert(allocatedSize >= size + 1);
      assert(data[size] == '\0');
      // Use the medium string storage
      ml_.data_ = data;
      ml_.size_ = size;
      // Don't forget about null terminator
      ml_.capacity_ = (allocatedSize - 1)
                      | static_cast<category_type>(Category::isMedium);
    } else {
      // No need for the memory
      free(data);
      setSmallSize(0);
    }
  }

  // swap below doesn't test whether &rhs == this (and instead
  // potentially does extra work) on the premise that the rarity of
  // that situation actually makes the check more expensive than is
  // worth.
  void swap(fbstring_core & rhs) {
    auto const t = ml_;
    ml_ = rhs.ml_;
    rhs.ml_ = t;
  }

  // In C++11 data() and c_str() are 100% equivalent.
  const Char * data() const {
    return c_str();
  }

  Char * mutable_data() {
    auto const c = category();
    if (c == Category::isSmall) {
      return small_;
    }
    assert(c == Category::isMedium || c == Category::isLarge);
    if (c == Category::isLarge && RefCounted::refs(ml_.data_) > 1) {
      // Ensure unique.
      size_t effectiveCapacity = ml_.capacity();
      auto const newRC = RefCounted::create(& effectiveCapacity);
      // If this fails, someone placed the wrong capacity in an
      // fbstring.
      assert(effectiveCapacity >= ml_.capacity());
      fbstring_detail::pod_copy(ml_.data_, ml_.data_ + ml_.size_ + 1,
                                newRC->data_);
      RefCounted::decrementRefs(ml_.data_);
      ml_.data_ = newRC->data_;
      // No need to call writeTerminator(), we have + 1 above.
    }
    return ml_.data_;
  }

  const Char * c_str() const {
    auto const c = category();
    if (c == Category::isSmall) {
      assert(small_[smallSize()] == '\0');
      return small_;
    }
    assert(c == Category::isMedium || c == Category::isLarge);
    assert(ml_.data_[ml_.size_] == '\0');
    return ml_.data_;
  }

  void shrink(const size_t delta) {
    if (category() == Category::isSmall) {
      // Check for underflow
      assert(delta <= smallSize());
      setSmallSize(smallSize() - delta);
    } else if (category() == Category::isMedium ||
               RefCounted::refs(ml_.data_) == 1) {
      // Medium strings and unique large strings need no special
      // handling.
      assert(ml_.size_ >= delta);
      ml_.size_ -= delta;
      writeTerminator();
    } else {
      assert(ml_.size_ >= delta);
      // Shared large string, must make unique. This is because of the
      // durn terminator must be written, which may trample the shared
      // data.
      if (delta) {
        fbstring_core(ml_.data_, ml_.size_ - delta).swap(*this);
      }
      // No need to write the terminator.
    }
  }

  void reserve(size_t minCapacity) {
    if (category() == Category::isLarge) {
      // Ensure unique
      if (RefCounted::refs(ml_.data_) > 1) {
        // We must make it unique regardless; in-place reallocation is
        // useless if the string is shared. In order to not surprise
        // people, reserve the new block at current capacity or
        // more. That way, a string's capacity never shrinks after a
        // call to reserve.
        minCapacity = std::max(minCapacity, ml_.capacity());
        auto const newRC = RefCounted::create(& minCapacity);
        fbstring_detail::pod_copy(ml_.data_, ml_.data_ + ml_.size_ + 1,
                                   newRC->data_);
        // Done with the old data. No need to call writeTerminator(),
        // we have + 1 above.
        RefCounted::decrementRefs(ml_.data_);
        ml_.data_ = newRC->data_;
        ml_.capacity_ = minCapacity
                        | static_cast<category_type>(Category::isLarge);
        // size remains unchanged
      } else {
        // String is not shared, so let's try to realloc (if needed)
        if (minCapacity > ml_.capacity()) {
          // Asking for more memory
          auto const newRC =
               RefCounted::reallocate(ml_.data_, ml_.size_,
                                      ml_.capacity(), minCapacity);
          ml_.data_ = newRC->data_;
          ml_.capacity_ = minCapacity
                          | static_cast<category_type>(Category::isLarge);
          writeTerminator();
        }
        assert(capacity() >= minCapacity);
      }
    } else if (category() == Category::isMedium) {
      // String is not shared
      if (minCapacity <= ml_.capacity()) {
        return; // nothing to do, there's enough room
      }
      if (minCapacity <= maxMediumSize) {
        // Keep the string at medium size. Don't forget to allocate
        // one extra Char for the terminating null.
        size_t capacityBytes = goodMallocSize((1 + minCapacity) * sizeof(Char));
        ml_.data_ = static_cast<Char *>(
          smartRealloc(
            ml_.data_,
            ml_.size_ * sizeof(Char),
            (ml_.capacity() + 1) * sizeof(Char),
            capacityBytes));
        writeTerminator();
        ml_.capacity_ = (capacityBytes / sizeof(Char) - 1)
                        | static_cast<category_type>(Category::isMedium);
      } else {
        // Conversion from medium to large string
        fbstring_core nascent;
        // Will recurse to another branch of this function
        nascent.reserve(minCapacity);
        nascent.ml_.size_ = ml_.size_;
        fbstring_detail::pod_copy(ml_.data_, ml_.data_ + ml_.size_,
                                  nascent.ml_.data_);
        nascent.swap(*this);
        writeTerminator();
        assert(capacity() >= minCapacity);
      }
    } else {
      assert(category() == Category::isSmall);
      if (minCapacity > maxMediumSize) {
        // large
        auto const newRC = RefCounted::create(& minCapacity);
        auto const size = smallSize();
        fbstring_detail::pod_copy(small_, small_ + size + 1, newRC->data_);
        // No need for writeTerminator(), we wrote it above with + 1.
        ml_.data_ = newRC->data_;
        ml_.size_ = size;
        ml_.capacity_ = minCapacity
                        | static_cast<category_type>(Category::isLarge);
        assert(capacity() >= minCapacity);
      } else if (minCapacity > maxSmallSize) {
        // medium
        // Don't forget to allocate one extra Char for the terminating null
        auto const allocSizeBytes =
          goodMallocSize((1 + minCapacity) * sizeof(Char));
        auto const data = static_cast<Char*>(checkedMalloc(allocSizeBytes));
        auto const size = smallSize();
        fbstring_detail::pod_copy(small_, small_ + size + 1, data);
        // No need for writeTerminator(), we wrote it above with + 1.
        ml_.data_ = data;
        ml_.size_ = size;
        ml_.capacity_ = (allocSizeBytes / sizeof(Char) - 1)
                        | static_cast<category_type>(Category::isMedium);
      } else {
        // small
        // Nothing to do, everything stays put
      }
    }
    assert(capacity() >= minCapacity);
  }

  Char * expand_noinit(const size_t delta) {
    // Strategy is simple: make room, then change size
    assert(capacity() >= size());
    size_t sz, newSz;
    if (category() == Category::isSmall) {
      sz = smallSize();
      newSz = sz + delta;
      if (newSz <= maxSmallSize) {
        setSmallSize(newSz);
        return small_ + sz;
      }
      reserve(newSz);
    } else {
      sz = ml_.size_;
      newSz = ml_.size_ + delta;
      if (newSz > capacity()) {
        reserve(newSz);
      }
    }
    assert(capacity() >= newSz);
    // Category can't be small - we took care of that above
    assert(category() == Category::isMedium || category() == Category::isLarge);
    ml_.size_ = newSz;
    writeTerminator();
    assert(size() == newSz);
    return ml_.data_ + sz;
  }

  void push_back(Char c) {
    assert(capacity() >= size());
    size_t sz;
    if (category() == Category::isSmall) {
      sz = smallSize();
      if (sz < maxSmallSize) {
        small_[sz] = c;
        setSmallSize(sz + 1);
        return;
      }
      reserve(maxSmallSize * 2);
    } else {
      sz = ml_.size_;
      if (sz == capacity()) {  // always true for isShared()
        reserve(1 + sz * 3 / 2);  // ensures not shared
      }
    }
    assert(!isShared());
    assert(capacity() >= sz + 1);
    // Category can't be small - we took care of that above
    assert(category() == Category::isMedium || category() == Category::isLarge);
    ml_.size_ = sz + 1;
    ml_.data_[sz] = c;
    writeTerminator();
  }

  size_t size() const {
    return category() == Category::isSmall ? smallSize() : ml_.size_;
  }

  size_t capacity() const {
    switch (category()) {
      case Category::isSmall:
        return maxSmallSize;
      case Category::isLarge:
        // For large-sized strings, a multi-referenced chunk has no
        // available capacity. This is because any attempt to append
        // data would trigger a new allocation.
        if (RefCounted::refs(ml_.data_) > 1) return ml_.size_;
      default: {}
    }
    return ml_.capacity();
  }

  bool isShared() const {
    return category() == Category::isLarge && RefCounted::refs(ml_.data_) > 1;
  }

  void writeTerminator() {
    if (category() == Category::isSmall) {
      const auto s = smallSize();
      if (s != maxSmallSize) {
        small_[s] = '\0';
      }
    } else {
      ml_.data_[ml_.size_] = '\0';
    }
  }

private:
  // Disabled
  fbstring_core & operator=(const fbstring_core & rhs);

  struct MediumLarge {
    Char * data_;
    size_t size_;
    size_t capacity_;

    size_t capacity() const {
      return capacity_ & capacityExtractMask;
    }
  };

  struct RefCounted {
    std::atomic<size_t> refCount_;
    Char data_[1];

    static RefCounted * fromData(Char * p) {
      return static_cast<RefCounted*>(
        static_cast<void*>(
          static_cast<unsigned char*>(static_cast<void*>(p))
          - sizeof(refCount_)));
    }

    static size_t refs(Char * p) {
      return fromData(p)->refCount_.load(std::memory_order_acquire);
    }

    static void incrementRefs(Char * p) {
      fromData(p)->refCount_.fetch_add(1, std::memory_order_acq_rel);
    }

    static void decrementRefs(Char * p) {
      auto const dis = fromData(p);
      size_t oldcnt = dis->refCount_.fetch_sub(1, std::memory_order_acq_rel);
      assert(oldcnt > 0);
      if (oldcnt == 1) {
        free(dis);
      }
    }

    static RefCounted * create(size_t * size) {
      // Don't forget to allocate one extra Char for the terminating
      // null. In this case, however, one Char is already part of the
      // struct.
      const size_t allocSize = goodMallocSize(
        sizeof(RefCounted) + *size * sizeof(Char));
      auto result = static_cast<RefCounted*>(checkedMalloc(allocSize));
      result->refCount_.store(1, std::memory_order_release);
      *size = (allocSize - sizeof(RefCounted)) / sizeof(Char);
      return result;
    }

    static RefCounted * create(const Char * data, size_t * size) {
      const size_t effectiveSize = *size;
      auto result = create(size);
      fbstring_detail::pod_copy(data, data + effectiveSize, result->data_);
      return result;
    }

    static RefCounted * reallocate(Char *const data,
                                   const size_t currentSize,
                                   const size_t currentCapacity,
                                   const size_t newCapacity) {
      assert(newCapacity > 0 && newCapacity > currentSize);
      auto const dis = fromData(data);
      assert(dis->refCount_.load(std::memory_order_acquire) == 1);
      // Don't forget to allocate one extra Char for the terminating
      // null. In this case, however, one Char is already part of the
      // struct.
      auto result = static_cast<RefCounted*>(
             smartRealloc(dis,
                          sizeof(RefCounted) + currentSize * sizeof(Char),
                          sizeof(RefCounted) + currentCapacity * sizeof(Char),
                          sizeof(RefCounted) + newCapacity * sizeof(Char)));
      assert(result->refCount_.load(std::memory_order_acquire) == 1);
      return result;
    }
  };

  union {
    Char small_[sizeof(MediumLarge) / sizeof(Char)];
    MediumLarge ml_;
  };

  enum {
    lastChar = sizeof(MediumLarge) - 1,
    maxSmallSize = lastChar / sizeof(Char),
    maxMediumSize = 254 / sizeof(Char),            // coincides with the small
                                                   // bin size in dlmalloc
    categoryExtractMask = sizeof(size_t) == 4 ? 0xC0000000 : 0xC000000000000000,
    capacityExtractMask = ~categoryExtractMask,
  };
  static_assert(!(sizeof(MediumLarge) % sizeof(Char)),
                "Corrupt memory layout for fbstring.");

  typedef std::conditional<sizeof(size_t) == 4, uint32_t, uint64_t>::type
          category_type;

  enum class Category : category_type {
    isSmall = 0,
    isMedium = sizeof(size_t) == 4 ? 0x80000000 : 0x8000000000000000,
    isLarge =  sizeof(size_t) == 4 ? 0x40000000 : 0x4000000000000000,
  };

  Category category() const {
    // Assumes little endian
    return static_cast<Category>(ml_.capacity_ & categoryExtractMask);
  }

  size_t smallSize() const {
    assert(category() == Category::isSmall &&
           static_cast<size_t>(small_[maxSmallSize])
           <= static_cast<size_t>(maxSmallSize));
    return static_cast<size_t>(maxSmallSize)
      - static_cast<size_t>(small_[maxSmallSize]);
  }

  void setSmallSize(size_t s) {
    // Warning: this should work with uninitialized strings too,
    // so don't assume anything about the previous value of
    // small_[maxSmallSize].
    assert(s <= maxSmallSize);
    small_[maxSmallSize] = maxSmallSize - s;
    writeTerminator();
  }
};

#if defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif

#ifndef _LIBSTDCXX_FBSTRING
/**
 * Dummy fbstring core that uses an actual std::string. This doesn't
 * make any sense - it's just for testing purposes.
 */
template <class Char>
class dummy_fbstring_core {
public:
  dummy_fbstring_core() {
  }
  dummy_fbstring_core(const dummy_fbstring_core& another)
      : backend_(another.backend_) {
  }
  dummy_fbstring_core(const Char * s, size_t n)
      : backend_(s, n) {
  }
  void swap(dummy_fbstring_core & rhs) {
    backend_.swap(rhs.backend_);
  }
  const Char * data() const {
    return backend_.data();
  }
  Char * mutable_data() {
    //assert(!backend_.empty());
    return &*backend_.begin();
  }
  void shrink(size_t delta) {
    assert(delta <= size());
    backend_.resize(size() - delta);
  }
  Char * expand_noinit(size_t delta) {
    auto const sz = size();
    backend_.resize(size() + delta);
    return backend_.data() + sz;
  }
  void push_back(Char c) {
    backend_.push_back(c);
  }
  size_t size() const {
    return backend_.size();
  }
  size_t capacity() const {
    return backend_.capacity();
  }
  bool isShared() const {
    return false;
  }
  void reserve(size_t minCapacity) {
    backend_.reserve(minCapacity);
  }

private:
  std::basic_string<Char> backend_;
};
#endif // !_LIBSTDCXX_FBSTRING

/**
 * This is the basic_string replacement. For conformity,
 * basic_fbstring takes the same template parameters, plus the last
 * one which is the core.
 */
#ifdef _LIBSTDCXX_FBSTRING
template <typename E, class T, class A, class Storage>
#else
template <typename E,
          class T = std::char_traits<E>,
          class A = std::allocator<E>,
          class Storage = fbstring_core<E> >
#endif
class basic_fbstring {

  static void enforce(
      bool condition,
      void (*throw_exc)(const char*),
      const char* msg) {
    if (!condition) throw_exc(msg);
  }

  bool isSane() const {
    return
      begin() <= end() &&
      empty() == (size() == 0) &&
      empty() == (begin() == end()) &&
      size() <= max_size() &&
      capacity() <= max_size() &&
      size() <= capacity() &&
      begin()[size()] == '\0';
  }

  struct Invariant;
  friend struct Invariant;
  struct Invariant {
#ifndef NDEBUG
    explicit Invariant(const basic_fbstring& s) : s_(s) {
      assert(s_.isSane());
    }
    ~Invariant() {
      assert(s_.isSane());
    }
  private:
    const basic_fbstring& s_;
#else
    explicit Invariant(const basic_fbstring&) {}
#endif
    Invariant& operator=(const Invariant&);
  };

public:
  // types
  typedef T traits_type;
  typedef typename traits_type::char_type value_type;
  typedef A allocator_type;
  typedef typename A::size_type size_type;
  typedef typename A::difference_type difference_type;

  typedef typename A::reference reference;
  typedef typename A::const_reference const_reference;
  typedef typename A::pointer pointer;
  typedef typename A::const_pointer const_pointer;

  typedef E* iterator;
  typedef const E* const_iterator;
  typedef std::reverse_iterator<iterator
#ifdef NO_ITERATOR_TRAITS
                                , value_type
#endif
                                > reverse_iterator;
  typedef std::reverse_iterator<const_iterator
#ifdef NO_ITERATOR_TRAITS
                                , const value_type
#endif
                                > const_reverse_iterator;

  static const size_type npos;                     // = size_type(-1)

private:
  static void procrustes(size_type& n, size_type nmax) {
    if (n > nmax) n = nmax;
  }

public:
  // C++11 21.4.2 construct/copy/destroy
  explicit basic_fbstring(const A& a = A()) noexcept {
  }

  basic_fbstring(const basic_fbstring& str)
      : store_(str.store_) {
  }

  // Move constructor
  basic_fbstring(basic_fbstring&& goner) noexcept
      : store_(std::move(goner.store_)) {
  }

#ifndef _LIBSTDCXX_FBSTRING
  // This is defined for compatibility with std::string
  /* implicit */ basic_fbstring(const std::string& str)
      : store_(str.data(), str.size()) {
  }
#endif

  basic_fbstring(const basic_fbstring& str, size_type pos,
                 size_type n = npos, const A& a = A()) {
    assign(str, pos, n);
  }

  /* implicit */ basic_fbstring(const value_type* s, const A& a = A())
      : store_(s, s
          ? traits_type::length(s)
          : (std::__throw_logic_error(
                "basic_fbstring: null pointer initializer not valid"),
             0)) {
  }

  basic_fbstring(const value_type* s, size_type n, const A& a = A())
      : store_(s, n) {
  }

  basic_fbstring(size_type n, value_type c, const A& a = A()) {
    auto const data = store_.expand_noinit(n);
    fbstring_detail::pod_fill(data, data + n, c);
    store_.writeTerminator();
  }

  template <class InIt>
  basic_fbstring(InIt begin, InIt end,
                 typename std::enable_if<
                 !std::is_same<typename std::remove_const<InIt>::type,
                 value_type*>::value, const A>::type & a = A()) {
    assign(begin, end);
  }

  // Specialization for const char*, const char*
  basic_fbstring(const value_type* b, const value_type* e)
      : store_(b, e - b) {
  }

  // Nonstandard constructor
  basic_fbstring(value_type *s, size_type n, size_type c,
                 AcquireMallocatedString a)
      : store_(s, n, c, a) {
  }

  // Construction from initialization list
  basic_fbstring(std::initializer_list<value_type> il) {
    assign(il.begin(), il.end());
  }

  ~basic_fbstring() noexcept {
  }

  basic_fbstring& operator=(const basic_fbstring& lhs) {
    if (FBSTRING_UNLIKELY(&lhs == this)) {
      return *this;
    }
    auto const oldSize = size();
    auto const srcSize = lhs.size();
    if (capacity() >= srcSize && !store_.isShared()) {
      // great, just copy the contents
      if (oldSize < srcSize)
        store_.expand_noinit(srcSize - oldSize);
      else
        store_.shrink(oldSize - srcSize);
      assert(size() == srcSize);
      fbstring_detail::pod_copy(lhs.begin(), lhs.end(), begin());
      store_.writeTerminator();
    } else {
      // need to reallocate, so we may as well create a brand new string
      basic_fbstring(lhs).swap(*this);
    }
    return *this;
  }

  // Move assignment
  basic_fbstring& operator=(basic_fbstring&& goner) noexcept {
    if (FBSTRING_UNLIKELY(&goner == this)) {
      // Compatibility with std::basic_string<>,
      // C++11 21.4.2 [string.cons] / 23 requires self-move-assignment support.
      return *this;
    }
    // No need of this anymore
    this->~basic_fbstring();
    // Move the goner into this
    new(&store_) fbstring_core<E>(std::move(goner.store_));
    return *this;
  }

#ifndef _LIBSTDCXX_FBSTRING
  // Compatibility with std::string
  basic_fbstring & operator=(const std::string & rhs) {
    return assign(rhs.data(), rhs.size());
  }

  // Compatibility with std::string
  std::string toStdString() const {
    return std::string(data(), size());
  }
#else
  // A lot of code in fbcode still uses this method, so keep it here for now.
  const basic_fbstring& toStdString() const {
    return *this;
  }
#endif

  basic_fbstring& operator=(const value_type* s) {
    return assign(s);
  }

  basic_fbstring& operator=(value_type c) {
    if (empty()) {
      store_.expand_noinit(1);
    } else if (store_.isShared()) {
      basic_fbstring(1, c).swap(*this);
      return *this;
    } else {
      store_.shrink(size() - 1);
    }
    *store_.mutable_data() = c;
    store_.writeTerminator();
    return *this;
  }

  basic_fbstring& operator=(std::initializer_list<value_type> il) {
    return assign(il.begin(), il.end());
  }

  // C++11 21.4.3 iterators:
  iterator begin() { return store_.mutable_data(); }

  const_iterator begin() const { return store_.data(); }

  const_iterator cbegin() const { return begin(); }

  iterator end() {
    return store_.mutable_data() + store_.size();
  }

  const_iterator end() const {
    return store_.data() + store_.size();
  }

  const_iterator cend() const { return end(); }

  reverse_iterator rbegin() {
    return reverse_iterator(end());
  }

  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }

  const_reverse_iterator crbegin() const { return rbegin(); }

  reverse_iterator rend() {
    return reverse_iterator(begin());
  }

  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

  const_reverse_iterator crend() const { return rend(); }

  // Added by C++11
  // C++11 21.4.5, element access:
  const value_type& front() const { return *begin(); }
  const value_type& back() const {
    assert(!empty());
    // Should be begin()[size() - 1], but that branches twice
    return *(end() - 1);
  }
  value_type& front() { return *begin(); }
  value_type& back() {
    assert(!empty());
    // Should be begin()[size() - 1], but that branches twice
    return *(end() - 1);
  }
  void pop_back() {
    assert(!empty());
    store_.shrink(1);
  }

  // C++11 21.4.4 capacity:
  size_type size() const { return store_.size(); }

  size_type length() const { return size(); }

  size_type max_size() const {
    return std::numeric_limits<size_type>::max();
  }

  void resize(const size_type n, const value_type c = value_type()) {
    auto size = this->size();
    if (n <= size) {
      store_.shrink(size - n);
    } else {
      // Do this in two steps to minimize slack memory copied (see
      // smartRealloc).
      auto const capacity = this->capacity();
      assert(capacity >= size);
      if (size < capacity) {
        auto delta = std::min(n, capacity) - size;
        store_.expand_noinit(delta);
        fbstring_detail::pod_fill(begin() + size, end(), c);
        size += delta;
        if (size == n) {
          store_.writeTerminator();
          return;
        }
        assert(size < n);
      }
      auto const delta = n - size;
      store_.expand_noinit(delta);
      fbstring_detail::pod_fill(end() - delta, end(), c);
      store_.writeTerminator();
    }
    assert(this->size() == n);
  }

  size_type capacity() const { return store_.capacity(); }

  void reserve(size_type res_arg = 0) {
    enforce(res_arg <= max_size(), std::__throw_length_error, "");
    store_.reserve(res_arg);
  }

  void shrink_to_fit() {
    // Shrink only if slack memory is sufficiently large
    if (capacity() < size() * 3 / 2) {
      return;
    }
    basic_fbstring(cbegin(), cend()).swap(*this);
  }

  void clear() { resize(0); }

  bool empty() const { return size() == 0; }

  // C++11 21.4.5 element access:
  const_reference operator[](size_type pos) const {
    return *(begin() + pos);
  }

  reference operator[](size_type pos) {
    return *(begin() + pos);
  }

  const_reference at(size_type n) const {
    enforce(n <= size(), std::__throw_out_of_range, "");
    return (*this)[n];
  }

  reference at(size_type n) {
    enforce(n < size(), std::__throw_out_of_range, "");
    return (*this)[n];
  }

  // C++11 21.4.6 modifiers:
  basic_fbstring& operator+=(const basic_fbstring& str) {
    return append(str);
  }

  basic_fbstring& operator+=(const value_type* s) {
    return append(s);
  }

  basic_fbstring& operator+=(const value_type c) {
    push_back(c);
    return *this;
  }

  basic_fbstring& operator+=(std::initializer_list<value_type> il) {
    append(il);
    return *this;
  }

  basic_fbstring& append(const basic_fbstring& str) {
#ifndef NDEBUG
    auto desiredSize = size() + str.size();
#endif
    append(str.data(), str.size());
    assert(size() == desiredSize);
    return *this;
  }

  basic_fbstring& append(const basic_fbstring& str, const size_type pos,
                         size_type n) {
    const size_type sz = str.size();
    enforce(pos <= sz, std::__throw_out_of_range, "");
    procrustes(n, sz - pos);
    return append(str.data() + pos, n);
  }

  basic_fbstring& append(const value_type* s, size_type n) {
#ifndef NDEBUG
    Invariant checker(*this);
    (void) checker;
#endif
    if (FBSTRING_UNLIKELY(!n)) {
      // Unlikely but must be done
      return *this;
    }
    auto const oldSize = size();
    auto const oldData = data();
    // Check for aliasing (rare). We could use "<=" here but in theory
    // those do not work for pointers unless the pointers point to
    // elements in the same array. For that reason we use
    // std::less_equal, which is guaranteed to offer a total order
    // over pointers. See discussion at http://goo.gl/Cy2ya for more
    // info.
    std::less_equal<const value_type*> le;
    if (FBSTRING_UNLIKELY(le(oldData, s) && !le(oldData + oldSize, s))) {
      assert(le(s + n, oldData + oldSize));
      const size_type offset = s - oldData;
      store_.reserve(oldSize + n);
      // Restore the source
      s = data() + offset;
    }
    // Warning! Repeated appends with short strings may actually incur
    // practically quadratic performance. Avoid that by pushing back
    // the first character (which ensures exponential growth) and then
    // appending the rest normally. Worst case the append may incur a
    // second allocation but that will be rare.
    push_back(*s++);
    --n;
    memcpy(store_.expand_noinit(n), s, n * sizeof(value_type));
    assert(size() == oldSize + n + 1);
    return *this;
  }

  basic_fbstring& append(const value_type* s) {
    return append(s, traits_type::length(s));
  }

  basic_fbstring& append(size_type n, value_type c) {
    resize(size() + n, c);
    return *this;
  }

  template<class InputIterator>
  basic_fbstring& append(InputIterator first, InputIterator last) {
    insert(end(), first, last);
    return *this;
  }

  basic_fbstring& append(std::initializer_list<value_type> il) {
    return append(il.begin(), il.end());
  }

  void push_back(const value_type c) {             // primitive
    store_.push_back(c);
  }

  basic_fbstring& assign(const basic_fbstring& str) {
    if (&str == this) return *this;
    return assign(str.data(), str.size());
  }

  basic_fbstring& assign(basic_fbstring&& str) {
    return *this = std::move(str);
  }

  basic_fbstring& assign(const basic_fbstring& str, const size_type pos,
                         size_type n) {
    const size_type sz = str.size();
    enforce(pos <= sz, std::__throw_out_of_range, "");
    procrustes(n, sz - pos);
    return assign(str.data() + pos, n);
  }

  basic_fbstring& assign(const value_type* s, const size_type n) {
    Invariant checker(*this);
    (void) checker;
    if (size() >= n) {
      std::copy(s, s + n, begin());
      resize(n);
      assert(size() == n);
    } else {
      const value_type *const s2 = s + size();
      std::copy(s, s2, begin());
      append(s2, n - size());
      assert(size() == n);
    }
    store_.writeTerminator();
    assert(size() == n);
    return *this;
  }

  basic_fbstring& assign(const value_type* s) {
    return assign(s, traits_type::length(s));
  }

  basic_fbstring& assign(std::initializer_list<value_type> il) {
    return assign(il.begin(), il.end());
  }

  template <class ItOrLength, class ItOrChar>
  basic_fbstring& assign(ItOrLength first_or_n, ItOrChar last_or_c) {
    return replace(begin(), end(), first_or_n, last_or_c);
  }

  basic_fbstring& insert(size_type pos1, const basic_fbstring& str) {
    return insert(pos1, str.data(), str.size());
  }

  basic_fbstring& insert(size_type pos1, const basic_fbstring& str,
                         size_type pos2, size_type n) {
    enforce(pos2 <= str.length(), std::__throw_out_of_range, "");
    procrustes(n, str.length() - pos2);
    return insert(pos1, str.data() + pos2, n);
  }

  basic_fbstring& insert(size_type pos, const value_type* s, size_type n) {
    enforce(pos <= length(), std::__throw_out_of_range, "");
    insert(begin() + pos, s, s + n);
    return *this;
  }

  basic_fbstring& insert(size_type pos, const value_type* s) {
    return insert(pos, s, traits_type::length(s));
  }

  basic_fbstring& insert(size_type pos, size_type n, value_type c) {
    enforce(pos <= length(), std::__throw_out_of_range, "");
    insert(begin() + pos, n, c);
    return *this;
  }

  iterator insert(const_iterator p, const value_type c) {
    const size_type pos = p - begin();
    insert(p, 1, c);
    return begin() + pos;
  }

private:
  template <int i> class Selector {};

  iterator insertImplDiscr(const_iterator p,
                           size_type n, value_type c, Selector<1>) {
    Invariant checker(*this);
    (void) checker;
    auto const pos = p - begin();
    assert(p >= begin() && p <= end());
    if (capacity() - size() < n) {
      const size_type sz = p - begin();
      reserve(size() + n);
      p = begin() + sz;
    }
    const iterator oldEnd = end();
    if (n < size_type(oldEnd - p)) {
      append(oldEnd - n, oldEnd);
      //std::copy(
      //    reverse_iterator(oldEnd - n),
      //    reverse_iterator(p),
      //    reverse_iterator(oldEnd));
      fbstring_detail::pod_move(&*p, &*oldEnd - n,
                                begin() + pos + n);
      std::fill(begin() + pos, begin() + pos + n, c);
    } else {
      append(n - (end() - p), c);
      append(iterator(p), oldEnd);
      std::fill(iterator(p), oldEnd, c);
    }
    store_.writeTerminator();
    return begin() + pos;
  }

  template<class InputIter>
  iterator insertImplDiscr(const_iterator i,
                           InputIter b, InputIter e, Selector<0>) {
    return insertImpl(i, b, e,
               typename std::iterator_traits<InputIter>::iterator_category());
  }

  template <class FwdIterator>
  iterator insertImpl(const_iterator i,
                  FwdIterator s1, FwdIterator s2, std::forward_iterator_tag) {
    Invariant checker(*this);
    (void) checker;
    const size_type pos = i - begin();
    const typename std::iterator_traits<FwdIterator>::difference_type n2 =
      std::distance(s1, s2);
    assert(n2 >= 0);
    using namespace fbstring_detail;
    assert(pos <= size());

    const typename std::iterator_traits<FwdIterator>::difference_type maxn2 =
      capacity() - size();
    if (maxn2 < n2) {
      // realloc the string
      reserve(size() + n2);
      i = begin() + pos;
    }
    if (pos + n2 <= size()) {
      const iterator tailBegin = end() - n2;
      store_.expand_noinit(n2);
      fbstring_detail::pod_copy(tailBegin, tailBegin + n2, end() - n2);
      std::copy(const_reverse_iterator(tailBegin), const_reverse_iterator(i),
                reverse_iterator(tailBegin + n2));
      std::copy(s1, s2, begin() + pos);
    } else {
      FwdIterator t = s1;
      const size_type old_size = size();
      std::advance(t, old_size - pos);
      const size_t newElems = std::distance(t, s2);
      store_.expand_noinit(n2);
      std::copy(t, s2, begin() + old_size);
      fbstring_detail::pod_copy(data() + pos, data() + old_size,
                                 begin() + old_size + newElems);
      std::copy(s1, t, begin() + pos);
    }
    store_.writeTerminator();
    return begin() + pos;
  }

  template <class InputIterator>
  iterator insertImpl(const_iterator i,
                      InputIterator b, InputIterator e,
                      std::input_iterator_tag) {
    const auto pos = i - begin();
    basic_fbstring temp(begin(), i);
    for (; b != e; ++b) {
      temp.push_back(*b);
    }
    temp.append(i, cend());
    swap(temp);
    return begin() + pos;
  }

public:
  template <class ItOrLength, class ItOrChar>
  iterator insert(const_iterator p, ItOrLength first_or_n, ItOrChar last_or_c) {
    Selector<std::numeric_limits<ItOrLength>::is_specialized> sel;
    return insertImplDiscr(p, first_or_n, last_or_c, sel);
  }

  iterator insert(const_iterator p, std::initializer_list<value_type> il) {
    return insert(p, il.begin(), il.end());
  }

  basic_fbstring& erase(size_type pos = 0, size_type n = npos) {
    Invariant checker(*this);
    (void) checker;
    enforce(pos <= length(), std::__throw_out_of_range, "");
    procrustes(n, length() - pos);
    std::copy(begin() + pos + n, end(), begin() + pos);
    resize(length() - n);
    return *this;
  }

  iterator erase(iterator position) {
    const size_type pos(position - begin());
    enforce(pos <= size(), std::__throw_out_of_range, "");
    erase(pos, 1);
    return begin() + pos;
  }

  iterator erase(iterator first, iterator last) {
    const size_type pos(first - begin());
    erase(pos, last - first);
    return begin() + pos;
  }

  // Replaces at most n1 chars of *this, starting with pos1 with the
  // content of str
  basic_fbstring& replace(size_type pos1, size_type n1,
                          const basic_fbstring& str) {
    return replace(pos1, n1, str.data(), str.size());
  }

  // Replaces at most n1 chars of *this, starting with pos1,
  // with at most n2 chars of str starting with pos2
  basic_fbstring& replace(size_type pos1, size_type n1,
                          const basic_fbstring& str,
                          size_type pos2, size_type n2) {
    enforce(pos2 <= str.length(), std::__throw_out_of_range, "");
    return replace(pos1, n1, str.data() + pos2,
                   std::min(n2, str.size() - pos2));
  }

  // Replaces at most n1 chars of *this, starting with pos, with chars from s
  basic_fbstring& replace(size_type pos, size_type n1, const value_type* s) {
    return replace(pos, n1, s, traits_type::length(s));
  }

  // Replaces at most n1 chars of *this, starting with pos, with n2
  // occurrences of c
  //
  // consolidated with
  //
  // Replaces at most n1 chars of *this, starting with pos, with at
  // most n2 chars of str.  str must have at least n2 chars.
  template <class StrOrLength, class NumOrChar>
  basic_fbstring& replace(size_type pos, size_type n1,
                          StrOrLength s_or_n2, NumOrChar n_or_c) {
    Invariant checker(*this);
    (void) checker;
    enforce(pos <= size(), std::__throw_out_of_range, "");
    procrustes(n1, length() - pos);
    const iterator b = begin() + pos;
    return replace(b, b + n1, s_or_n2, n_or_c);
  }

  basic_fbstring& replace(iterator i1, iterator i2, const basic_fbstring& str) {
    return replace(i1, i2, str.data(), str.length());
  }

  basic_fbstring& replace(iterator i1, iterator i2, const value_type* s) {
    return replace(i1, i2, s, traits_type::length(s));
  }

private:
  basic_fbstring& replaceImplDiscr(iterator i1, iterator i2,
                                   const value_type* s, size_type n,
                                   Selector<2>) {
    assert(i1 <= i2);
    assert(begin() <= i1 && i1 <= end());
    assert(begin() <= i2 && i2 <= end());
    return replace(i1, i2, s, s + n);
  }

  basic_fbstring& replaceImplDiscr(iterator i1, iterator i2,
                                   size_type n2, value_type c, Selector<1>) {
    const size_type n1 = i2 - i1;
    if (n1 > n2) {
      std::fill(i1, i1 + n2, c);
      erase(i1 + n2, i2);
    } else {
      std::fill(i1, i2, c);
      insert(i2, n2 - n1, c);
    }
    assert(isSane());
    return *this;
  }

  template <class InputIter>
  basic_fbstring& replaceImplDiscr(iterator i1, iterator i2,
                                   InputIter b, InputIter e,
                                   Selector<0>) {
    replaceImpl(i1, i2, b, e,
                typename std::iterator_traits<InputIter>::iterator_category());
    return *this;
  }

private:
  template <class FwdIterator>
  bool replaceAliased(iterator i1, iterator i2,
                      FwdIterator s1, FwdIterator s2, std::false_type) {
    return false;
  }

  template <class FwdIterator>
  bool replaceAliased(iterator i1, iterator i2,
                      FwdIterator s1, FwdIterator s2, std::true_type) {
    static const std::less_equal<const value_type*> le =
      std::less_equal<const value_type*>();
    const bool aliased = le(&*begin(), &*s1) && le(&*s1, &*end());
    if (!aliased) {
      return false;
    }
    // Aliased replace, copy to new string
    basic_fbstring temp;
    temp.reserve(size() - (i2 - i1) + std::distance(s1, s2));
    temp.append(begin(), i1).append(s1, s2).append(i2, end());
    swap(temp);
    return true;
  }

  template <class FwdIterator>
  void replaceImpl(iterator i1, iterator i2,
                   FwdIterator s1, FwdIterator s2, std::forward_iterator_tag) {
    Invariant checker(*this);
    (void) checker;

    // Handle aliased replace
    if (replaceAliased(i1, i2, s1, s2,
          std::integral_constant<bool,
            std::is_same<FwdIterator, iterator>::value ||
            std::is_same<FwdIterator, const_iterator>::value>())) {
      return;
    }

    auto const n1 = i2 - i1;
    assert(n1 >= 0);
    auto const n2 = std::distance(s1, s2);
    assert(n2 >= 0);

    if (n1 > n2) {
      // shrinks
      std::copy(s1, s2, i1);
      erase(i1 + n2, i2);
    } else {
      // grows
      fbstring_detail::copy_n(s1, n1, i1);
      std::advance(s1, n1);
      insert(i2, s1, s2);
    }
    assert(isSane());
  }

  template <class InputIterator>
  void replaceImpl(iterator i1, iterator i2,
                   InputIterator b, InputIterator e, std::input_iterator_tag) {
    basic_fbstring temp(begin(), i1);
    temp.append(b, e).append(i2, end());
    swap(temp);
  }

public:
  template <class T1, class T2>
  basic_fbstring& replace(iterator i1, iterator i2,
                          T1 first_or_n_or_s, T2 last_or_c_or_n) {
    const bool
      num1 = std::numeric_limits<T1>::is_specialized,
      num2 = std::numeric_limits<T2>::is_specialized;
    return replaceImplDiscr(
      i1, i2, first_or_n_or_s, last_or_c_or_n,
      Selector<num1 ? (num2 ? 1 : -1) : (num2 ? 2 : 0)>());
  }

  size_type copy(value_type* s, size_type n, size_type pos = 0) const {
    enforce(pos <= size(), std::__throw_out_of_range, "");
    procrustes(n, size() - pos);

    fbstring_detail::pod_copy(
      data() + pos,
      data() + pos + n,
      s);
    return n;
  }

  void swap(basic_fbstring& rhs) {
    store_.swap(rhs.store_);
  }

  const value_type* c_str() const {
    return store_.c_str();
  }

  const value_type* data() const { return c_str(); }

  allocator_type get_allocator() const {
    return allocator_type();
  }

  size_type find(const basic_fbstring& str, size_type pos = 0) const {
    return find(str.data(), pos, str.length());
  }

  size_type find(const value_type* needle, const size_type pos,
                 const size_type nsize) const {
    if (!nsize) return pos;
    auto const size = this->size();
    // nsize + pos can overflow (eg pos == npos), guard against that by checking
    // that nsize + pos does not wrap around.
    if (nsize + pos > size || nsize + pos < pos) return npos;
    // Don't use std::search, use a Boyer-Moore-like trick by comparing
    // the last characters first
    auto const haystack = data();
    auto const nsize_1 = nsize - 1;
    auto const lastNeedle = needle[nsize_1];

    // Boyer-Moore skip value for the last char in the needle. Zero is
    // not a valid value; skip will be computed the first time it's
    // needed.
    size_type skip = 0;

    const E * i = haystack + pos;
    auto iEnd = haystack + size - nsize_1;

    while (i < iEnd) {
      // Boyer-Moore: match the last element in the needle
      while (i[nsize_1] != lastNeedle) {
        if (++i == iEnd) {
          // not found
          return npos;
        }
      }
      // Here we know that the last char matches
      // Continue in pedestrian mode
      for (size_t j = 0; ; ) {
        assert(j < nsize);
        if (i[j] != needle[j]) {
          // Not found, we can skip
          // Compute the skip value lazily
          if (skip == 0) {
            skip = 1;
            while (skip <= nsize_1 && needle[nsize_1 - skip] != lastNeedle) {
              ++skip;
            }
          }
          i += skip;
          break;
        }
        // Check if done searching
        if (++j == nsize) {
          // Yay
          return i - haystack;
        }
      }
    }
    return npos;
  }

  size_type find(const value_type* s, size_type pos = 0) const {
    return find(s, pos, traits_type::length(s));
  }

  size_type find (value_type c, size_type pos = 0) const {
    return find(&c, pos, 1);
  }

  size_type rfind(const basic_fbstring& str, size_type pos = npos) const {
    return rfind(str.data(), pos, str.length());
  }

  size_type rfind(const value_type* s, size_type pos, size_type n) const {
    if (n > length()) return npos;
    pos = std::min(pos, length() - n);
    if (n == 0) return pos;

    const_iterator i(begin() + pos);
    for (; ; --i) {
      if (traits_type::eq(*i, *s)
          && traits_type::compare(&*i, s, n) == 0) {
        return i - begin();
      }
      if (i == begin()) break;
    }
    return npos;
  }

  size_type rfind(const value_type* s, size_type pos = npos) const {
    return rfind(s, pos, traits_type::length(s));
  }

  size_type rfind(value_type c, size_type pos = npos) const {
    return rfind(&c, pos, 1);
  }

  size_type find_first_of(const basic_fbstring& str, size_type pos = 0) const {
    return find_first_of(str.data(), pos, str.length());
  }

  size_type find_first_of(const value_type* s,
                          size_type pos, size_type n) const {
    if (pos > length() || n == 0) return npos;
    const_iterator i(begin() + pos),
      finish(end());
    for (; i != finish; ++i) {
      if (traits_type::find(s, n, *i) != 0) {
        return i - begin();
      }
    }
    return npos;
  }

  size_type find_first_of(const value_type* s, size_type pos = 0) const {
    return find_first_of(s, pos, traits_type::length(s));
  }

  size_type find_first_of(value_type c, size_type pos = 0) const {
    return find_first_of(&c, pos, 1);
  }

  size_type find_last_of (const basic_fbstring& str,
                          size_type pos = npos) const {
    return find_last_of(str.data(), pos, str.length());
  }

  size_type find_last_of (const value_type* s, size_type pos,
                          size_type n) const {
    if (!empty() && n > 0) {
      pos = std::min(pos, length() - 1);
      const_iterator i(begin() + pos);
      for (;; --i) {
        if (traits_type::find(s, n, *i) != 0) {
          return i - begin();
        }
        if (i == begin()) break;
      }
    }
    return npos;
  }

  size_type find_last_of (const value_type* s,
                          size_type pos = npos) const {
    return find_last_of(s, pos, traits_type::length(s));
  }

  size_type find_last_of (value_type c, size_type pos = npos) const {
    return find_last_of(&c, pos, 1);
  }

  size_type find_first_not_of(const basic_fbstring& str,
                              size_type pos = 0) const {
    return find_first_not_of(str.data(), pos, str.size());
  }

  size_type find_first_not_of(const value_type* s, size_type pos,
                              size_type n) const {
    if (pos < length()) {
      const_iterator
        i(begin() + pos),
        finish(end());
      for (; i != finish; ++i) {
        if (traits_type::find(s, n, *i) == 0) {
          return i - begin();
        }
      }
    }
    return npos;
  }

  size_type find_first_not_of(const value_type* s,
                              size_type pos = 0) const {
    return find_first_not_of(s, pos, traits_type::length(s));
  }

  size_type find_first_not_of(value_type c, size_type pos = 0) const {
    return find_first_not_of(&c, pos, 1);
  }

  size_type find_last_not_of(const basic_fbstring& str,
                             size_type pos = npos) const {
    return find_last_not_of(str.data(), pos, str.length());
  }

  size_type find_last_not_of(const value_type* s, size_type pos,
                             size_type n) const {
    if (!this->empty()) {
      pos = std::min(pos, size() - 1);
      const_iterator i(begin() + pos);
      for (;; --i) {
        if (traits_type::find(s, n, *i) == 0) {
          return i - begin();
        }
        if (i == begin()) break;
      }
    }
    return npos;
  }

  size_type find_last_not_of(const value_type* s,
                             size_type pos = npos) const {
    return find_last_not_of(s, pos, traits_type::length(s));
  }

  size_type find_last_not_of (value_type c, size_type pos = npos) const {
    return find_last_not_of(&c, pos, 1);
  }

  basic_fbstring substr(size_type pos = 0, size_type n = npos) const& {
    enforce(pos <= size(), std::__throw_out_of_range, "");
    return basic_fbstring(data() + pos, std::min(n, size() - pos));
  }

  basic_fbstring substr(size_type pos = 0, size_type n = npos) && {
    enforce(pos <= size(), std::__throw_out_of_range, "");
    erase(0, pos);
    if (n < size()) resize(n);
    return std::move(*this);
  }

  int compare(const basic_fbstring& str) const {
    // FIX due to Goncalo N M de Carvalho July 18, 2005
    return compare(0, size(), str);
  }

  int compare(size_type pos1, size_type n1,
              const basic_fbstring& str) const {
    return compare(pos1, n1, str.data(), str.size());
  }

  int compare(size_type pos1, size_type n1,
              const value_type* s) const {
    return compare(pos1, n1, s, traits_type::length(s));
  }

  int compare(size_type pos1, size_type n1,
              const value_type* s, size_type n2) const {
    enforce(pos1 <= size(), std::__throw_out_of_range, "");
    procrustes(n1, size() - pos1);
    // The line below fixed by Jean-Francois Bastien, 04-23-2007. Thanks!
    const int r = traits_type::compare(pos1 + data(), s, std::min(n1, n2));
    return r != 0 ? r : n1 > n2 ? 1 : n1 < n2 ? -1 : 0;
  }

  int compare(size_type pos1, size_type n1,
              const basic_fbstring& str,
              size_type pos2, size_type n2) const {
    enforce(pos2 <= str.size(), std::__throw_out_of_range, "");
    return compare(pos1, n1, str.data() + pos2,
                   std::min(n2, str.size() - pos2));
  }

  // Code from Jean-Francois Bastien (03/26/2007)
  int compare(const value_type* s) const {
    // Could forward to compare(0, size(), s, traits_type::length(s))
    // but that does two extra checks
    const size_type n1(size()), n2(traits_type::length(s));
    const int r = traits_type::compare(data(), s, std::min(n1, n2));
    return r != 0 ? r : n1 > n2 ? 1 : n1 < n2 ? -1 : 0;
  }

private:
  // Data
  Storage store_;
};

// non-member functions
// C++11 21.4.8.1/1
template <typename E, class T, class A, class S>
inline
basic_fbstring<E, T, A, S> operator+(const basic_fbstring<E, T, A, S>& lhs,
                                     const basic_fbstring<E, T, A, S>& rhs) {

  basic_fbstring<E, T, A, S> result;
  result.reserve(lhs.size() + rhs.size());
  result.append(lhs).append(rhs);
  return std::move(result);
}

// C++11 21.4.8.1/2
template <typename E, class T, class A, class S>
inline
basic_fbstring<E, T, A, S> operator+(basic_fbstring<E, T, A, S>&& lhs,
                                     const basic_fbstring<E, T, A, S>& rhs) {
  return std::move(lhs.append(rhs));
}

// C++11 21.4.8.1/3
template <typename E, class T, class A, class S>
inline
basic_fbstring<E, T, A, S> operator+(const basic_fbstring<E, T, A, S>& lhs,
                                     basic_fbstring<E, T, A, S>&& rhs) {
  if (rhs.capacity() >= lhs.size() + rhs.size()) {
    // Good, at least we don't need to reallocate
    return std::move(rhs.insert(0, lhs));
  }
  // Meh, no go. Forward to operator+(const&, const&).
  auto const& rhsC = rhs;
  return lhs + rhsC;
}

// C++11 21.4.8.1/4
template <typename E, class T, class A, class S>
inline
basic_fbstring<E, T, A, S> operator+(basic_fbstring<E, T, A, S>&& lhs,
                                     basic_fbstring<E, T, A, S>&& rhs) {
  return std::move(lhs.append(rhs));
}

// C++11 21.4.8.1/5
template <typename E, class T, class A, class S>
inline
basic_fbstring<E, T, A, S> operator+(
  const E* lhs,
  const basic_fbstring<E, T, A, S>& rhs) {
  //
  basic_fbstring<E, T, A, S> result;
  const auto len = basic_fbstring<E, T, A, S>::traits_type::length(lhs);
  result.reserve(len + rhs.size());
  result.append(lhs, len).append(rhs);
  return result;
}

// C++11 21.4.8.1/6
template <typename E, class T, class A, class S>
inline
basic_fbstring<E, T, A, S> operator+(
  const E* lhs,
  basic_fbstring<E, T, A, S>&& rhs) {
  //
  const auto len = basic_fbstring<E, T, A, S>::traits_type::length(lhs);
  if (rhs.capacity() >= len + rhs.size()) {
    // Good, at least we don't need to reallocate
    return std::move(rhs.insert(rhs.begin(), lhs, lhs + len));
  }
  // Meh, no go. Do it by hand since we have len already.
  basic_fbstring<E, T, A, S> result;
  result.reserve(len + rhs.size());
  result.append(lhs, len).append(rhs);
  return result;
}

// C++11 21.4.8.1/7
template <typename E, class T, class A, class S>
inline
basic_fbstring<E, T, A, S> operator+(
  E lhs,
  const basic_fbstring<E, T, A, S>& rhs) {

  basic_fbstring<E, T, A, S> result;
  result.reserve(1 + rhs.size());
  result.push_back(lhs);
  result.append(rhs);
  return result;
}

// C++11 21.4.8.1/8
template <typename E, class T, class A, class S>
inline
basic_fbstring<E, T, A, S> operator+(
  E lhs,
  basic_fbstring<E, T, A, S>&& rhs) {
  //
  if (rhs.capacity() > rhs.size()) {
    // Good, at least we don't need to reallocate
    return std::move(rhs.insert(rhs.begin(), lhs));
  }
  // Meh, no go. Forward to operator+(E, const&).
  auto const& rhsC = rhs;
  return lhs + rhsC;
}

// C++11 21.4.8.1/9
template <typename E, class T, class A, class S>
inline
basic_fbstring<E, T, A, S> operator+(
  const basic_fbstring<E, T, A, S>& lhs,
  const E* rhs) {

  typedef typename basic_fbstring<E, T, A, S>::size_type size_type;
  typedef typename basic_fbstring<E, T, A, S>::traits_type traits_type;

  basic_fbstring<E, T, A, S> result;
  const size_type len = traits_type::length(rhs);
  result.reserve(lhs.size() + len);
  result.append(lhs).append(rhs, len);
  return result;
}

// C++11 21.4.8.1/10
template <typename E, class T, class A, class S>
inline
basic_fbstring<E, T, A, S> operator+(
  basic_fbstring<E, T, A, S>&& lhs,
  const E* rhs) {
  //
  return std::move(lhs += rhs);
}

// C++11 21.4.8.1/11
template <typename E, class T, class A, class S>
inline
basic_fbstring<E, T, A, S> operator+(
  const basic_fbstring<E, T, A, S>& lhs,
  E rhs) {

  basic_fbstring<E, T, A, S> result;
  result.reserve(lhs.size() + 1);
  result.append(lhs);
  result.push_back(rhs);
  return result;
}

// C++11 21.4.8.1/12
template <typename E, class T, class A, class S>
inline
basic_fbstring<E, T, A, S> operator+(
  basic_fbstring<E, T, A, S>&& lhs,
  E rhs) {
  //
  return std::move(lhs += rhs);
}

template <typename E, class T, class A, class S>
inline
bool operator==(const basic_fbstring<E, T, A, S>& lhs,
                const basic_fbstring<E, T, A, S>& rhs) {
  return lhs.size() == rhs.size() && lhs.compare(rhs) == 0; }

template <typename E, class T, class A, class S>
inline
bool operator==(const typename basic_fbstring<E, T, A, S>::value_type* lhs,
                const basic_fbstring<E, T, A, S>& rhs) {
  return rhs == lhs; }

template <typename E, class T, class A, class S>
inline
bool operator==(const basic_fbstring<E, T, A, S>& lhs,
                const typename basic_fbstring<E, T, A, S>::value_type* rhs) {
  return lhs.compare(rhs) == 0; }

template <typename E, class T, class A, class S>
inline
bool operator!=(const basic_fbstring<E, T, A, S>& lhs,
                const basic_fbstring<E, T, A, S>& rhs) {
  return !(lhs == rhs); }

template <typename E, class T, class A, class S>
inline
bool operator!=(const typename basic_fbstring<E, T, A, S>::value_type* lhs,
                const basic_fbstring<E, T, A, S>& rhs) {
  return !(lhs == rhs); }

template <typename E, class T, class A, class S>
inline
bool operator!=(const basic_fbstring<E, T, A, S>& lhs,
                const typename basic_fbstring<E, T, A, S>::value_type* rhs) {
  return !(lhs == rhs); }

template <typename E, class T, class A, class S>
inline
bool operator<(const basic_fbstring<E, T, A, S>& lhs,
               const basic_fbstring<E, T, A, S>& rhs) {
  return lhs.compare(rhs) < 0; }

template <typename E, class T, class A, class S>
inline
bool operator<(const basic_fbstring<E, T, A, S>& lhs,
               const typename basic_fbstring<E, T, A, S>::value_type* rhs) {
  return lhs.compare(rhs) < 0; }

template <typename E, class T, class A, class S>
inline
bool operator<(const typename basic_fbstring<E, T, A, S>::value_type* lhs,
               const basic_fbstring<E, T, A, S>& rhs) {
  return rhs.compare(lhs) > 0; }

template <typename E, class T, class A, class S>
inline
bool operator>(const basic_fbstring<E, T, A, S>& lhs,
               const basic_fbstring<E, T, A, S>& rhs) {
  return rhs < lhs; }

template <typename E, class T, class A, class S>
inline
bool operator>(const basic_fbstring<E, T, A, S>& lhs,
               const typename basic_fbstring<E, T, A, S>::value_type* rhs) {
  return rhs < lhs; }

template <typename E, class T, class A, class S>
inline
bool operator>(const typename basic_fbstring<E, T, A, S>::value_type* lhs,
               const basic_fbstring<E, T, A, S>& rhs) {
  return rhs < lhs; }

template <typename E, class T, class A, class S>
inline
bool operator<=(const basic_fbstring<E, T, A, S>& lhs,
                const basic_fbstring<E, T, A, S>& rhs) {
  return !(rhs < lhs); }

template <typename E, class T, class A, class S>
inline
bool operator<=(const basic_fbstring<E, T, A, S>& lhs,
                const typename basic_fbstring<E, T, A, S>::value_type* rhs) {
  return !(rhs < lhs); }

template <typename E, class T, class A, class S>
inline
bool operator<=(const typename basic_fbstring<E, T, A, S>::value_type* lhs,
                const basic_fbstring<E, T, A, S>& rhs) {
  return !(rhs < lhs); }

template <typename E, class T, class A, class S>
inline
bool operator>=(const basic_fbstring<E, T, A, S>& lhs,
                const basic_fbstring<E, T, A, S>& rhs) {
  return !(lhs < rhs); }

template <typename E, class T, class A, class S>
inline
bool operator>=(const basic_fbstring<E, T, A, S>& lhs,
                const typename basic_fbstring<E, T, A, S>::value_type* rhs) {
  return !(lhs < rhs); }

template <typename E, class T, class A, class S>
inline
bool operator>=(const typename basic_fbstring<E, T, A, S>::value_type* lhs,
                const basic_fbstring<E, T, A, S>& rhs) {
 return !(lhs < rhs);
}

// C++11 21.4.8.8
template <typename E, class T, class A, class S>
void swap(basic_fbstring<E, T, A, S>& lhs, basic_fbstring<E, T, A, S>& rhs) {
  lhs.swap(rhs);
}

// TODO: make this faster.
template <typename E, class T, class A, class S>
inline
std::basic_istream<
  typename basic_fbstring<E, T, A, S>::value_type,
  typename basic_fbstring<E, T, A, S>::traits_type>&
  operator>>(
    std::basic_istream<typename basic_fbstring<E, T, A, S>::value_type,
    typename basic_fbstring<E, T, A, S>::traits_type>& is,
    basic_fbstring<E, T, A, S>& str) {
  typename std::basic_istream<E, T>::sentry sentry(is);
  typedef std::basic_istream<typename basic_fbstring<E, T, A, S>::value_type,
                             typename basic_fbstring<E, T, A, S>::traits_type>
                        __istream_type;
  typedef typename __istream_type::ios_base __ios_base;
  size_t extracted = 0;
  auto err = __ios_base::goodbit;
  if (sentry) {
    auto n = is.width();
    if (n <= 0) {
      n = str.max_size();
    }
    str.erase();
    for (auto got = is.rdbuf()->sgetc(); extracted != size_t(n); ++extracted) {
      if (got == T::eof()) {
        err |= __ios_base::eofbit;
        is.width(0);
        break;
      }
      if (isspace(got)) break;
      str.push_back(got);
      got = is.rdbuf()->snextc();
    }
  }
  if (!extracted) {
    err |= __ios_base::failbit;
  }
  if (err) {
    is.setstate(err);
  }
  return is;
}

template <typename E, class T, class A, class S>
inline
std::basic_ostream<typename basic_fbstring<E, T, A, S>::value_type,
                   typename basic_fbstring<E, T, A, S>::traits_type>&
operator<<(
  std::basic_ostream<typename basic_fbstring<E, T, A, S>::value_type,
  typename basic_fbstring<E, T, A, S>::traits_type>& os,
    const basic_fbstring<E, T, A, S>& str) {
#if _LIBCPP_VERSION
  typename std::basic_ostream<
    typename basic_fbstring<E, T, A, S>::value_type,
    typename basic_fbstring<E, T, A, S>::traits_type>::sentry __s(os);
  if (__s) {
    typedef std::ostreambuf_iterator<
      typename basic_fbstring<E, T, A, S>::value_type,
      typename basic_fbstring<E, T, A, S>::traits_type> _Ip;
    size_t __len = str.size();
    bool __left =
      (os.flags() & std::ios_base::adjustfield) == std::ios_base::left;
    if (__pad_and_output(_Ip(os),
                         str.data(),
                         __left ? str.data() + __len : str.data(),
                         str.data() + __len,
                         os,
                         os.fill()).failed()) {
      os.setstate(std::ios_base::badbit | std::ios_base::failbit);
    }
  }
#else
  std::__ostream_insert(os, str.data(), str.size());
#endif
  return os;
}

#ifndef _LIBSTDCXX_FBSTRING

template <typename E, class T, class A, class S>
inline
std::basic_istream<typename basic_fbstring<E, T, A, S>::value_type,
                   typename basic_fbstring<E, T, A, S>::traits_type>&
getline(
  std::basic_istream<typename basic_fbstring<E, T, A, S>::value_type,
  typename basic_fbstring<E, T, A, S>::traits_type>& is,
    basic_fbstring<E, T, A, S>& str,
  typename basic_fbstring<E, T, A, S>::value_type delim) {
  // Use the nonstandard getdelim()
  char * buf = nullptr;
  size_t size = 0;
  for (;;) {
    // This looks quadratic but it really depends on realloc
    auto const newSize = size + 128;
    buf = static_cast<char*>(checkedRealloc(buf, newSize));
    is.getline(buf + size, newSize - size, delim);
    if (is.bad() || is.eof() || !is.fail()) {
      // done by either failure, end of file, or normal read
      size += std::strlen(buf + size);
      break;
    }
    // Here we have failed due to too short a buffer
    // Minus one to discount the terminating '\0'
    size = newSize - 1;
    assert(buf[size] == 0);
    // Clear the error so we can continue reading
    is.clear();
  }
  basic_fbstring<E, T, A, S> result(buf, size, size + 1,
                                    AcquireMallocatedString());
  result.swap(str);
  return is;
}

template <typename E, class T, class A, class S>
inline
std::basic_istream<typename basic_fbstring<E, T, A, S>::value_type,
                   typename basic_fbstring<E, T, A, S>::traits_type>&
getline(
  std::basic_istream<typename basic_fbstring<E, T, A, S>::value_type,
  typename basic_fbstring<E, T, A, S>::traits_type>& is,
  basic_fbstring<E, T, A, S>& str) {
  // Just forward to the version with a delimiter
  return getline(is, str, '\n');
}

#endif

template <typename E1, class T, class A, class S>
const typename basic_fbstring<E1, T, A, S>::size_type
basic_fbstring<E1, T, A, S>::npos =
              static_cast<typename basic_fbstring<E1, T, A, S>::size_type>(-1);

#ifndef _LIBSTDCXX_FBSTRING
// basic_string compatibility routines

template <typename E, class T, class A, class S>
inline
bool operator==(const basic_fbstring<E, T, A, S>& lhs,
                const std::string& rhs) {
  return lhs.compare(0, lhs.size(), rhs.data(), rhs.size()) == 0;
}

template <typename E, class T, class A, class S>
inline
bool operator==(const std::string& lhs,
                const basic_fbstring<E, T, A, S>& rhs) {
  return rhs == lhs;
}

template <typename E, class T, class A, class S>
inline
bool operator!=(const basic_fbstring<E, T, A, S>& lhs,
                const std::string& rhs) {
  return !(lhs == rhs);
}

template <typename E, class T, class A, class S>
inline
bool operator!=(const std::string& lhs,
                const basic_fbstring<E, T, A, S>& rhs) {
  return !(lhs == rhs);
}

#if !defined(_LIBSTDCXX_FBSTRING)
typedef basic_fbstring<char> fbstring;
#endif

// fbstring is relocatable
template <class T, class R, class A, class S>
FOLLY_ASSUME_RELOCATABLE(basic_fbstring<T, R, A, S>);

#else
_GLIBCXX_END_NAMESPACE_VERSION
#endif

} // namespace folly

#ifndef _LIBSTDCXX_FBSTRING

// Hash functions to make fbstring usable with e.g. hash_map
//
// Handle interaction with different C++ standard libraries, which
// expect these types to be in different namespaces.

#define FOLLY_FBSTRING_HASH1(T) \
  template <> \
  struct hash< ::folly::basic_fbstring<T> > { \
    size_t operator()(const ::folly::fbstring& s) const { \
      return ::folly::hash::fnv32_buf(s.data(), s.size()); \
    } \
  };

// The C++11 standard says that these four are defined
#define FOLLY_FBSTRING_HASH \
  FOLLY_FBSTRING_HASH1(char) \
  FOLLY_FBSTRING_HASH1(char16_t) \
  FOLLY_FBSTRING_HASH1(char32_t) \
  FOLLY_FBSTRING_HASH1(wchar_t)

namespace std {

FOLLY_FBSTRING_HASH

}  // namespace std

#if FOLLY_HAVE_DEPRECATED_ASSOC
#if defined(_GLIBCXX_SYMVER) && !defined(__BIONIC__)
namespace __gnu_cxx {

FOLLY_FBSTRING_HASH

}  // namespace __gnu_cxx
#endif // _GLIBCXX_SYMVER && !__BIONIC__
#endif // FOLLY_HAVE_DEPRECATED_ASSOC

#undef FOLLY_FBSTRING_HASH
#undef FOLLY_FBSTRING_HASH1

#endif // _LIBSTDCXX_FBSTRING

#pragma GCC diagnostic pop

#undef FBSTRING_DISABLE_ADDRESS_SANITIZER
#undef throw
#undef FBSTRING_LIKELY
#undef FBSTRING_UNLIKELY

#endif // FOLLY_BASE_FBSTRING_H_
