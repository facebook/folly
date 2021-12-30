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

#include <folly/CppAttributes.h>
#include <folly/python/error.h>
#include <folly/python/fiber_manager_api.h>

namespace folly {
namespace python {
namespace {

void do_import() {
  if (0 != import_folly__fiber_manager()) {
    handlePythonError("import_folly__fiber_manager failed: ");
  }
}

} // namespace

folly::fibers::FiberManager* getFiberManager(
    const folly::fibers::FiberManager::Options& opts) {
  DCHECK(!folly::fibers::onFiber());
  FOLLY_MAYBE_UNUSED static bool done = (do_import(), false);
  return get_fiber_manager(opts);
}

} // namespace python
} // namespace folly
