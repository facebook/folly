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

#include <folly/wangle/channel/Handler.h>
#include <folly/wangle/channel/Pipeline.h>
#include <folly/wangle/channel/StaticPipeline.h>
#include <folly/wangle/channel/AsyncSocketHandler.h>
#include <folly/wangle/channel/OutputBufferingHandler.h>
#include <folly/wangle/channel/test/MockHandler.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace folly;
using namespace folly::wangle;
using namespace testing;

typedef StrictMock<MockHandlerAdapter<int, int>> IntHandler;

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
TEST(PipelineTest, RealHandlersCompile) {
  EventBase eb;
  auto socket = AsyncSocket::newSocket(&eb);
  // static
  {
    StaticPipeline<IOBufQueue&, std::unique_ptr<IOBuf>,
      AsyncSocketHandler,
      OutputBufferingHandler>
    pipeline{AsyncSocketHandler(socket), OutputBufferingHandler()};
    EXPECT_TRUE(pipeline.getHandler<AsyncSocketHandler>(0));
    EXPECT_TRUE(pipeline.getHandler<OutputBufferingHandler>(1));
  }
  // dynamic
  {
    Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
    pipeline
      .addBack(AsyncSocketHandler(socket))
      .addBack(OutputBufferingHandler())
      .finalize();
    EXPECT_TRUE(pipeline.getHandler<AsyncSocketHandler>(0));
    EXPECT_TRUE(pipeline.getHandler<OutputBufferingHandler>(1));
  }
}

// Test that handlers correctly fire the next handler when directed
TEST(PipelineTest, FireActions) {
  IntHandler handler1;
  IntHandler handler2;

  {
    InSequence sequence;
    EXPECT_CALL(handler2, attachPipeline(_));
    EXPECT_CALL(handler1, attachPipeline(_));
  }

  StaticPipeline<int, int, IntHandler, IntHandler>
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

  {
    InSequence sequence;
    EXPECT_CALL(handler1, detachPipeline(_));
    EXPECT_CALL(handler2, detachPipeline(_));
  }
}

