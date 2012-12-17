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

#ifndef FOLLY_PORTABILITY_H_
#define FOLLY_PORTABILITY_H_

#include "folly-config.h"

#ifdef FOLLY_HAVE_SCHED_H
 #include <sched.h>
 #ifndef FOLLY_HAVE_PTHREAD_YIELD
  #define pthread_yield sched_yield
 #endif
#endif

// Define macro wrappers for C++11's "final" and "override" keywords, which
// are supported in gcc 4.7 but not gcc 4.6.
//
// TODO(tudorb/agallagher): Autotoolize this.
#undef FOLLY_FINAL
#undef FOLLY_OVERRIDE

#if defined(__clang__)
#  define FOLLY_FINAL final
#  define FOLLY_OVERRIDE override
#elif defined(__GNUC__)
# include <features.h>
# if __GNUC_PREREQ(4,7)
#  define FOLLY_FINAL final
#  define FOLLY_OVERRIDE override
# endif
#endif

#ifndef FOLLY_FINAL
# define FOLLY_FINAL
#endif

#ifndef FOLLY_OVERRIDE
# define FOLLY_OVERRIDE
#endif


// MaxAlign: max_align_t isn't supported by gcc
#ifdef __GNUC__
struct MaxAlign { char c; } __attribute__((aligned));
#else /* !__GNUC__ */
# error Cannot define MaxAlign on this platform
#endif

#endif // FOLLY_PORTABILITY_H_
