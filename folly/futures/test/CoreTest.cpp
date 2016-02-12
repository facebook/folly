/*
 * Copyright 2016 Facebook, Inc.
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

#include <gtest/gtest.h>

#include <folly/futures/Future.h>
#include <folly/futures/detail/Core.h>

using namespace folly;

// On android, we see a different way of memory alignment of this structure.
// Adding some extra padding to make it pass.
#ifdef __ANDROID_
constexpr size_t extraPadding = 8;
#else
constexpr size_t extraPadding = 0;
#endif

TEST(Core, size) {
  struct Gold {
    char lambdaBuf_[8 * sizeof(void*)];
    folly::Optional<Try<Unit>> result_;
    std::function<void(Try<Unit>&&)> callback_;
    detail::FSM<detail::State> fsm_;
    std::atomic<unsigned char> attached_;
    std::atomic<bool> active_;
    std::atomic<bool> interruptHandlerSet_;
    folly::MicroSpinLock interruptLock_;
    folly::MicroSpinLock executorLock_;
    int8_t priority_;
    Executor* executor_;
    std::shared_ptr<RequestContext> context_;
    std::unique_ptr<exception_wrapper> interrupt_;
    std::function<void(exception_wrapper const&)> interruptHandler_;
  };
  // If this number goes down, it's fine!
  // If it goes up, please seek professional advice ;-)
  EXPECT_GE(sizeof(Gold) + extraPadding, sizeof(detail::Core<Unit>));
}
