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

/*
 * Nicholas Ormrod      (njormrod)
 * Andrei Alexandrescu  (aalexandre)
 *
 * FBVector is Facebook's drop-in implementation of std::vector. It has special
 * optimizations for use with relocatable types and jemalloc.
 */

#ifndef FOLLY_FBVECTOR_H
#define FOLLY_FBVECTOR_H

//=============================================================================
// headers

#include <algorithm>
#include <cassert>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "folly/Likely.h"
#include "folly/Malloc.h"
#include "folly/Traits.h"

#include <boost/operators.hpp>

// some files expected these from FBVector
#include <limits>
#include "folly/Foreach.h"
#include <boost/type_traits.hpp>
#include <boost/utility/enable_if.hpp>

//=============================================================================
// forward declaration

#ifdef FOLLY_BENCHMARK_USE_NS_IFOLLY
namespace Ifolly {
#else
namespace folly {
#endif
  template <class T, class Allocator = std::allocator<T>>
  class fbvector;
}

//=============================================================================
// compatibility

#if __GNUC__ < 4 || __GNUC__ == 4 && __GNUC_MINOR__ < 7
// PLEASE UPGRADE TO GCC 4.7 or above
#define FOLLY_FBV_COMPATIBILITY_MODE
#endif

#ifndef FOLLY_FBV_COMPATIBILITY_MODE

namespace folly {

template <typename A>
struct fbv_allocator_traits
  : std::allocator_traits<A> {};

template <typename T>
struct fbv_is_nothrow_move_constructible
  : std::is_nothrow_move_constructible<T> {};

template <typename T, typename... Args>
struct fbv_is_nothrow_constructible
  : std::is_nothrow_constructible<T, Args...> {};

template <typename T>
struct fbv_is_copy_constructible
  : std::is_copy_constructible<T> {};

}

#else

namespace folly {

template <typename A>
struct fbv_allocator_traits {
  static_assert(sizeof(A) == 0,
    "If you want to use a custom allocator, then you must upgrade to gcc 4.7");
  // for some old code that deals with this case, see D566719, diff number 10.
};

template <typename T>
struct fbv_allocator_traits<std::allocator<T>> {
  typedef std::allocator<T> A;

  typedef T* pointer;
  typedef const T* const_pointer;
  typedef size_t size_type;

  typedef std::false_type propagate_on_container_copy_assignment;
  typedef std::false_type propagate_on_container_move_assignment;
  typedef std::false_type propagate_on_container_swap;

  static pointer allocate(A& a, size_type n) {
    return static_cast<pointer>(::operator new(n * sizeof(T)));
  }
  static void deallocate(A& a, pointer p, size_type n) {
    ::operator delete(p);
  }

  template <typename R, typename... Args>
  static void construct(A& a, R* p, Args&&... args) {
    new (p) R(std::forward<Args>(args)...);
  }
  template <typename R>
  static void destroy(A& a, R* p) {
    p->~R();
  }

  static A select_on_container_copy_construction(const A& a) {
    return a;
  }
};

template <typename T>
struct fbv_is_nothrow_move_constructible
  : std::false_type {};

template <typename T, typename... Args>
struct fbv_is_nothrow_constructible
  : std::false_type {};

template <typename T>
struct fbv_is_copy_constructible
  : std::true_type {};

}

#endif

//=============================================================================
// unrolling

#define FOLLY_FBV_UNROLL_PTR(first, last, OP) do {  \
  for (; (last) - (first) >= 4; (first) += 4) {     \
    OP(((first) + 0));                              \
    OP(((first) + 1));                              \
    OP(((first) + 2));                              \
    OP(((first) + 3));                              \
  }                                                 \
  for (; (first) != (last); ++(first)) OP((first)); \
} while(0);

//=============================================================================
///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//                              fbvector class                               //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#ifdef FOLLY_BENCHMARK_USE_NS_IFOLLY
namespace Ifolly {
#else
namespace folly {
#endif

template <class T, class Allocator>
class fbvector : private boost::totally_ordered<fbvector<T, Allocator>> {

  //===========================================================================
  //---------------------------------------------------------------------------
  // implementation
private:

  typedef folly::fbv_allocator_traits<Allocator> A;

  struct Impl : public Allocator {
    // typedefs
    typedef typename A::pointer pointer;
    typedef typename A::size_type size_type;

    // data
    pointer b_, e_, z_;

    // constructors
    Impl() : Allocator(), b_(nullptr), e_(nullptr), z_(nullptr) {}
    Impl(const Allocator& a)
      : Allocator(a), b_(nullptr), e_(nullptr), z_(nullptr) {}
    Impl(Allocator&& a)
      : Allocator(std::move(a)), b_(nullptr), e_(nullptr), z_(nullptr) {}

    Impl(size_type n, const Allocator& a = Allocator())
      : Allocator(a)
      { init(n); }

    Impl(Impl&& other)
      : Allocator(std::move(other)),
        b_(other.b_), e_(other.e_), z_(other.z_)
      { other.b_ = other.e_ = other.z_ = nullptr; }

    // destructor
    ~Impl() {
      destroy();
    }

    // allocation
    // note that 'allocate' and 'deallocate' are inherited from Allocator
    T* D_allocate(size_type n) {
      if (usingStdAllocator::value) {
        return static_cast<T*>(malloc(n * sizeof(T)));
      } else {
        return folly::fbv_allocator_traits<Allocator>::allocate(*this, n);
      }
    }

    void D_deallocate(T* p, size_type n) noexcept {
      if (usingStdAllocator::value) {
        free(p);
      } else {
        folly::fbv_allocator_traits<Allocator>::deallocate(*this, p, n);
      }
    }

    // helpers
    void swapData(Impl& other) {
      std::swap(b_, other.b_);
      std::swap(e_, other.e_);
      std::swap(z_, other.z_);
    }

    // data ops
    inline void destroy() noexcept {
      if (b_) {
        // THIS DISPATCH CODE IS DUPLICATED IN fbvector::D_destroy_range_a.
        // It has been inlined here for speed. It calls the static fbvector
        //  methods to perform the actual destruction.
        if (usingStdAllocator::value) {
          S_destroy_range(b_, e_);
        } else {
          S_destroy_range_a(*this, b_, e_);
        }

        D_deallocate(b_, z_ - b_);
      }
    }

    void init(size_type n) {
      if (UNLIKELY(n == 0)) {
        b_ = e_ = z_ = nullptr;
      } else {
        size_type sz = folly::goodMallocSize(n * sizeof(T)) / sizeof(T);
        b_ = D_allocate(sz);
        e_ = b_;
        z_ = b_ + sz;
      }
    }

    void
    set(pointer newB, size_type newSize, size_type newCap) {
      z_ = newB + newCap;
      e_ = newB + newSize;
      b_ = newB;
    }

