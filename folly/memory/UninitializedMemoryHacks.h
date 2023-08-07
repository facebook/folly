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

#pragma once

#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

// On MSVC an incorrect <version> header get's picked up
#if !defined(_MSC_VER) && __has_include(<version>)
#include <version>
#endif

namespace {
// This struct is different in every translation unit.  We use template
// instantiations to define inline freestanding methods.  Since the
// methods are inline it is fine to define them in multiple translation
// units, but the instantiation itself would be an ODR violation if it is
// present in the program more than once.  By tagging the instantiations
// with this struct, we avoid ODR problems for the instantiation while
// allowing the resulting methods to be inline-able.  If you think that
// seems hacky keep reading...
struct FollyMemoryDetailTranslationUnitTag {};
} // namespace
namespace folly {
namespace detail {
template <typename T>
void unsafeStringSetLargerSize(std::basic_string<T>& s, std::size_t n);
template <typename T>
void unsafeVectorSetLargerSize(std::vector<T>& v, std::size_t n);
} // namespace detail

/*
 * This file provides helper functions resizeWithoutInitialization()
 * that can resize std::basic_string or std::vector without constructing
 * or initializing new elements.
 *
 * IMPORTANT: These functions can be unsafe if used improperly.  If you
 * don't write to an element with index >= oldSize and < newSize, reading
 * the element can expose arbitrary memory contents to the world, including
 * the contents of old strings.  If you're lucky you'll get a segfault,
 * because the kernel is only required to fault in new pages on write
 * access.  MSAN should be able to catch problems in the common case that
 * the string or vector wasn't previously shrunk.
 *
 * Pay extra attention to your failure paths.  For example, if you try
 * to read directly into a caller-provided string, make sure to clear
 * the string when you get an I/O error.
 *
 * You should only use this if you have profiling data from production
 * that shows that this is not a premature optimization.  This code is
 * designed for retroactively optimizing code where touching every element
 * twice (or touching never-used elements once) shows up in profiling,
 * and where restructuring the code to use fixed-length arrays or IOBuf-s
 * would be difficult.
 *
 * NOTE: Just because .resize() shows up in your profile (probably
 * via one of the intrinsic memset implementations) doesn't mean that
 * these functions will make your program faster.  A lot of the cost
 * of memset comes from cache misses, so avoiding the memset can mean
 * that the cache miss cost just gets pushed to the following code.
 * resizeWithoutInitialization can be a win when the contents are bigger
 * than a cache level, because the second access isn't free in that case.
 * It can be a win when the memory is already cached, so touching it
 * doesn't help later code.  It can also be a win if the final length
 * of the string or vector isn't actually known, so the suffix will be
 * chopped off with a second call to .resize().
 */

/**
 * Like calling s.resize(n), but when growing the string does not
 * initialize new elements.  It is undefined behavior to read from
 * any element added to the string by this method unless it has been
 * written to by an operation that follows this call.
 *
 * Use the FOLLY_DECLARE_STRING_RESIZE_WITHOUT_INIT(T) macro to
 * declare (and inline define) the internals required to call
 * resizeWithoutInitialization for a std::basic_string<T>.
 * See detailed description of a similar macro for std::vector<T> below.
 *
 * IMPORTANT: Read the warning at the top of this header file.
 */
template <
    typename T,
    typename =
        typename std::enable_if<std::is_trivially_destructible<T>::value>::type>
inline void resizeWithoutInitialization(
    std::basic_string<T>& s, std::size_t n) {
  if (n <= s.size()) {
    s.resize(n);
  } else {
    // careful not to call reserve unless necessary, as it causes
    // shrink_to_fit on many platforms
    if (n > s.capacity()) {
      s.reserve(n);
    }
    detail::unsafeStringSetLargerSize(s, n);
  }
}

/**
 * Like calling v.resize(n), but when growing the vector does not construct
 * or initialize new elements.  It is undefined behavior to read from any
 * element added to the vector by this method unless it has been written
 * to by an operation that follows this call.
 *
 * Use the FOLLY_DECLARE_VECTOR_RESIZE_WITHOUT_INIT(T) macro to
 * declare (and inline define) the internals required to call
 * resizeWithoutInitialization for a std::vector<T>.  This must
 * be done exactly once in each translation unit that wants to call
 * resizeWithoutInitialization(std::vector<T>&,size_t).  char and unsigned
 * char are provided by default.  If you don't do this you will get linker
 * errors about folly::detail::unsafeVectorSetLargerSize.  Requiring that
 * T be trivially_destructible is only an approximation of the property
 * required of T.  In fact what is required is that any random sequence of
 * bytes may be safely reinterpreted as a T and passed to T's destructor.
 *
 * std::vector<bool> has specialized internals and is not supported.
 *
 * IMPORTANT: Read the warning at the top of this header file.
 */
template <
    typename T,
    typename = typename std::enable_if<
        std::is_trivially_destructible<T>::value &&
        !std::is_same<T, bool>::value>::type>
void resizeWithoutInitialization(std::vector<T>& v, std::size_t n) {
  if (n <= v.size()) {
    v.resize(n);
  } else {
    if (n > v.capacity()) {
      v.reserve(n);
    }
    detail::unsafeVectorSetLargerSize(v, n);
  }
}

namespace detail {

// This machinery bridges template expansion and macro expansion
#define FOLLY_DECLARE_STRING_RESIZE_WITHOUT_INIT_IMPL(TYPE)                    \
  namespace folly {                                                            \
  namespace detail {                                                           \
  void unsafeStringSetLargerSizeImpl(std::basic_string<TYPE>& s, std::size_t); \
  template <>                                                                  \
  inline void unsafeStringSetLargerSize<TYPE>(                                 \
      std::basic_string<TYPE> & s, std::size_t n) {                            \
    unsafeStringSetLargerSizeImpl(s, n);                                       \
  }                                                                            \
  }                                                                            \
  }

#if defined(_LIBCPP_STRING)
// libc++

template <typename Tag, typename T, typename A, A Ptr__set_size>
struct MakeUnsafeStringSetLargerSize {
  friend void unsafeStringSetLargerSizeImpl(
      std::basic_string<T>& s, std::size_t n) {
    // s.__set_size(n);
    (s.*Ptr__set_size)(n);
    (&s[0])[n] = '\0';
  }
};

#define FOLLY_DECLARE_STRING_RESIZE_WITHOUT_INIT(TYPE)            \
  template void std::basic_string<TYPE>::__set_size(std::size_t); \
  template struct folly::detail::MakeUnsafeStringSetLargerSize<   \
      FollyMemoryDetailTranslationUnitTag,                        \
      TYPE,                                                       \
      void (std::basic_string<TYPE>::*)(std::size_t),             \
      &std::basic_string<TYPE>::__set_size>;                      \
  FOLLY_DECLARE_STRING_RESIZE_WITHOUT_INIT_IMPL(TYPE)

#elif defined(_GLIBCXX_STRING) && _GLIBCXX_USE_CXX11_ABI
// libstdc++ new implementation with SSO

template <typename Tag, typename T, typename A, A Ptr_M_set_length>
struct MakeUnsafeStringSetLargerSize {
  friend void unsafeStringSetLargerSizeImpl(
      std::basic_string<T>& s, std::size_t n) {
    // s._M_set_length(n);
    (s.*Ptr_M_set_length)(n);
  }
};

#define FOLLY_DECLARE_STRING_RESIZE_WITHOUT_INIT(TYPE)               \
  template void std::basic_string<TYPE>::_M_set_length(std::size_t); \
  template struct folly::detail::MakeUnsafeStringSetLargerSize<      \
      FollyMemoryDetailTranslationUnitTag,                           \
      TYPE,                                                          \
      void (std::basic_string<TYPE>::*)(std::size_t),                \
      &std::basic_string<TYPE>::_M_set_length>;                      \
  FOLLY_DECLARE_STRING_RESIZE_WITHOUT_INIT_IMPL(TYPE)

#elif defined(_GLIBCXX_STRING)
// libstdc++ old implementation

template <
    typename Tag,
    typename T,
    typename A,
    A Ptr_M_rep,
    typename B,
    B Ptr_M_set_length_and_sharable>
struct MakeUnsafeStringSetLargerSize {
  friend void unsafeStringSetLargerSizeImpl(
      std::basic_string<T>& s, std::size_t n) {
    // s._M_rep()->_M_set_length_and_sharable(n);
    auto rep = (s.*Ptr_M_rep)();
    (rep->*Ptr_M_set_length_and_sharable)(n);
  }
};

#define FOLLY_DECLARE_STRING_RESIZE_WITHOUT_INIT(TYPE)                      \
  template std::basic_string<TYPE>::_Rep* std::basic_string<TYPE>::_M_rep() \
      const;                                                                \
  template void std::basic_string<TYPE>::_Rep::_M_set_length_and_sharable(  \
      std::size_t);                                                         \
  template struct folly::detail::MakeUnsafeStringSetLargerSize<             \
      FollyMemoryDetailTranslationUnitTag,                                  \
      TYPE,                                                                 \
      std::basic_string<TYPE>::_Rep* (std::basic_string<TYPE>::*)() const,  \
      &std::basic_string<TYPE>::_M_rep,                                     \
      void (std::basic_string<TYPE>::_Rep::*)(std::size_t),                 \
      &std::basic_string<TYPE>::_Rep::_M_set_length_and_sharable>;          \
  FOLLY_DECLARE_STRING_RESIZE_WITHOUT_INIT_IMPL(TYPE)

#elif defined(_MSC_VER)
// MSVC

template <typename Tag, typename T, typename A, A Ptr_Eos>
struct MakeUnsafeStringSetLargerSize {
  friend void unsafeStringSetLargerSizeImpl(
      std::basic_string<T>& s, std::size_t n) {
    // _Eos method is public for _MSC_VER <= 1916, private after
    // s._Eos(n);
    (s.*Ptr_Eos)(n);
  }
};

#define FOLLY_DECLARE_STRING_RESIZE_WITHOUT_INIT(TYPE)          \
  template void std::basic_string<TYPE>::_Eos(std::size_t);     \
  template struct folly::detail::MakeUnsafeStringSetLargerSize< \
      FollyMemoryDetailTranslationUnitTag,                      \
      TYPE,                                                     \
      void (std::basic_string<TYPE>::*)(std::size_t),           \
      &std::basic_string<TYPE>::_Eos>;                          \
  FOLLY_DECLARE_STRING_RESIZE_WITHOUT_INIT_IMPL(TYPE)

#else
#warning \
    "No implementation for resizeWithoutInitialization of std::basic_string"
#endif

} // namespace detail
} // namespace folly

