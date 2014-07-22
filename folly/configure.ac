
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

m4_define([folly_version_str], m4_esyscmd_s([cat VERSION]))

AC_INIT([folly], m4_translit(folly_version_str, [:], [.]), [folly@fb.com])

# We assume all revisions are backwards incompatible.
LT_VERSION=folly_version_str:0
AC_SUBST([LT_VERSION])

AC_CONFIG_SRCDIR([Likely.h])
AC_CONFIG_HEADERS([config.h])
AX_PREFIX_CONFIG_H([folly-config.h], [folly], [config.h])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([foreign dist-bzip2 nostdinc subdir-objects])

AC_CONFIG_MACRO_DIR([m4])

AX_CONFIG_FEATURE_DEFAULT_DISABLED
AX_CONFIG_FEATURE(
        [deprecated-assoc],
        [supports deprecated associative containers (hash_map/hash_set)],
        [HAVE_DEPRECATED_ASSOC],
        [Define if you want to support deprecated associative containers])

AC_PROG_INSTALL
AM_PROG_LIBTOOL

AC_LANG([C++])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_CXX_COMPILE_STDCXX_0X

# Be sure to add any -std option to CXXFLAGS before we invoke any
# AC_COMPILE_IFELSE() or similar macros. Any such macros that are invoked
# before we update CXXFLAGS will not be run with the same options that we use
# during the real build.
STD=""
if test "x$ac_cv_cxx_compile_cxx0x_cxx" = xyes; then
   STD="-std=c++0x"
fi
if test "x$ac_cv_cxx_compile_cxx0x_gxx" = xyes; then
   STD="-std=gnu++0x"
fi

CXXFLAGS="$STD $CXXFLAGS"

# Checks for libraries.
AC_CHECK_LIB([glog],[openlog],[],[AC_MSG_ERROR(
             [Please install google-glog library])])
AC_CHECK_LIB([gflags],[getenv],[],[AC_MSG_ERROR(
             [Please install google-gflags library])])

# check for boost libs
AX_BOOST_BASE([1.20.0], [], [AC_MSG_ERROR(
              [Please install boost >= 1.20.0 (thread, regex, and system)])])
AX_BOOST_THREAD
AX_BOOST_REGEX
AX_BOOST_SYSTEM
AX_BOOST_FILESYSTEM

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h features.h inttypes.h limits.h stdint.h stdlib.h string.h sys/time.h unistd.h mutex.h malloc.h emmintrin.h byteswap.h bits/functexcept.h bits/c++config.h])

