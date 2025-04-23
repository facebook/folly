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

#include <folly/result/result.h>

#include <glog/logging.h>
#include <folly/Indestructible.h>

#if FOLLY_HAS_RESULT

namespace folly::detail {

const non_value_result& dfatal_get_empty_result_error() {
  static const folly::Indestructible<non_value_result> r{empty_result_error{}};
  LOG(DFATAL) << "`folly::result` had an empty underlying `folly::Expected`";
  return *r;
}

const non_value_result& dfatal_get_bad_result_access_error() {
  static const folly::Indestructible<non_value_result> r{
      bad_result_access_error{}};
  LOG(DFATAL)
      << "Used `non_value()` accessor for `folly::result` in value state";
  return *r;
}

void fatal_if_exception_wrapper_invalid(const exception_wrapper& ew) {
  if (!ew.has_exception_ptr()) {
    LOG(FATAL) << "`result` may not contain an empty `std::exception_ptr`";
  }
  if (folly::get_exception<folly::OperationCancelled>(ew)) {
    LOG(FATAL)
        << "Do not store `OperationCancelled` in `result`. If you got this "
        << "error while extracting an `exception_wrapper`, `exception_ptr`, "
        << "or similar, you must check `has_stopped()` before doing that!";
  }
}

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