#if defined(FOLLY_DECLARE_STRING_RESIZE_WITHOUT_INIT)
FOLLY_DECLARE_STRING_RESIZE_WITHOUT_INIT(char)
FOLLY_DECLARE_STRING_RESIZE_WITHOUT_INIT(wchar_t)
#endif

namespace folly {
namespace detail {

// This machinery bridges template expansion and macro expansion
#define FOLLY_DECLARE_VECTOR_RESIZE_WITHOUT_INIT_IMPL(TYPE)              \
  namespace folly {                                                      \
  namespace detail {                                                     \
  void unsafeVectorSetLargerSizeImpl(std::vector<TYPE>& v, std::size_t); \
  template <>                                                            \
  inline void unsafeVectorSetLargerSize<TYPE>(                           \
      std::vector<TYPE> & v, std::size_t n) {                            \
    unsafeVectorSetLargerSizeImpl(v, n);                                 \
  }                                                                      \
  }                                                                      \
  }

#if defined(_LIBCPP_VECTOR)
// libc++

template <typename T, typename Alloc = std::allocator<T>>
struct std_vector_layout {
  static_assert(!std::is_same<T, bool>::value, "bad instance");
  using allocator_type = Alloc;
  using pointer = typename std::allocator_traits<allocator_type>::pointer;

