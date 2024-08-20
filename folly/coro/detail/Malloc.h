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

#include <folly/CPortability.h>

#include <cstddef>

extern "C" {

// Heap allocations for coroutine-frames for all async coroutines
// (Task, AsyncGenerator, etc.) should be funneled through these
// functions to allow better tracing/profiling of coroutine allocations.
FOLLY_NOINLINE
void* folly_coro_async_malloc(std::size_t size);

FOLLY_NOINLINE
void folly_coro_async_free(void* ptr, std::size_t size);
} // extern "C"
