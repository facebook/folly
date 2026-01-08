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

#include <folly/result/detail/rich_exception_ptr_storage.h>

#include <folly/Portability.h> // FOLLY_HAS_RESULT

#include <glog/logging.h>

#if FOLLY_HAS_RESULT

namespace folly::detail {

#ifndef NDEBUG
void rich_exception_ptr_base_storage::debug_assert(const char* msg, bool cond) {
  DCHECK(cond) << msg;
}
#endif

// Analog of `exception_wrapper::onNoExceptionError`
[[noreturn]] void
rich_exception_ptr_base_storage::terminate_on_empty_or_invalid_eptr(bits_t b) {
  LOG(FATAL) << "Cannot use `throw_exception()` with an empty or invalid "
             << "`folly::rich_exception_ptr` (bits " << b << ")." << std::endl;
}

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
