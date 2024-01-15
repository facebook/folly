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

#include <folly/CPortability.h>
#include <folly/portability/Config.h>

#if defined(_MSC_VER)
#define FOLLY_CPLUSPLUS _MSVC_LANG
#else
#define FOLLY_CPLUSPLUS __cplusplus
#endif

// On MSVC an incorrect <version> header get's picked up
#if !defined(_MSC_VER) && __has_include(<version>)
#include <version>
#endif

static_assert(FOLLY_CPLUSPLUS >= 201703L, "__cplusplus >= 201703L");

#if defined(__GNUC__) && !defined(__clang__)
static_assert(__GNUC__ >= 7, "__GNUC__ >= 7");
#endif

#if defined(_MSC_VER) || defined(_CPPLIB_VER)
static_assert(FOLLY_CPLUSPLUS >= 201703L, "__cplusplus >= 201703L");
#endif

// Unaligned loads and stores
namespace folly {
#if defined(FOLLY_HAVE_UNALIGNED_ACCESS) && FOLLY_HAVE_UNALIGNED_ACCESS
constexpr bool kHasUnalignedAccess = true;
#else
constexpr bool kHasUnalignedAccess = false;
#endif
} // namespace folly

// compiler specific attribute translation
// msvc should come first, so if clang is in msvc mode it gets the right defines

// NOTE: this will only do checking in msvc with versions that support /analyze
#ifdef _MSC_VER
#ifdef _USE_ATTRIBUTES_FOR_SAL
#undef _USE_ATTRIBUTES_FOR_SAL
#endif
/* nolint */
#define _USE_ATTRIBUTES_FOR_SAL 1
#include <sal.h> // @manual
#define FOLLY_PRINTF_FORMAT _Printf_format_string_
#define FOLLY_PRINTF_FORMAT_ATTR(format_param, dots_param) /**/
#else
#define FOLLY_PRINTF_FORMAT /**/
#define FOLLY_PRINTF_FORMAT_ATTR(format_param, dots_param) \
  __attribute__((__format__(__printf__, format_param, dots_param)))
#endif

// warn unused result
#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(nodiscard)
#if defined(__clang__) || defined(__GNUC__)
#if __clang_major__ >= 10 || __GNUC__ >= 10
// early clang and gcc both warn on [[nodiscard]] when applied to class ctors
// easiest option is just to avoid emitting [[nodiscard]] under early clang/gcc
#define FOLLY_NODISCARD [[nodiscard]]
#endif
#endif
#endif
#endif
#ifndef FOLLY_NODISCARD
#define FOLLY_NODISCARD
#endif

// older clang-format gets confused by [[deprecated(...)]] on class decls
#define FOLLY_DEPRECATED(...) [[deprecated(__VA_ARGS__)]]

// target
#ifdef _MSC_VER
#define FOLLY_TARGET_ATTRIBUTE(target)
#else
#define FOLLY_TARGET_ATTRIBUTE(target) __attribute__((__target__(target)))
#endif

// detection for 64 bit
#if defined(__x86_64__) || defined(_M_X64)
#define FOLLY_X64 1
#else
#define FOLLY_X64 0
#endif

#if defined(__arm__)
#define FOLLY_ARM 1
#else
#define FOLLY_ARM 0
#endif

#if defined(__aarch64__)
#define FOLLY_AARCH64 1
#else
#define FOLLY_AARCH64 0
#endif

#if defined(__powerpc64__)
#define FOLLY_PPC64 1
#else
#define FOLLY_PPC64 0
#endif

#if defined(__s390x__)
#define FOLLY_S390X 1
#else
#define FOLLY_S390X 0
#endif

#if defined(__riscv)
#define FOLLY_RISCV64 1
#else
#define FOLLY_RISCV64 0
#endif

namespace folly {
constexpr bool kIsArchArm = FOLLY_ARM == 1;
constexpr bool kIsArchAmd64 = FOLLY_X64 == 1;
constexpr bool kIsArchAArch64 = FOLLY_AARCH64 == 1;
constexpr bool kIsArchPPC64 = FOLLY_PPC64 == 1;
constexpr bool kIsArchS390X = FOLLY_S390X == 1;
constexpr bool kIsArchRISCV64 = FOLLY_RISCV64 == 1;
} // namespace folly

