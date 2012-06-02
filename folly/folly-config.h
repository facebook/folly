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

#ifndef _FOLLY_CONFIG_H
#define _FOLLY_CONFIG_H 1

/* folly-config.h. Generated automatically at end of configure. */
/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

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

/* Define to 1 if you have the <dlfcn.h> header file. */
#ifndef FOLLY_HAVE_DLFCN_H
#define FOLLY_HAVE_DLFCN_H 1
#endif

/* Define to 1 if you have the <fcntl.h> header file. */
#ifndef FOLLY_HAVE_FCNTL_H
#define FOLLY_HAVE_FCNTL_H 1
#endif

/* Define to 1 if you have the <features.h> header file. */
#ifndef FOLLY_HAVE_FEATURES_H
#define FOLLY_HAVE_FEATURES_H 1
#endif

/* Define to 1 if you have the `ffsll' function. */
#ifndef FOLLY_HAVE_FFSLL
#define FOLLY_HAVE_FFSLL 1
#endif

/* Define to 1 if you have the `getdelim' function. */
#ifndef FOLLY_HAVE_GETDELIM
#define FOLLY_HAVE_GETDELIM 1
#endif

/* Define to 1 if you have the `gettimeofday' function. */
#ifndef FOLLY_HAVE_GETTIMEOFDAY
#define FOLLY_HAVE_GETTIMEOFDAY 1
#endif

/* Define to 1 if you have the <inttypes.h> header file. */
#ifndef FOLLY_HAVE_INTTYPES_H
#define FOLLY_HAVE_INTTYPES_H 1
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
#ifndef FOLLY_HAVE_LIBJEMALLOC
#define FOLLY_HAVE_LIBJEMALLOC 1
#endif

/* Define to 1 if you have the `tcmalloc' library (-ltcmalloc). */
#ifndef FOLLY_HAVE_LIBTCMALLOC
#define FOLLY_HAVE_LIBTCMALLOC 1
#endif

/* Define to 1 if you have the <limits.h> header file. */
#ifndef FOLLY_HAVE_LIMITS_H
#define FOLLY_HAVE_LIMITS_H 1
#endif

/* Define to 1 if you have the `malloc_size' function. */
/* #undef HAVE_MALLOC_SIZE */

/* Define to 1 if you have the `malloc_usable_size' function. */
#ifndef FOLLY_HAVE_MALLOC_USABLE_SIZE
#define FOLLY_HAVE_MALLOC_USABLE_SIZE 1
#endif

/* Define to 1 if you have the `memmove' function. */
#ifndef FOLLY_HAVE_MEMMOVE
#define FOLLY_HAVE_MEMMOVE 1
#endif

/* Define to 1 if you have the <memory.h> header file. */
#ifndef FOLLY_HAVE_MEMORY_H
#define FOLLY_HAVE_MEMORY_H 1
#endif

/* Define to 1 if you have the `memset' function. */
#ifndef FOLLY_HAVE_MEMSET
#define FOLLY_HAVE_MEMSET 1
#endif

/* Define to 1 if you have the <mutex.h> header file. */
/* #undef HAVE_MUTEX_H */

/* Define to 1 if you have the `pow' function. */
#ifndef FOLLY_HAVE_POW
#define FOLLY_HAVE_POW 1
#endif

/* Define to 1 if you have the `pthread_yield' function. */
/* #undef HAVE_PTHREAD_YIELD */

/* Define to 1 if the system has the type `ptrdiff_t'. */
#ifndef FOLLY_HAVE_PTRDIFF_T
#define FOLLY_HAVE_PTRDIFF_T 1
#endif

/* Define to 1 if you have the `rallocm' function. */
/* #undef HAVE_RALLOCM */

/* Define to 1 if you have the <sched.h> header file. */
#ifndef FOLLY_HAVE_SCHED_H
#define FOLLY_HAVE_SCHED_H 1
#endif

/* Define to 1 if you have the `sched_yield' function. */
#ifndef FOLLY_HAVE_SCHED_YIELD
#define FOLLY_HAVE_SCHED_YIELD 1
#endif

/* Define to 1 if stdbool.h conforms to C99. */
#ifndef FOLLY_HAVE_STDBOOL_H
#define FOLLY_HAVE_STDBOOL_H 1
#endif

/* Define if g++ supports C++0x features. */
#ifndef FOLLY_HAVE_STDCXX_0X
#define FOLLY_HAVE_STDCXX_0X /**/
#endif

/* Define to 1 if you have the <stdint.h> header file. */
#ifndef FOLLY_HAVE_STDINT_H
#define FOLLY_HAVE_STDINT_H 1
#endif

/* Define to 1 if you have the <stdlib.h> header file. */
#ifndef FOLLY_HAVE_STDLIB_H
#define FOLLY_HAVE_STDLIB_H 1
#endif

/* Define to 1 if you have the `strerror' function. */
#ifndef FOLLY_HAVE_STRERROR
#define FOLLY_HAVE_STRERROR 1
#endif

/* Define to 1 if you have the <strings.h> header file. */
#ifndef FOLLY_HAVE_STRINGS_H
#define FOLLY_HAVE_STRINGS_H 1
#endif

/* Define to 1 if you have the <string.h> header file. */
#ifndef FOLLY_HAVE_STRING_H
#define FOLLY_HAVE_STRING_H 1
#endif

/* Define to 1 if you have the <sys/stat.h> header file. */
#ifndef FOLLY_HAVE_SYS_STAT_H
#define FOLLY_HAVE_SYS_STAT_H 1
#endif

/* Define to 1 if you have the <sys/time.h> header file. */
#ifndef FOLLY_HAVE_SYS_TIME_H
#define FOLLY_HAVE_SYS_TIME_H 1
#endif

/* Define to 1 if you have the <sys/types.h> header file. */
#ifndef FOLLY_HAVE_SYS_TYPES_H
#define FOLLY_HAVE_SYS_TYPES_H 1
#endif

/* Define to 1 if you have the <unistd.h> header file. */
#ifndef FOLLY_HAVE_UNISTD_H
#define FOLLY_HAVE_UNISTD_H 1
#endif

/* Define to 1 if you have the <malloc.h> header file. */
#ifndef FOLLY_HAVE_MALLOC_H
#define FOLLY_HAVE_MALLOC_H 1
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

/* Version number of package */
#ifndef FOLLY_VERSION
#define FOLLY_VERSION "0.1"
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

/* once: _FOLLY_CONFIG_H */
#endif
