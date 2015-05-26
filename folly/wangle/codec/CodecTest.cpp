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
#include <gtest/gtest.h>

#include <folly/wangle/codec/FixedLengthFrameDecoder.h>
#include <folly/wangle/codec/LengthFieldBasedFrameDecoder.h>
#include <folly/wangle/codec/LengthFieldPrepender.h>
#include <folly/wangle/codec/LineBasedFrameDecoder.h>

using namespace folly;
using namespace folly::wangle;
using namespace folly::io;

class FrameTester
    : public InboundHandler<std::unique_ptr<IOBuf>> {
 public:
  explicit FrameTester(std::function<void(std::unique_ptr<IOBuf>)> test)
    : test_(test) {}

  void read(Context* ctx, std::unique_ptr<IOBuf> buf) {
    test_(std::move(buf));
  }

  void readException(Context* ctx, exception_wrapper w) {
    test_(nullptr);
  }
 private:
  std::function<void(std::unique_ptr<IOBuf>)> test_;
};

class BytesReflector
    : public BytesToBytesHandler {
 public:

  Future<void> write(Context* ctx, std::unique_ptr<IOBuf> buf) {
    IOBufQueue q_(IOBufQueue::cacheChainLength());
    q_.append(std::move(buf));
    ctx->fireRead(q_);

    return makeFuture();
  }
};

TEST(FixedLengthFrameDecoder, FailWhenLengthFieldEndOffset) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(FixedLengthFrameDecoder(10))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 10);
      }))
    .finalize();

  auto buf3 = IOBuf::create(3);
  buf3->append(3);
  auto buf11 = IOBuf::create(11);
  buf11->append(11);
  auto buf16 = IOBuf::create(16);
  buf16->append(16);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(buf3));
  pipeline.read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(buf11));
  pipeline.read(q);
  EXPECT_EQ(called, 1);

  q.append(std::move(buf16));
  pipeline.read(q);
  EXPECT_EQ(called, 3);
}

TEST(LengthFieldFramePipeline, SimpleTest) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(BytesReflector())
    .addBack(LengthFieldPrepender())
    .addBack(LengthFieldBasedFrameDecoder())
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 2);
      }))
    .finalize();

  auto buf = IOBuf::create(2);
  buf->append(2);
  pipeline.write(std::move(buf));
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFramePipeline, LittleEndian) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(BytesReflector())
    .addBack(LengthFieldBasedFrameDecoder(4, 100, 0, 0, 4, false))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 1);
      }))
    .addBack(LengthFieldPrepender(4, 0, false, false))
    .finalize();

  auto buf = IOBuf::create(1);
  buf->append(1);
  pipeline.write(std::move(buf));
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, Simple) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LengthFieldBasedFrameDecoder())
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 1);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(4);
  bufFrame->append(4);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint32_t)1);
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline.read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline.read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, NoStrip) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LengthFieldBasedFrameDecoder(2, 10, 0, 0, 0))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 3);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(2);
  bufFrame->append(2);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint16_t)1);
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline.read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline.read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, Adjustment) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LengthFieldBasedFrameDecoder(2, 10, 0, -2, 0))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 3);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(2);
  bufFrame->append(2);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint16_t)3); // includes frame size
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline.read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline.read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, PreHeader) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LengthFieldBasedFrameDecoder(2, 10, 2, 0, 0))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 5);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(4);
  bufFrame->append(4);
  RWPrivateCursor c(bufFrame.get());
  c.write((uint16_t)100); // header
  c.writeBE((uint16_t)1); // frame size
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline.read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline.read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, PostHeader) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LengthFieldBasedFrameDecoder(2, 10, 0, 2, 0))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 5);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(4);
  bufFrame->append(4);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint16_t)1); // frame size
  c.write((uint16_t)100); // header
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline.read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline.read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoderStrip, PrePostHeader) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LengthFieldBasedFrameDecoder(2, 10, 2, 2, 4))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 3);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(6);
  bufFrame->append(6);
  RWPrivateCursor c(bufFrame.get());
  c.write((uint16_t)100); // pre header
  c.writeBE((uint16_t)1); // frame size
  c.write((uint16_t)100); // post header
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline.read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline.read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, StripPrePostHeaderFrameInclHeader) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LengthFieldBasedFrameDecoder(2, 10, 2, -2, 4))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 3);
      }))
    .finalize();

  auto bufFrame = IOBuf::create(6);
  bufFrame->append(6);
  RWPrivateCursor c(bufFrame.get());
  c.write((uint16_t)100); // pre header
  c.writeBE((uint16_t)5); // frame size
  c.write((uint16_t)100); // post header
  auto bufData = IOBuf::create(1);
  bufData->append(1);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline.read(q);
  EXPECT_EQ(called, 0);

  q.append(std::move(bufData));
  pipeline.read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, FailTestLengthFieldEndOffset) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LengthFieldBasedFrameDecoder(4, 10, 4, -2, 4))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        ASSERT_EQ(nullptr, buf);
        called++;
      }))
    .finalize();

  auto bufFrame = IOBuf::create(8);
  bufFrame->append(8);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint32_t)0); // frame size
  c.write((uint32_t)0); // crap

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline.read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, FailTestLengthFieldFrameSize) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LengthFieldBasedFrameDecoder(4, 10, 0, 0, 4))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        ASSERT_EQ(nullptr, buf);
        called++;
      }))
    .finalize();

  auto bufFrame = IOBuf::create(16);
  bufFrame->append(16);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint32_t)12); // frame size
  c.write((uint32_t)0); // nothing
  c.write((uint32_t)0); // nothing
  c.write((uint32_t)0); // nothing

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline.read(q);
  EXPECT_EQ(called, 1);
}