  pointer __begin_;
  pointer __end_;
  std::__compressed_pair<pointer, allocator_type> __end_cap_;
};

template <typename T>
void unsafeVectorSetLargerSize(std::vector<T>& v, std::size_t n) {
  using real = std::vector<T>;
  using fake = std_vector_layout<T>;
  using pointer = typename fake::pointer;
  static_assert(sizeof(fake) == sizeof(real), "mismatch");
  static_assert(alignof(fake) == alignof(real), "mismatch");

  auto const l = reinterpret_cast<unsigned char*>(&v);

  auto const s = v.size();

  auto& e = *reinterpret_cast<pointer*>(l + offsetof(fake, __end_));
  e += (n - s);

  // libc++ contiguous containers use special annotation functions that help
  // the address sanitizer to detect improper memory accesses. When ASAN is
  // enabled we need to call the appropriate annotation functions in order to
  // stop ASAN from reporting false positives. When ASAN is disabled, the
  // annotation function is a no-op.
#ifndef _LIBCPP_HAS_NO_ASAN
  __sanitizer_annotate_contiguous_container(
      v.data(), v.data() + v.capacity(), v.data() + s, v.data() + n);
#endif
}

#define FOLLY_DECLARE_VECTOR_RESIZE_WITHOUT_INIT(TYPE)

#elif defined(_GLIBCXX_VECTOR)
// libstdc++

template <typename T, typename Alloc>
struct std_vector_layout_impl {
  static_assert(!std::is_same<T, bool>::value, "bad instance");
  template <typename A>
  using alloc_traits_t = typename __gnu_cxx::__alloc_traits<A>;
  using allocator_type = Alloc;
  using allocator_traits = alloc_traits_t<allocator_type>;
  using rebound_allocator_type =
      typename allocator_traits::template rebind<T>::other;
  using rebound_allocator_traits = alloc_traits_t<rebound_allocator_type>;
  using pointer = typename rebound_allocator_traits::pointer;

