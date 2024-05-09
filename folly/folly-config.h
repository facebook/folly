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

#ifdef __APPLE__
#include <TargetConditionals.h> // @manual
#endif

#ifdef __linux__
#include <linux/version.h>
#endif

#if !defined(FOLLY_MOBILE)
#if defined(__ANDROID__) || \
    (defined(__APPLE__) &&  \
     (TARGET_IPHONE_SIMULATOR || TARGET_OS_SIMULATOR || TARGET_OS_IPHONE))
#define FOLLY_MOBILE 1
#else
#define FOLLY_MOBILE 0
#endif
#endif // FOLLY_MOBILE

/* define if the Boost library is available */
#ifndef FOLLY_HAVE_BOOST
#define FOLLY_HAVE_BOOST /**/
#endif

/* define if the Boost::Regex library is available */
#ifndef FOLLY_HAVE_BOOST_REGEX
#define FOLLY_HAVE_BOOST_REGEX /**/
#endif

/* define if the Boost::Thread library is available */
#ifndef FOLLY_HAVE_BOOST_THREAD
#define FOLLY_HAVE_BOOST_THREAD /**/
#endif

/* Define to 1 if we support clock_gettime(2) */
#if !defined(FOLLY_HAVE_CLOCK_GETTIME) && !defined(_WIN32)
#define FOLLY_HAVE_CLOCK_GETTIME 1
#endif

#if !defined(_WIN32)
#define FOLLY_USE_SYMBOLIZER 1
#endif

#if !defined(FOLLY_HAVE_ELF)
#if defined(__linux__) && \
    (!defined(__ANDROID__) || (defined(__aarch64__) || defined(__x86_64__)))

#define FOLLY_HAVE_ELF 1
#else
#define FOLLY_HAVE_ELF 0
#endif
#endif

#if !defined(FOLLY_HAVE_DWARF) && defined(__linux__) && !defined(__ANDROID__)
#define FOLLY_HAVE_DWARF 1
#endif

#if !defined(FOLLY_HAVE_OPENAT2) && defined(__linux__)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
#define FOLLY_HAVE_OPENAT2 1
#else
#define FOLLY_HAVE_OPENAT2 0
#endif
#endif

#if !defined(FOLLY_HAVE_SWAPCONTEXT) && !defined(_WIN32) && \
    !defined(__ANDROID__)
#define FOLLY_HAVE_SWAPCONTEXT 1
#endif

#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__APPLE__)
#define FOLLY_HAVE_BACKTRACE 1
#endif

#if !defined(FOLLY_HAVE_LIBUNWIND) && !defined(_WIN32) && \
    !(defined(__APPLE__) && defined(__arm__)) && !defined(__ANDROID__)
#define FOLLY_HAVE_LIBUNWIND 1
#endif

#if __has_include(<features.h>)
#include <features.h>
#endif

/* Define to 1 if you have the `double_conversion' library
   (-ldouble_conversion). */
#ifndef FOLLY_HAVE_LIBDOUBLE_CONVERSION
#define FOLLY_HAVE_LIBDOUBLE_CONVERSION 1
#endif

/* Define to 1 if you have the `gflags' library (-lgflags). */
#ifndef FOLLY_HAVE_LIBGFLAGS
#define FOLLY_HAVE_LIBGFLAGS 1
#endif

/* Define to 1 if you have the `glog' library (-lglog). */
#ifndef FOLLY_HAVE_LIBGLOG
#define FOLLY_HAVE_LIBGLOG 1
#endif

/* Define to 1 if you have the `gtest' library (-lgtest). */
#ifndef FOLLY_HAVE_LIBGTEST
#define FOLLY_HAVE_LIBGTEST 1
#endif

/* Define to 1 if you have the `gtest_main' library (-lgtest_main). */
#ifndef FOLLY_HAVE_LIBGTEST_MAIN
#define FOLLY_HAVE_LIBGTEST_MAIN 1
#endif

/* Define to 1 if you have the `jemalloc' library (-ljemalloc). */
#if !defined(FOLLY_HAVE_LIBJEMALLOC) && !defined(_WIN32)
#define FOLLY_HAVE_LIBJEMALLOC 1
#endif

