/* folly-config.h for CMake builds */

/* C++11 support related */
#cmakedefine FOLLY_FINAL final
#cmakedefine FOLLY_OVERRIDE override
#cmakedefine FOLLY_HAVE_CONSTEXPR

/* Defines to change the behavior of Portability.h */
#cmakedefine FOLLY_USE_LIBCPP 1
#cmakedefine FOLLY_HAVE_CLOCK_GETTIME 1
#cmakedefine FOLLY_HAVE_BITS_FUNCTEXCEPT_H 1
#cmakedefine FOLLY_HAVE_STD__IS_TRIVIALLY_COPYABLE 1


#cmakedefine HAVE_BYTESWAP 1
#cmakedefine HAVE_CONSTEXPR_STRLEN 1
#cmakedefine HAVE_EMMINTRIN 1
#cmakedefine HAVE_UNISTD 1
#cmakedefine HAVE_TIME 1
#cmakedefine HAVE_SYS_TYPES_H 1
#cmakedefine HAVE_SYS_TIME 1
#cmakedefine HAVE_STRING 1
#cmakedefine HAVE_STRERROR 1
#cmakedefine HAVE_STD__THIS_THREAD__SLEEP_FOR 1

#cmakedefine HAVE_STDLIB 1
#cmakedefine HAVE_STDINT_H 1
#cmakedefine HAVE_STDDEF_H 1
#cmakedefine HAVE_FCNTL 1
#cmakedefine HAVE_FEATURES 1
#cmakedefine HAVE_GETDELIM 1
#cmakedefine HAVE_GETTIMEOFDAY 1
#cmakedefine HAVE_IFUNC 1
#cmakedefine HAVE_INTTYPES 1
#cmakedefine HAVE_LIMITS 1
#cmakedefine HAVE_MALLOC 1
#cmakedefine HAVE_MALLOC_SIZE 1
#cmakedefine HAVE_MALLOC_USABLE_SIZE 1
#cmakedefine HAVE_MEMMOVE 1
#cmakedefine HAVE_MEMRCHR 1
#cmakedefine HAVE_MEMSET 1
#cmakedefine HAVE_MUTEX 1
#cmakedefine HAVE_POW 1
#cmakedefine HAVE_PTHREAD_YIELD 1
#cmakedefine HAVE_PTRDIFF_T 1
#cmakedefine HAVE_RALLOCM 1
#cmakedefine HAVE_SCHED 1
#cmakedefine HAVE_SCHED_YIELD 1

#cmakedefine HAVE_WEAK_SYMBOLS 1

/* end folly-config.h for CMake builds */
