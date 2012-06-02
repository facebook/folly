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

// Andrei Alexandrescu (aalexandre)

/**
 * Vector type. Drop-in replacement for std::vector featuring
 * significantly faster primitives, see e.g. benchmark results at
 * https:*phabricator.fb.com/D235852.
 *
 * In order for a type to be used with fbvector, it must be
 * relocatable, see Traits.h.
 *
 * For user-defined types you must specialize templates
 * appropriately. Consult Traits.h for ways to do so and for a handy
 * family of macros FOLLY_ASSUME_FBVECTOR_COMPATIBLE*.
 *
 * For more information and documentation see folly/docs/FBVector.md
 */

#ifndef FOLLY_FBVECTOR_H_
#define FOLLY_FBVECTOR_H_

#include "folly/Foreach.h"
#include "folly/Malloc.h"
#include "folly/Traits.h"
#include <iterator>
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <cassert>
#include <boost/type_traits.hpp>
#include <boost/operators.hpp>
#include <boost/utility/enable_if.hpp>
#include <type_traits>

namespace folly {
/**
 * Forward declaration for use by FOLLY_ASSUME_FBVECTOR_COMPATIBLE_2,
 * see folly/Traits.h.
 */
template <typename T, class Allocator = std::allocator<T> >
class fbvector;
}

// You can define an fbvector of fbvectors.
FOLLY_ASSUME_FBVECTOR_COMPATIBLE_2(folly::fbvector);

namespace folly {
namespace fbvector_detail {

/**
 * isForwardIterator<T>::value yields true if T is a forward iterator
 * or better, and false otherwise.
 */
template <class It> struct isForwardIterator {
  enum { value = boost::is_convertible<
         typename std::iterator_traits<It>::iterator_category,
         std::forward_iterator_tag>::value
  };
};

/**
 * Destroys all elements in the range [b, e). If the type referred to
 * by the iterators has a trivial destructor, does nothing.
 */
template <class It>
void destroyRange(It b, It e) {
  typedef typename boost::remove_reference<decltype(*b)>::type T;
  if (boost::has_trivial_destructor<T>::value) return;
  for (; b != e; ++b) {
    (*b).~T();
  }
}

/**
 * Moves the "interesting" part of value to the uninitialized memory
 * at address addr, and leaves value in a destroyable state.
 */

template <class T>
typename boost::enable_if_c<
  boost::has_trivial_assign<T>::value
>::type
uninitialized_destructive_move(T& value, T* addr) {
  // Just assign the thing; this is most efficient
  *addr = value;
}

template <class T>
typename boost::enable_if_c<
  !boost::has_trivial_assign<T>::value &&
  boost::has_nothrow_constructor<T>::value
>::type
uninitialized_destructive_move(T& value, T* addr) {
  // Cheap default constructor - move and reinitialize
  memcpy(addr, &value, sizeof(T));
  new(&value) T;
}

template <class T>
typename std::enable_if<
  !boost::has_trivial_assign<T>::value &&
  !boost::has_nothrow_constructor<T>::value
>::type
uninitialized_destructive_move(T& value, T* addr) {
  // User defined move construction.

  // TODO: we should probably prefer this over the above memcpy()
  // version when the type has a user-defined move constructor.  We
  // don't right now because 4.6 doesn't implement
  // std::is_move_constructible<> yet.
  new (addr) T(std::move(value));
}

/**
 * Fills n objects of type T starting at address b with T's default
 * value. If the operation throws, destroys all objects constructed so
 * far and calls free(b).
 */
template <class T>
void uninitializedFillDefaultOrFree(T * b, size_t n) {
  if (boost::is_arithmetic<T>::value || boost::is_pointer<T>::value) {
    if (n <= 16384 / sizeof(T)) {
      memset(b, 0, n * sizeof(T));
    } else {
      goto duff_fill;
    }
  } else if (boost::has_nothrow_constructor<T>::value) {
    duff_fill:
    auto i = b;
    auto const e1 = b + (n & ~size_t(7));
    for (; i != e1; i += 8) {
      new(i) T();
      new(i + 1) T();
      new(i + 2) T();
      new(i + 3) T();
      new(i + 4) T();
      new(i + 5) T();
      new(i + 6) T();
      new(i + 7) T();
    }
    for (auto const e = b + n; i != e; ++i) {
      new(i) T();
    }
  } else {
    // Conservative approach
    auto i = b;
    try {
      for (auto const e = b + n; i != e; ++i) {
        new(i) T;
      }
    } catch (...) {
      destroyRange(b, i);
      free(b);
      throw;
    }
  }
}

/**
 * Fills n objects of type T starting at address b with value. If the
 * operation throws, destroys all objects constructed so far and calls
 * free(b).
 */
template <class T>
void uninitializedFillOrFree(T * b, size_t n, const T& value) {
  auto const e = b + n;
  if (boost::has_trivial_copy<T>::value) {
    auto i = b;
    auto const e1 = b + (n & ~size_t(7));
    for (; i != e1; i += 8) {
      new(i) T(value);
      new(i + 1) T(value);
      new(i + 2) T(value);
      new(i + 3) T(value);
      new(i + 4) T(value);
      new(i + 5) T(value);
      new(i + 6) T(value);
      new(i + 7) T(value);
    }
    for (; i != e; ++i) {
      new(i) T(value);
    }
  } else {
    // Conservative approach
    auto i = b;
    try {
      for (; i != e; ++i) {
        new(i) T(value);
      }
    } catch (...) {
      destroyRange(b, i);
      free(b);
      throw;
    }
  }
}
} // namespace fbvector_detail

/**
 * This is the std::vector replacement. For conformity, fbvector takes
 * the same template parameters, but it doesn't use the
 * allocator. Instead, it uses malloc, and when present, jemalloc's
 * extensions.
 */
template <class T, class Allocator>
class fbvector : private boost::totally_ordered<fbvector<T,Allocator> > {
  bool isSane() const {
    return
      begin() <= end() &&
      empty() == (size() == 0) &&
      empty() == (begin() == end()) &&
      size() <= max_size() &&
      capacity() <= max_size() &&
      size() <= capacity() &&

      // Either we have no capacity or our pointers should make sense:
      ((!b_ && !e_ && !z_) || (b_ != z_ && e_ <= z_));
  }

