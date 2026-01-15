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

#include <folly/result/errc_rich_error.h>

#if FOLLY_HAS_RESULT

namespace fmt {

fmt::appender formatter<folly::detail::errc_format_as>::format(
    folly::detail::errc_format_as ec, format_context& ctx) const {
  return fmt::format_to(
      ctx.out(),
      "{} ({})",
      static_cast<int>(ec.code),
      std::make_error_code(ec.code).message());
}

} // namespace fmt

#endif // FOLLY_HAS_RESULT
