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

#include <cstddef>

namespace folly {

// This is an AARCH64 optimized memchr, which works best for long strings
// _long here is more than 128 byted to scan for ch.
// When scanning for up to 16 bytes, libc memchr is 1 CPU cycle faster than this implementation
// and between 16 and 128 bytes libc memchr is still faster.
extern "C" void* memchr_long(const void* ptr, int ch, std::size_t count);

} // namespace folly