namespace folly {

/**
 * folly::kIsLibrarySanitizeAddress reports if folly was compiled with ASAN
 * enabled.  Note that for compilation units outside of folly that include
 * folly/Portability.h, the value of kIsLibrarySanitizeAddress may be different
 * from whether or not the current compilation unit is being compiled with ASAN.
 */
#if FOLLY_LIBRARY_SANITIZE_ADDRESS
constexpr bool kIsLibrarySanitizeAddress = true;
#else
constexpr bool kIsLibrarySanitizeAddress = false;
#endif

#ifdef FOLLY_SANITIZE_ADDRESS
constexpr bool kIsSanitizeAddress = true;
#else
constexpr bool kIsSanitizeAddress = false;
#endif

#ifdef FOLLY_SANITIZE_THREAD
constexpr bool kIsSanitizeThread = true;
#else
constexpr bool kIsSanitizeThread = false;
#endif

#ifdef FOLLY_SANITIZE_DATAFLOW
constexpr bool kIsSanitizeDataflow = true;
#else
constexpr bool kIsSanitizeDataflow = false;
#endif

#ifdef FOLLY_SANITIZE
constexpr bool kIsSanitize = true;
#else
constexpr bool kIsSanitize = false;
#endif
} // namespace folly

// packing is very ugly in msvc
#ifdef _MSC_VER
#define FOLLY_PACK_ATTR /**/
#define FOLLY_PACK_PUSH __pragma(pack(push, 1))
#define FOLLY_PACK_POP __pragma(pack(pop))
#elif defined(__GNUC__)
#define FOLLY_PACK_ATTR __attribute__((__packed__))
#define FOLLY_PACK_PUSH /**/
#define FOLLY_PACK_POP /**/
#else
#define FOLLY_PACK_ATTR /**/
#define FOLLY_PACK_PUSH /**/
#define FOLLY_PACK_POP /**/
#endif

// It turns out that GNU libstdc++ and LLVM libc++ differ on how they implement
// the 'std' namespace; the latter uses inline namespaces. Wrap this decision
// up in a macro to make forward-declarations easier.
#if defined(_LIBCPP_VERSION)
#define FOLLY_NAMESPACE_STD_BEGIN _LIBCPP_BEGIN_NAMESPACE_STD
#define FOLLY_NAMESPACE_STD_END _LIBCPP_END_NAMESPACE_STD
#else
#define FOLLY_NAMESPACE_STD_BEGIN namespace std {
#define FOLLY_NAMESPACE_STD_END }
#endif

// If the new c++ ABI is used, __cxx11 inline namespace needs to be added to
// some types, e.g. std::list.
#if defined(_GLIBCXX_USE_CXX11_ABI) && _GLIBCXX_USE_CXX11_ABI
#define FOLLY_GLIBCXX_NAMESPACE_CXX11_BEGIN \
  inline _GLIBCXX_BEGIN_NAMESPACE_CXX11
#define FOLLY_GLIBCXX_NAMESPACE_CXX11_END _GLIBCXX_END_NAMESPACE_CXX11
#else
#define FOLLY_GLIBCXX_NAMESPACE_CXX11_BEGIN
#define FOLLY_GLIBCXX_NAMESPACE_CXX11_END
#endif

// MSVC specific defines
// mainly for posix compat
#ifdef _MSC_VER

// We have compiler support for the newest of the new, but
// MSVC doesn't tell us that.
//
// Clang pretends to be MSVC on Windows, but it refuses to compile
// SSE4.2 intrinsics unless -march argument is specified.
// So cannot unconditionally define __SSE4_2__ in clang.
#ifndef __clang__
#if !defined(_M_ARM) && !defined(_M_ARM64)
#define __SSE4_2__ 1
#endif // !defined(_M_ARM) && !defined(_M_ARM64)

// Hide a GCC specific thing that breaks MSVC if left alone.
#define __extension__

// compiler specific to compiler specific
// nolint
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif

