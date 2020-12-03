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

#pragma once

#include <folly/experimental/observer/detail/ObserverManager.h>

namespace folly {
namespace observer_detail {
template <typename F>
observer::Observer<ResultOfNoObserverUnwrap<F>> makeObserver(F&& creator) {
  return observer::makeObserver([creator = std::forward<F>(creator)]() mutable {
    return std::make_shared<ResultOfNoObserverUnwrap<F>>(creator());
  });
}

template <typename F>
observer::Observer<ResultOfNoObserverUnwrap<F>> makeValueObserver(F&& creator) {
  return observer::makeValueObserver(
      [creator = std::forward<F>(creator)]() mutable {
        return std::make_shared<ResultOfNoObserverUnwrap<F>>(creator());
      });
}
} // namespace observer_detail

namespace observer {

template <typename T>
Snapshot<T> Observer<T>::getSnapshot() const {
  auto data = core_->getData();
  return Snapshot<T>(
      *core_,
      std::static_pointer_cast<const T>(std::move(data.data)),
      data.version);
}

template <typename T>
Observer<T>::Observer(observer_detail::Core::Ptr core)
    : core_(std::move(core)) {}

template <typename T>
Observer<T> unwrap(Observer<T> o) {
  return o;
}

template <typename T>
Observer<T> unwrapValue(Observer<T> o) {
  return makeValueObserver(std::move(o));
}

template <typename T>
Observer<T> unwrap(Observer<Observer<T>> oo) {
  return makeObserver([oo = std::move(oo)] {
    return oo.getSnapshot()->getSnapshot().getShared();
  });
}

template <typename T>
Observer<T> unwrapValue(Observer<Observer<T>> oo) {
  return makeValueObserver([oo = std::move(oo)] {
    return oo.getSnapshot()->getSnapshot().getShared();
  });
}

template <typename F>
Observer<observer_detail::ResultOfUnwrapSharedPtr<F>> makeObserver(
    F&& creator) {
  auto core = observer_detail::Core::create(
      [creator = std::forward<F>(creator)]() mutable {
        return std::static_pointer_cast<const void>(creator());
      });

  observer_detail::ObserverManager::initCore(core);

  return Observer<observer_detail::ResultOfUnwrapSharedPtr<F>>(core);
}

template <typename F>
Observer<observer_detail::ResultOf<F>> makeObserver(F&& creator) {
  return observer_detail::makeObserver(std::forward<F>(creator));
}

template <typename F>
Observer<observer_detail::ResultOfUnwrapObserver<F>> makeObserver(F&& creator) {
  return unwrap(observer_detail::makeObserver(std::forward<F>(creator)));
}

template <typename T>
Observer<T> makeStaticObserver(T value) {
  return makeStaticObserver<T>(std::make_shared<T>(std::move(value)));
}

template <typename T>
Observer<T> makeStaticObserver(std::shared_ptr<T> value) {
  return makeObserver([value = std::move(value)] { return value; });
}

template <typename T>
TLObserver<T>::TLObserver(Observer<T> observer)
    : observer_(observer),
      snapshot_([&] { return new Snapshot<T>(observer_.getSnapshot()); }) {}

template <typename T>
TLObserver<T>::TLObserver(const TLObserver<T>& other)
    : TLObserver(other.observer_) {}

template <typename T>
const Snapshot<T>& TLObserver<T>::getSnapshotRef() const {
  auto& snapshot = *snapshot_;
  if (observer_.needRefresh(snapshot) ||
      observer_detail::ObserverManager::inManagerThread()) {
    snapshot = observer_.getSnapshot();
  }

  return snapshot;
}

template <typename T>
ReadMostlyTLObserver<T>::ReadMostlyTLObserver(Observer<T> observer)
    : observer_(observer),
      callback_(
          observer_.addCallback([this](folly::observer::Snapshot<T> snapshot) {
            globalData_.lock()->reset(snapshot.getShared());
            globalVersion_ = snapshot.getVersion();
          })) {}

template <typename T>
ReadMostlyTLObserver<T>::ReadMostlyTLObserver(
    const ReadMostlyTLObserver<T>& other)
    : ReadMostlyTLObserver(other.observer_) {}

template <typename T>
folly::ReadMostlySharedPtr<const T> ReadMostlyTLObserver<T>::getShared() const {
  if (localSnapshot_->version_ == globalVersion_.load()) {
    if (auto data = localSnapshot_->data_.lock()) {
      return data;
    }
  }
  return refresh();
}

template <typename T>
folly::ReadMostlySharedPtr<const T> ReadMostlyTLObserver<T>::refresh() const {
  auto version = globalVersion_.load();
  auto globalData = globalData_.lock();
  *localSnapshot_ = LocalSnapshot(*globalData, version);
  return globalData->getShared();
}

struct CallbackHandle::Context {
  Optional<Observer<folly::Unit>> observer;
  Synchronized<bool> canceled{false};
};

inline CallbackHandle::CallbackHandle() {}

template <typename T>
CallbackHandle::CallbackHandle(
    Observer<T> observer,
    folly::Function<void(Snapshot<T>)> callback) {
  context_ = std::make_shared<Context>();
  context_->observer = makeObserver([observer = std::move(observer),
                                     callback = std::move(callback),
                                     context = context_]() mutable {
    auto rCanceled = context->canceled.rlock();
    if (*rCanceled) {
      return folly::unit;
    }
    callback(*observer);
    return folly::unit;
  });
}

inline CallbackHandle& CallbackHandle::operator=(
    CallbackHandle&& handle) noexcept {
  cancel();
  context_ = std::move(handle.context_);
  return *this;
}

inline CallbackHandle::~CallbackHandle() {
  cancel();
}

inline void CallbackHandle::cancel() {
  if (!context_) {
    return;
  }
  context_->observer.reset();
  context_->canceled = true;
  context_.reset();
}

template <typename T>
CallbackHandle Observer<T>::addCallback(
    folly::Function<void(Snapshot<T>)> callback) const {
  return CallbackHandle(*this, std::move(callback));
}

template <typename T>
Observer<T> makeValueObserver(Observer<T> observer) {
  return makeValueObserver(
      [observer] { return observer.getSnapshot().getShared(); });
}

template <typename F>
Observer<observer_detail::ResultOf<F>> makeValueObserver(F&& creator) {
  return observer_detail::makeValueObserver(std::forward<F>(creator));
}

template <typename F>
Observer<observer_detail::ResultOfUnwrapObserver<F>> makeValueObserver(
    F&& creator) {
  return unwrapValue(observer_detail::makeObserver(std::forward<F>(creator)));
}

template <typename F>
Observer<observer_detail::ResultOfUnwrapSharedPtr<F>> makeValueObserver(
    F&& creator) {
  auto activeValue = creator();
  return makeObserver([activeValue = std::move(activeValue),
                       creator = std::forward<F>(creator)]() mutable {
    auto newValue = creator();
    if (!(*activeValue == *newValue)) {
      activeValue = newValue;
    }
    return activeValue;
  });
}
} // namespace observer
} // namespace folly
