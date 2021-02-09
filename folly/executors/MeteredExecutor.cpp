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

#include <folly/executors/MeteredExecutor.h>

#include <limits>

#include <folly/io/async/AtomicNotificationQueue.h>

namespace folly {

MeteredExecutor::MeteredExecutor(KeepAlive keepAlive)
    : kaInner_(std::move(keepAlive)) {
  queue_.setMaxReadAtOnce(1);
  queue_.arm();
}

MeteredExecutor::MeteredExecutor(std::unique_ptr<Executor> executor)
    : MeteredExecutor(getKeepAliveToken(*executor)) {
  ownedExecutor_ = std::move(executor);
}

void MeteredExecutor::setMaxReadAtOnce(uint32_t maxAtOnce) {
  queue_.setMaxReadAtOnce(maxAtOnce);
}

void MeteredExecutor::add(Func func) {
  if (queue_.push(std::move(func))) {
    scheduleCallback();
  }
}

void MeteredExecutor::loopCallback() {
  Consumer consumer(*this);
  if (queue_.drive(consumer) || !queue_.arm()) {
    scheduleCallback();
  }
  consumer.executeIfNotEmpty();
}

void MeteredExecutor::scheduleCallback() {
  folly::RequestContextScopeGuard g{std::shared_ptr<RequestContext>()};
  kaInner_->add([self = getKeepAliveToken(this)] { self->loopCallback(); });
}

MeteredExecutor::~MeteredExecutor() {
  joinKeepAlive();
}

MeteredExecutor::Consumer::~Consumer() {
  DCHECK(!first_);
}

void MeteredExecutor::Consumer::executeIfNotEmpty() {
  if (first_) {
    RequestContextScopeGuard guard(std::move(firstRctx_));
    auto first = std::move(first_);
    first();
  }
}

void MeteredExecutor::Consumer::operator()(
    Func&& func, std::shared_ptr<RequestContext>&& rctx) {
  if (!first_) {
    first_ = std::move(func);
    firstRctx_ = std::move(rctx);
  } else {
    self_.kaInner_->add(
        [func = std::move(func), rctx = std::move(rctx)]() mutable {
          RequestContextScopeGuard guard(std::move(rctx));
          func();
        });
  }
}

} // namespace folly
