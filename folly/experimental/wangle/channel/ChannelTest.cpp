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

#include <folly/experimental/wangle/channel/ChannelHandler.h>
#include <folly/experimental/wangle/channel/ChannelPipeline.h>
#include <folly/io/IOBufQueue.h>
#include <folly/Conv.h>
#include <gtest/gtest.h>

using namespace folly;
using namespace folly::wangle;

class ToString : public ChannelHandler<int, std::string> {
 public:
  virtual ~ToString() {}
  void read(Context* ctx, int msg) override {
    LOG(INFO) << "ToString read";
    ctx->fireRead(folly::to<std::string>(msg));
  }
  Future<void> write(Context* ctx, std::string msg) override {
    LOG(INFO) << "ToString write";
    return ctx->fireWrite(folly::to<int>(msg));
  }
};

class KittyPrepender : public ChannelHandlerAdapter<std::string> {
 public:
  virtual ~KittyPrepender() {}
  void read(Context* ctx, std::string msg) override {
    LOG(INFO) << "KittyAppender read";
    ctx->fireRead(folly::to<std::string>("kitty", msg));
  }
  Future<void> write(Context* ctx, std::string msg) override {
    LOG(INFO) << "KittyAppender write";
    return ctx->fireWrite(msg.substr(5));
  }
};

typedef ChannelHandlerAdapter<IOBuf> BytesPassthrough;

class EchoService : public ChannelHandlerAdapter<std::string> {
 public:
  virtual ~EchoService() {}
  void read(Context* ctx, std::string str) override {
    LOG(INFO) << "ECHO: " << str;
    ctx->fireWrite(str).then([](Try<void>&& t) {
      LOG(INFO) << "done writing";
    });
  }
};

TEST(ChannelTest, PlzCompile) {
  ChannelPipeline<
    BytesPassthrough,
    BytesPassthrough,
    // If this were useful it wouldn't be that hard
    // ChannelPipeline<BytesPassthrough>,
    BytesPassthrough>
  pipeline(BytesPassthrough(), BytesPassthrough(), BytesPassthrough);

  ChannelPipeline<
    ChannelHandlerPtr<ToString>,
    KittyPrepender,
    KittyPrepender,
    EchoService>
  kittyPipeline(
      std::make_shared<ToString>(),
      KittyPrepender{},
      KittyPrepender{},
      EchoService{});
  kittyPipeline.read(5);

  auto handler = kittyPipeline.getHandler<KittyPrepender>(2);
  CHECK(handler);
}