    void reset(size_type newCap) {
      destroy();
      try {
        init(newCap);
      } catch (...) {
        init(0);
        throw;
      }
    }
    void reset() { // same as reset(0)
      destroy();
      b_ = e_ = z_ = nullptr;
    }
  } impl_;

  static void swap(Impl& a, Impl& b) {
    using std::swap;
    if (!usingStdAllocator::value) swap<Allocator>(a, b);
    a.swapData(b);
  }

  //===========================================================================
  //---------------------------------------------------------------------------
  // types and constants
public:

  typedef T                                           value_type;
  typedef value_type&                                 reference;
  typedef const value_type&                           const_reference;
  typedef T*                                          iterator;
  typedef const T*                                    const_iterator;
  typedef size_t                                      size_type;
  typedef typename std::make_signed<size_type>::type  difference_type;
  typedef Allocator                                   allocator_type;
  typedef typename A::pointer                         pointer;
  typedef typename A::const_pointer                   const_pointer;
  typedef std::reverse_iterator<iterator>             reverse_iterator;
  typedef std::reverse_iterator<const_iterator>       const_reverse_iterator;

private:

  typedef std::integral_constant<bool,
      std::has_trivial_copy_constructor<T>::value &&
      sizeof(T) <= 16 // don't force large structures to be passed by value
    > should_pass_by_value;
  typedef typename std::conditional<
      should_pass_by_value::value, T, const T&>::type VT;
  typedef typename std::conditional<
      should_pass_by_value::value, T, T&&>::type MT;

  typedef std::integral_constant<bool,
      std::is_same<Allocator, std::allocator<T>>::value> usingStdAllocator;
  typedef std::integral_constant<bool,
      usingStdAllocator::value ||
      A::propagate_on_container_move_assignment::value> moveIsSwap;

  //===========================================================================
  //---------------------------------------------------------------------------
  // allocator helpers
private:

  //---------------------------------------------------------------------------
  // allocate

  T* M_allocate(size_type n) {
    return impl_.D_allocate(n);
  }

  //---------------------------------------------------------------------------
  // deallocate

  void M_deallocate(T* p, size_type n) noexcept {
    impl_.D_deallocate(p, n);
  }

  //---------------------------------------------------------------------------
  // construct

  // GCC is very sensitive to the exact way that construct is called. For
  //  that reason there are several different specializations of construct.

  template <typename U, typename... Args>
  void M_construct(U* p, Args&&... args) {
    if (usingStdAllocator::value) {
      new (p) U(std::forward<Args>(args)...);
    } else {
      folly::fbv_allocator_traits<Allocator>::construct(
        impl_, p, std::forward<Args>(args)...);
    }
  }

  template <typename U, typename... Args>
  static void S_construct(U* p, Args&&... args) {
    new (p) U(std::forward<Args>(args)...);
  }

  template <typename U, typename... Args>
  static void S_construct_a(Allocator& a, U* p, Args&&... args) {
    folly::fbv_allocator_traits<Allocator>::construct(
      a, p, std::forward<Args>(args)...);
  }

  // scalar optimization
  // TODO we can expand this optimization to: default copyable and assignable
  template <typename U, typename Enable = typename
    std::enable_if<std::is_scalar<U>::value>::type>
  void M_construct(U* p, U arg) {
    if (usingStdAllocator::value) {
      *p = arg;
    } else {
      folly::fbv_allocator_traits<Allocator>::construct(impl_, p, arg);
    }
  }

  template <typename U, typename Enable = typename
    std::enable_if<std::is_scalar<U>::value>::type>
  static void S_construct(U* p, U arg) {
    *p = arg;
  }

  template <typename U, typename Enable = typename
    std::enable_if<std::is_scalar<U>::value>::type>
  static void S_construct_a(Allocator& a, U* p, U arg) {
    folly::fbv_allocator_traits<Allocator>::construct(a, p, arg);
  }

  // const& optimization
  template <typename U, typename Enable = typename
    std::enable_if<!std::is_scalar<U>::value>::type>
  void M_construct(U* p, const U& value) {
    if (usingStdAllocator::value) {
      new (p) U(value);
    } else {
      folly::fbv_allocator_traits<Allocator>::construct(impl_, p, value);
    }
  }

  template <typename U, typename Enable = typename
    std::enable_if<!std::is_scalar<U>::value>::type>
  static void S_construct(U* p, const U& value) {
    new (p) U(value);
  }

  template <typename U, typename Enable = typename
    std::enable_if<!std::is_scalar<U>::value>::type>
  static void S_construct_a(Allocator& a, U* p, const U& value) {
    folly::fbv_allocator_traits<Allocator>::construct(a, p, value);
  }

  //---------------------------------------------------------------------------
  // destroy

  void M_destroy(T* p) noexcept {
    if (usingStdAllocator::value) {
      if (!std::has_trivial_destructor<T>::value) p->~T();
    } else {
      folly::fbv_allocator_traits<Allocator>::destroy(impl_, p);
    }
  }

  //===========================================================================
  //---------------------------------------------------------------------------
  // algorithmic helpers
private:

  //---------------------------------------------------------------------------
  // destroy_range

  // wrappers
  void M_destroy_range_e(T* pos) noexcept {
    D_destroy_range_a(pos, impl_.e_);
    impl_.e_ = pos;
  }

  // dispatch
  // THIS DISPATCH CODE IS DUPLICATED IN IMPL. SEE IMPL FOR DETAILS.
  void D_destroy_range_a(T* first, T* last) noexcept {
    if (usingStdAllocator::value) {
      S_destroy_range(first, last);
    } else {
      S_destroy_range_a(impl_, first, last);
    }
  }

  // allocator
  static void S_destroy_range_a(Allocator& a, T* first, T* last) noexcept {
    for (; first != last; ++first)
      folly::fbv_allocator_traits<Allocator>::destroy(a, first);
  }

  // optimized
  static void S_destroy_range(T* first, T* last) noexcept {
    if (!std::has_trivial_destructor<T>::value) {
      // EXPERIMENTAL DATA on fbvector<vector<int>> (where each vector<int> has
      //  size 0).
      // The unrolled version seems to work faster for small to medium sized
      //  fbvectors. It gets a 10% speedup on fbvectors of size 1024, 64, and
      //  16.
      // The simple loop version seems to work faster for large fbvectors. The
      //  unrolled version is about 6% slower on fbvectors on size 16384.
      // The two methods seem tied for very large fbvectors. The unrolled
      //  version is about 0.5% slower on size 262144.

      // for (; first != last; ++first) first->~T();
      #define FOLLY_FBV_OP(p) (p)->~T()
      FOLLY_FBV_UNROLL_PTR(first, last, FOLLY_FBV_OP)
      #undef FOLLY_FBV_OP
    }
  }

