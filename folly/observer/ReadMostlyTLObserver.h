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

#pragma once

#include <folly/concurrency/memory/ReadMostlySharedPtr.h>
#include <folly/experimental/observer/detail/ObserverManager.h>
#include <folly/observer/Observer.h>

namespace folly {
namespace observer {

/**
 * A TLObserver that optimizes for getting a shared_ptr-like pointer to data.
 */

template <typename T>
class ReadMostlyTLObserver {
 public:
  explicit ReadMostlyTLObserver(Observer<T> observer)
      : observer_(std::move(observer)) {
    refresh();
  }

  ReadMostlyTLObserver(const ReadMostlyTLObserver<T>& other)
      : ReadMostlyTLObserver(other.observer_) {}

  ReadMostlySharedPtr<const T> getShared() const {
    if (!observer_.needRefresh(localSnapshot_->version_) &&
        !observer_detail::ObserverManager::inManagerThread()) {
      if (auto data = localSnapshot_->data_.lock()) {
        return data;
      }
    }
    return refresh();
  }

  Observer<T> getUnderlyingObserver() const { return observer_; }

 private:
  ReadMostlySharedPtr<const T> refresh() const {
    auto snapshot = observer_.getSnapshot();
    auto globalData = globalData_.lock();
    if (globalVersion_.load() < snapshot.getVersion()) {
      globalData->reset(snapshot.getShared());
      globalVersion_ = snapshot.getVersion();
    }
    *localSnapshot_ = LocalSnapshot(*globalData, globalVersion_.load());
    return globalData->getShared();
  }

  struct LocalSnapshot {
    LocalSnapshot() {}
    LocalSnapshot(const ReadMostlyMainPtr<const T>& data, size_t version)
        : data_(data), version_(version) {}

    ReadMostlyWeakPtr<const T> data_;
    size_t version_;
  };

  Observer<T> observer_;

  mutable Synchronized<ReadMostlyMainPtr<const T>, std::mutex> globalData_;
  mutable std::atomic<size_t> globalVersion_{0};

  ThreadLocal<LocalSnapshot> localSnapshot_;
};

/**
 * Same as makeObserver(...), but creates ReadMostlyTLObserver.
 */
template <typename T>
ReadMostlyTLObserver<T> makeReadMostlyTLObserver(Observer<T> observer) {
  return ReadMostlyTLObserver<T>(std::move(observer));
}

template <typename F>
auto makeReadMostlyTLObserver(F&& creator) {
  return makeReadMostlyTLObserver(makeObserver(std::forward<F>(creator)));
}

} // namespace observer
} // namespace folly