  struct Invariant {
#ifndef NDEBUG
    explicit Invariant(const fbvector& s) : s_(s) {
      assert(s_.isSane());
    }
    ~Invariant() {
      assert(s_.isSane());
    }
  private:
    const fbvector& s_;
#else
    explicit Invariant(const fbvector&) {}
#endif
    Invariant& operator=(const Invariant&);
  };

public:

// types:
  typedef T value_type;
  typedef value_type& reference;
  typedef const value_type& const_reference;
  typedef T* iterator;
  typedef const T* const_iterator;
  typedef size_t size_type;
  typedef ssize_t difference_type;
  // typedef typename allocator_traits<Allocator>::pointer pointer;
  // typedef typename allocator_traits<Allocator>::const_pointer const_pointer;
  typedef Allocator allocator_type;
  typedef typename Allocator::pointer pointer;
  typedef typename Allocator::const_pointer const_pointer;
  typedef std::reverse_iterator<iterator> reverse_iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

// 23.3.6.1 construct/copy/destroy:
  fbvector() : b_(NULL), e_(NULL), z_(NULL) {}

  explicit fbvector(const Allocator&) {
    new(this) fbvector;
  }

  explicit fbvector(const size_type n) {
    if (n == 0) {
      b_ = e_ = z_ = 0;
      return;
    }

    auto const nBytes = goodMallocSize(n * sizeof(T));
    b_ = static_cast<T*>(malloc(nBytes));
    fbvector_detail::uninitializedFillDefaultOrFree(b_, n);
    e_ = b_ + n;
    z_ = b_ + nBytes / sizeof(T);
  }

  fbvector(const size_type n, const T& value) {
    if (!n) {
      b_ = e_ = z_ = 0;
      return;
    }

    auto const nBytes = goodMallocSize(n * sizeof(T));
    b_ = static_cast<T*>(malloc(nBytes));
    fbvector_detail::uninitializedFillOrFree(b_, n, value);
    e_ = b_ + n;
    z_ = b_ + nBytes / sizeof(T);
  }

  fbvector(const size_type n, const T& value, const Allocator&) {
    new(this) fbvector(n, value);
  }

  template <class InputIteratorOrNum>
  fbvector(InputIteratorOrNum first, InputIteratorOrNum last) {
    new(this) fbvector;
    assign(first, last);
  }

  template <class InputIterator>
  fbvector(InputIterator first, InputIterator last,
           const Allocator&) {
    new(this) fbvector(first, last);
  }

  fbvector(const fbvector& rhs) {
    new(this) fbvector(rhs.begin(), rhs.end());
  }
  fbvector(const fbvector& rhs, const Allocator&) {
    new(this) fbvector(rhs);
  }

