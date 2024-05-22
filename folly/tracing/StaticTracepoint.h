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

#include <folly/portability/Config.h>

#if FOLLY_HAVE_ELF &&                                                    \
    (defined(__x86_64__) || defined(__i386__) || defined(__aarch64__) || \
     defined(__arm__)) &&                                                \
    !FOLLY_DISABLE_SDT

#define FOLLY_HAVE_SDT 1

#include <folly/tracing/StaticTracepoint-ELF.h>

#define FOLLY_SDT(provider, name, ...) \
  FOLLY_SDT_PROBE_N(                   \
      provider, name, 0, FOLLY_SDT_NARG(0, ##__VA_ARGS__), ##__VA_ARGS__)
// Use FOLLY_SDT_DEFINE_SEMAPHORE(provider, name) to define the semaphore
// as global variable before using the FOLLY_SDT_WITH_SEMAPHORE macro
#define FOLLY_SDT_WITH_SEMAPHORE(provider, name, ...) \
  FOLLY_SDT_PROBE_N(                                  \
      provider, name, 1, FOLLY_SDT_NARG(0, ##__VA_ARGS__), ##__VA_ARGS__)
#define FOLLY_SDT_IS_ENABLED(provider, name) \
  (FOLLY_SDT_SEMAPHORE(provider, name) > 0)

#else

#define FOLLY_HAVE_SDT 0

// Mark variadic macro args as unused from https://stackoverflow.com/a/31470425
#define FOLLY_UNUSED0()
#define FOLLY_UNUSED1(a) (void)(a)
#define FOLLY_UNUSED2(a, b) (void)(a), FOLLY_UNUSED1(b)
#define FOLLY_UNUSED3(a, b, c) (void)(a), FOLLY_UNUSED2(b, c)
#define FOLLY_UNUSED4(a, b, c, d) (void)(a), FOLLY_UNUSED3(b, c, d)
#define FOLLY_UNUSED5(a, b, c, d, e) (void)(a), FOLLY_UNUSED4(b, c, d, e)
#define FOLLY_UNUSED6(a, b, c, d, e, f) (void)(a), FOLLY_UNUSED5(b, c, d, e, f)
#define FOLLY_UNUSED7(a, b, c, d, e, f, g) \
  (void)(a), FOLLY_UNUSED6(b, c, d, e, f, g)
#define FOLLY_UNUSED8(a, b, c, d, e, f, g, h) \
  (void)(a), FOLLY_UNUSED7(b, c, d, e, f, g, h)

#define FOLLY_VA_NUM_ARGS_IMPL(_0, _1, _2, _3, _4, _5, _6, _7, _8, N, ...) N
#define FOLLY_VA_NUM_ARGS(...) \
  FOLLY_VA_NUM_ARGS_IMPL(100, ##__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)

#define FOLLY_ALL_UNUSED_IMPL_(nargs) FOLLY_UNUSED##nargs
#define FOLLY_ALL_UNUSED_IMPL(nargs) FOLLY_ALL_UNUSED_IMPL_(nargs)

#if defined(_MSC_VER)
#define FOLLY_ALL_UNUSED(...)
#else
#define FOLLY_ALL_UNUSED(...) \
  FOLLY_ALL_UNUSED_IMPL(FOLLY_VA_NUM_ARGS(__VA_ARGS__))(__VA_ARGS__)
#endif

#define FOLLY_SDT(provider, name, ...) \
  do {                                 \
    FOLLY_ALL_UNUSED(__VA_ARGS__);     \
  } while (0)
#define FOLLY_SDT_WITH_SEMAPHORE(provider, name, ...) \
  do {                                                \
    FOLLY_ALL_UNUSED(__VA_ARGS__);                    \
  } while (0)
#define FOLLY_SDT_IS_ENABLED(provider, name) (false)
#define FOLLY_SDT_SEMAPHORE(provider, name) \
  folly_sdt_semaphore_##provider##_##name
#define FOLLY_SDT_DEFINE_SEMAPHORE(provider, name)    \
  extern "C" {                                        \
  unsigned short FOLLY_SDT_SEMAPHORE(provider, name); \
  }
#define FOLLY_SDT_DECLARE_SEMAPHORE(provider, name) \
  extern "C" unsigned short FOLLY_SDT_SEMAPHORE(provider, name)

#endif
