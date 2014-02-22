/*
 * Copyright 2014 Facebook, Inc.
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
#include "ThreadGate.h"
#include "Executor.h"
#include <type_traits>

namespace folly { namespace wangle {

/// This generic threadgate takes two executors and an optional waiter (if you
/// need to support waiting). Hint: use executors that inherit from Executor
/// (in Executor.h), then you just do
///
///   GenericThreadGate tg(westExecutor, eastExecutor, waiter);
template <
  class WestExecutorPtr = Executor*,
  class EastExecutorPtr = Executor*,
  class WaiterPtr = void*>
class GenericThreadGate : public ThreadGate {
public:
  /**
    EastExecutor and WestExecutor respond threadsafely to
    `add(std::function<void()>&&)`

    Waiter responds to `makeProgress()`. It may block, as long as progress
    will be made on the west front.
    */
  GenericThreadGate(WestExecutorPtr west,
                    EastExecutorPtr east,
                    WaiterPtr waiter = nullptr) :
    westExecutor(west),
    eastExecutor(east),
    waiter(waiter)
  {}

  void addWest(std::function<void()>&& fn) { westExecutor->add(std::move(fn)); }
  void addEast(std::function<void()>&& fn) { eastExecutor->add(std::move(fn)); }

  virtual void makeProgress() {
    makeProgress_(std::is_same<WaiterPtr, void*>());
  }

  WestExecutorPtr westExecutor;
  EastExecutorPtr eastExecutor;
  WaiterPtr waiter;

private:
  void makeProgress_(std::true_type const&) {
    throw std::logic_error("No waiter.");
  }

  void makeProgress_(std::false_type const&) {
    waiter->makeProgress();
  }
};

}} // executor