AC_CHECK_HEADER(double-conversion/double-conversion.h, [], [AC_MSG_ERROR(
                [Couldn't find double-conversion.h, please download from \
                http://code.google.com/p/double-conversion/])], [])
AC_CHECK_LIB([double-conversion],[ceil],[],[AC_MSG_ERROR(
             [Please install double-conversion library])])

AC_CHECK_LIB([event], [event_set], [], [AC_MSG_ERROR([Unable to find libevent])])

AC_CHECK_LIB([jemalloc], [rallocm])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_C_VOLATILE
AC_CHECK_TYPE([__int128],
  [AC_DEFINE([HAVE_INT128_T], [1], [Define if __int128 exists])],
  [AC_DEFINE([HAVE_INT128_T], [0], [Define if __int128 does not exist])])
AC_CHECK_TYPES([ptrdiff_t])

AC_CACHE_CHECK(
  [for ifunc support],
  [folly_cv_prog_cc_ifunc],
  [AC_COMPILE_IFELSE(
    [AC_LANG_SOURCE[
      #pragma GCC diagnostic error "-Wattributes"
      extern "C" void (*test_ifunc(void))() { return 0; }
      void func() __attribute__((ifunc("test_ifunc")));]
    ],
    [folly_cv_prog_cc_ifunc=yes],
    [folly_cv_prog_cc_ifunc=no])])

if test "$folly_cv_prog_cc_ifunc" = "yes"; then
  AC_DEFINE([HAVE_IFUNC], [1], [Define to 1 if the compiler supports ifunc])
fi

AC_CACHE_CHECK(
  [for final and override support],
  [folly_cv_c_final_override],
  [AC_COMPILE_IFELSE(
    [AC_LANG_SOURCE[class C { virtual void f() final {} virtual void g() {} };
                    class D : public C { virtual void g() override {} };]],
    [folly_cv_c_final_override=yes],
    [folly_cv_c_final_override=no])])

if test "$folly_cv_c_final_override" = "yes"; then
  final_val=final
  override_val=override
else
  final_val=
  override_val=
fi

AC_DEFINE_UNQUOTED(
  [FINAL], [$final_val],
  [Define to "final" if the compiler supports C++11 "final"])
AC_DEFINE_UNQUOTED(
  [OVERRIDE], [$override_val],
  [Define to "override" if the compiler supports C++11 "override"])

AC_CACHE_CHECK(
  [for std::this_thread::sleep_for],
  [folly_cv_func_this_thread_sleep_for],
  [AC_COMPILE_IFELSE(
    [AC_LANG_SOURCE[
      #include <thread>
      #include <chrono>
      void func() { std::this_thread::sleep_for(std::chrono::seconds(1)); }]],
    [folly_cv_func_this_thread_sleep_for=yes],
    [folly_cv_func_this_thread_sleep_for=no])])

if test "$folly_cv_func_this_thread_sleep_for" = yes; then
    AC_DEFINE([HAVE_STD__THIS_THREAD__SLEEP_FOR], [1],
              [Define to 1 if std::this_thread::sleep_for() is defined.])
fi

AC_CACHE_CHECK(
  [for constexpr strlen],
  [folly_cv_func_constexpr_strlen],
  [AC_COMPILE_IFELSE(
    [AC_LANG_SOURCE[
      #include <cstring>
      static constexpr int val = strlen("foo");]],
    [folly_cv_func_constexpr_strlen=yes],
    [folly_cv_func_constexpr_strlen=no])])

if test "$folly_cv_func_constexpr_strlen" = yes; then
    AC_DEFINE([HAVE_CONSTEXPR_STRLEN], [1],
              [Define to 1 if strlen(3) is constexpr.])
fi

AC_CACHE_CHECK(
  [for libc++],
  [folly_cv_lib_libcpp],
  [AC_COMPILE_IFELSE(
    [AC_LANG_SOURCE[
      #include <type_traits>
      #if !_LIBCPP_VERSION
      #error No libc++
      #endif
      void func() {}]
    ],
    [folly_cv_lib_libcpp=yes],
    [folly_cv_lib_libcpp=no])])

if test "$folly_cv_lib_libcpp" = yes; then
  AC_DEFINE([USE_LIBCPP], [1], [Define to 1 if we're using libc++.])
fi

AC_CACHE_CHECK(
  [for usable std::is_trivially_copyable],
  [folly_cv_decl_std_is_trivially_copyable],
  [AC_COMPILE_IFELSE(
    [AC_LANG_SOURCE[
      #include <type_traits>
      const bool val = std::is_trivially_copyable<bool>::value;]
    ],
    [folly_cv_decl_std_is_trivially_copyable=yes],
    [folly_cv_decl_std_is_trivially_copyable=no])])

if test "$folly_cv_decl_std_is_trivially_copyable" = yes; then
  AC_DEFINE([HAVE_STD__IS_TRIVIALLY_COPYABLE], [1],
            [Define to 1 if we have a usable std::is_trivially_copyable<T>
             implementation.])
fi

AC_CACHE_CHECK(
  [gflags namespace],
  [folly_cv_decl_gflags_namespace],
  [AC_COMPILE_IFELSE(
    [AC_LANG_SOURCE[
      #include <gflags/gflags.h>
      void foo() { gflags::GetArgv(); }]
    ],
    [folly_cv_decl_gflags_namespace=gflags],
    [AC_COMPILE_IFELSE(
      [AC_LANG_SOURCE[
        #include <gflags/gflags.h>
        void foo() { google::GetArgv(); }]
      ],
      [folly_cv_decl_gflags_namespace=google],
      [folly_cv_decl_gflags_namespace=error])])])

if test "$folly_cv_decl_gflags_namespace" = error; then
  AC_MSG_ERROR([Cannot determine gflags namespace])
else
  AC_DEFINE_UNQUOTED(
    [GFLAGS_NAMESPACE], [$folly_cv_decl_gflags_namespace],
    [Define to gflags namespace (usually "google" or "gflags")])
  if test "$folly_cv_decl_gflags_namespace" != gflags; then
     AC_DEFINE([UNUSUAL_GFLAGS_NAMESPACE], [1],
               [Define to 1 if the gflags namespace is not "gflags"])
  fi
fi

# Figure out if we support weak symbols. If not, we will link in some null
# stubs for functions that would otherwise be weak.
AC_CACHE_CHECK(
  [for weak symbol support],
  [folly_cv_prog_cc_weak_symbols],
  [AC_LINK_IFELSE(
    [AC_LANG_SOURCE[
      extern "C" void configure_link_extern_weak_test() __attribute__((weak));
      int main(int argc, char** argv) {
          return configure_link_extern_weak_test == nullptr;
      }]],
    [folly_cv_prog_cc_weak_symbols="yes"],
    [folly_cv_prog_cc_weak_symbols="no"])])

if test "$folly_cv_prog_cc_weak_symbols" = yes; then
  AC_DEFINE([HAVE_WEAK_SYMBOLS], [1],
            [Define to 1 if the linker supports weak symbols.])
fi

AC_SEARCH_LIBS([cplus_demangle_v3_callback], [iberty_pic iberty])
if test "$ac_cv_search_cplus_demangle_v3_callback" != "no" ; then
  AC_DEFINE([HAVE_CPLUS_DEMANGLE_V3_CALLBACK], [1],
            [Define to 1 if we have cplus_demangle_v3_callback.])
fi

# Check for clock_gettime(2). This is not in an AC_CHECK_FUNCS() because we
# want to link with librt if necessary.
AC_SEARCH_LIBS([clock_gettime], [rt],
  AC_DEFINE(
    [HAVE_CLOCK_GETTIME],
    [1],
    [Define to 1 if we support clock_gettime(2).]),
  [])

# Checks for library functions.
AC_CHECK_FUNCS([getdelim \
                gettimeofday \
                memmove \
                memset \
                pow \
                strerror \
                pthread_yield \
                rallocm \
                malloc_size \
                malloc_usable_size \
                memrchr \
                pipe2])

if test "$ac_cv_func_pthread_yield" = "no"; then
   AC_CHECK_HEADERS([sched.h])
   AC_CHECK_FUNCS([sched_yield])
fi

AC_CHECK_HEADER([lz4.h], AC_CHECK_LIB([lz4], [LZ4_decompress_safe]))
AC_CHECK_HEADER([snappy.h], AC_CHECK_LIB([snappy], [main]))
AC_CHECK_HEADER([zlib.h], AC_CHECK_LIB([z], [main]))
AC_CHECK_HEADER([lzma.h], AC_CHECK_LIB([lzma], [main]))

# Include directory that contains "folly" so #include <folly/Foo.h> works
AM_CPPFLAGS='-I$(top_srcdir)/..'
AM_CPPFLAGS="$AM_CPPFLAGS $BOOST_CPPFLAGS"
AM_LDFLAGS="$AM_LDFLAGS $BOOST_THREAD_LIB $BOOST_FILESYSTEM_LIB"
AM_LDFLAGS="$AM_LDFLAGS $BOOST_SYSTEM_LIB $BOOST_REGEX_LIB -lpthread"

AC_SUBST([AM_CPPFLAGS])
AC_SUBST([AM_LDFLAGS])

AM_CONDITIONAL([HAVE_STD_THREAD], [test "$ac_cv_header_features" = "yes"])
AM_CONDITIONAL([HAVE_X86_64], [test "$build_cpu" = "x86_64"])
AM_CONDITIONAL([HAVE_LINUX], [test "$build_os" == "linux-gnu"])
AM_CONDITIONAL([HAVE_WEAK_SYMBOLS],
               [test "$folly_cv_prog_cc_weak_symbols" = "yes"])
AM_CONDITIONAL([HAVE_BITS_FUNCTEXCEPT], [test "$ac_cv_header_bits_functexcept" = "yes"])

# Output
AC_CONFIG_FILES([Makefile
                 test/Makefile
                 test/function_benchmark/Makefile])
AC_OUTPUT
