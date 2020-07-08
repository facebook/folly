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

#include <folly/futures/detail/Core.h>

namespace folly {
namespace futures {
namespace detail {

void UniqueDeleter::operator()(DeferredExecutor* ptr) {
  if (ptr) {
    ptr->release();
  }
}

KeepAliveOrDeferred::KeepAliveOrDeferred() = default;

KeepAliveOrDeferred::KeepAliveOrDeferred(Executor::KeepAlive<> ka)
    : storage_{std::move(ka)} {
  DCHECK(!isDeferred());
}

KeepAliveOrDeferred::KeepAliveOrDeferred(DeferredWrapper deferred)
    : storage_{std::move(deferred)} {}

KeepAliveOrDeferred::KeepAliveOrDeferred(KeepAliveOrDeferred&& other) noexcept
    : storage_{std::move(other.storage_)} {}

KeepAliveOrDeferred::~KeepAliveOrDeferred() = default;

KeepAliveOrDeferred& KeepAliveOrDeferred::operator=(
    KeepAliveOrDeferred&& other) {
  storage_ = std::move(other.storage_);
  return *this;
}

DeferredExecutor* KeepAliveOrDeferred::getDeferredExecutor() const {
  if (!isDeferred()) {
    return nullptr;
  }
  return asDeferred().get();
}

Executor* KeepAliveOrDeferred::getKeepAliveExecutor() const {
  if (isDeferred()) {
    return nullptr;
  }
  return asKeepAlive().get();
}

Executor::KeepAlive<> KeepAliveOrDeferred::stealKeepAlive() && {
  if (isDeferred()) {
    return Executor::KeepAlive<>{};
  }
  return std::move(asKeepAlive());
}

std::unique_ptr<DeferredExecutor, UniqueDeleter>
KeepAliveOrDeferred::stealDeferred() && {
  if (!isDeferred()) {
    return std::unique_ptr<DeferredExecutor, UniqueDeleter>{};
  }
  return std::move(asDeferred());
}

bool KeepAliveOrDeferred::isDeferred() const {
  return boost::get<DeferredWrapper>(&storage_) != nullptr;
}

bool KeepAliveOrDeferred::isKeepAlive() const {
  return !isDeferred();
}

KeepAliveOrDeferred KeepAliveOrDeferred::copy() const {
  if (isDeferred()) {
    if (auto def = getDeferredExecutor()) {
      return KeepAliveOrDeferred{def->copy()};
    } else {
      return KeepAliveOrDeferred{};
    }
  } else {
    return KeepAliveOrDeferred{asKeepAlive()};
  }
}

/* explicit */ KeepAliveOrDeferred::operator bool() const {
  return getDeferredExecutor() || getKeepAliveExecutor();
}

Executor::KeepAlive<>& KeepAliveOrDeferred::asKeepAlive() {
  return boost::get<Executor::KeepAlive<>>(storage_);
}

const Executor::KeepAlive<>& KeepAliveOrDeferred::asKeepAlive() const {
  return boost::get<Executor::KeepAlive<>>(storage_);
}

DeferredWrapper& KeepAliveOrDeferred::asDeferred() {
  return boost::get<DeferredWrapper>(storage_);
}

const DeferredWrapper& KeepAliveOrDeferred::asDeferred() const {
  return boost::get<DeferredWrapper>(storage_);
}

void DeferredExecutor::addFrom(
    Executor::KeepAlive<>&& completingKA,
    Executor::KeepAlive<>::KeepAliveFunc func) {
  auto state = state_.load(std::memory_order_acquire);
  if (state == State::DETACHED) {
    return;
  }

  // If we are completing on the current executor, call inline, otherwise
  // add
  auto addWithInline =
      [&](Executor::KeepAlive<>::KeepAliveFunc&& addFunc) mutable {
        if (completingKA.get() == executor_.get()) {
          addFunc(std::move(completingKA));
        } else {
          executor_.copy().add(std::move(addFunc));
        }
      };

  if (state == State::HAS_EXECUTOR) {
    addWithInline(std::move(func));
    return;
  }
  DCHECK(state == State::EMPTY);
  func_ = std::move(func);
  if (folly::atomic_compare_exchange_strong_explicit(
          &state_,
          &state,
          State::HAS_FUNCTION,
          std::memory_order_release,
          std::memory_order_acquire)) {
    return;
  }
  DCHECK(state == State::DETACHED || state == State::HAS_EXECUTOR);
  if (state == State::DETACHED) {
    std::exchange(func_, nullptr);
    return;
  }
  addWithInline(std::exchange(func_, nullptr));
}

Executor* DeferredExecutor::getExecutor() const {
  assert(executor_.get());
  return executor_.get();
}

void DeferredExecutor::setExecutor(folly::Executor::KeepAlive<> executor) {
  if (nestedExecutors_) {
    auto nestedExecutors = std::exchange(nestedExecutors_, nullptr);
    for (auto& nestedExecutor : *nestedExecutors) {
      assert(nestedExecutor.get());
      nestedExecutor.get()->setExecutor(executor.copy());
    }
  }
  executor_ = std::move(executor);
  auto state = state_.load(std::memory_order_acquire);
  if (state == State::EMPTY &&
      folly::atomic_compare_exchange_strong_explicit(
          &state_,
          &state,
          State::HAS_EXECUTOR,
          std::memory_order_release,
          std::memory_order_acquire)) {
    return;
  }

  DCHECK(state == State::HAS_FUNCTION);
  state_.store(State::HAS_EXECUTOR, std::memory_order_release);
  executor_.copy().add(std::exchange(func_, nullptr));
}

void DeferredExecutor::setNestedExecutors(
    std::vector<DeferredWrapper> executors) {
  DCHECK(!nestedExecutors_);
  nestedExecutors_ =
      std::make_unique<std::vector<DeferredWrapper>>(std::move(executors));
}

void DeferredExecutor::detach() {
  if (nestedExecutors_) {
    auto nestedExecutors = std::exchange(nestedExecutors_, nullptr);
    for (auto& nestedExecutor : *nestedExecutors) {
      assert(nestedExecutor.get());
      nestedExecutor.get()->detach();
    }
  }
  auto state = state_.load(std::memory_order_acquire);
  if (state == State::EMPTY &&
      folly::atomic_compare_exchange_strong_explicit(
          &state_,
          &state,
          State::DETACHED,
          std::memory_order_release,
          std::memory_order_acquire)) {
    return;
  }

  DCHECK(state == State::HAS_FUNCTION);
  state_.store(State::DETACHED, std::memory_order_release);
  std::exchange(func_, nullptr);
}

DeferredWrapper DeferredExecutor::copy() {
  acquire();
  return DeferredWrapper(this);
}

/* static */ DeferredWrapper DeferredExecutor::create() {
  return DeferredWrapper(new DeferredExecutor{});
}

DeferredExecutor::DeferredExecutor() {}

bool DeferredExecutor::acquire() {
  auto keepAliveCount = keepAliveCount_.fetch_add(1, std::memory_order_relaxed);
  DCHECK(keepAliveCount > 0);
  return true;
}

void DeferredExecutor::release() {
  auto keepAliveCount = keepAliveCount_.fetch_sub(1, std::memory_order_acq_rel);
  DCHECK(keepAliveCount > 0);
  if (keepAliveCount == 1) {
    delete this;
  }
}

#if FOLLY_USE_EXTERN_FUTURE_UNIT
template class Core<folly::Unit>;
#endif

} // namespace detail
} // namespace futures
} // namespace folly
