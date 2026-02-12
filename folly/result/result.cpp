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

#include <ostream>

#include <glog/logging.h>
#include <folly/Indestructible.h>

#if FOLLY_HAS_RESULT

namespace folly {
namespace detail {

const error_or_stopped& dfatal_get_bad_result_access_error() {
  static const folly::Indestructible<error_or_stopped> r{
      bad_result_access_error{}};
  LOG(DFATAL)
      << "Used `error_or_stopped()` accessor for `folly::result` in value state";
  return *r;
}

void fatal_if_eptr_empty_or_stopped(const std::exception_ptr& eptr) {
  if (!eptr) {
    LOG(FATAL) << "`result` may not contain an empty `std::exception_ptr`";
  }
  if (folly::get_exception<folly::OperationCancelled>(eptr)) {
    LOG(FATAL)
        << "Do not store `OperationCancelled` in `result`. If you got this "
        << "error while extracting an `exception_wrapper`, `exception_ptr`, "
        << "or similar, you must check `has_stopped()` before doing that!";
  }
}

} // namespace detail

std::ostream& operator<<(std::ostream& os, const error_or_stopped& eos) {
  return detail::ostream_write_via_fmt(os, eos);
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
