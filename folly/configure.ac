
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(folly, 0.1, folly@fb.com)
AC_CONFIG_SRCDIR([Likely.h])
AC_CONFIG_HEADERS([config.h])
AX_PREFIX_CONFIG_H([folly-config.h], [folly], [config.h])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([foreign dist-bzip2])

AC_CONFIG_MACRO_DIR([m4])

AC_PROG_INSTALL
AM_PROG_LIBTOOL

AC_LANG([C++])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_CXX_COMPILE_STDCXX_0X

# Checks for libraries.
AC_CHECK_LIB([glog],[openlog],[],[AC_MSG_ERROR(
             [Please install google-glog library])])
AC_CHECK_LIB([gflags],[getenv],[],[AC_MSG_ERROR(
             [Please install google-gflags library])])

# check for boost libs
AX_BOOST_BASE
AX_BOOST_THREAD
AX_BOOST_REGEX

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h inttypes.h limits.h stdint.h stdlib.h string.h sys/time.h unistd.h mutex.h features.h malloc.h])

AC_CHECK_HEADER(double-conversion.h, [], [AC_MSG_ERROR(
                [Couldn't find double-conversion.h, please download from \
                http://code.google.com/p/double-conversion/])], [])
AC_CHECK_LIB([double_conversion_pic],[ceil],[],[AC_MSG_ERROR(
             [Please install double-conversion library])])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_C_VOLATILE
AC_CHECK_TYPES([ptrdiff_t])

# Checks for library functions.
AC_CHECK_FUNCS([getdelim \
                gettimeofday \
                memmove \
                memset \
                pow \
                strerror \
                pthread_yield \
                ffsll \
                rallocm \
                malloc_size \
                malloc_usable_size])

if test "$ac_cv_func_pthread_yield" = "no"; then
   AC_CHECK_HEADERS([sched.h])
   AC_CHECK_FUNCS([sched_yield])
fi

CXX_FLAGS=""
if test "$ac_cv_cxx_compile_cxx0x_cxx" = yes; then
   CXX_FLAGS="-std=c++0x"
fi
if test "$ac_cv_cxx_compile_cxx0x_gxx" = yes; then
   CXX_FLAGS="-std=gnu++0x"
fi

AC_SUBST(AM_CPPFLAGS, '-I../$(top_srcdir)'" "'-I$(top_srcdir)/io'" "'-I$(top_srcdir)/test'" -lstdc++ $CXX_FLAGS $BOOST_CPPFLAGS")
AC_SUBST(AM_CXXFLAGS, "$BOOST_THREAD_LIB")

AM_CONDITIONAL([HAVE_STD_THREAD], [test "$ac_cv_header_features" = "yes"])
AM_CONDITIONAL([HAVE_X86_64], [test "$build_cpu" = "x86_64"])

# Output
AC_CONFIG_FILES([Makefile
                 test/Makefile
                 test/function_benchmark/Makefile])
AC_OUTPUT
