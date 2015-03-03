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

#include <folly/wangle/channel/ChannelHandler.h>
#include <folly/wangle/channel/ChannelPipeline.h>
#include <folly/wangle/channel/AsyncSocketHandler.h>
#include <folly/wangle/channel/OutputBufferingHandler.h>
#include <folly/wangle/channel/test/MockChannelHandler.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace folly;
using namespace folly::wangle;
using namespace testing;

typedef StrictMock<MockChannelHandlerAdapter<int, int>> IntHandler;
typedef ChannelHandlerPtr<IntHandler, false> IntHandlerPtr;

ACTION(FireRead) {
  arg0->fireRead(arg1);
}

ACTION(FireReadEOF) {
  arg0->fireReadEOF();
}

ACTION(FireReadException) {
  arg0->fireReadException(arg1);
}

ACTION(FireWrite) {
  arg0->fireWrite(arg1);
}

ACTION(FireClose) {
  arg0->fireClose();
}

// Test move only types, among other things
TEST(ChannelTest, RealHandlersCompile) {
  EventBase eb;
  auto socket = AsyncSocket::newSocket(&eb);
  // static
  {
    ChannelPipeline<IOBufQueue&, std::unique_ptr<IOBuf>,
      AsyncSocketHandler,
      OutputBufferingHandler>
    pipeline{AsyncSocketHandler(socket), OutputBufferingHandler()};
    EXPECT_TRUE(pipeline.getHandler<AsyncSocketHandler>(0));
    EXPECT_TRUE(pipeline.getHandler<OutputBufferingHandler>(1));
  }
  // dynamic
  {
    ChannelPipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
    pipeline
      .addBack(AsyncSocketHandler(socket))
      .addBack(OutputBufferingHandler())
      .finalize();
    EXPECT_TRUE(pipeline.getHandler<AsyncSocketHandler>(0));
    EXPECT_TRUE(pipeline.getHandler<OutputBufferingHandler>(1));
  }
}

// Test that handlers correctly fire the next handler when directed
TEST(ChannelTest, FireActions) {
  IntHandler handler1;
  IntHandler handler2;

  EXPECT_CALL(handler1, attachPipeline(_));
  EXPECT_CALL(handler2, attachPipeline(_));

  ChannelPipeline<int, int, IntHandlerPtr, IntHandlerPtr>
  pipeline(&handler1, &handler2);

  EXPECT_CALL(handler1, read_(_, _)).WillOnce(FireRead());
  EXPECT_CALL(handler2, read_(_, _)).Times(1);
  pipeline.read(1);

  EXPECT_CALL(handler1, readEOF(_)).WillOnce(FireReadEOF());
  EXPECT_CALL(handler2, readEOF(_)).Times(1);
  pipeline.readEOF();

  EXPECT_CALL(handler1, readException(_, _)).WillOnce(FireReadException());
  EXPECT_CALL(handler2, readException(_, _)).Times(1);
  pipeline.readException(make_exception_wrapper<std::runtime_error>("blah"));

  EXPECT_CALL(handler2, write_(_, _)).WillOnce(FireWrite());
  EXPECT_CALL(handler1, write_(_, _)).Times(1);
  EXPECT_NO_THROW(pipeline.write(1).value());

  EXPECT_CALL(handler2, close_(_)).WillOnce(FireClose());
  EXPECT_CALL(handler1, close_(_)).Times(1);
  EXPECT_NO_THROW(pipeline.close().value());

  EXPECT_CALL(handler1, detachPipeline(_));
  EXPECT_CALL(handler2, detachPipeline(_));
}

// Test that nothing bad happens when actions reach the end of the pipeline
// (a warning will be logged, however)
TEST(ChannelTest, ReachEndOfPipeline) {
  IntHandler handler;
  EXPECT_CALL(handler, attachPipeline(_));
  ChannelPipeline<int, int, IntHandlerPtr>
  pipeline(&handler);

  EXPECT_CALL(handler, read_(_, _)).WillOnce(FireRead());
  pipeline.read(1);

  EXPECT_CALL(handler, readEOF(_)).WillOnce(FireReadEOF());
  pipeline.readEOF();

  EXPECT_CALL(handler, readException(_, _)).WillOnce(FireReadException());
  pipeline.readException(make_exception_wrapper<std::runtime_error>("blah"));

  EXPECT_CALL(handler, write_(_, _)).WillOnce(FireWrite());
  EXPECT_NO_THROW(pipeline.write(1).value());

  EXPECT_CALL(handler, close_(_)).WillOnce(FireClose());
  EXPECT_NO_THROW(pipeline.close().value());

  EXPECT_CALL(handler, detachPipeline(_));
}

