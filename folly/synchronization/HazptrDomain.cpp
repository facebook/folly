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

#include <folly/executors/QueuedImmediateExecutor.h>
#include <folly/synchronization/HazptrDomain.h>

namespace folly::detail {

folly::Executor::KeepAlive<> hazptr_get_default_executor() {
  return &folly::QueuedImmediateExecutor::instance();
}

} // namespace folly::detail
