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

#include <folly/python/fibers.h>

#include <stdexcept>

#include <folly/python/fiber_manager_api.h>
#include <folly/python/import.h>

namespace folly {
namespace python {

FOLLY_CONSTINIT static import_cache import_folly__fiber_manager_{
    import_folly__fiber_manager, "import_folly__fiber_manager"};

folly::fibers::FiberManager* getFiberManager(
    const folly::fibers::FiberManager::Options& opts) {
  DCHECK(!folly::fibers::onFiber());
  import_folly__fiber_manager_();
  return get_fiber_manager(opts);
}

} // namespace python
} // namespace folly
