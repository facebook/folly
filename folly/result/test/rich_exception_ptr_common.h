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

#include <folly/portability/GTest.h>
#include <folly/result/rich_exception_ptr.h>

// All `rich_exception_ptr.h` tests should use `rich_exception_ptr_packed` and
// `rich_exception_ptr_separate` to ensure that they cover both storage types.
// Although slightly redundant, they should also test `rich_exception_ptr`.

#if FOLLY_HAS_RESULT

namespace folly::detail {

// Minimal rich error for tests
struct RichErr : rich_error_base {
  using folly_get_exception_hint_types = rich_error_hints<RichErr>;
};

// `rich_exception_ptr` but storage guaranteed to be separate
class rich_exception_ptr_separate final
    : public rich_exception_ptr_impl<
          rich_exception_ptr_separate,
          detail::rich_exception_ptr_separate_storage> {
  using rich_exception_ptr_impl<
      rich_exception_ptr_separate,
      detail::rich_exception_ptr_separate_storage>::rich_exception_ptr_impl;
};

// `rich_exception_ptr`, with storage guaranteed to be packed
class rich_exception_ptr_packed final
    : public rich_exception_ptr_impl<
          rich_exception_ptr_packed,
          detail::rich_exception_ptr_packed_storage> {
  using rich_exception_ptr_impl<
      rich_exception_ptr_packed,
      detail::rich_exception_ptr_packed_storage>::rich_exception_ptr_impl;
};

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