#endif

// Define FOLLY_HAS_EXCEPTIONS
#if (defined(__cpp_exceptions) && __cpp_exceptions >= 199711) || \
    FOLLY_HAS_FEATURE(cxx_exceptions)
#define FOLLY_HAS_EXCEPTIONS 1
#elif __GNUC__
#if defined(__EXCEPTIONS) && __EXCEPTIONS
#define FOLLY_HAS_EXCEPTIONS 1
#else // __EXCEPTIONS
#define FOLLY_HAS_EXCEPTIONS 0
#endif // __EXCEPTIONS
#elif FOLLY_MICROSOFT_ABI_VER
#if _CPPUNWIND
#define FOLLY_HAS_EXCEPTIONS 1
#else // _CPPUNWIND
#define FOLLY_HAS_EXCEPTIONS 0
#endif // _CPPUNWIND
#else
#define FOLLY_HAS_EXCEPTIONS 1 // default assumption for unknown platforms
#endif

// Debug
namespace folly {
#ifdef NDEBUG
constexpr auto kIsDebug = false;
#else
constexpr auto kIsDebug = true;
#endif
} // namespace folly

// Exceptions
namespace folly {
#if FOLLY_HAS_EXCEPTIONS
constexpr auto kHasExceptions = true;
#else
constexpr auto kHasExceptions = false;
#endif
} // namespace folly

// Endianness
namespace folly {
#ifdef _MSC_VER
// It's MSVC, so we just have to guess ... and allow an override
#ifdef FOLLY_ENDIAN_BE
constexpr auto kIsLittleEndian = false;
#else
constexpr auto kIsLittleEndian = true;
#endif
#else
constexpr auto kIsLittleEndian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
#endif
constexpr auto kIsBigEndian = !kIsLittleEndian;
} // namespace folly

// Weak
namespace folly {
#if FOLLY_HAVE_WEAK_SYMBOLS
constexpr auto kHasWeakSymbols = true;
#else
constexpr auto kHasWeakSymbols = false;
#endif
} // namespace folly

#ifndef FOLLY_SSE
#if defined(__SSE4_2__)
#define FOLLY_SSE 4
#define FOLLY_SSE_MINOR 2
#elif defined(__SSE4_1__)
#define FOLLY_SSE 4
#define FOLLY_SSE_MINOR 1
#elif defined(__SSE4__)
#define FOLLY_SSE 4
#define FOLLY_SSE_MINOR 0
#elif defined(__SSE3__)
#define FOLLY_SSE 3
#define FOLLY_SSE_MINOR 0
#elif defined(__SSE2__)
#define FOLLY_SSE 2
#define FOLLY_SSE_MINOR 0
#elif defined(__SSE__)
#define FOLLY_SSE 1
#define FOLLY_SSE_MINOR 0
#else
#define FOLLY_SSE 0
#define FOLLY_SSE_MINOR 0
#endif
#endif

#ifndef FOLLY_SSSE
#if defined(__SSSE3__)
#define FOLLY_SSSE 3
#else
#define FOLLY_SSSE 0
#endif
#endif

#define FOLLY_SSE_PREREQ(major, minor) \
  (FOLLY_SSE > major || FOLLY_SSE == major && FOLLY_SSE_MINOR >= minor)

#ifndef FOLLY_NEON
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && !defined(__CUDACC__)
#define FOLLY_NEON 1
#else
#define FOLLY_NEON 0
#endif
#endif

#ifndef FOLLY_ARM_FEATURE_CRC32
#ifdef __ARM_FEATURE_CRC32
#define FOLLY_ARM_FEATURE_CRC32 1
#else
#define FOLLY_ARM_FEATURE_CRC32 0
#endif
#endif

// RTTI may not be enabled for this compilation unit.
#if defined(__GXX_RTTI) || defined(__cpp_rtti) || \
    (defined(_MSC_VER) && defined(_CPPRTTI))
#define FOLLY_HAS_RTTI 1
#else
#define FOLLY_HAS_RTTI 0
#endif

namespace folly {
constexpr bool const kHasRtti = FOLLY_HAS_RTTI;
} // namespace folly