// Test having the last read handler turn around and write
TEST(ChannelTest, TurnAround) {
  IntHandler handler1;
  IntHandler handler2;

  EXPECT_CALL(handler1, attachPipeline(_));
  EXPECT_CALL(handler2, attachPipeline(_));

  ChannelPipeline<int, int, IntHandlerPtr, IntHandlerPtr>
  pipeline(&handler1, &handler2);

  EXPECT_CALL(handler1, read_(_, _)).WillOnce(FireRead());
  EXPECT_CALL(handler2, read_(_, _)).WillOnce(FireWrite());
  EXPECT_CALL(handler1, write_(_, _)).Times(1);
  pipeline.read(1);

  EXPECT_CALL(handler1, detachPipeline(_));
  EXPECT_CALL(handler2, detachPipeline(_));
}

TEST(ChannelTest, DynamicFireActions) {
  IntHandler handler1, handler2, handler3;
  EXPECT_CALL(handler2, attachPipeline(_));
  ChannelPipeline<int, int, IntHandlerPtr>
  pipeline(&handler2);

  EXPECT_CALL(handler1, attachPipeline(_));
  EXPECT_CALL(handler3, attachPipeline(_));

  pipeline
    .addFront(IntHandlerPtr(&handler1))
    .addBack(IntHandlerPtr(&handler3))
    .finalize();

  EXPECT_TRUE(pipeline.getHandler<IntHandlerPtr>(0));
  EXPECT_TRUE(pipeline.getHandler<IntHandlerPtr>(1));
  EXPECT_TRUE(pipeline.getHandler<IntHandlerPtr>(2));

  EXPECT_CALL(handler1, read_(_, _)).WillOnce(FireRead());
  EXPECT_CALL(handler2, read_(_, _)).WillOnce(FireRead());
  EXPECT_CALL(handler3, read_(_, _)).Times(1);
  pipeline.read(1);

  EXPECT_CALL(handler3, write_(_, _)).WillOnce(FireWrite());
  EXPECT_CALL(handler2, write_(_, _)).WillOnce(FireWrite());
  EXPECT_CALL(handler1, write_(_, _)).Times(1);
  EXPECT_NO_THROW(pipeline.write(1).value());

  EXPECT_CALL(handler1, detachPipeline(_));
  EXPECT_CALL(handler2, detachPipeline(_));
  EXPECT_CALL(handler3, detachPipeline(_));
}

template <class Rin, class Rout = Rin, class Win = Rout, class Wout = Rin>
class ConcreteChannelHandler : public ChannelHandler<Rin, Rout, Win, Wout> {
  typedef typename ChannelHandler<Rin, Rout, Win, Wout>::Context Context;
 public:
  void read(Context* ctx, Rin msg) {}
  Future<void> write(Context* ctx, Win msg) { return makeFuture(); }
};

typedef ChannelHandlerAdapter<std::string, std::string> StringHandler;
typedef ConcreteChannelHandler<int, std::string> IntToStringHandler;
typedef ConcreteChannelHandler<std::string, int> StringToIntHandler;

TEST(ChannelPipeline, DynamicConstruction) {
  {
    ChannelPipeline<int, int> pipeline;
    EXPECT_THROW(
      pipeline
        .addBack(ChannelHandlerAdapter<std::string, std::string>{})
        .finalize(), std::invalid_argument);
  }
  {
    ChannelPipeline<int, int> pipeline;
    EXPECT_THROW(
      pipeline
        .addFront(ChannelHandlerAdapter<std::string, std::string>{})
        .finalize(),
      std::invalid_argument);
  }
  {
    ChannelPipeline<std::string, std::string, StringHandler, StringHandler>
    pipeline{StringHandler(), StringHandler()};

    // Exercise both addFront and addBack. Final pipeline is
    // StI <-> ItS <-> StS <-> StS <-> StI <-> ItS
    EXPECT_NO_THROW(
      pipeline
        .addFront(IntToStringHandler{})
        .addFront(StringToIntHandler{})
        .addBack(StringToIntHandler{})
        .addBack(IntToStringHandler{})
        .finalize());
  }
}

TEST(ChannelPipeline, AttachTransport) {
  IntHandler handler;
  EXPECT_CALL(handler, attachPipeline(_));
  ChannelPipeline<int, int, IntHandlerPtr>
  pipeline(&handler);

  EventBase eb;
  auto socket = AsyncSocket::newSocket(&eb);

  EXPECT_CALL(handler, attachTransport(_));
  pipeline.attachTransport(socket);

  EXPECT_CALL(handler, detachTransport(_));
  pipeline.detachTransport();

  EXPECT_CALL(handler, detachPipeline(_));
}