  fbvector(fbvector&& o, const Allocator& = Allocator())
    : b_(o.b_)
    , e_(o.e_)
    , z_(o.z_)
  {
    o.b_ = o.e_ = o.z_ = 0;
  }

  fbvector(std::initializer_list<T> il, const Allocator& = Allocator()) {
    new(this) fbvector(il.begin(), il.end());
  }

  ~fbvector() {
    // fbvector only works with relocatable objects. We insert this
    // static check inside the destructor because pretty much any
    // instantiation of fbvector<T> will generate the destructor (and
    // therefore refuse compilation if the assertion fails). To see
    // how you can enable IsRelocatable for your type, refer to the
    // definition of IsRelocatable in Traits.h.
    BOOST_STATIC_ASSERT(IsRelocatable<T>::value);
    if (!b_) return;
    fbvector_detail::destroyRange(b_, e_);
    free(b_);
  }
  fbvector& operator=(const fbvector& rhs) {
    assign(rhs.begin(), rhs.end());
    return *this;
  }

  fbvector& operator=(fbvector&& v) {
    clear();
    swap(v);
    return *this;
  }

  fbvector& operator=(std::initializer_list<T> il) {
    assign(il.begin(), il.end());
    return *this;
  }

  bool operator==(const fbvector& rhs) const {
    return size() == rhs.size() && std::equal(begin(), end(), rhs.begin());
  }

  bool operator<(const fbvector& rhs) const {
    return std::lexicographical_compare(begin(), end(),
                                        rhs.begin(), rhs.end());
  }

private:
  template <class InputIterator>
  void assignImpl(InputIterator first, InputIterator last, boost::false_type) {
    // Pair of iterators
    if (fbvector_detail::isForwardIterator<InputIterator>::value) {
      if (b_ <= &*first && &*first < e_) {
        // Aliased assign, work on the side
        fbvector result(first, last);
        result.swap(*this);
        return;
      }

      auto const oldSize = size();
      auto const newSize = std::distance(first, last);

      if (static_cast<difference_type>(oldSize) >= newSize) {
        // No reallocation, nice
        auto const newEnd = std::copy(first, last, b_);
        fbvector_detail::destroyRange(newEnd, e_);
        e_ = newEnd;
        return;
      }

      // Must reallocate - just do it on the side
      auto const nBytes = goodMallocSize(newSize * sizeof(T));
      auto const b = static_cast<T*>(malloc(nBytes));
      std::uninitialized_copy(first, last, b);
      this->fbvector::~fbvector();
      b_ = b;
      e_ = b + newSize;
      z_ = b_ + nBytes / sizeof(T);
    } else {
      // Input iterator sucks
      FOR_EACH (i, *this) {
        if (first == last) {
          fbvector_detail::destroyRange(i, e_);
          e_ = i;
          return;
        }
        *i = *first;
        ++first;
      }
      FOR_EACH_RANGE (i, first, last) {
        push_back(*i);
      }
    }
  }

  void assignImpl(const size_type newSize, const T value, boost::true_type) {
    // Arithmetic type, forward back to unambiguous definition
    assign(newSize, value);
  }

public:
  // Classic ambiguity (and a lot of unnecessary complexity) in
  // std::vector: assign(10, 20) for vector<int> means "assign 10
  // elements all having the value 20" but is intercepted by the
  // two-iterators overload assign(first, last). So we need to
  // disambiguate here. There is no pretty solution. We use here
  // overloading based on is_arithmetic. Method insert has the same
  // issue (and the same solution in this implementation).
  template <class InputIteratorOrNum>
  void assign(InputIteratorOrNum first, InputIteratorOrNum last) {
    assignImpl(first, last, boost::is_arithmetic<InputIteratorOrNum>());
  }

  void assign(const size_type newSize, const T& value) {
    if (b_ <= &value && &value < e_) {
      // Need to check for aliased assign, sigh
      return assign(newSize, T(value));
    }

    auto const oldSize = size();
    if (oldSize >= newSize) {
      // No reallocation, nice
      auto const newEnd = b_ + newSize;
      fbvector_detail::destroyRange(newEnd, e_);
      e_ = newEnd;
      return;
    }

    // Need to reallocate
    if (reserve_in_place(newSize)) {
      // Careful here, fill and uninitialized_fill may throw. The
      // latter is transactional, so no need to worry about a
      // buffer partially filled in case of exception.
      std::fill(b_, e_, value);
      auto const newEnd = b_ + newSize;
      std::uninitialized_fill(e_, newEnd, value);
      e_ = newEnd;
      return;
    }

    // Cannot expand or jemalloc not present at all; must just
    // allocate a new chunk and discard the old one. This is
    // tantamount with creating a new fbvector altogether. This won't
    // recurse infinitely; the constructor implements its own.
    fbvector temp(newSize, value);
    temp.swap(*this);
  }

