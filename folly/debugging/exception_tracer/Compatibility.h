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

#include <version>

#if (defined(__GLIBCXX__) || defined(_LIBCPP_VERSION))
#define FOLLY_DETAIL_EXN_TRACER_CXX_STDLIB_COMPATIBLE 1
#else
#define FOLLY_DETAIL_EXN_TRACER_CXX_STDLIB_COMPATIBLE 0
#endif // (defined(__GLIBCXX__) || defined(_LIBCPP_VERSION))

#if (!defined(__FreeBSD__) && !_WIN32)
#define FOLLY_DETAIL_EXN_TRACER_OS_CXX_ABI_COMPATIBLE 1
#else
#define FOLLY_DETAIL_EXN_TRACER_OS_CXX_ABI_COMPATIBLE 0
#endif // (!defined(__FreeBSD__) && ! _WIN32)

#if FOLLY_DETAIL_EXN_TRACER_CXX_STDLIB_COMPATIBLE && \
    FOLLY_DETAIL_EXN_TRACER_OS_CXX_ABI_COMPATIBLE
#define FOLLY_HAS_EXCEPTION_TRACER 1
#else
#define FOLLY_HAS_EXCEPTION_TRACER 0
#endif // CXX_STDLIB_COMPATIBLE && OS_CXX_ABI_COMPATIBLE