// Test that nothing bad happens when actions reach the end of the pipeline
// (a warning will be logged, however)
TEST(PipelineTest, ReachEndOfPipeline) {
  IntHandler handler;
  EXPECT_CALL(handler, attachPipeline(_));
  StaticPipeline<int, int, IntHandler>
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
TEST(PipelineTest, TurnAround) {
  IntHandler handler1;
  IntHandler handler2;

  {
    InSequence sequence;
    EXPECT_CALL(handler2, attachPipeline(_));
    EXPECT_CALL(handler1, attachPipeline(_));
  }

  StaticPipeline<int, int, IntHandler, IntHandler>
  pipeline(&handler1, &handler2);

  EXPECT_CALL(handler1, read_(_, _)).WillOnce(FireRead());
  EXPECT_CALL(handler2, read_(_, _)).WillOnce(FireWrite());
  EXPECT_CALL(handler1, write_(_, _)).Times(1);
  pipeline.read(1);

  {
    InSequence sequence;
    EXPECT_CALL(handler1, detachPipeline(_));
    EXPECT_CALL(handler2, detachPipeline(_));
  }
}

TEST(PipelineTest, DynamicFireActions) {
  IntHandler handler1, handler2, handler3;
  EXPECT_CALL(handler2, attachPipeline(_));
  StaticPipeline<int, int, IntHandler>
  pipeline(&handler2);

  {
    InSequence sequence;
    EXPECT_CALL(handler3, attachPipeline(_));
    EXPECT_CALL(handler1, attachPipeline(_));
  }

  pipeline
    .addFront(&handler1)
    .addBack(&handler3)
    .finalize();

  EXPECT_TRUE(pipeline.getHandler<IntHandler>(0));
  EXPECT_TRUE(pipeline.getHandler<IntHandler>(1));
  EXPECT_TRUE(pipeline.getHandler<IntHandler>(2));

  EXPECT_CALL(handler1, read_(_, _)).WillOnce(FireRead());
  EXPECT_CALL(handler2, read_(_, _)).WillOnce(FireRead());
  EXPECT_CALL(handler3, read_(_, _)).Times(1);
  pipeline.read(1);

  EXPECT_CALL(handler3, write_(_, _)).WillOnce(FireWrite());
  EXPECT_CALL(handler2, write_(_, _)).WillOnce(FireWrite());
  EXPECT_CALL(handler1, write_(_, _)).Times(1);
  EXPECT_NO_THROW(pipeline.write(1).value());

  {
    InSequence sequence;
    EXPECT_CALL(handler1, detachPipeline(_));
    EXPECT_CALL(handler2, detachPipeline(_));
    EXPECT_CALL(handler3, detachPipeline(_));
  }
}

TEST(PipelineTest, DynamicAttachDetachOrder) {
  IntHandler handler1, handler2;
  Pipeline<int, int> pipeline;
  {
    InSequence sequence;
    EXPECT_CALL(handler2, attachPipeline(_));
    EXPECT_CALL(handler1, attachPipeline(_));
  }
  pipeline
    .addBack(&handler1)
    .addBack(&handler2)
    .finalize();
  {
    InSequence sequence;
    EXPECT_CALL(handler1, detachPipeline(_));
    EXPECT_CALL(handler2, detachPipeline(_));
  }
}

TEST(PipelineTest, GetContext) {
  IntHandler handler;
  EXPECT_CALL(handler, attachPipeline(_));
  StaticPipeline<int, int, IntHandler> pipeline(&handler);
  EXPECT_TRUE(handler.getContext());
  EXPECT_CALL(handler, detachPipeline(_));
}

TEST(PipelineTest, HandlerInMultiplePipelines) {
  IntHandler handler;
  EXPECT_CALL(handler, attachPipeline(_)).Times(2);
  StaticPipeline<int, int, IntHandler> pipeline1(&handler);
  StaticPipeline<int, int, IntHandler> pipeline2(&handler);
  EXPECT_FALSE(handler.getContext());
  EXPECT_CALL(handler, detachPipeline(_)).Times(2);
}

TEST(PipelineTest, HandlerInPipelineTwice) {
  IntHandler handler;
  EXPECT_CALL(handler, attachPipeline(_)).Times(2);
  StaticPipeline<int, int, IntHandler, IntHandler> pipeline(&handler, &handler);
  EXPECT_FALSE(handler.getContext());
  EXPECT_CALL(handler, detachPipeline(_)).Times(2);
}

template <class Rin, class Rout = Rin, class Win = Rout, class Wout = Rin>
class ConcreteHandler : public Handler<Rin, Rout, Win, Wout> {
  typedef typename Handler<Rin, Rout, Win, Wout>::Context Context;
 public:
  void read(Context* ctx, Rin msg) {}
  Future<void> write(Context* ctx, Win msg) { return makeFuture(); }
};

typedef HandlerAdapter<std::string, std::string> StringHandler;
typedef ConcreteHandler<int, std::string> IntToStringHandler;
typedef ConcreteHandler<std::string, int> StringToIntHandler;

TEST(Pipeline, DynamicConstruction) {
  {
    Pipeline<int, int> pipeline;
    EXPECT_THROW(
      pipeline
        .addBack(HandlerAdapter<std::string, std::string>{})
        .finalize(), std::invalid_argument);
  }
  {
    Pipeline<int, int> pipeline;
    EXPECT_THROW(
      pipeline
        .addFront(HandlerAdapter<std::string, std::string>{})
        .finalize(),
      std::invalid_argument);
  }
  {
    StaticPipeline<std::string, std::string, StringHandler, StringHandler>
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

TEST(Pipeline, AttachTransport) {
  IntHandler handler;
  EXPECT_CALL(handler, attachPipeline(_));
  StaticPipeline<int, int, IntHandler>
  pipeline(&handler);

  EventBase eb;
  auto socket = AsyncSocket::newSocket(&eb);

  EXPECT_CALL(handler, attachTransport(_));
  pipeline.attachTransport(socket);

  EXPECT_CALL(handler, detachTransport(_));
  pipeline.detachTransport();

  EXPECT_CALL(handler, detachPipeline(_));
}