  void assign(std::initializer_list<T> il) {
    assign(il.begin(), il.end());
  }

  allocator_type get_allocator() const {
    // whatevs
    return allocator_type();
  }

// iterators:
  iterator begin() {
    return b_;
  }
  const_iterator begin() const {
    return b_;
  }
  iterator end() {
    return e_;
  }
  const_iterator end() const {
    return e_;
  }
  reverse_iterator rbegin() {
    return reverse_iterator(end());
  }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }
  reverse_iterator rend() {
    return reverse_iterator(begin());
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }
  const_iterator cbegin() const {
    return b_;
  }
  const_iterator cend() const {
    return e_;
  }

// 23.3.6.2 capacity:
  size_type size() const {
    return e_ - b_;
  }

  size_type max_size() {
    // good luck gettin' there
    return ~size_type(0);
  }

  void resize(const size_type sz) {
    auto const oldSize = size();
    if (sz <= oldSize) {
      auto const newEnd = b_ + sz;
      fbvector_detail::destroyRange(newEnd, e_);
      e_ = newEnd;
    } else {
      // Must expand
      reserve(sz);
      auto newEnd = b_ + sz;
      std::uninitialized_fill(e_, newEnd, T());
      e_ = newEnd;
    }
  }

  void resize(const size_type sz, const T& c) {
    auto const oldSize = size();
    if (sz <= oldSize) {
      auto const newEnd = b_ + sz;
      fbvector_detail::destroyRange(newEnd, e_);
      e_ = newEnd;
    } else {
      // Must expand
      reserve(sz);
      auto newEnd = b_ + sz;
      std::uninitialized_fill(e_, newEnd, c);
      e_ = newEnd;
    }
  }

  size_type capacity() const {
    return z_ - b_;
  }
  bool empty() const {
    return b_ == e_;
  }

private:
  bool reserve_in_place(const size_type n) {
    auto const crtCapacity = capacity();
    if (n <= crtCapacity) return true;
    if (!rallocm) return false;

    // using jemalloc's API. Don't forget that jemalloc can never grow
    // in place blocks smaller than 4096 bytes.
    auto const crtCapacityBytes = crtCapacity * sizeof(T);
    if (crtCapacityBytes < jemallocMinInPlaceExpandable) return false;

    auto const newCapacityBytes = goodMallocSize(n * sizeof(T));
    void* p = b_;
    if (rallocm(&p, NULL, newCapacityBytes, 0, ALLOCM_NO_MOVE)
        != ALLOCM_SUCCESS) {
      return false;
    }

    // Managed to expand in place, reflect that in z_
    assert(b_ == p);
    z_ = b_ + newCapacityBytes / sizeof(T);
    return true;
  }

  void reserve_with_move(const size_type n) {
    // Here we can be sure we'll need to do a full reallocation
    auto const crtCapacity = capacity();
    assert(crtCapacity < n); // reserve_in_place should have taken
                             // care of this
    auto const newCapacityBytes = goodMallocSize(n * sizeof(T));
    auto b = static_cast<T*>(malloc(newCapacityBytes));
    auto const oldSize = size();
    memcpy(b, b_, oldSize * sizeof(T));
    // Done with the old chunk. Free but don't call destructors!
    free(b_);
    b_ = b;
    e_ = b_ + oldSize;
    z_ = b_ + newCapacityBytes / sizeof(T);
    // done with the old chunk
  }

public:
  void reserve(const size_type n) {
    if (reserve_in_place(n)) return;
    reserve_with_move(n);
  }

