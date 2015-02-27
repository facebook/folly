/*
 * Copyright 2015 Facebook, Inc.
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

#include <atomic>
#include <functional>

namespace folly {

typedef std::function<void()> Func;

/// An Executor accepts units of work with add(), which should be
/// threadsafe.
class Executor {
 public:
  virtual ~Executor() = default;

  /// Enqueue a function to executed by this executor. This and all
  /// variants must be threadsafe.
  virtual void add(Func) = 0;

  /// A convenience function for shared_ptr to legacy functors.
  ///
  /// Sometimes you have a functor that is move-only, and therefore can't be
  /// converted to a std::function (e.g. std::packaged_task). In that case,
  /// wrap it in a shared_ptr (or maybe folly::MoveWrapper) and use this.
  template <class P>
  void addPtr(P fn) {
    this->add([fn]() mutable { (*fn)(); });
  }
};

} // folly
