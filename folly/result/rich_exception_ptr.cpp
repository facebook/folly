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

#include <folly/result/rich_exception_ptr.h>

#if FOLLY_HAS_RESULT

namespace folly::detail {

const std::exception_ptr& bad_result_access_singleton() {
  // Union trick: the empty destructor ensures `value` is never destroyed.
  // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
  union Storage {
    std::exception_ptr value;
    Storage()
        : value{make_exception_ptr_with(
              std::in_place_type<STUB_bad_result_access_error>)} {}
    ~Storage() {}
  };

  // Meyer singleton is thread-safe past C++11. Leak it to avoid SDOF.
  static Storage storage;
  return storage.value;
}

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