  //---------------------------------------------------------------------------
  // uninitialized_fill_n

  // wrappers
  void M_uninitialized_fill_n_e(size_type sz) {
    D_uninitialized_fill_n_a(impl_.e_, sz);
    impl_.e_ += sz;
  }

  void M_uninitialized_fill_n_e(size_type sz, VT value) {
    D_uninitialized_fill_n_a(impl_.e_, sz, value);
    impl_.e_ += sz;
  }

  // dispatch
  void D_uninitialized_fill_n_a(T* dest, size_type sz) {
    if (usingStdAllocator::value) {
      S_uninitialized_fill_n(dest, sz);
    } else {
      S_uninitialized_fill_n_a(impl_, dest, sz);
    }
  }

  void D_uninitialized_fill_n_a(T* dest, size_type sz, VT value) {
    if (usingStdAllocator::value) {
      S_uninitialized_fill_n(dest, sz, value);
    } else {
      S_uninitialized_fill_n_a(impl_, dest, sz, value);
    }
  }

  // allocator
  template <typename... Args>
  static void S_uninitialized_fill_n_a(Allocator& a, T* dest,
                                       size_type sz, Args&&... args) {
    auto b = dest;
    auto e = dest + sz;
    try {
      for (; b != e; ++b)
        folly::fbv_allocator_traits<Allocator>::construct(a, b,
          std::forward<Args>(args)...);
    } catch (...) {
      S_destroy_range_a(a, dest, b);
      throw;
    }
  }

  // optimized
  static void S_uninitialized_fill_n(T* dest, size_type n) {
    if (folly::IsZeroInitializable<T>::value) {
      std::memset(dest, 0, sizeof(T) * n);
    } else {
      auto b = dest;
      auto e = dest + n;
      try {
        for (; b != e; ++b) S_construct(b);
      } catch (...) {
        --b;
        for (; b >= dest; --b) b->~T();
        throw;
      }
    }
  }

  static void S_uninitialized_fill_n(T* dest, size_type n, const T& value) {
    auto b = dest;
    auto e = dest + n;
    try {
      for (; b != e; ++b) S_construct(b, value);
    } catch (...) {
      S_destroy_range(dest, b);
      throw;
    }
  }

  //---------------------------------------------------------------------------
  // uninitialized_copy

  // it is possible to add an optimization for the case where
  // It = move(T*) and IsRelocatable<T> and Is0Initiailizable<T>

  // wrappers
  template <typename It>
  void M_uninitialized_copy_e(It first, It last) {
    D_uninitialized_copy_a(impl_.e_, first, last);
    impl_.e_ += std::distance(first, last);
  }

  template <typename It>
  void M_uninitialized_move_e(It first, It last) {
    D_uninitialized_move_a(impl_.e_, first, last);
    impl_.e_ += std::distance(first, last);
  }

  // dispatch
  template <typename It>
  void D_uninitialized_copy_a(T* dest, It first, It last) {
    if (usingStdAllocator::value) {
      if (folly::IsTriviallyCopyable<T>::value) {
        S_uninitialized_copy_bits(dest, first, last);
      } else {
        S_uninitialized_copy(dest, first, last);
      }
    } else {
      S_uninitialized_copy_a(impl_, dest, first, last);
    }
  }

  template <typename It>
  void D_uninitialized_move_a(T* dest, It first, It last) {
    D_uninitialized_copy_a(dest,
      std::make_move_iterator(first), std::make_move_iterator(last));
  }

  // allocator
  template <typename It>
  static void
  S_uninitialized_copy_a(Allocator& a, T* dest, It first, It last) {
    auto b = dest;
    try {
      for (; first != last; ++first, ++b)
        folly::fbv_allocator_traits<Allocator>::construct(a, b, *first);
    } catch (...) {
      S_destroy_range_a(a, dest, b);
      throw;
    }
  }

  // optimized
  template <typename It>
  static void S_uninitialized_copy(T* dest, It first, It last) {
    auto b = dest;
    try {
      for (; first != last; ++first, ++b)
        S_construct(b, *first);
    } catch (...) {
      S_destroy_range(dest, b);
      throw;
    }
  }

  static void
  S_uninitialized_copy_bits(T* dest, const T* first, const T* last) {
    std::memcpy(dest, first, (last - first) * sizeof(T));
  }

  static void
  S_uninitialized_copy_bits(T* dest, std::move_iterator<T*> first,
                       std::move_iterator<T*> last) {
    T* bFirst = first.base();
    T* bLast = last.base();
    std::memcpy(dest, bFirst, (bLast - bFirst) * sizeof(T));
  }

  template <typename It>
  static void
  S_uninitialized_copy_bits(T* dest, It first, It last) {
    S_uninitialized_copy(dest, first, last);
  }

  //---------------------------------------------------------------------------
  // copy_n

  // This function is "unsafe": it assumes that the iterator can be advanced at
  //  least n times. However, as a private function, that unsafety is managed
  //  wholly by fbvector itself.

  template <typename It>
  static It S_copy_n(T* dest, It first, size_type n) {
    auto e = dest + n;
    for (; dest != e; ++dest, ++first) *dest = *first;
    return first;
  }

  static const T* S_copy_n(T* dest, const T* first, size_type n) {
    if (folly::IsTriviallyCopyable<T>::value) {
      std::memcpy(dest, first, n * sizeof(T));
      return first + n;
    } else {
      return S_copy_n<const T*>(dest, first, n);
    }
  }

  static std::move_iterator<T*>
  S_copy_n(T* dest, std::move_iterator<T*> mIt, size_type n) {
    if (folly::IsTriviallyCopyable<T>::value) {
      T* first = mIt.base();
      std::memcpy(dest, first, n * sizeof(T));
      return std::make_move_iterator(first + n);
    } else {
      return S_copy_n<std::move_iterator<T*>>(dest, mIt, n);
    }
  }

  //===========================================================================
  //---------------------------------------------------------------------------
  // relocation helpers
private:

  // Relocation is divided into three parts:
  //
  //  1: relocate_move
  //     Performs the actual movement of data from point a to point b.
  //
  //  2: relocate_done
  //     Destroys the old data.
  //
  //  3: relocate_undo
  //     Destoys the new data and restores the old data.
  //
  // The three steps are used because there may be an exception after part 1
  //  has completed. If that is the case, then relocate_undo can nullify the
  //  initial move. Otherwise, relocate_done performs the last bit of tidying
  //  up.
  //
  // The relocation trio may use either memcpy, move, or copy. It is decided
  //  by the following case statement:
  //
  //  IsRelocatable && usingStdAllocator    -> memcpy
  //  has_nothrow_move && usingStdAllocator -> move
  //  cannot copy                           -> move
  //  default                               -> copy
  //
  // If the class is non-copyable then it must be movable. However, if the
  //  move constructor is not noexcept, i.e. an error could be thrown, then
  //  relocate_undo will be unable to restore the old data, for fear of a
  //  second exception being thrown. This is a known and unavoidable
  //  deficiency. In lieu of a strong exception guarantee, relocate_undo does
  //  the next best thing: it provides a weak exception guarantee by
  //  destorying the new data, but leaving the old data in an indeterminate
  //  state. Note that that indeterminate state will be valid, since the
  //  old data has not been destroyed; it has merely been the source of a
  //  move, which is required to leave the source in a valid state.

