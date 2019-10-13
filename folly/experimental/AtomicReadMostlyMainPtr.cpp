/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/experimental/AtomicReadMostlyMainPtr.h>

#include <folly/executors/InlineExecutor.h>

namespace folly {
namespace detail {

namespace {
struct FailingExecutor : folly::Executor {
  // We shouldn't be invoking any callbacks.
  void add(Func func) override {
    LOG(DFATAL)
        << "Added an RCU callback to the AtomicReadMostlyMainPtr executor.";
    InlineExecutor::instance().add(std::move(func));
  }
};
} // namespace

// *All* modifications of *all* AtomicReadMostlyMainPtrs use the same mutex and
// domain. The first of these just shrinks the size of the individual objects a
// little, but the second is necessary for correctness; all rcu_domains need
// their own tag (as an rcu_domain API constraint), but we want to support
// arbitrarily many AtomicReadMostlyMainPtrs.
Indestructible<std::mutex> atomicReadMostlyMu;
Indestructible<folly::rcu_domain<detail::AtomicReadMostlyTag>>
    atomicReadMostlyDomain(new FailingExecutor);

} // namespace detail
} // namespace folly
