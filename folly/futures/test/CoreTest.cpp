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

#include <folly/futures/detail/Core.h>

#include <folly/futures/Future.h>
#include <folly/portability/GTest.h>

using namespace folly;

TEST(Core, size) {
  struct KeepAliveOrDeferredGold {
    enum class State {};
    State state_;
    Executor* executor_;
  };
  class CoreBaseGold {
   public:
    virtual ~CoreBaseGold() = 0;

   private:
    folly::Function<void(Try<Unit>&&)> callback_;
    std::atomic<futures::detail::State> state_;
    std::atomic<unsigned char> attached_;
    std::atomic<unsigned char> callbackReferences_;
    KeepAliveOrDeferredGold executor_;
    std::shared_ptr<RequestContext> context_;
    std::atomic<uintptr_t> interrupt_;
    CoreBaseGold* proxy_;
  };
  class CoreGold : Try<Unit>, public CoreBaseGold {};
  // If this number goes down, it's fine!
  // If it goes up, please seek professional advice ;-)
  EXPECT_EQ(sizeof(CoreGold), sizeof(futures::detail::Core<Unit>));
  EXPECT_EQ(alignof(CoreGold), alignof(futures::detail::Core<Unit>));
}
