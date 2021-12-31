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

#include <folly/executors/MeteredExecutor.h>

#include <limits>

#include <folly/io/async/AtomicNotificationQueue.h>

namespace folly {

MeteredExecutor::MeteredExecutor(KeepAlive keepAlive, Options options)
    : options_(std::move(options)), kaInner_(std::move(keepAlive)) {
  queue_.setMaxReadAtOnce(1);
  queue_.arm();
}

MeteredExecutor::MeteredExecutor(
    std::unique_ptr<Executor> executor, Options options)
    : MeteredExecutor(getKeepAliveToken(*executor), std::move(options)) {
  ownedExecutor_ = std::move(executor);
}

std::unique_ptr<QueueObserver> MeteredExecutor::setupQueueObserver() {
  if (options_.enableQueueObserver) {
    std::string name = "unk";
    if (options_.name != "") {
      name = options_.name;
    }
    if (auto factory = folly::QueueObserverFactory::make(
            "mex." + name, options_.numPriorities)) {
      return factory->create(options_.priority);
    }
  }
  return nullptr;
}

void MeteredExecutor::add(Func func) {
  auto task = Task(std::move(func));
  auto rctxp = RequestContext::saveContext();
  if (queueObserver_) {
    auto payload = queueObserver_->onEnqueued(rctxp.get());
    task.setQueueObserverPayload(payload);
  }
  if (queue_.push(std::move(rctxp), std::move(task))) {
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
  DCHECK(!first_ || !first_->hasFunc());
}

void MeteredExecutor::Consumer::executeIfNotEmpty() {
  if (first_) {
    RequestContextScopeGuard guard(std::move(firstRctx_));
    auto first = std::move(first_);
    first->run();
  }
}

void MeteredExecutor::Consumer::operator()(
    Task&& task, std::shared_ptr<RequestContext>&& rctx) {
  if (self_.queueObserver_) {
    self_.queueObserver_->onDequeued(task.getQueueObserverPayload());
  }
  DCHECK(!first_);
  first_ = std::make_optional<Task>(std::move(task));
  firstRctx_ = std::move(rctx);
}

} // namespace folly