  // wrappers
  void M_relocate(T* newB) {
    relocate_move(newB, impl_.b_, impl_.e_);
    relocate_done(newB, impl_.b_, impl_.e_);
  }

  // dispatch type trait
  typedef std::integral_constant<bool,
      folly::IsRelocatable<T>::value && usingStdAllocator::value
    > relocate_use_memcpy;

  typedef std::integral_constant<bool,
      (folly::fbv_is_nothrow_move_constructible<T>::value
       && usingStdAllocator::value)
      || !folly::fbv_is_copy_constructible<T>::value
    > relocate_use_move;

  // move
  void relocate_move(T* dest, T* first, T* last) {
    relocate_move_or_memcpy(dest, first, last, relocate_use_memcpy());
  }

  void relocate_move_or_memcpy(T* dest, T* first, T* last, std::true_type) {
    std::memcpy(dest, first, (last - first) * sizeof(T));
  }

  void relocate_move_or_memcpy(T* dest, T* first, T* last, std::false_type) {
    relocate_move_or_copy(dest, first, last, relocate_use_move());
  }

  void relocate_move_or_copy(T* dest, T* first, T* last, std::true_type) {
    D_uninitialized_move_a(dest, first, last);
  }

  void relocate_move_or_copy(T* dest, T* first, T* last, std::false_type) {
    D_uninitialized_copy_a(dest, first, last);
  }

  // done
  void relocate_done(T* dest, T* first, T* last) noexcept {
    if (folly::IsRelocatable<T>::value && usingStdAllocator::value) {
      // used memcpy; data has been relocated, do not call destructor
    } else {
      D_destroy_range_a(first, last);
    }
  }

  // undo
  void relocate_undo(T* dest, T* first, T* last) noexcept {
    if (folly::IsRelocatable<T>::value && usingStdAllocator::value) {
      // used memcpy, old data is still valid, nothing to do
    } else if (folly::fbv_is_nothrow_move_constructible<T>::value &&
               usingStdAllocator::value) {
      // noexcept move everything back, aka relocate_move
      relocate_move(first, dest, dest + (last - first));
    } else if (!folly::fbv_is_copy_constructible<T>::value) {
      // weak guarantee
      D_destroy_range_a(dest, dest + (last - first));
    } else {
      // used copy, old data is still valid
      D_destroy_range_a(dest, dest + (last - first));
    }
  }


  //===========================================================================
  //---------------------------------------------------------------------------
  // construct/copy/destroy
public:

  fbvector() = default;

  explicit fbvector(const Allocator& a) : impl_(a) {}

  explicit fbvector(size_type n, const Allocator& a = Allocator())
    : impl_(n, a)
    { M_uninitialized_fill_n_e(n); }

  fbvector(size_type n, VT value, const Allocator& a = Allocator())
    : impl_(n, a)
    { M_uninitialized_fill_n_e(n, value); }

  template <class It, class Category = typename
            std::iterator_traits<It>::iterator_category>
  fbvector(It first, It last, const Allocator& a = Allocator())
    #ifndef FOLLY_FBV_COMPATIBILITY_MODE
    : fbvector(first, last, a, Category()) {}
    #else
    : impl_(std::distance(first, last), a)
    { fbvector_init(first, last, Category()); }
    #endif

  fbvector(const fbvector& other)
    : impl_(other.size(), A::select_on_container_copy_construction(other.impl_))
    { M_uninitialized_copy_e(other.begin(), other.end()); }

  fbvector(fbvector&& other) noexcept : impl_(std::move(other.impl_)) {}

  fbvector(const fbvector& other, const Allocator& a)
    #ifndef FOLLY_FBV_COMPATIBILITY_MODE
    : fbvector(other.begin(), other.end(), a) {}
    #else
    : impl_(other.size(), a)
    { fbvector_init(other.begin(), other.end(), std::forward_iterator_tag()); }
    #endif

  fbvector(fbvector&& other, const Allocator& a) : impl_(a) {
    if (impl_ == other.impl_) {
      impl_.swapData(other.impl_);
    } else {
      impl_.init(other.size());
      M_uninitialized_move_e(other.begin(), other.end());
    }
  }

  fbvector(std::initializer_list<T> il, const Allocator& a = Allocator())
    #ifndef FOLLY_FBV_COMPATIBILITY_MODE
    : fbvector(il.begin(), il.end(), a) {}
    #else
    : impl_(std::distance(il.begin(), il.end()), a)
    { fbvector_init(il.begin(), il.end(), std::forward_iterator_tag()); }
    #endif

  ~fbvector() = default; // the cleanup occurs in impl_

  fbvector& operator=(const fbvector& other) {
    if (UNLIKELY(this == &other)) return *this;

    if (!usingStdAllocator::value &&
        A::propagate_on_container_copy_assignment::value) {
      if (impl_ != other.impl_) {
        // can't use other's different allocator to clean up self
        impl_.reset();
      }
      (Allocator&)impl_ = (Allocator&)other.impl_;
    }

    assign(other.begin(), other.end());
    return *this;
  }

  fbvector& operator=(fbvector&& other) {
    if (UNLIKELY(this == &other)) return *this;
    moveFrom(std::move(other), moveIsSwap());
    return *this;
  }

  fbvector& operator=(std::initializer_list<T> il) {
    assign(il.begin(), il.end());
    return *this;
  }

  template <class It, class Category = typename
            std::iterator_traits<It>::iterator_category>
  void assign(It first, It last) {
    assign(first, last, Category());
  }

  void assign(size_type n, VT value) {
    if (n > capacity()) {
      // Not enough space. Do not reserve in place, since we will
      // discard the old values anyways.
      if (dataIsInternalAndNotVT(value)) {
        T copy(std::move(value));
        impl_.reset(n);
        M_uninitialized_fill_n_e(n, copy);
      } else {
        impl_.reset(n);
        M_uninitialized_fill_n_e(n, value);
      }
    } else if (n <= size()) {
      auto newE = impl_.b_ + n;
      std::fill(impl_.b_, newE, value);
      M_destroy_range_e(newE);
    } else {
      std::fill(impl_.b_, impl_.e_, value);
      M_uninitialized_fill_n_e(n - size(), value);
    }
  }

