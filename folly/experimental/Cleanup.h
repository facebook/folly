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

#include <memory>
#include <mutex>

#include <glog/logging.h>

#include <folly/futures/Future.h>

namespace folly {

// Structured Async Cleanup
//

// Structured Async Cleanup - traits
//

namespace detail {
struct cleanup_fn {
  template <
      class T,
      class R = decltype(std::declval<T>().cleanup()),
      std::enable_if_t<std::is_same_v<R, folly::SemiFuture<folly::Unit>>, int> =
          0>
  R operator()(T&& t) const {
    return ((T &&) t).cleanup();
  }
};
} // namespace detail

template <class T>
constexpr bool is_cleanup_v = folly::is_invocable_v<detail::cleanup_fn, T>;

template <typename T>
using is_cleanup = std::bool_constant<is_cleanup_v<T>>;

// Structured Async Cleanup
//
// This helps compose a task with async cleanup
// The task result is stored until cleanup completes and is then produced
// The cleanup task is not allowed to fail.
//
// This can be used with collectAll to combine multiple async resources
//
// ensureCleanupAfterTask(collectAll(a.run(), b.run()), collectAll(a.cleanup(),
// b.cleanup())).wait();
//
template <typename T>
folly::SemiFuture<T> ensureCleanupAfterTask(
    folly::SemiFuture<T> task, folly::SemiFuture<folly::Unit> cleanup) {
  return folly::makeSemiFuture()
      .deferValue([task_ = std::move(task)](folly::Unit) mutable {
        return std::move(task_);
      })
      .defer([cleanup_ = std::move(cleanup)](folly::Try<T> taskResult) mutable {
        return std::move(cleanup_).defer(
            [taskResult_ = std::move(taskResult)](folly::Try<folly::Unit> t) {
              if (t.hasException()) {
                terminate_with<std::logic_error>("cleanup must not throw");
              }
              return std::move(taskResult_).value();
            });
      });
}

// Structured Async Cleanup
//
// This implementation is a base class that collects a set of cleanup tasks
// and runs them in reverse order.
//
// A class derived from Cleanup
//  - only allows cleanup to be run once
//  - is required to complete cleanup before running the destructor
//  - *should not* run cleanup tasks. Running the cleanup task should be
//    delegated to the owner of the derived class
//  - *should not* be owned by a shared_ptr. Cleanup is intended to remove
//    shared ownership.
//
class Cleanup {
 public:
  Cleanup() : safe_to_destruct_(false), cleanup_(folly::makeSemiFuture()) {}
  ~Cleanup() {
    if (!safe_to_destruct_) {
      LOG(FATAL) << "Cleanup must complete before it is destructed.";
    }
  }

  // Returns: a SemiFuture that, just like destructors, sequences the cleanup
  // tasks added in reverse of the order they were added.
  //
  // calls to cleanup() do not mutate state. The returned SemiFuture, once
  // it has been given an executor, does mutate state and must not overlap with
  // any calls to addCleanup().
  //
  folly::SemiFuture<folly::Unit> cleanup() {
    return folly::makeSemiFuture()
        .deferValue([this](folly::Unit) {
          if (!cleanup_.valid()) {
            LOG(FATAL) << "cleanup already run - cleanup task invalid.";
          }
          return std::move(cleanup_);
        })
        .defer([this](folly::Try<folly::Unit> t) {
          if (t.hasException()) {
            LOG(FATAL) << "Cleanup actions must be noexcept.";
          }
          this->safe_to_destruct_ = true;
        });
  }

 protected:
  // includes the provided SemiFuture under the scope of this.
  //
  // when the cleanup() for this started it will get this SemiFuture first.
  //
  // order matters, just like destructors, cleanup tasks will be run in reverse
  // of the order they were added.
  //
  // all gets will use the Executor provided to the SemiFuture returned by
  // cleanup()
  //
  // calls to addCleanup() must not overlap with each other and must not overlap
  // with a running SemiFuture returned from addCleanup().
  //
  void addCleanup(folly::SemiFuture<folly::Unit> c) {
    if (!cleanup_.valid()) {
      LOG(FATAL)
          << "Cleanup::addCleanup must not be called after Cleanup::cleanup.";
    }
    cleanup_ = std::move(c).deferValue(
        [nested = std::move(cleanup_)](folly::Unit) mutable {
          return std::move(nested);
        });
  }

  // includes the provided model of Cleanup under the scope of this
  //
  // when the cleanup() for this started it will cleanup this first.
  //
  // order matters, just like destructors, cleanup tasks will be run in reverse
  // of the order they were added.
  //
  // all gets will use the Executor provided to the SemiFuture returned by
  // cleanup()
  //
  // calls to addCleanup() must not overlap with each other and must not overlap
  // with a running SemiFuture returned from addCleanup().
  //
  template <
      class OtherCleanup,
      std::enable_if_t<is_cleanup_v<OtherCleanup>, int> = 0>
  void addCleanup(OtherCleanup&& c) {
    addCleanup(((OtherCleanup &&) c).cleanup());
  }

 private:
  bool safe_to_destruct_;
  folly::SemiFuture<folly::Unit> cleanup_;
};

} // namespace folly
