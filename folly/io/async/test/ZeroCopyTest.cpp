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

#include <folly/io/async/test/ZeroCopy.h>

#include <folly/portability/GTest.h>

using namespace folly;

static auto constexpr kMaxLoops = 20;
static auto constexpr kBufferSize = 4096;
static auto constexpr kBufferSizeLarge = kBufferSize * 1024;

TEST(ZeroCopyTest, zeroCopyInProgress) {
  ZeroCopyTest test(1, kMaxLoops, true, kBufferSize);
  CHECK(test.run());
}

TEST(ZeroCopyTest, zeroCopyInProgressLargeClientClose) {
  ZeroCopyTest test(1, 1, true, kBufferSize);
  test.setSendBufSize(kBufferSizeLarge);
  test.setCloseAfterSend(true);
  CHECK(test.run());
}

TEST(ZeroCopyTest, zeroCopyInProgressLargeServerClose) {
  ZeroCopyTest test(1, kMaxLoops, true, kBufferSize);
  test.setCloseAfterAccept(true);
  CHECK(test.run());
}