  void assign(std::initializer_list<T> il) {
    assign(il.begin(), il.end());
  }

  allocator_type get_allocator() const noexcept {
    return impl_;
  }

private:

  #ifndef FOLLY_FBV_COMPATIBILITY_MODE
  // contract dispatch for iterator types fbvector(It first, It last)
  template <class ForwardIterator>
  fbvector(ForwardIterator first, ForwardIterator last,
           const Allocator& a, std::forward_iterator_tag)
    : impl_(std::distance(first, last), a)
    { M_uninitialized_copy_e(first, last); }

  template <class InputIterator>
  fbvector(InputIterator first, InputIterator last,
           const Allocator& a, std::input_iterator_tag)
    : impl_(a)
    { for (; first != last; ++first) emplace_back(*first); }

  #else
  // contract dispatch for iterator types without constructor forwarding
  template <class ForwardIterator>
  void
  fbvector_init(ForwardIterator first, ForwardIterator last,
                std::forward_iterator_tag)
    { M_uninitialized_copy_e(first, last); }

  template <class InputIterator>
  void
  fbvector_init(InputIterator first, InputIterator last,
                std::input_iterator_tag)
    { for (; first != last; ++first) emplace_back(*first); }
  #endif

  // contract dispatch for allocator movement in operator=(fbvector&&)
  void
  moveFrom(fbvector&& other, std::true_type) {
    swap(impl_, other.impl_);
  }
  void moveFrom(fbvector&& other, std::false_type) {
    if (impl_ == other.impl_) {
      impl_.swapData(other.impl_);
    } else {
      impl_.reset(other.size());
      M_uninitialized_move_e(other.begin(), other.end());
    }
  }

  // contract dispatch for iterator types in assign(It first, It last)
  template <class ForwardIterator>
  void assign(ForwardIterator first, ForwardIterator last,
              std::forward_iterator_tag) {
    auto const newSize = std::distance(first, last);
    if (newSize > capacity()) {
      impl_.reset(newSize);
      M_uninitialized_copy_e(first, last);
    } else if (newSize <= size()) {
      auto newEnd = std::copy(first, last, impl_.b_);
      M_destroy_range_e(newEnd);
    } else {
      auto mid = S_copy_n(impl_.b_, first, size());
      M_uninitialized_copy_e<decltype(last)>(mid, last);
    }
  }

  template <class InputIterator>
  void assign(InputIterator first, InputIterator last,
              std::input_iterator_tag) {
    auto p = impl_.b_;
    for (; first != last && p != impl_.e_; ++first, ++p) {
      *p = *first;
    }
    if (p != impl_.e_) {
      M_destroy_range_e(p);
    } else {
      for (; first != last; ++first) emplace_back(*first);
    }
  }

  // contract dispatch for aliasing under VT optimization
  bool dataIsInternalAndNotVT(const T& t) {
    if (should_pass_by_value::value) return false;
    return dataIsInternal(t);
  }
  bool dataIsInternal(const T& t) {
    return UNLIKELY(impl_.b_ <= std::addressof(t) &&
                    std::addressof(t) < impl_.e_);
  }


  //===========================================================================
  //---------------------------------------------------------------------------
  // iterators
public:

  iterator begin() noexcept {
    return impl_.b_;
  }
  const_iterator begin() const noexcept {
    return impl_.b_;
  }
  iterator end() noexcept {
    return impl_.e_;
  }
  const_iterator end() const noexcept {
    return impl_.e_;
  }
  reverse_iterator rbegin() noexcept {
    return reverse_iterator(end());
  }
  const_reverse_iterator rbegin() const noexcept {
    return const_reverse_iterator(end());
  }
  reverse_iterator rend() noexcept {
    return reverse_iterator(begin());
  }
  const_reverse_iterator rend() const noexcept {
    return const_reverse_iterator(begin());
  }

