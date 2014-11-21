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

#include "folly/experimental/wangle/bootstrap/ServerBootstrap.h"
#include "folly/experimental/wangle/bootstrap/ClientBootstrap.h"
#include "folly/experimental/wangle/channel/ChannelHandler.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly::wangle;
using namespace folly;

typedef ChannelPipeline<IOBufQueue&, std::unique_ptr<IOBuf>> Pipeline;

class TestServer : public ServerBootstrap<Pipeline> {
  Pipeline* newPipeline(std::shared_ptr<AsyncSocket>) {
    return nullptr;
  }
};

class TestClient : public ClientBootstrap<Pipeline> {
  Pipeline* newPipeline(std::shared_ptr<AsyncSocket> sock) {
    CHECK(sock->good());

    // We probably aren't connected immedately, check after a small delay
    EventBaseManager::get()->getEventBase()->runAfterDelay([sock](){
      CHECK(sock->readable());
    }, 100);
    return nullptr;
  }
};

class TestPipelineFactory : public PipelineFactory<Pipeline> {
 public:
  Pipeline* newPipeline(std::shared_ptr<AsyncSocket> sock) {
    pipelines++;
    return new Pipeline();
  }
  std::atomic<int> pipelines{0};
};

TEST(Bootstrap, Basic) {
  TestServer server;
  TestClient client;
}

TEST(Bootstrap, ServerWithPipeline) {
  TestServer server;
  server.childPipeline(std::make_shared<TestPipelineFactory>());
  server.bind(0);
  auto base = EventBaseManager::get()->getEventBase();
  base->runAfterDelay([&](){
    server.stop();
  }, 500);
  base->loop();
}

TEST(Bootstrap, ClientServerTest) {
  TestServer server;
  auto factory = std::make_shared<TestPipelineFactory>();
  server.childPipeline(factory);
  server.bind(0);
  auto base = EventBaseManager::get()->getEventBase();

  SocketAddress address;
  server.getSockets()[0]->getAddress(&address);

  TestClient client;
  client.connect(address);
  base->runAfterDelay([&](){
    server.stop();
  }, 500);
  base->loop();

  CHECK(factory->pipelines == 1);
}

TEST(Bootstrap, ClientConnectionManagerTest) {
  // Create a single IO thread, and verify that
  // client connections are pooled properly

  TestServer server;
  auto factory = std::make_shared<TestPipelineFactory>();
  server.childPipeline(factory);
  server.group(std::make_shared<IOThreadPoolExecutor>(1));
  server.bind(0);
  auto base = EventBaseManager::get()->getEventBase();

  SocketAddress address;
  server.getSockets()[0]->getAddress(&address);

  TestClient client;
  client.connect(address);

  TestClient client2;
  client2.connect(address);

  base->runAfterDelay([&](){
    server.stop();
  }, 500);

  base->loop();

  CHECK(factory->pipelines == 2);
}