/* Define to 1 if you have the `tcmalloc' library (-ltcmalloc). */
#if !defined(FOLLY_HAVE_LIBTCMALLOC) && !defined(_WIN32)
#define FOLLY_HAVE_LIBTCMALLOC 1
#endif

/* Define to 1 if you have the `lz4' library (-llz4). */
#if !defined(FOLLY_HAVE_LIBLZ4) && !FOLLY_MOBILE && !defined(__APPLE__)
#define FOLLY_HAVE_LIBLZ4 1
#endif

/* Define to 1 if you have the `lzma' library (-llzma). */
#if !defined(FOLLY_HAVE_LIBLZMA) && !FOLLY_MOBILE && !defined(__APPLE__)
#define FOLLY_HAVE_LIBLZMA 1
#endif

/* Define to 1 if you have the `snappy' library (-lsnappy). */
#if !defined(FOLLY_HAVE_LIBSNAPPY) && !FOLLY_MOBILE && !defined(__APPLE__)
#define FOLLY_HAVE_LIBSNAPPY 1
#endif

/* Define to 1 if you have the `z' library (-lz). */
#ifndef FOLLY_HAVE_LIBZ
#define FOLLY_HAVE_LIBZ 1
#endif

/* Define to 1 if you have the `zstd' library (-lzstd). */
#ifndef FOLLY_HAVE_LIBZSTD
#define FOLLY_HAVE_LIBZSTD 1
#endif

/* Define to 1 if you have the `bz2' library (-lbz2). */
#if !defined(FOLLY_HAVE_LIBBZ2) && !FOLLY_MOBILE && !defined(__APPLE__)
#define FOLLY_HAVE_LIBBZ2 1
#endif

/* Defined to 1 if you have linux vdso */
#if !defined(FOLLY_HAVE_LINUX_VDSO) && !defined(_MSC_VER) && !FOLLY_MOBILE && \
    !defined(__APPLE__) && !defined(_WIN32)
#define FOLLY_HAVE_LINUX_VDSO 1
#endif

/* Define to 1 if you have the `malloc_size' function. */
/* #undef HAVE_MALLOC_SIZE */

/* Define to 1 if you have the `malloc_usable_size' function. */
#if !defined(FOLLY_HAVE_MALLOC_USABLE_SIZE) && !defined(__APPLE__) && \
    !defined(_WIN32) && !(defined(ANDROID) && __ANDROID_API__ < 17)
#define FOLLY_HAVE_MALLOC_USABLE_SIZE 1
#endif

// Clang doesn't support ifuncs. This also allows ifunc support to be explicitly
// passed in as a compile flag.
#ifndef FOLLY_HAVE_IFUNC
#if defined(__clang__) || defined(__APPLE__) && !defined(_WIN32)
#define FOLLY_HAVE_IFUNC 0
#else
#define FOLLY_HAVE_IFUNC 1
#endif
#endif

/* Define to 1 if the system has the type `__int128'. */
#if !defined(FOLLY_HAVE_INT128_T) && __SIZEOF_INT128__ >= 16 && \
    (!defined(_MSC_VER) || defined(__clang__))
#define FOLLY_HAVE_INT128_T 1
#endif

/* Define if g++ supports C++0x features. */
#ifndef FOLLY_HAVE_STDCXX_0X
#define FOLLY_HAVE_STDCXX_0X /**/
#endif

// Define to 1 if you have the `preadv' and `pwritev' functions, respectively
#if !defined(FOLLY_HAVE_PREADV) && !defined(FOLLY_HAVE_PWRITEV)
#if defined(__GLIBC_PREREQ) && !defined(__APPLE__)
#define FOLLY_HAVE_PREADV 1
#define FOLLY_HAVE_PWRITEV 1
#endif
#endif

#ifndef FOLLY_HAVE_PREADV
#define FOLLY_HAVE_PREADV 0
#endif

#ifndef FOLLY_HAVE_PWRITEV
#define FOLLY_HAVE_PWRITEV 0
#endif

