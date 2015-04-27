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

#include "folly/wangle/bootstrap/ServerBootstrap.h"
#include "folly/wangle/bootstrap/ClientBootstrap.h"
#include "folly/wangle/channel/Handler.h"

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <boost/thread.hpp>

using namespace folly::wangle;
using namespace folly;

typedef Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>> BytesPipeline;

typedef ServerBootstrap<BytesPipeline> TestServer;
typedef ClientBootstrap<BytesPipeline> TestClient;

class TestClientPipelineFactory : public PipelineFactory<BytesPipeline> {
 public:
  BytesPipeline* newPipeline(std::shared_ptr<AsyncSocket> sock) {
    CHECK(sock->good());

    // We probably aren't connected immedately, check after a small delay
    EventBaseManager::get()->getEventBase()->tryRunAfterDelay([sock](){
      CHECK(sock->readable());
    }, 100);
    return nullptr;
  }
};

class TestPipelineFactory : public PipelineFactory<BytesPipeline> {
 public:
  BytesPipeline* newPipeline(std::shared_ptr<AsyncSocket> sock) {
    pipelines++;
    return new BytesPipeline();
  }
  std::atomic<int> pipelines{0};
};

class TestAcceptor : public Acceptor {
EventBase base_;
 public:
  TestAcceptor() : Acceptor(ServerSocketConfig()) {
    Acceptor::init(nullptr, &base_);
  }
  void onNewConnection(
      AsyncSocket::UniquePtr sock,
      const folly::SocketAddress* address,
      const std::string& nextProtocolName,
        const TransportInfo& tinfo) {
  }
};

class TestAcceptorFactory : public AcceptorFactory {
 public:
  std::shared_ptr<Acceptor> newAcceptor(EventBase* base) {
    return std::make_shared<TestAcceptor>();
  }
};

TEST(Bootstrap, Basic) {
  TestServer server;
  TestClient client;
}

TEST(Bootstrap, ServerWithPipeline) {
  TestServer server;
  server.childPipeline(std::make_shared<TestPipelineFactory>());
  server.bind(0);
  server.stop();
}

TEST(Bootstrap, ServerWithChildHandler) {
  TestServer server;
  server.childHandler(std::make_shared<TestAcceptorFactory>());
  server.bind(0);
  server.stop();
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
  client.pipelineFactory(std::make_shared<TestClientPipelineFactory>());
  client.connect(address);
  base->loop();
  server.stop();

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
  client.pipelineFactory(std::make_shared<TestClientPipelineFactory>());

  client.connect(address);

  TestClient client2;
  client2.pipelineFactory(std::make_shared<TestClientPipelineFactory>());
  client2.connect(address);

  base->loop();
  server.stop();

  CHECK(factory->pipelines == 2);
}

TEST(Bootstrap, ServerAcceptGroupTest) {
  // Verify that server is using the accept IO group

  TestServer server;
  auto factory = std::make_shared<TestPipelineFactory>();
  server.childPipeline(factory);
  server.group(std::make_shared<IOThreadPoolExecutor>(1), nullptr);
  server.bind(0);

  SocketAddress address;
  server.getSockets()[0]->getAddress(&address);

  boost::barrier barrier(2);
  auto thread = std::thread([&](){
    TestClient client;
    client.pipelineFactory(std::make_shared<TestClientPipelineFactory>());
    client.connect(address);
    EventBaseManager::get()->getEventBase()->loop();
    barrier.wait();
  });
  barrier.wait();
  server.stop();
  thread.join();

  CHECK(factory->pipelines == 1);
}

TEST(Bootstrap, ServerAcceptGroup2Test) {
  // Verify that server is using the accept IO group

  // Check if reuse port is supported, if not, don't run this test
  try {
    EventBase base;
    auto serverSocket = AsyncServerSocket::newSocket(&base);
    serverSocket->bind(0);
    serverSocket->listen(0);
    serverSocket->startAccepting();
    serverSocket->setReusePortEnabled(true);
    serverSocket->stopAccepting();
  } catch(...) {
    LOG(INFO) << "Reuse port probably not supported";
    return;
  }

  TestServer server;
  auto factory = std::make_shared<TestPipelineFactory>();
  server.childPipeline(factory);
  server.group(std::make_shared<IOThreadPoolExecutor>(4), nullptr);
  server.bind(0);

  SocketAddress address;
  server.getSockets()[0]->getAddress(&address);

  TestClient client;
  client.pipelineFactory(std::make_shared<TestClientPipelineFactory>());

  client.connect(address);
  EventBaseManager::get()->getEventBase()->loop();

  server.stop();

  CHECK(factory->pipelines == 1);
}