TEST(LengthFieldFrameDecoder, FailTestLengthFieldInitialBytes) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LengthFieldBasedFrameDecoder(4, 10, 0, 0, 10))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        ASSERT_EQ(nullptr, buf);
        called++;
      }))
    .finalize();

  auto bufFrame = IOBuf::create(16);
  bufFrame->append(16);
  RWPrivateCursor c(bufFrame.get());
  c.writeBE((uint32_t)4); // frame size
  c.write((uint32_t)0); // nothing
  c.write((uint32_t)0); // nothing
  c.write((uint32_t)0); // nothing

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(bufFrame));
  pipeline.read(q);
  EXPECT_EQ(called, 1);
}

TEST(LineBasedFrameDecoder, Simple) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LineBasedFrameDecoder(10))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 3);
      }))
    .finalize();

  auto buf = IOBuf::create(3);
  buf->append(3);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 0);

  buf = IOBuf::create(1);
  buf->append(1);
  RWPrivateCursor c(buf.get());
  c.write<char>('\n');
  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(4);
  buf->append(4);
  RWPrivateCursor c1(buf.get());
  c1.write(' ');
  c1.write(' ');
  c1.write(' ');

  c1.write('\r');
  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(1);
  buf->append(1);
  RWPrivateCursor c2(buf.get());
  c2.write('\n');
  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 2);
}

TEST(LineBasedFrameDecoder, SaveDelimiter) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LineBasedFrameDecoder(10, false))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 4);
      }))
    .finalize();

  auto buf = IOBuf::create(3);
  buf->append(3);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 0);

  buf = IOBuf::create(1);
  buf->append(1);
  RWPrivateCursor c(buf.get());
  c.write<char>('\n');
  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(3);
  buf->append(3);
  RWPrivateCursor c1(buf.get());
  c1.write(' ');
  c1.write(' ');
  c1.write('\r');
  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(1);
  buf->append(1);
  RWPrivateCursor c2(buf.get());
  c2.write('\n');
  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 2);
}

TEST(LineBasedFrameDecoder, Fail) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LineBasedFrameDecoder(10))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        ASSERT_EQ(nullptr, buf);
        called++;
      }))
    .finalize();

  auto buf = IOBuf::create(11);
  buf->append(11);

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(1);
  buf->append(1);
  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(2);
  buf->append(2);
  RWPrivateCursor c(buf.get());
  c.write(' ');
  c.write<char>('\n');
  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 1);

  buf = IOBuf::create(12);
  buf->append(12);
  RWPrivateCursor c2(buf.get());
  for (int i = 0; i < 11; i++) {
    c2.write(' ');
  }
  c2.write<char>('\n');
  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 2);
}

TEST(LineBasedFrameDecoder, NewLineOnly) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LineBasedFrameDecoder(
               10, true, LineBasedFrameDecoder::TerminatorType::NEWLINE))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 1);
      }))
    .finalize();

  auto buf = IOBuf::create(2);
  buf->append(2);
  RWPrivateCursor c(buf.get());
  c.write<char>('\r');
  c.write<char>('\n');

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 1);
}

TEST(LineBasedFrameDecoder, CarriageNewLineOnly) {
  Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> pipeline;
  int called = 0;

  pipeline
    .addBack(LineBasedFrameDecoder(
              10, true, LineBasedFrameDecoder::TerminatorType::CARRIAGENEWLINE))
    .addBack(FrameTester([&](std::unique_ptr<IOBuf> buf) {
        auto sz = buf->computeChainDataLength();
        called++;
        EXPECT_EQ(sz, 1);
      }))
    .finalize();

  auto buf = IOBuf::create(3);
  buf->append(3);
  RWPrivateCursor c(buf.get());
  c.write<char>('\n');
  c.write<char>('\r');
  c.write<char>('\n');

  IOBufQueue q(IOBufQueue::cacheChainLength());

  q.append(std::move(buf));
  pipeline.read(q);
  EXPECT_EQ(called, 1);
}