/* Define to 1 if your architecture can handle unaligned loads and stores. */
#if !defined(FOLLY_HAVE_UNALIGNED_ACCESS) && !defined(__ANDROID__) && \
    !defined(__arm__)
#define FOLLY_HAVE_UNALIGNED_ACCESS 1
#endif

/* Define to 1 if the linker supports weak symbols. */
#if !defined(FOLLY_HAVE_WEAK_SYMBOLS) && !defined(__APPLE__) && !defined(_WIN32)
#define FOLLY_HAVE_WEAK_SYMBOLS 1
#else
#define FOLLY_HAVE_WEAK_SYMBOLS 0
#endif

#if !defined(FOLLY_HAVE_WCHAR_SUPPORT) && !defined(__ANDROID__)
#define FOLLY_HAVE_WCHAR_SUPPORT 1
#endif

/* Define to 1 if has <ext/random>. */
#if !defined(FOLLY_HAVE_EXTRANDOM_SFMT19937) && defined(__has_include) && \
    !defined(__ANDROID__) && !defined(__aarch64__)
#if __has_include(<ext/random>)
#define FOLLY_HAVE_EXTRANDOM_SFMT19937 1
#endif
#endif

/* Define to 1 if the compiler has VLA (variable-length array) support,
   otherwise define to 0 */
#if !defined(FOLLY_HAVE_VLA) && !defined(_WIN32)
#define FOLLY_HAVE_VLA 1
#endif

/* Define to 1 if the system has the type `_Bool'. */
/* #undef HAVE__BOOL */

/* Define to the sub-directory in which libtool stores uninstalled libraries.
 */
#ifndef FOLLY_LT_OBJDIR
#define FOLLY_LT_OBJDIR ".libs/"
#endif

/* Name of package */
#ifndef FOLLY_PACKAGE
#define FOLLY_PACKAGE "folly"
#endif

/* Define to the address where bug reports for this package should be sent. */
#ifndef FOLLY_PACKAGE_BUGREPORT
#define FOLLY_PACKAGE_BUGREPORT "folly@fb.com"
#endif

/* Define to the full name of this package. */
#ifndef FOLLY_PACKAGE_NAME
#define FOLLY_PACKAGE_NAME "folly"
#endif

/* Define to the full name and version of this package. */
#ifndef FOLLY_PACKAGE_STRING
#define FOLLY_PACKAGE_STRING "folly 0.1"
#endif

/* Define to the one symbol short name of this package. */
#ifndef FOLLY_PACKAGE_TARNAME
#define FOLLY_PACKAGE_TARNAME "folly"
#endif

/* Define to the home page for this package. */
#ifndef FOLLY_PACKAGE_URL
#define FOLLY_PACKAGE_URL ""
#endif

/* Define to the version of this package. */
#ifndef FOLLY_PACKAGE_VERSION
#define FOLLY_PACKAGE_VERSION "0.1"
#endif

/* Define to 1 if you have the ANSI C header files. */
#ifndef FOLLY_STDC_HEADERS
#define FOLLY_STDC_HEADERS 1
#endif

/* Define to 1 if you can safely include both <sys/time.h> and <time.h>. */
#ifndef FOLLY_TIME_WITH_SYS_TIME
#define FOLLY_TIME_WITH_SYS_TIME 1
#endif

/* Define to 1 if this build supports use in shared libraries */
#ifndef FOLLY_SUPPORT_SHARED_LIBRARY
#define FOLLY_SUPPORT_SHARED_LIBRARY 1
#endif

/* Define to 1 if this build should use extern template for Future<Unit> and
 * SemiFuture<Unit> */
#ifndef FOLLY_USE_EXTERN_FUTURE_UNIT
#if !defined(__ANDROID__) && !defined(__APPLE__)
#define FOLLY_USE_EXTERN_FUTURE_UNIT 1
#else
#define FOLLY_USE_EXTERN_FUTURE_UNIT 0
#endif
#endif

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
/* #undef inline */
#endif

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