  struct impl_type : rebound_allocator_type {
    pointer _M_start;
    pointer _M_finish;
    pointer _M_end_of_storage;
  };
};
template <typename T, typename Alloc = std::allocator<T>>
struct std_vector_layout : std_vector_layout_impl<T, Alloc>::impl_type {
  using pointer = typename std_vector_layout_impl<T, Alloc>::pointer;
};

template <typename T>
void unsafeVectorSetLargerSize(std::vector<T>& v, std::size_t n) {
  using real = std::vector<T>;
  using fake = std_vector_layout<T>;
  using pointer = typename fake::pointer;
  static_assert(sizeof(fake) == sizeof(real), "mismatch");
  static_assert(alignof(fake) == alignof(real), "mismatch");

  auto const l = reinterpret_cast<unsigned char*>(&v);

  auto& e = *reinterpret_cast<pointer*>(l + offsetof(fake, _M_finish));
  e += (n - v.size());
}

#define FOLLY_DECLARE_VECTOR_RESIZE_WITHOUT_INIT(TYPE)

#elif defined(_MSC_VER) && _MSC_VER <= 1916
// MSVC <= VS2017

template <typename Tag, typename T>
struct MakeUnsafeVectorSetLargerSize : std::vector<T> {
  friend void unsafeVectorSetLargerSizeImpl(std::vector<T>& v, std::size_t n) {
    v._Mylast() += (n - v.size());
  }
};

#define FOLLY_DECLARE_VECTOR_RESIZE_WITHOUT_INIT(TYPE)          \
  template struct folly::detail::MakeUnsafeVectorSetLargerSize< \
      FollyMemoryDetailTranslationUnitTag,                      \
      TYPE>;                                                    \
  FOLLY_DECLARE_VECTOR_RESIZE_WITHOUT_INIT_IMPL(TYPE)

#elif defined(_MSC_VER) && _MSC_VER > 1916
// MSVC >= VS2019

template <
    typename Tag,
    typename T,
    typename A,
    A Ptr_Mypair,
    typename B,
    B Ptr_Myval2,
    typename C,
    C Ptr_Mylast>
struct MakeUnsafeVectorSetLargerSize : std::vector<T> {
  friend void unsafeVectorSetLargerSizeImpl(std::vector<T>& v, std::size_t n) {
    // v._Mypair._Myval2._Mylast += (n - v.size());
    ((v.*Ptr_Mypair).*Ptr_Myval2).*Ptr_Mylast += (n - v.size());
  }
};

#define FOLLY_DECLARE_VECTOR_RESIZE_WITHOUT_INIT(TYPE)                         \
  template struct folly::detail::MakeUnsafeVectorSetLargerSize<                \
      FollyMemoryDetailTranslationUnitTag,                                     \
      TYPE,                                                                    \
      decltype(&std::vector<TYPE>::_Mypair),                                   \
      &std::vector<TYPE>::_Mypair,                                             \
      decltype(&decltype(std::declval<std::vector<TYPE>>()._Mypair)::_Myval2), \
      &decltype(std::declval<std::vector<TYPE>>()._Mypair)::_Myval2,           \
      decltype(&decltype(std::declval<std::vector<TYPE>>()                     \
                             ._Mypair._Myval2)::_Mylast),                      \
      &decltype(std::declval<std::vector<TYPE>>()._Mypair._Myval2)::_Mylast>;  \
  FOLLY_DECLARE_VECTOR_RESIZE_WITHOUT_INIT_IMPL(TYPE)

#else
#warning "No implementation for resizeWithoutInitialization of std::vector"
#endif

} // namespace detail
} // namespace folly

#if defined(FOLLY_DECLARE_VECTOR_RESIZE_WITHOUT_INIT)
FOLLY_DECLARE_VECTOR_RESIZE_WITHOUT_INIT(char)
FOLLY_DECLARE_VECTOR_RESIZE_WITHOUT_INIT(unsigned char)
#endif
