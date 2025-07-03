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

#include <folly/rust/request_context/request_context.h>
#include <folly/rust/request_context/request_context.rs.h>

namespace facebook {
namespace rust {

std::shared_ptr<folly::RequestContext> with_folly_request_context(
    std::shared_ptr<folly::RequestContext> const& ctxt,
    ::rust::Fn<void(WithInner&)> func,
    WithInner& innerarg) {
  folly::RequestContextScopeGuard guard(ctxt);
  func(innerarg);

  // Return possibly updated context
  return folly::RequestContext::saveContext();
}

} // namespace rust
} // namespace facebook