#if defined(__APPLE__) || defined(_MSC_VER)
#define FOLLY_STATIC_CTOR_PRIORITY_MAX
#else
// 101 is the highest priority allowed by the init_priority attribute.
// This priority is already used by JEMalloc and other memory allocators so
// we will take the next one.
#define FOLLY_STATIC_CTOR_PRIORITY_MAX __attribute__((__init_priority__(102)))
#endif

#if defined(__APPLE__) && TARGET_OS_IOS
#define FOLLY_APPLE_IOS 1
#else
#define FOLLY_APPLE_IOS 0
#endif

#if defined(__APPLE__) && TARGET_OS_OSX
#define FOLLY_APPLE_MACOS 1
#else
#define FOLLY_APPLE_MACOS 0
#endif

#if defined(__APPLE__) && TARGET_OS_TV
#define FOLLY_APPLE_TVOS 1
#else
#define FOLLY_APPLE_TVOS 0
#endif

#if defined(__APPLE__) && TARGET_OS_WATCH
#define FOLLY_APPLE_WATCHOS 1
#else
#define FOLLY_APPLE_WATCHOS 0
#endif

namespace folly {

#ifdef __OBJC__
constexpr auto kIsObjC = true;
#else
constexpr auto kIsObjC = false;
#endif

#if FOLLY_MOBILE
constexpr auto kIsMobile = true;
#else
constexpr auto kIsMobile = false;
#endif

#if defined(__linux__) && !FOLLY_MOBILE
constexpr auto kIsLinux = true;
#else
constexpr auto kIsLinux = false;
#endif

#if defined(_WIN32)
constexpr auto kIsWindows = true;
#else
constexpr auto kIsWindows = false;
#endif

#if defined(__APPLE__)
constexpr auto kIsApple = true;
#else
constexpr auto kIsApple = false;
#endif

constexpr bool kIsAppleIOS = FOLLY_APPLE_IOS == 1;
constexpr bool kIsAppleMacOS = FOLLY_APPLE_MACOS == 1;
constexpr bool kIsAppleTVOS = FOLLY_APPLE_TVOS == 1;
constexpr bool kIsAppleWatchOS = FOLLY_APPLE_WATCHOS == 1;

#if defined(__GLIBCXX__)
constexpr auto kIsGlibcxx = true;
#else
constexpr auto kIsGlibcxx = false;
#endif

#if defined(__GLIBCXX__) && _GLIBCXX_RELEASE // major version, 7+
constexpr auto kGlibcxxVer = _GLIBCXX_RELEASE;
#else
constexpr auto kGlibcxxVer = 0;
#endif

#if defined(__GLIBCXX__) && defined(_GLIBCXX_ASSERTIONS)
constexpr auto kGlibcxxAssertions = true;
#else
constexpr auto kGlibcxxAssertions = false;
#endif

#ifdef _LIBCPP_VERSION
constexpr auto kIsLibcpp = true;
#else
constexpr auto kIsLibcpp = false;
#endif

#if defined(__GLIBCXX__)
constexpr auto kIsLibstdcpp = true;
#else
constexpr auto kIsLibstdcpp = false;
#endif

#ifdef _MSC_VER
constexpr auto kMscVer = _MSC_VER;
#else
constexpr auto kMscVer = 0;
#endif

#if defined(__GNUC__) && __GNUC__
constexpr auto kGnuc = __GNUC__;
#else
constexpr auto kGnuc = 0;
#endif

#if __clang__
constexpr auto kIsClang = true;
constexpr auto kClangVerMajor = __clang_major__;
#else
constexpr auto kIsClang = false;
constexpr auto kClangVerMajor = 0;
#endif

#ifdef FOLLY_MICROSOFT_ABI_VER
constexpr auto kMicrosoftAbiVer = FOLLY_MICROSOFT_ABI_VER;
#else
constexpr auto kMicrosoftAbiVer = 0;
#endif

// cpplib is an implementation of the standard library, and is the one typically
// used with the msvc compiler
#ifdef _CPPLIB_VER
constexpr auto kCpplibVer = _CPPLIB_VER;
#else
constexpr auto kCpplibVer = 0;
#endif
} // namespace folly

