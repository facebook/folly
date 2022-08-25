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

#include <limits>

#include <folly/io/async/AtomicNotificationQueue.h>

namespace folly {
namespace detail {

template <template <typename> class Atom>
MeteredExecutorImpl<Atom>::MeteredExecutorImpl(
    KeepAlive keepAlive, Options options)
    : options_(std::move(options)), kaInner_(std::move(keepAlive)) {
  queue_.setMaxReadAtOnce(1);
  queue_.arm();
}

template <template <typename> class Atom>
MeteredExecutorImpl<Atom>::MeteredExecutorImpl(
    std::unique_ptr<Executor> executor, Options options)
    : MeteredExecutorImpl(getKeepAliveToken(*executor), std::move(options)) {
  ownedExecutor_ = std::move(executor);
}

template <template <typename> class Atom>
std::unique_ptr<QueueObserver> MeteredExecutorImpl<Atom>::setupQueueObserver() {
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

template <template <typename> class Atom>
void MeteredExecutorImpl<Atom>::add(Func func) {
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

template <template <typename> class Atom>
bool MeteredExecutorImpl<Atom>::pause() {
  auto state = state_.execState.load();
  do {
    DCHECK(state == PAUSED || state == PAUSED_PENDING || state == RUNNING);
    if (state != RUNNING) {
      return false;
    }
  } while (!state_.execState.compare_exchange_weak(state, PAUSED));
  return true;
}

template <template <typename> class Atom>
bool MeteredExecutorImpl<Atom>::resume() {
  auto state = state_.execState.load();
  do {
    DCHECK(state == PAUSED || state == PAUSED_PENDING || state == RUNNING);
    if (state == RUNNING) {
      return false;
    }
  } while (!state_.execState.compare_exchange_weak(state, RUNNING));
  if (state == PAUSED_PENDING) {
    scheduleCallback();
  }
  return true;
}

template <template <typename> class Atom>
bool MeteredExecutorImpl<Atom>::shouldSkip() {
  uint8_t state = state_.execState.load();
  // we should never have shouldSkip invoked twice once marked as callback
  // pending
  DCHECK(state == RUNNING || state == PAUSED);
  if (UNLIKELY(state != RUNNING)) {
    if (state_.execState.compare_exchange_strong(state, PAUSED_PENDING)) {
      return true;
    }
  }
  return state != RUNNING;
}

template <template <typename> class Atom>
void MeteredExecutorImpl<Atom>::loopCallback() {
  DCHECK(state_.callbackScheduled.exchange(false));
  if (shouldSkip()) {
    return;
  }
  Consumer consumer(*this);
  if (queue_.drive(consumer) || !queue_.arm()) {
    scheduleCallback();
  }
  consumer.executeIfNotEmpty();
}

template <template <typename> class Atom>
void MeteredExecutorImpl<Atom>::scheduleCallback() {
  DCHECK(!state_.callbackScheduled.exchange(true));
  folly::RequestContextScopeGuard g{std::shared_ptr<RequestContext>()};
  kaInner_->add([self = getKeepAliveToken(this)] { self->loopCallback(); });
}

template <template <typename> class Atom>
MeteredExecutorImpl<Atom>::~MeteredExecutorImpl() {
  joinKeepAlive();
}

template <template <typename> class Atom>
MeteredExecutorImpl<Atom>::Consumer::~Consumer() {
  DCHECK(!first_ || !first_->hasFunc());
}

template <template <typename> class Atom>
void MeteredExecutorImpl<Atom>::Consumer::executeIfNotEmpty() {
  if (first_) {
    RequestContextScopeGuard guard(std::move(firstRctx_));
    auto first = std::move(first_);
    first->run();
  }
}

template <template <typename> class Atom>
void MeteredExecutorImpl<Atom>::Consumer::operator()(
    Task&& task, std::shared_ptr<RequestContext>&& rctx) {
  if (self_.queueObserver_) {
    self_.queueObserver_->onDequeued(task.getQueueObserverPayload());
  }
  DCHECK(!first_);
  first_ = std::make_optional<Task>(std::move(task));
  firstRctx_ = std::move(rctx);
}

} // namespace detail
} // namespace folly
