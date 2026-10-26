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

#if defined(__linux__) && __has_include(<liburing/io_uring_version.h>)
#include <liburing/io_uring_version.h>
#endif

// TODO: T294823393: remove these version checks once every liburing that folly
// builds against is 2.15 or newer.
// folly's io_uring code uses APIs that first shipped in liburing 2.10, such as
// io_uring_set_iowait().
#if defined(IO_URING_VERSION_MAJOR) && \
    (IO_URING_VERSION_MAJOR > 2 ||     \
     (IO_URING_VERSION_MAJOR == 2 && IO_URING_VERSION_MINOR >= 10))
#define FOLLY_HAS_LIBURING 1
#else
#define FOLLY_HAS_LIBURING 0
#endif

// The ZCRX query and control APIs first shipped in liburing 2.15.
#if FOLLY_HAS_LIBURING &&          \
    (IO_URING_VERSION_MAJOR > 2 || \
     (IO_URING_VERSION_MAJOR == 2 && IO_URING_VERSION_MINOR >= 15))
#define FOLLY_HAS_LIBURING_ZCRX 1
#else
#define FOLLY_HAS_LIBURING_ZCRX 0
#endif