/* Define to empty if the keyword `volatile' does not work. Warning: valid
   code using `volatile' can become incorrect without. Disable with care. */
/* #undef volatile */

/* Define to 1 if the gflags namespace is not "gflags" */
#ifndef FOLLY_UNUSUAL_GFLAGS_NAMESPACE
#define FOLLY_UNUSUAL_GFLAGS_NAMESPACE 1
#endif

/* Define to gflags namespace ("google" or "gflags") */
#ifndef FOLLY_GFLAGS_NAMESPACE
#define FOLLY_GFLAGS_NAMESPACE google
#endif

#if !defined(FOLLY_HAVE_PTHREAD_SPINLOCK_T) && !FOLLY_MOBILE && \
    !defined(__APPLE__) && !defined(_WIN32)
#define FOLLY_HAVE_PTHREAD_SPINLOCK_T 1
#endif

#if !defined(FOLLY_HAVE_PTHREAD_ATFORK) && !defined(__ANDROID__) && \
    !defined(_WIN32)
#define FOLLY_HAVE_PTHREAD_ATFORK 1
#endif

#define FOLLY_DEMANGLE_MAX_SYMBOL_SIZE 1024

#if defined(__GNUC__) && __GNUC__ && !__clang__
#define FOLLY_HAVE_SHADOW_LOCAL_WARNINGS 1
#endif

#ifndef _WIN32
#define FOLLY_HAVE_PTHREAD 1
#endif

/* For internal fbsource-based builds, ASAN is always enabled globally for the
 * entire build or not.  FOLLY_LIBRARY_SANITIZE_ADDRESS is therefore the same as
 * FOLLY_SANITIZE_ADDRESS.  We never have builds where folly is compiled with
 * ASAN enabled but other libraries have ASAN disabled, or vice-versa.
 */
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#define FOLLY_LIBRARY_SANITIZE_ADDRESS 1
#else
#define FOLLY_LIBRARY_SANITIZE_ADDRESS 0
#endif
#else // defined(__has_feature)
#if __SANITIZE_ADDRESS__
#define FOLLY_LIBRARY_SANITIZE_ADDRESS 1
#else
#define FOLLY_LIBRARY_SANITIZE_ADDRESS 0
#endif
#endif // defined(__has_feature)

// We depend on JEMalloc headers in fbcode, so use them (note that when using
// sanitizers, `#ifdef` gates in the code will not use JEMalloc headers,
// despite this setting).  This set of defines is a bit complex, but we try to
// come up with a set of defines that will only match in fbcode, where JEMalloc
// is available.
#if FOLLY_HAVE_LIBJEMALLOC && !defined(FOLLY_USE_JEMALLOC) && !FOLLY_MOBILE && \
    !defined(__APPLE__) && !defined(__ANDROID__) && !defined(_WIN32) &&        \
    !defined(__wasm32__)
#define FOLLY_USE_JEMALLOC 1
#endif

// pipe2
#ifdef __GLIBC__
#ifndef FOLLY_HAVE_PIPE2
#define FOLLY_HAVE_PIPE2 1
#endif
#endif

// accept4, recvmmsg, sendmmsg
#if defined(__linux__)
#if defined(__ANDROID__) && __ANDROID_API__ >= 21
#define FOLLY_HAVE_ACCEPT4 1
#define FOLLY_HAVE_RECVMMSG 1
#define FOLLY_HAVE_SENDMMSG 1
#elif defined(__GLIBC_PREREQ)
#define FOLLY_HAVE_ACCEPT4 1
#define FOLLY_HAVE_RECVMMSG 1
#define FOLLY_HAVE_SENDMMSG 1
#endif
#endif

#if defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 25)
#define FOLLY_HAVE_GETRANDOM 1
#else
#define FOLLY_HAVE_GETRANDOM 0
#endif
#else
#define FOLLY_HAVE_GETRANDOM 0
#endif

#ifndef FOLLY_HAVE_LIBRT
#if defined(__linux__) && !FOLLY_MOBILE
#define FOLLY_HAVE_LIBRT 1
#else
#define FOLLY_HAVE_LIBRT 0
#endif
#endif