  void shrink_to_fit() {
    if (!rallocm) return;

    // using jemalloc's API. Don't forget that jemalloc can never
    // shrink in place blocks smaller than 4096 bytes.
    void* p = b_;
    auto const crtCapacityBytes = capacity() * sizeof(T);
    auto const newCapacityBytes = goodMallocSize(size() * sizeof(T));
    if (crtCapacityBytes >= jemallocMinInPlaceExpandable &&
        rallocm(&p, NULL, newCapacityBytes, 0, ALLOCM_NO_MOVE)
        == ALLOCM_SUCCESS) {
      // Celebrate
      z_ = b_ + newCapacityBytes / sizeof(T);
    }
  }

// element access
  reference operator[](size_type n) {
    assert(n < size());
    return b_[n];
  }
  const_reference operator[](size_type n) const {
    assert(n < size());
    return b_[n];
  }
  const_reference at(size_type n) const {
    if (n > size()) {
      throw std::out_of_range("fbvector: index is greater than size.");
    }
    return (*this)[n];
  }
  reference at(size_type n) {
    auto const& cThis = *this;
    return const_cast<reference>(cThis.at(n));
  }
  reference front() {
    assert(!empty());
    return *b_;
  }
  const_reference front() const {
    assert(!empty());
    return *b_;
  }
  reference back()  {
    assert(!empty());
    return e_[-1];
  }
  const_reference back() const {
    assert(!empty());
    return e_[-1];
  }

// 23.3.6.3 data access
  T* data() {
    return b_;
  }
  const T* data() const {
    return b_;
  }

private:
  size_t computePushBackCapacity() const {
    return empty() ? std::max(64 / sizeof(T), size_t(1))
      : capacity() < jemallocMinInPlaceExpandable ? capacity() * 2
      : (capacity() * 3) / 2;
  }

public:
// 23.3.6.4 modifiers:
  template <class... Args>
  void emplace_back(Args&&... args) {
    if (e_ == z_) {
      if (!reserve_in_place(size() + 1)) {
        reserve_with_move(computePushBackCapacity());
      }
    }
    new (e_) T(std::forward<Args>(args)...);
    ++e_;
  }

  void push_back(T x) {
    if (e_ == z_) {
      if (!reserve_in_place(size() + 1)) {
        reserve_with_move(computePushBackCapacity());
      }
    }
    fbvector_detail::uninitialized_destructive_move(x, e_);
    ++e_;
  }

private:
  bool expand() {
    if (!rallocm) return false;
    auto const capBytes = capacity() * sizeof(T);
    if (capBytes < jemallocMinInPlaceExpandable) return false;
    auto const newCapBytes = goodMallocSize(capBytes + sizeof(T));
    void * bv = b_;
    if (rallocm(&bv, NULL, newCapBytes, 0, ALLOCM_NO_MOVE) != ALLOCM_SUCCESS) {
      return false;
    }
    // Managed to expand in place
    assert(bv == b_); // nothing moved
    z_ = b_ + newCapBytes / sizeof(T);
    assert(capacity() > capBytes / sizeof(T));
    return true;
  }

public:
  void pop_back() {
    assert(!empty());
    --e_;
    if (!boost::has_trivial_destructor<T>::value) {
      e_->T::~T();
    }
  }
  // template <class... Args>
  // iterator emplace(const_iterator position, Args&&... args);

  iterator insert(const_iterator position, T x) {
    size_t newSize; // intentionally uninitialized
    if (e_ == z_ && !reserve_in_place(newSize = size() + 1)) {
      // Can't reserve in place, make a copy
      auto const offset = position - cbegin();
      fbvector tmp;
      tmp.reserve(newSize);
      memcpy(tmp.b_, b_, offset * sizeof(T));
      fbvector_detail::uninitialized_destructive_move(
        x,
        tmp.b_ + offset);
      memcpy(tmp.b_ + offset + 1, b_ + offset, (size() - offset) * sizeof(T));
      // Brutally reassign this to refer to tmp's guts
      free(b_);
      b_ = tmp.b_;
      e_ = b_ + newSize;
      z_ = tmp.z_;
      // get rid of tmp's guts
      new(&tmp) fbvector;
      return begin() + offset;
    }
    // Here we have enough room
    memmove(const_cast<T*>(&*position) + 1,
            const_cast<T*>(&*position),
            sizeof(T) * (e_ - position));
    fbvector_detail::uninitialized_destructive_move(
      x,
      const_cast<T*>(&*position));
    ++e_;
    return const_cast<iterator>(position);
  }