TEST(Bootstrap, SharedThreadPool) {
  // Check if reuse port is supported, if not, don't run this test
  try {
    EventBase base;
    auto serverSocket = AsyncServerSocket::newSocket(&base);
    serverSocket->bind(0);
    serverSocket->listen(0);
    serverSocket->startAccepting();
    serverSocket->setReusePortEnabled(true);
    serverSocket->stopAccepting();
  } catch(...) {
    LOG(INFO) << "Reuse port probably not supported";
    return;
  }

  auto pool = std::make_shared<IOThreadPoolExecutor>(2);

  TestServer server;
  auto factory = std::make_shared<TestPipelineFactory>();
  server.childPipeline(factory);
  server.group(pool, pool);

  server.bind(0);

  SocketAddress address;
  server.getSockets()[0]->getAddress(&address);

  TestClient client;
  client.pipelineFactory(std::make_shared<TestClientPipelineFactory>());
  client.connect(address);

  TestClient client2;
  client2.pipelineFactory(std::make_shared<TestClientPipelineFactory>());
  client2.connect(address);

  TestClient client3;
  client3.pipelineFactory(std::make_shared<TestClientPipelineFactory>());
  client3.connect(address);

  TestClient client4;
  client4.pipelineFactory(std::make_shared<TestClientPipelineFactory>());
  client4.connect(address);

  TestClient client5;
  client5.pipelineFactory(std::make_shared<TestClientPipelineFactory>());
  client5.connect(address);

  EventBaseManager::get()->getEventBase()->loop();

  server.stop();
  CHECK(factory->pipelines == 5);
}

TEST(Bootstrap, ExistingSocket) {
  TestServer server;
  auto factory = std::make_shared<TestPipelineFactory>();
  server.childPipeline(factory);
  folly::AsyncServerSocket::UniquePtr socket(new AsyncServerSocket);
  server.bind(std::move(socket));
}

std::atomic<int> connections{0};

class TestHandlerPipeline
    : public HandlerAdapter<void*,
                                   std::exception> {
 public:
  void read(Context* ctx, void* conn) {
    connections++;
    return ctx->fireRead(conn);
  }

  Future<void> write(Context* ctx, std::exception e) {
    return ctx->fireWrite(e);
  }
};

template <typename HandlerPipeline>
class TestHandlerPipelineFactory
    : public PipelineFactory<ServerBootstrap<BytesPipeline>::AcceptPipeline> {
 public:
  ServerBootstrap<BytesPipeline>::AcceptPipeline* newPipeline(std::shared_ptr<AsyncSocket>) {
    auto pipeline = new ServerBootstrap<BytesPipeline>::AcceptPipeline;
    pipeline->addBack(HandlerPipeline());
    return pipeline;
  }
};

TEST(Bootstrap, LoadBalanceHandler) {
  TestServer server;
  auto factory = std::make_shared<TestPipelineFactory>();
  server.childPipeline(factory);

  auto pipelinefactory =
    std::make_shared<TestHandlerPipelineFactory<TestHandlerPipeline>>();
  server.pipeline(pipelinefactory);
  server.bind(0);
  auto base = EventBaseManager::get()->getEventBase();

  SocketAddress address;
  server.getSockets()[0]->getAddress(&address);

  TestClient client;
  client.pipelineFactory(std::make_shared<TestClientPipelineFactory>());
  client.connect(address);
  base->loop();
  server.stop();

  CHECK(factory->pipelines == 1);
  CHECK(connections == 1);
}

class TestUDPPipeline
    : public HandlerAdapter<void*,
                                   std::exception> {
 public:
  void read(Context* ctx, void* conn) {
    connections++;
  }

  Future<void> write(Context* ctx, std::exception e) {
    return ctx->fireWrite(e);
  }
};

TEST(Bootstrap, UDP) {
  TestServer server;
  auto factory = std::make_shared<TestPipelineFactory>();
  auto pipelinefactory =
    std::make_shared<TestHandlerPipelineFactory<TestUDPPipeline>>();
  server.pipeline(pipelinefactory);
  server.channelFactory(std::make_shared<AsyncUDPServerSocketFactory>());
  server.bind(0);
}

TEST(Bootstrap, UDPClientServerTest) {
  connections = 0;

  TestServer server;
  auto factory = std::make_shared<TestPipelineFactory>();
  auto pipelinefactory =
    std::make_shared<TestHandlerPipelineFactory<TestUDPPipeline>>();
  server.pipeline(pipelinefactory);
  server.channelFactory(std::make_shared<AsyncUDPServerSocketFactory>());
  server.bind(0);

  auto base = EventBaseManager::get()->getEventBase();

  SocketAddress address;
  server.getSockets()[0]->getAddress(&address);

  SocketAddress localhost("::1", 0);
  AsyncUDPSocket client(base);
  client.bind(localhost);
  auto data = IOBuf::create(1);
  data->append(1);
  *(data->writableData()) = 'a';
  client.write(address, std::move(data));
  base->loop();
  server.stop();

  CHECK(connections == 1);
}
