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

namespace folly {
namespace symbolizer {
namespace test {

// This function is compiled with -fomit-frame-pointer in a separate
// translation unit. It calls the provided callback, simulating a middle
// frame in a call chain where the frame pointer is not preserved.
FOLLY_NOINLINE void omitFpMiddle(void (*next)());

} // namespace test
} // namespace symbolizer
} // namespace folly