  iterator insert(const_iterator position, const size_type n, const T& x) {
    if (e_ + n >= z_) {
      if (b_ <= &x && &x < e_) {
        // Ew, aliased insert
        auto copy = x;
        return insert(position, n, copy);
      }
      auto const m = position - b_;
      reserve(size() + n);
      position = b_ + m;
    }
    memmove(const_cast<T*>(position) + n,
            position,
            sizeof(T) * (e_ - position));
    if (boost::has_trivial_copy<T>::value) {
      std::uninitialized_fill(const_cast<T*>(position),
                              const_cast<T*>(position) + n,
                              x);
    } else {
      try {
        std::uninitialized_fill(const_cast<T*>(position),
                                const_cast<T*>(position) + n,
                                x);
      } catch (...) {
        // Oops, put things back where they were
        memmove(const_cast<T*>(position),
                position + n,
                sizeof(T) * (e_ - position));
        throw;
      }
    }
    e_ += n;
    return const_cast<iterator>(position);
  }

private:
  template <class InputIterator>
  iterator insertImpl(const_iterator position,
                      InputIterator first, InputIterator last,
                      boost::false_type) {
    // Pair of iterators
    if (fbvector_detail::isForwardIterator<InputIterator>::value) {
      // Can compute distance
      auto const n = std::distance(first, last);
      if (e_ + n >= z_) {
        if (b_ <= &*first && &*first < e_) {
          // Ew, aliased insert
          goto conservative;
        }
        auto const m = position - b_;
        reserve(size() + n);
        position = b_ + m;
      }
      memmove(const_cast<T*>(position) + n,
              position,
              sizeof(T) * (e_ - position));
      try {
        std::uninitialized_copy(first, last,
                           const_cast<T*>(position));
      } catch (...) {
        // Oops, put things back where they were
        memmove(const_cast<T*>(position),
                position + n,
                sizeof(T) * (e_ - position));
        throw;
      }
      e_ += n;
      return const_cast<iterator>(position);
    } else {
      // Cannot compute distance, crappy approach
      // TODO: OPTIMIZE
      conservative:
      fbvector result(cbegin(), position);
      auto const offset = result.size();
      FOR_EACH_RANGE (i, first, last) {
        result.push_back(*i);
      }
      result.insert(result.end(), position, cend());
      result.swap(*this);
      return begin() + offset;
    }
  }

  iterator insertImpl(const_iterator position,
                      const size_type count, const T value, boost::true_type) {
    // Forward back to unambiguous function
    return insert(position, count, value);
  }

public:
  template <class InputIteratorOrNum>
  iterator insert(const_iterator position, InputIteratorOrNum first,
                  InputIteratorOrNum last) {
    return insertImpl(position, first, last,
                      boost::is_arithmetic<InputIteratorOrNum>());
  }

  iterator insert(const_iterator position, std::initializer_list<T> il) {
    return insert(position, il.begin(), il.end());
  }

  iterator erase(const_iterator position) {
    if (position == e_) return e_;
    auto p = const_cast<T*>(position);
    (*p).T::~T();
    memmove(p, p + 1, sizeof(T) * (e_ - p - 1));
    --e_;
    return p;
  }

  iterator erase(const_iterator first, const_iterator last) {
    assert(first <= last);
    auto p1 = const_cast<T*>(first);
    auto p2 = const_cast<T*>(last);
    fbvector_detail::destroyRange(p1, p2);
    memmove(p1, last, sizeof(T) * (e_ - last));
    e_ -= last - first;
    return p1;
  }

  void swap(fbvector& rhs) {
    std::swap(b_, rhs.b_);
    std::swap(e_, rhs.e_);
    std::swap(z_, rhs.z_);
  }

  void clear() {
    fbvector_detail::destroyRange(b_, e_);
    e_ = b_;
  }

private:
  // Data
  T *b_, *e_, *z_;
};

template <class T, class A>
bool operator!=(const fbvector<T, A>& lhs,
                const fbvector<T, A>& rhs) {
  return !(lhs == rhs);
}

template <class T, class A>
void swap(fbvector<T, A>& lhs, fbvector<T, A>& rhs) {
  lhs.swap(rhs);
}

/**
 * Resizes *v to exactly n elements.  May reallocate the vector to a
 * smaller buffer if too much space will be left unused.
 */
template <class T>
static void compactResize(folly::fbvector<T> * v, size_t size) {
  auto const oldCap = v->capacity();
  if (oldCap > size + 1024 && size < oldCap * 0.3) {
    // Too much slack memory, reallocate a smaller buffer
    auto const oldSize = v->size();
    if (size <= oldSize) {
      // Shrink
      folly::fbvector<T>(v->begin(), v->begin() + size).swap(*v);
    } else {
      // Expand
      folly::fbvector<T> temp;
      temp.reserve(size);
      copy(v->begin(), v->end(), back_inserter(temp));
      temp.resize(size);
      temp.swap(*v);
    }
  } else {
    // Nolo contendere
    v->resize(size);
  }
}

} // namespace folly

#endif // FOLLY_FBVECTOR_H_
