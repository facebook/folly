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

#include <folly/wangle/channel/StaticPipeline.h>
#include <folly/wangle/channel/OutputBufferingHandler.h>
#include <folly/wangle/channel/test/MockHandler.h>
#include <folly/io/async/AsyncSocket.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace folly;
using namespace folly::wangle;
using namespace testing;

typedef StrictMock<MockHandlerAdapter<
  IOBufQueue&,
  std::unique_ptr<IOBuf>>>
MockBytesHandler;

MATCHER_P(IOBufContains, str, "") { return arg->moveToFbString() == str; }

TEST(OutputBufferingHandlerTest, Basic) {
  MockBytesHandler mockHandler;
  EXPECT_CALL(mockHandler, attachPipeline(_));
  StaticPipeline<IOBufQueue&, std::unique_ptr<IOBuf>,
    MockBytesHandler,
    OutputBufferingHandler>
  pipeline(&mockHandler, OutputBufferingHandler{});

  EventBase eb;
  auto socket = AsyncSocket::newSocket(&eb);
  EXPECT_CALL(mockHandler, attachTransport(_));
  pipeline.attachTransport(socket);

  // Buffering should prevent writes until the EB loops, and the writes should
  // be batched into one write call.
  auto f1 = pipeline.write(IOBuf::copyBuffer("hello"));
  auto f2 = pipeline.write(IOBuf::copyBuffer("world"));
  EXPECT_FALSE(f1.isReady());
  EXPECT_FALSE(f2.isReady());
  EXPECT_CALL(mockHandler, write_(_, IOBufContains("helloworld")));
  eb.loopOnce();
  EXPECT_TRUE(f1.isReady());
  EXPECT_TRUE(f2.isReady());
  EXPECT_CALL(mockHandler, detachPipeline(_));
}
