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

#ifndef FOLLY_PORTABILITY_H_
#define FOLLY_PORTABILITY_H_

#ifndef FOLLY_NO_CONFIG
#include "folly-config.h"
#endif

#ifdef FOLLY_HAVE_FEATURES_H
#include <features.h>
#endif


#ifdef FOLLY_HAVE_SCHED_H
 #include <sched.h>
 #ifndef FOLLY_HAVE_PTHREAD_YIELD
  #define pthread_yield sched_yield
 #endif
#endif


// MaxAlign: max_align_t isn't supported by gcc
#ifdef __GNUC__
struct MaxAlign { char c; } __attribute__((aligned));
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

#endif // FOLLY_PORTABILITY_H_
