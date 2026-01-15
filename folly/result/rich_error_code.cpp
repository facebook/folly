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

#include <folly/result/rich_error_code.h>

#include <folly/lang/Exception.h>

#if FOLLY_HAS_RESULT

namespace folly::detail {

void retrieve_rich_error_code_from_exception_ptr(
    const std::exception_ptr& ep, rich_error_code_query& query) {
  if (ep) {
    if (auto* ex = exception_ptr_get_object_hint<const rich_error_base>(ep)) {
      return ex->retrieve_code(query);
    }
  }
}

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
