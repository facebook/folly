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

#include <folly/result/epitaph.h>

#include <folly/Portability.h> // FOLLY_HAS_RESULT

#if FOLLY_HAS_RESULT

namespace folly::detail {

folly::source_location epitaph_non_value::source_location() const noexcept {
  return msg_.location();
}

const char* epitaph_non_value::partial_message() const noexcept {
  return msg_.message();
}

const rich_exception_ptr* epitaph_non_value::next_error_for_epitaph()
    const noexcept {
  return &next_;
}

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
