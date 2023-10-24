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

#include <cstdint>

namespace folly {

/**
 * Generates a 64-bit id that is unique within the process. The returned ids
 * should not be persisted or passed to other processes, and there are no
 * ordering guarantees.
 *
 * It is guaranteed that 0 is never returned, hence 0 can be used as a sentinel
 * value, similarly to nullptr.
 *
 * The function is thread-safe.
 *
 * The uniqueness guarantee can be broken if enough ids are generated, but even
 * in the most pessimistic scenario it would take a few hundred years.
 */
uint64_t processLocalUniqueId();

} // namespace folly
