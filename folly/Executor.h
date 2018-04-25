/*
 * Copyright 2014-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <climits>

#include <folly/Function.h>

namespace folly {

using Func = Function<void()>;

/// An Executor accepts units of work with add(), which should be
/// threadsafe.
class Executor {
 public:
  // Workaround for a linkage problem with explicitly defaulted dtor t22914621
  virtual ~Executor() {}

  /// Enqueue a function to executed by this executor. This and all
  /// variants must be threadsafe.
  virtual void add(Func) = 0;

  /// Enqueue a function with a given priority, where 0 is the medium priority
  /// This is up to the implementation to enforce
  virtual void addWithPriority(Func, int8_t priority);

  virtual uint8_t getNumPriorities() const {
    return 1;
  }

  static const int8_t LO_PRI  = SCHAR_MIN;
  static const int8_t MID_PRI = 0;
  static const int8_t HI_PRI  = SCHAR_MAX;

  class KeepAlive {
   public:
    KeepAlive() : executor_(nullptr, Deleter(true)) {}

    void reset() {
      executor_.reset();
    }

    explicit operator bool() const {
      return executor_ != nullptr;
    }

    Executor* get() const {
      return executor_.get();
    }

   private:
    friend class Executor;
    KeepAlive(folly::Executor* executor, bool dummy)
        : executor_(executor, Deleter(dummy)) {}

    struct Deleter {
      explicit Deleter(bool dummy) : dummy_(dummy) {}
      void operator()(folly::Executor* executor) {
        if (dummy_) {
          return;
        }
        executor->keepAliveRelease();
      }

     private:
      bool dummy_;
    };
    std::unique_ptr<folly::Executor, Deleter> executor_;
  };

  /// Returns a keep-alive token which guarantees that Executor will keep
  /// processing tasks until the token is released (if supported by Executor).
  /// KeepAlive always contains a valid pointer to an Executor.
  KeepAlive getKeepAliveToken() {
    if (keepAliveAcquire()) {
      return makeKeepAlive();
    }
    return KeepAlive{this, true};
  }

 protected:
  // Acquire a keep alive token. Should return false if keep-alive mechanism
  // is not supported.
  virtual bool keepAliveAcquire();
  // Release a keep alive token previously acquired by keepAliveAcquire().
  // Will never be called if keepAliveAcquire() returns false.
  virtual void keepAliveRelease();

  KeepAlive makeKeepAlive() {
    return KeepAlive{this, false};
  }
};

} // namespace folly
