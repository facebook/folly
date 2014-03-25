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

#ifndef FOLLY_PORTABILITY_H_
#define FOLLY_PORTABILITY_H_

#ifndef FOLLY_NO_CONFIG
#include "folly-config.h"
#endif

#if FOLLY_HAVE_FEATURES_H
#include <features.h>
#endif

#include "CPortability.h"

#if FOLLY_HAVE_SCHED_H
 #include <sched.h>
 #ifndef FOLLY_HAVE_PTHREAD_YIELD
  #define pthread_yield sched_yield
 #endif
#endif


// MaxAlign: max_align_t isn't supported by gcc
#ifdef __GNUC__
struct MaxAlign { char c; } __attribute__((aligned));
#elif _MSC_VER
struct MaxAlign { char c; } max_align_t;
#else /* !__GNUC__ */
# error Cannot define MaxAlign on this platform
#endif


// noreturn
#if defined(__clang__) || defined(__GNUC__)
# define FOLLY_NORETURN __attribute__((noreturn))
#else
# define FOLLY_NORETURN
#endif


// portable version check
#ifndef __GNUC_PREREQ
# if defined __GNUC__ && defined __GNUC_MINOR__
#  define __GNUC_PREREQ(maj, min) ((__GNUC__ << 16) + __GNUC_MINOR__ >= \
                                   ((maj) << 16) + (min))
# else
#  define __GNUC_PREREQ(maj, min) 0
# endif
#endif


/* Define macro wrappers for C++11's "final" and "override" keywords, which
 * are supported in gcc 4.7 but not gcc 4.6. */
#if !defined(FOLLY_FINAL) && !defined(FOLLY_OVERRIDE)
# if defined(__clang__) || __GNUC_PREREQ(4, 7)
#  define FOLLY_FINAL final
#  define FOLLY_OVERRIDE override
# else
#  define FOLLY_FINAL /**/
#  define FOLLY_OVERRIDE /**/
# endif
#endif


// Define to 1 if you have the `preadv' and `pwritev' functions, respectively
#if !defined(FOLLY_HAVE_PREADV) && !defined(FOLLY_HAVE_PWRITEV)
# if defined(__GLIBC_PREREQ)
#  if __GLIBC_PREREQ(2, 10)
#   define FOLLY_HAVE_PREADV 1
#   define FOLLY_HAVE_PWRITEV 1
#  endif
# endif
#endif

// It turns out that GNU libstdc++ and LLVM libc++ differ on how they implement
// the 'std' namespace; the latter uses inline namepsaces. Wrap this decision
// up in a macro to make forward-declarations easier.
#if FOLLY_USE_LIBCPP
#define FOLLY_NAMESPACE_STD_BEGIN     _LIBCPP_BEGIN_NAMESPACE_STD
#define FOLLY_NAMESPACE_STD_END       _LIBCPP_END_NAMESPACE_STD
#else
#define FOLLY_NAMESPACE_STD_BEGIN     namespace std {
#define FOLLY_NAMESPACE_STD_END       }
#endif

// Some platforms lack clock_gettime(2) and clock_getres(2). Inject our own
// versions of these into the global namespace.
#if FOLLY_HAVE_CLOCK_GETTIME
#include <time.h>
#else
#include "folly/detail/Clock.h"
#endif

// Provide our own std::__throw_* wrappers for platforms that don't have them
#if FOLLY_HAVE_BITS_FUNCTEXCEPT_H
#include <bits/functexcept.h>
#else
#include "folly/detail/FunctionalExcept.h"
#endif

#if defined(__cplusplus)
// Unfortunately, boost::has_trivial_copy<T> is broken in libc++ due to its
// usage of __has_trivial_copy(), so we can't use it as a
// least-common-denominator for C++11 implementations that don't support
// std::is_trivially_copyable<T>.
//
//      http://stackoverflow.com/questions/12754886/has-trivial-copy-behaves-differently-in-clang-and-gcc-whos-right
//
// As a result, use std::is_trivially_copyable() where it exists, and fall back
// to Boost otherwise.
#if FOLLY_HAVE_STD__IS_TRIVIALLY_COPYABLE
#include <type_traits>
#define FOLLY_IS_TRIVIALLY_COPYABLE(T)                   \
  (std::is_trivially_copyable<T>::value)
#else
#include <boost/type_traits.hpp>
#define FOLLY_IS_TRIVIALLY_COPYABLE(T)                   \
  (boost::has_trivial_copy<T>::value &&                  \
   boost::has_trivial_destructor<T>::value)
#endif
#endif // __cplusplus

/**
 * Macros related to compiler specific functionality
 * clang tends to copy the "normal" behavior for the 
 * current platform
 */

// Prefix a function you do not want inlined
#if defined(_MSC_VER)
#define FOLLY_NOINLINE __attribute__((noinline))
#elif defined(__GNUC__)
#define FOLLY_NOINLINE __declspec(noinline)
#else
#define NOINLINE
#endif

#if defined(_MSC_VER)
#define FOLLY_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__)
#define FOLLY_ALWAYS_INLINE inline __attribute__((__always_inline__))
#endif

// packing is ugly with push/pop
#if defined(_MSC_VER)
# define FOLLY_PACK_ATTR /**/
# define FOLLY_PACK_PUSH __pragma(pack(push, 1))
# define FOLLY_PACK_POP __pragma(pack(pop))
#else
# define FOLLY_PACK_ATTR __attribute__((packed))
# define FOLLY_PACK_PUSH /**/
# define FOLLY_PACK_POP  /**/
#endif

// Deal with alignas and alignof
#if !defined(FOLLY_HAS_ALIGNAS)
# ifdef _MSC_VER
#  define FOLLY_ALIGNAS(x) __declspec(align(x))
# else
#  define FOLLY_ALIGNAS(x) __attribute__((aligned(x)))
# endif
#else
# define FOLLY_ALIGNAS(x) alignas(x)
#endif

#if !defined(FOLLY_HAS_ALIGNOF)
# ifdef _MSC_VER
#  define FOLLY_ALIGNOF(x) __alignof(x)
# else
#  define FOLLY_ALIGNOF(x) __alignof__(x))
# endif
#else
# define FOLLY_ALIGNOF(x) alignof(x)
#endif

// MSVC has max_align_t
#ifdef _MSC_VER
#  define FOLLY_MAXALIGN FOLLY_ALIGNAS(max_align_t)
#else
# define FOLLY_MAXALIGN __attribute__((aligned))
#endif

#if defined(__GNUC__)
#define FOLLY_PRINTF_FORMAT(format_param, dots_param) \
	__attribute__((format(printf, format_param, dots_param)))
#else
#define FOLLY_PRINTF_FORMAT(format_param, dots_param)
#endif

#endif // FOLLY_PORTABILITY_H_