//  MSVC does not permit:
//
//    extern int const num;
//    constexpr int const num = 3;
//
//  Instead:
//
//    extern int const num;
//    FOLLY_STORAGE_CONSTEXPR int const num = 3;
//
//  True as of MSVC 2017.
#ifdef _MSC_VER
#define FOLLY_STORAGE_CONSTEXPR
#else
#define FOLLY_STORAGE_CONSTEXPR constexpr
#endif

//  FOLLY_CXX20_CONSTEXPR
//
//  C++20 permits more cases to be marked constexpr, including constructors that
//  leave members uninitialized and virtual functions.
#if FOLLY_CPLUSPLUS >= 202002L
#define FOLLY_CXX20_CONSTEXPR constexpr
#else
#define FOLLY_CXX20_CONSTEXPR
#endif

// C++20 constinit
#if defined(__cpp_constinit) && __cpp_constinit >= 201907L
#define FOLLY_CONSTINIT constinit
#else
#define FOLLY_CONSTINIT
#endif

#if defined(FOLLY_CFG_NO_COROUTINES)
#define FOLLY_HAS_COROUTINES 0
#else
#if FOLLY_CPLUSPLUS >= 201703L
// folly::coro requires C++17 support
#if defined(__NVCC__)
// For now, NVCC matches other compilers but does not offer coroutines.
#define FOLLY_HAS_COROUTINES 0
#elif defined(_WIN32) && defined(__clang__) && !defined(LLVM_COROUTINES)
// LLVM and MSVC coroutines are ABI incompatible, so for the MSVC implementation
// of <experimental/coroutine> on Windows we *don't* have coroutines.
//
// LLVM_COROUTINES indicates that LLVM compatible header is added to include
// path and can be used.
//
// Worse, if we define FOLLY_HAS_COROUTINES 1 we will include
// <experimental/coroutine> which will conflict with anyone who wants to load
// the LLVM implementation of coroutines on Windows.
#define FOLLY_HAS_COROUTINES 0
#elif defined(_MSC_VER) && _MSC_VER && defined(_RESUMABLE_FUNCTIONS_SUPPORTED)
// NOTE: MSVC 2017 does not currently support the full Coroutines TS since it
// does not yet support symmetric-transfer.
#define FOLLY_HAS_COROUTINES 0
#elif (                                                                    \
    (defined(__cpp_coroutines) && __cpp_coroutines >= 201703L) ||          \
    (defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L)) && \
    (__has_include(<coroutine>) || __has_include(<experimental/coroutine>))
#define FOLLY_HAS_COROUTINES 1
// This is mainly to workaround bugs triggered by LTO, when stack allocated
// variables in await_suspend end up on a coroutine frame.
#define FOLLY_CORO_AWAIT_SUSPEND_NONTRIVIAL_ATTRIBUTES FOLLY_NOINLINE
#else
#define FOLLY_HAS_COROUTINES 0
#endif
#else
#define FOLLY_HAS_COROUTINES 0
#endif // FOLLY_CPLUSPLUS >= 201703L
#endif // FOLLY_CFG_NO_COROUTINES

#if __cpp_inline_variables >= 201606L || FOLLY_CPLUSPLUS >= 201703L
#define FOLLY_HAS_INLINE_VARIABLES 1
#define FOLLY_INLINE_VARIABLE inline
#else
#define FOLLY_HAS_INLINE_VARIABLES 0
#define FOLLY_INLINE_VARIABLE
#endif

// feature test __cpp_lib_string_view is defined in <string>, which is
// too heavy to include here.
#if __has_include(<string_view>) && FOLLY_CPLUSPLUS >= 201703L
#define FOLLY_HAS_STRING_VIEW 1
#else
#define FOLLY_HAS_STRING_VIEW 0
#endif

// C++20 consteval
#if FOLLY_CPLUSPLUS >= 202002L
#define FOLLY_CONSTEVAL consteval
#else
#define FOLLY_CONSTEVAL constexpr
#endif
