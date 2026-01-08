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

#include <folly/result/rich_error_base.h>

#if FOLLY_HAS_RESULT

namespace folly {

source_location rich_error_base::source_location() const noexcept {
  // The content of the default-constructed class is unspecified by the
  // standard, but ...  libstdc++, libc++, and MSVC all default to an empty
  // name.  So, rather than make this `std::optional`, our `format_to` omits
  // the location if `sl.file_name() != source_location{}.file_name()`.
  return {};
}

const rich_exception_ptr* rich_error_base::next_error_for_enriched_message()
    const noexcept {
  return nullptr;
}

} // namespace folly

#endif // FOLLY_HAS_RESULT