  const_iterator cbegin() const noexcept {
    return impl_.b_;
  }
  const_iterator cend() const noexcept {
    return impl_.e_;
  }
  const_reverse_iterator crbegin() const noexcept {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator crend() const noexcept {
    return const_reverse_iterator(begin());
  }

  //===========================================================================
  //---------------------------------------------------------------------------
  // capacity
public:

  size_type size() const noexcept {
    return impl_.e_ - impl_.b_;
  }

  size_type max_size() const noexcept {
    // good luck gettin' there
    return ~size_type(0);
  }

  void resize(size_type n) {
    if (n <= size()) {
      M_destroy_range_e(impl_.b_ + n);
    } else {
      reserve(n);
      M_uninitialized_fill_n_e(n - size());
    }
  }

  void resize(size_type n, VT t) {
    if (n <= size()) {
      M_destroy_range_e(impl_.b_ + n);
    } else if (dataIsInternalAndNotVT(t) && n > capacity()) {
      T copy(t);
      reserve(n);
      M_uninitialized_fill_n_e(n - size(), copy);
    } else {
      reserve(n);
      M_uninitialized_fill_n_e(n - size(), t);
    }
  }

  size_type capacity() const noexcept {
    return impl_.z_ - impl_.b_;
  }

  bool empty() const noexcept {
    return impl_.b_ == impl_.e_;
  }

  void reserve(size_type n) {
    if (n <= capacity()) return;
    if (impl_.b_ && reserve_in_place(n)) return;

    auto newCap = folly::goodMallocSize(n * sizeof(T)) / sizeof(T);
    auto newB = M_allocate(newCap);
    try {
      M_relocate(newB);
    } catch (...) {
      M_deallocate(newB, newCap);
      throw;
    }
    if (impl_.b_)
      M_deallocate(impl_.b_, impl_.z_ - impl_.b_);
    impl_.z_ = newB + newCap;
    impl_.e_ = newB + (impl_.e_ - impl_.b_);
    impl_.b_ = newB;
  }

  void shrink_to_fit() noexcept {
    auto const newCapacityBytes = folly::goodMallocSize(size() * sizeof(T));
    auto const newCap = newCapacityBytes / sizeof(T);
    auto const oldCap = capacity();

    if (newCap >= oldCap) return;

    void* p = impl_.b_;
    if ((rallocm && usingStdAllocator::value) &&
        newCapacityBytes >= folly::jemallocMinInPlaceExpandable &&
        rallocm(&p, NULL, newCapacityBytes, 0, ALLOCM_NO_MOVE)
          == ALLOCM_SUCCESS) {
      impl_.z_ += newCap - oldCap;
    } else {
      T* newB; // intentionally uninitialized
      try {
        newB = M_allocate(newCap);
        try {
          M_relocate(newB);
        } catch (...) {
          M_deallocate(newB, newCap);
          return; // swallow the error
        }
      } catch (...) {
        return;
      }
      if (impl_.b_)
        M_deallocate(impl_.b_, impl_.z_ - impl_.b_);
      impl_.z_ = newB + newCap;
      impl_.e_ = newB + (impl_.e_ - impl_.b_);
      impl_.b_ = newB;
    }
  }

private:

  bool reserve_in_place(size_type n) {
    if (!usingStdAllocator::value || !rallocm) return false;

    // jemalloc can never grow in place blocks smaller than 4096 bytes.
    if ((impl_.z_ - impl_.b_) * sizeof(T) <
      folly::jemallocMinInPlaceExpandable) return false;

    auto const newCapacityBytes = folly::goodMallocSize(n * sizeof(T));
    void* p = impl_.b_;
    if (rallocm(&p, NULL, newCapacityBytes, 0, ALLOCM_NO_MOVE)
        == ALLOCM_SUCCESS) {
      impl_.z_ = impl_.b_ + newCapacityBytes / sizeof(T);
      return true;
    }
    return false;
  }

  //===========================================================================
  //---------------------------------------------------------------------------
  // element access
public:

  reference operator[](size_type n) {
    assert(n < size());
    return impl_.b_[n];
  }
  const_reference operator[](size_type n) const {
    assert(n < size());
    return impl_.b_[n];
  }
  const_reference at(size_type n) const {
    if (UNLIKELY(n >= size())) {
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
    return *impl_.b_;
  }
  const_reference front() const {
    assert(!empty());
    return *impl_.b_;
  }
  reference back()  {
    assert(!empty());
    return impl_.e_[-1];
  }
  const_reference back() const {
    assert(!empty());
    return impl_.e_[-1];
  }

  //===========================================================================
  //---------------------------------------------------------------------------
  // data access
public:

  T* data() noexcept {
    return impl_.b_;
  }
  const T* data() const noexcept {
    return impl_.b_;
  }

  //===========================================================================
  //---------------------------------------------------------------------------
  // modifiers (common)
public:

  template <class... Args>
  void emplace_back(Args&&... args)  {
    if (impl_.e_ != impl_.z_) {
      M_construct(impl_.e_, std::forward<Args>(args)...);
      ++impl_.e_;
    } else {
      emplace_back_aux(std::forward<Args>(args)...);
    }
  }

  void
  push_back(const T& value) {
    if (impl_.e_ != impl_.z_) {
      M_construct(impl_.e_, value);
      ++impl_.e_;
    } else {
      emplace_back_aux(value);
    }
  }

  void
  push_back(T&& value) {
    if (impl_.e_ != impl_.z_) {
      M_construct(impl_.e_, std::move(value));
      ++impl_.e_;
    } else {
      emplace_back_aux(std::move(value));
    }
  }

  void pop_back() {
    assert(!empty());
    --impl_.e_;
    M_destroy(impl_.e_);
  }

  void swap(fbvector& other) noexcept {
    if (!usingStdAllocator::value &&
        A::propagate_on_container_swap::value)
      swap(impl_, other.impl_);
    else impl_.swapData(other.impl_);
  }

  void clear() noexcept {
    M_destroy_range_e(impl_.b_);
  }

private:

  // std::vector implements a similar function with a different growth
  //  strategy: empty() ? 1 : capacity() * 2.
  //
  // fbvector grows differently on two counts:
  //
  // (1) initial size
  //     Instead of grwoing to size 1 from empty, and fbvector allocates at
  //     least 64 bytes. You may still use reserve to reserve a lesser amount
  //     of memory.
  // (2) 1.5x
  //     For medium-sized vectors, the growth strategy is 1.5x. See the docs
  //     for details.
  //     This does not apply to very small or very large fbvectors. This is a
  //     heuristic.
  //     A nice addition to fbvector would be the capability of having a user-
  //     defined growth strategy, probably as part of the allocator.
  //

  size_type computePushBackCapacity() const {
    return empty() ? std::max(64 / sizeof(T), size_type(1))
      : capacity() < folly::jemallocMinInPlaceExpandable / sizeof(T)
      ? capacity() * 2
      : sizeof(T) > folly::jemallocMinInPlaceExpandable / 2 && capacity() == 1
      ? 2
      : capacity() > 4096 * 32 / sizeof(T)
      ? capacity() * 2
      : (capacity() * 3 + 1) / 2;
  }

  template <class... Args>
  void emplace_back_aux(Args&&... args);

  //===========================================================================
  //---------------------------------------------------------------------------
  // modifiers (erase)
public:

  iterator erase(const_iterator position) {
    return erase(position, position + 1);
  }

  iterator erase(const_iterator first, const_iterator last) {
    assert(isValid(first) && isValid(last));
    assert(first <= last);
    if (first != last) {
      if (last == end()) {
        M_destroy_range_e((iterator)first);
      } else {
        if (folly::IsRelocatable<T>::value && usingStdAllocator::value) {
          D_destroy_range_a((iterator)first, (iterator)last);
          if (last - first >= cend() - last) {
            std::memcpy((iterator)first, last, (cend() - last) * sizeof(T));
          } else {
            std::memmove((iterator)first, last, (cend() - last) * sizeof(T));
          }
          impl_.e_ -= (last - first);
        } else {
          std::copy(std::make_move_iterator((iterator)last),
                    std::make_move_iterator(end()), (iterator)first);
          auto newEnd = impl_.e_ - std::distance(first, last);
          M_destroy_range_e(newEnd);
        }
      }
    }
    return (iterator)first;
  }

  //===========================================================================
  //---------------------------------------------------------------------------
  // modifiers (insert)
private: // we have the private section first because it defines some macros

  bool isValid(const_iterator it) {
    return cbegin() <= it && it <= cend();
  }

  size_type computeInsertCapacity(size_type n) {
    size_type nc = std::max(computePushBackCapacity(), size() + n);
    size_type ac = folly::goodMallocSize(nc * sizeof(T)) / sizeof(T);
    return ac;
  }

  //---------------------------------------------------------------------------
  //
  // make_window takes an fbvector, and creates an uninitialized gap (a
  //  window) at the given position, of the given size. The fbvector must
  //  have enough capacity.
  //
  // Explanation by picture.
  //
  //    123456789______
  //        ^
  //        make_window here of size 3
  //
  //    1234___56789___
  //
  // If something goes wrong and the window must be destroyed, use
  //  undo_window to provide a weak exception guarantee. It destroys
  //  the right ledge.
  //
  //    1234___________
  //
  //---------------------------------------------------------------------------
  //
  // wrap_frame takes an inverse window and relocates an fbvector around it.
  //  The fbvector must have at least as many elements as the left ledge.
  //
  // Explanation by picture.
  //
  //        START
  //    fbvector:             inverse window:
  //    123456789______       _____abcde_______
  //                          [idx][ n ]
  //
  //        RESULT
  //    _______________       12345abcde6789___
  //
  //---------------------------------------------------------------------------
  //
  // insert_use_fresh_memory returns true iff the fbvector should use a fresh
  //  block of memory for the insertion. If the fbvector does not have enough
  //  spare capacity, then it must return true. Otherwise either true or false
  //  may be returned.
  //
  //---------------------------------------------------------------------------
  //
  // These three functions, make_window, wrap_frame, and
  //  insert_use_fresh_memory, can be combined into a uniform interface.
  // Since that interface involves a lot of case-work, it is built into
  //  some macros: FOLLY_FBVECTOR_INSERT_(START|TRY|END)
  // Macros are used in an attempt to let GCC perform better optimizations,
  //  especially control flow optimization.
  //

  //---------------------------------------------------------------------------
  // window

  void make_window(iterator position, size_type n) {
    assert(isValid(position));
    assert(size() + n <= capacity());
    assert(n != 0);

    auto tail = std::distance(position, impl_.e_);

    if (tail <= n) {
      relocate_move(position + n, position, impl_.e_);
      relocate_done(position + n, position, impl_.e_);
      impl_.e_ += n;
    } else {
      if (folly::IsRelocatable<T>::value && usingStdAllocator::value) {
        std::memmove(position + n, position, tail * sizeof(T));
        impl_.e_ += n;
      } else {
        D_uninitialized_move_a(impl_.e_, impl_.e_ - n, impl_.e_);
        impl_.e_ += n;
        std::copy_backward(std::make_move_iterator(position),
                           std::make_move_iterator(impl_.e_ - n), impl_.e_);
        D_destroy_range_a(position, position + n);
      }
    }
  }

  void undo_window(iterator position, size_type n) noexcept {
    D_destroy_range_a(position + n, impl_.e_);
    impl_.e_ = position;
  }

  //---------------------------------------------------------------------------
  // frame

  void wrap_frame(T* ledge, size_type idx, size_type n) {
    assert(size() >= idx);
    assert(n != 0);

    relocate_move(ledge, impl_.b_, impl_.b_ + idx);
    try {
      relocate_move(ledge + idx + n, impl_.b_ + idx, impl_.e_);
    } catch (...) {
      relocate_undo(ledge, impl_.b_, impl_.b_ + idx);
      throw;
    }
    relocate_done(ledge, impl_.b_, impl_.b_ + idx);
    relocate_done(ledge + idx + n, impl_.b_ + idx, impl_.e_);
  }

  //---------------------------------------------------------------------------
  // use fresh?

  bool insert_use_fresh(const_iterator cposition, size_type n) {
    if (cposition == cend()) {
      if (size() + n <= capacity()) return false;
      if (reserve_in_place(size() + n)) return false;
      return true;
    }

    if (size() + n > capacity()) return true;

    return false;
  }

  //---------------------------------------------------------------------------
  // interface

  #define FOLLY_FBVECTOR_INSERT_START(cpos, n)                                \
    assert(isValid(cpos));                                                    \
    T* position = const_cast<T*>(cpos);                                       \
    size_type idx = std::distance(impl_.b_, position);                        \
    bool fresh = insert_use_fresh(position, n);                               \
    T* b;                                                                     \
    size_type newCap = 0;                                                     \
                                                                              \
    if (fresh) {                                                              \
      newCap = computeInsertCapacity(n);                                      \
      b = M_allocate(newCap);                                                 \
    } else {                                                                  \
      make_window(position, n);                                               \
      b = impl_.b_;                                                           \
    }                                                                         \
                                                                              \
    T* start = b + idx;                                                       \
                                                                              \
    try {                                                                     \

    // construct the inserted elements

  #define FOLLY_FBVECTOR_INSERT_TRY(cpos, n)                                  \
    } catch (...) {                                                           \
      if (fresh) {                                                            \
        M_deallocate(b, newCap);                                              \
      } else {                                                                \
        undo_window(position, n);                                             \
      }                                                                       \
      throw;                                                                  \
    }                                                                         \
                                                                              \
    if (fresh) {                                                              \
      try {                                                                   \
        wrap_frame(b, idx, n);                                                \
      } catch (...) {                                                         \


    // delete the inserted elements (exception has been thrown)

  #define FOLLY_FBVECTOR_INSERT_END(cpos, n)                                  \
        M_deallocate(b, newCap);                                              \
        throw;                                                                \
      }                                                                       \
      if (impl_.b_) M_deallocate(impl_.b_, capacity());                       \
      impl_.set(b, size() + n, newCap);                                       \
      return impl_.b_ + idx;                                                  \
    } else {                                                                  \
      return position;                                                        \
    }                                                                         \

  //---------------------------------------------------------------------------
  // insert functions
public:

  template <class... Args>
  iterator emplace(const_iterator cpos, Args&&... args) {
    FOLLY_FBVECTOR_INSERT_START(cpos, 1)
      M_construct(start, std::forward<Args>(args)...);
    FOLLY_FBVECTOR_INSERT_TRY(cpos, 1)
      M_destroy(start);
    FOLLY_FBVECTOR_INSERT_END(cpos, 1)
  }

  iterator insert(const_iterator cpos, const T& value) {
    if (dataIsInternal(value)) return insert(cpos, T(value));

    FOLLY_FBVECTOR_INSERT_START(cpos, 1)
      M_construct(start, value);
    FOLLY_FBVECTOR_INSERT_TRY(cpos, 1)
      M_destroy(start);
    FOLLY_FBVECTOR_INSERT_END(cpos, 1)
  }

  iterator insert(const_iterator cpos, T&& value) {
    if (dataIsInternal(value)) return insert(cpos, T(std::move(value)));

    FOLLY_FBVECTOR_INSERT_START(cpos, 1)
      M_construct(start, std::move(value));
    FOLLY_FBVECTOR_INSERT_TRY(cpos, 1)
      M_destroy(start);
    FOLLY_FBVECTOR_INSERT_END(cpos, 1)
  }

  iterator insert(const_iterator cpos, size_type n, VT value) {
    if (n == 0) return (iterator)cpos;
    if (dataIsInternalAndNotVT(value)) return insert(cpos, n, T(value));

    FOLLY_FBVECTOR_INSERT_START(cpos, n)
      D_uninitialized_fill_n_a(start, n, value);
    FOLLY_FBVECTOR_INSERT_TRY(cpos, n)
      D_destroy_range_a(start, start + n);
    FOLLY_FBVECTOR_INSERT_END(cpos, n)
  }

  template <class It, class Category = typename
            std::iterator_traits<It>::iterator_category>
  iterator insert(const_iterator cpos, It first, It last) {
    return insert(cpos, first, last, Category());
  }

  iterator insert(const_iterator cpos, std::initializer_list<T> il) {
    return insert(cpos, il.begin(), il.end());
  }

  //---------------------------------------------------------------------------
  // insert dispatch for iterator types
private:

  template <class FIt>
  iterator insert(const_iterator cpos, FIt first, FIt last,
                  std::forward_iterator_tag) {
    size_type n = std::distance(first, last);
    if (n == 0) return (iterator)cpos;

    FOLLY_FBVECTOR_INSERT_START(cpos, n)
      D_uninitialized_copy_a(start, first, last);
    FOLLY_FBVECTOR_INSERT_TRY(cpos, n)
      D_destroy_range_a(start, start + n);
    FOLLY_FBVECTOR_INSERT_END(cpos, n)
  }

  template <class IIt>
  iterator insert(const_iterator cpos, IIt first, IIt last,
                  std::input_iterator_tag) {
    T* position = const_cast<T*>(cpos);
    assert(isValid(position));
    size_type idx = std::distance(begin(), position);

    fbvector storage(std::make_move_iterator(position),
                     std::make_move_iterator(end()),
                     A::select_on_container_copy_construction(impl_));
    M_destroy_range_e(position);
    for (; first != last; ++first) emplace_back(*first);
    insert(cend(), std::make_move_iterator(storage.begin()),
           std::make_move_iterator(storage.end()));
    return impl_.b_ + idx;
  }

  //===========================================================================
  //---------------------------------------------------------------------------
  // lexicographical functions (others from boost::totally_ordered superclass)
public:

  bool operator==(const fbvector& other) const {
    return size() == other.size() && std::equal(begin(), end(), other.begin());
  }

  bool operator<(const fbvector& other) const {
    return std::lexicographical_compare(
      begin(), end(), other.begin(), other.end());
  }

  //===========================================================================
  //---------------------------------------------------------------------------
  // friends
private:

  template <class _T, class _A>
  friend _T* relinquish(fbvector<_T, _A>&);

  template <class _T, class _A>
  friend void attach(fbvector<_T, _A>&, _T* data, size_t sz, size_t cap);

}; // class fbvector


//=============================================================================
//-----------------------------------------------------------------------------
// outlined functions (gcc, you finicky compiler you)

template <typename T, typename Allocator>
template <class... Args>
void fbvector<T, Allocator>::emplace_back_aux(Args&&... args) {
  size_type byte_sz = folly::goodMallocSize(
    computePushBackCapacity() * sizeof(T));
  if (usingStdAllocator::value
      && rallocm
      && ((impl_.z_ - impl_.b_) * sizeof(T) >=
          folly::jemallocMinInPlaceExpandable)) {
    // Try to reserve in place.
    // Ask rallocm to allocate in place at least size()+1 and at most sz space.
    // rallocm will allocate as much as possible within that range, which
    //  is the best possible outcome: if sz space is available, take it all,
    //  otherwise take as much as possible. If nothing is available, then fail.
    // In this fashion, we never relocate if there is a possibility of
    //  expanding in place, and we never relocate by less than the desired
    //  amount unless we cannot expand further. Hence we will not relocate
    //  sub-optimally twice in a row (modulo the blocking memory being freed).
    size_type lower = folly::goodMallocSize(sizeof(T) + size() * sizeof(T));
    size_type upper = byte_sz;
    size_type extra = upper - lower;
    assert(extra >= 0);

    void* p = impl_.b_;
    size_t actual;

    if (rallocm(&p, &actual, lower, extra, ALLOCM_NO_MOVE)
        == ALLOCM_SUCCESS) {
      impl_.z_ = impl_.b_ + actual / sizeof(T);
      M_construct(impl_.e_, std::forward<Args>(args)...);
      ++impl_.e_;
      return;
    }
  }

  // Reallocation failed. Perform a manual relocation.
  size_type sz = byte_sz / sizeof(T);
  auto newB = M_allocate(sz);
  auto newE = newB + size();
  try {
    if (folly::IsRelocatable<T>::value && usingStdAllocator::value) {
      // For linear memory access, relocate before construction.
      // By the test condition, relocate is noexcept.
      // Note that there is no cleanup to do if M_construct throws - that's
      //  one of the beauties of relocation.
      // Benchmarks for this code have high variance, and seem to be close.
      relocate_move(newB, impl_.b_, impl_.e_);
      M_construct(newE, std::forward<Args>(args)...);
      ++newE;
    } else {
      M_construct(newE, std::forward<Args>(args)...);
      ++newE;
      try {
        M_relocate(newB);
      } catch (...) {
        M_destroy(newE - 1);
        throw;
      }
    }
  } catch (...) {
    M_deallocate(newB, sz);
    throw;
  }
  if (impl_.b_) M_deallocate(impl_.b_, size());
  impl_.b_ = newB;
  impl_.e_ = newE;
  impl_.z_ = newB + sz;
}

//=============================================================================
//-----------------------------------------------------------------------------
// specialized functions

template <class T, class A>
void swap(fbvector<T, A>& lhs, fbvector<T, A>& rhs) noexcept {
  lhs.swap(rhs);
}

//=============================================================================
//-----------------------------------------------------------------------------
// other

template <class T, class A>
void compactResize(fbvector<T, A>* v, size_t sz) {
  v->resize(sz);
  v->shrink_to_fit();
}

// DANGER
//
// relinquish and attach are not a members function specifically so that it is
//  awkward to call them. It is very easy to shoot yourself in the foot with
//  these functions.
//
// If you call relinquish, then it is your responsibility to free the data
//  and the storage, both of which may have been generated in a non-standard
//  way through the fbvector's allocator.
//
// If you call attach, it is your responsibility to ensure that the fbvector
//  is fresh (size and capacity both zero), and that the supplied data is
//  capable of being manipulated by the allocator.
// It is acceptable to supply a stack pointer IF:
//  (1) The vector's data does not outlive the stack pointer. This includes
//      extension of the data's life through a move operation.
//  (2) The pointer has enough capacity that the vector will never be
//      relocated.
//  (3) Insert is not called on the vector; these functions have leeway to
//      relocate the vector even if there is enough capacity.
//  (4) A stack pointer is compatible with the fbvector's allocator.
//

template <class T, class A>
T* relinquish(fbvector<T, A>& v) {
  T* ret = v.data();
  v.impl_.b_ = v.impl_.e_ = v.impl_.z_ = nullptr;
  return ret;
}

template <class T, class A>
void attach(fbvector<T, A>& v, T* data, size_t sz, size_t cap) {
  assert(v.data() == nullptr);
  v.impl_.b_ = data;
  v.impl_.e_ = data + sz;
  v.impl_.z_ = data + cap;
}

} // namespace folly

#endif // FOLLY_FBVECTOR_H

