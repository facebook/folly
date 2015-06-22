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

#include <folly/wangle/codec/StringCodec.h>
#include <folly/wangle/codec/ByteToMessageCodec.h>
#include <folly/wangle/service/ClientDispatcher.h>
#include <folly/wangle/service/ServerDispatcher.h>
#include <folly/wangle/service/Service.h>

namespace folly {

using namespace wangle;

typedef Pipeline<IOBufQueue&, std::string> ServicePipeline;

class SimpleDecode : public ByteToMessageCodec {
 public:
  std::unique_ptr<IOBuf> decode(Context* ctx,
                                IOBufQueue& buf,
                                size_t&) override {
    return buf.move();
  }
};

class EchoService : public Service<std::string, std::string> {
 public:
  Future<std::string> operator()(std::string req) override { return req; }
};

class EchoIntService : public Service<std::string, int> {
 public:
  Future<int> operator()(std::string req) override {
    return folly::to<int>(req);
  }
};

template <typename Req, typename Resp>
class ServerPipelineFactory
    : public PipelineFactory<ServicePipeline> {
 public:

  std::unique_ptr<ServicePipeline, folly::DelayedDestruction::Destructor>
  newPipeline(std::shared_ptr<AsyncSocket> socket) override {
    std::unique_ptr<ServicePipeline, folly::DelayedDestruction::Destructor> pipeline(
      new ServicePipeline());
    pipeline->addBack(AsyncSocketHandler(socket));
    pipeline->addBack(SimpleDecode());
    pipeline->addBack(StringCodec());
    pipeline->addBack(SerialServerDispatcher<Req, Resp>(&service_));
    pipeline->finalize();
    return pipeline;
  }

 private:
  EchoService service_;
};

template <typename Req, typename Resp>
class ClientPipelineFactory : public PipelineFactory<ServicePipeline> {
 public:

  std::unique_ptr<ServicePipeline, folly::DelayedDestruction::Destructor>
  newPipeline(std::shared_ptr<AsyncSocket> socket) override {
    std::unique_ptr<ServicePipeline, folly::DelayedDestruction::Destructor> pipeline(
      new ServicePipeline());
    pipeline->addBack(AsyncSocketHandler(socket));
    pipeline->addBack(SimpleDecode());
    pipeline->addBack(StringCodec());
    pipeline->finalize();
    return pipeline;
   }
};

template <typename Pipeline, typename Req, typename Resp>
class ClientServiceFactory : public ServiceFactory<Pipeline, Req, Resp> {
 public:
  class ClientService : public Service<Req, Resp> {
   public:
    explicit ClientService(Pipeline* pipeline) {
      dispatcher_.setPipeline(pipeline);
    }
    Future<Resp> operator()(Req request) override {
      return dispatcher_(std::move(request));
    }
   private:
    SerialClientDispatcher<Pipeline, Req, Resp> dispatcher_;
  };

  Future<std::shared_ptr<Service<Req, Resp>>> operator() (
    std::shared_ptr<ClientBootstrap<Pipeline>> client) override {
    return Future<std::shared_ptr<Service<Req, Resp>>>(
      std::make_shared<ClientService>(client->getPipeline()));
  }
};

TEST(Wangle, ClientServerTest) {
  int port = 1234;
  // server

  ServerBootstrap<ServicePipeline> server;
  server.childPipeline(
    std::make_shared<ServerPipelineFactory<std::string, std::string>>());
  server.bind(port);

  // client
  auto client = std::make_shared<ClientBootstrap<ServicePipeline>>();
  ClientServiceFactory<ServicePipeline, std::string, std::string> serviceFactory;
  client->pipelineFactory(
    std::make_shared<ClientPipelineFactory<std::string, std::string>>());
  SocketAddress addr("127.0.0.1", port);
  client->connect(addr);
  auto service = serviceFactory(client).value();
  auto rep = (*service)("test");

  rep.then([&](std::string value) {
    EXPECT_EQ("test", value);
    EventBaseManager::get()->getEventBase()->terminateLoopSoon();

  });
  EventBaseManager::get()->getEventBase()->loopForever();
  server.stop();
  client.reset();
}

class AppendFilter : public ServiceFilter<std::string, std::string> {
 public:
  explicit AppendFilter(
    std::shared_ptr<Service<std::string, std::string>> service) :
      ServiceFilter<std::string, std::string>(service) {}

  Future<std::string> operator()(std::string req) override {
    return (*service_)(req + "\n");
  }
};

class IntToStringFilter
    : public ServiceFilter<int, int, std::string, std::string> {
 public:
  explicit IntToStringFilter(
    std::shared_ptr<Service<std::string, std::string>> service) :
      ServiceFilter<int, int, std::string, std::string>(service) {}

  Future<int> operator()(int req) override {
    return (*service_)(folly::to<std::string>(req)).then([](std::string resp) {
      return folly::to<int>(resp);
    });
  }
};

TEST(Wangle, FilterTest) {
  auto service = std::make_shared<EchoService>();
  auto filter = std::make_shared<AppendFilter>(service);
  auto result = (*filter)("test");
  EXPECT_EQ(result.value(), "test\n");
}

TEST(Wangle, ComplexFilterTest) {
  auto service = std::make_shared<EchoService>();
  auto filter = std::make_shared<IntToStringFilter>(service);
  auto result = (*filter)(1);
  EXPECT_EQ(result.value(), 1);
}

class ChangeTypeFilter
    : public ServiceFilter<int, std::string, std::string, int> {
 public:
  explicit ChangeTypeFilter(
    std::shared_ptr<Service<std::string, int>> service) :
      ServiceFilter<int, std::string, std::string, int>(service) {}

  Future<std::string> operator()(int req) override {
    return (*service_)(folly::to<std::string>(req)).then([](int resp) {
      return folly::to<std::string>(resp);
    });
  }
};

TEST(Wangle, SuperComplexFilterTest) {
  auto service = std::make_shared<EchoIntService>();
  auto filter = std::make_shared<ChangeTypeFilter>(service);
  auto result = (*filter)(1);
  EXPECT_EQ(result.value(), "1");
}

template <typename Pipeline, typename Req, typename Resp>
class ConnectionCountFilter : public ServiceFactoryFilter<Pipeline, Req, Resp> {
 public:
  explicit ConnectionCountFilter(
    std::shared_ptr<ServiceFactory<Pipeline, Req, Resp>> factory)
      : ServiceFactoryFilter<Pipeline, Req, Resp>(factory) {}

  Future<std::shared_ptr<Service<Req, Resp>>> operator()(
      std::shared_ptr<ClientBootstrap<Pipeline>> client) override {
      connectionCount++;
      return (*this->serviceFactory_)(client);
    }

  int connectionCount{0};
};

TEST(Wangle, ServiceFactoryFilter) {
  auto clientFactory =
    std::make_shared<
    ClientServiceFactory<ServicePipeline, std::string, std::string>>();
  auto countingFactory =
    std::make_shared<
    ConnectionCountFilter<ServicePipeline, std::string, std::string>>(
      clientFactory);

  auto client = std::make_shared<ClientBootstrap<ServicePipeline>>();

  client->pipelineFactory(
    std::make_shared<ClientPipelineFactory<std::string, std::string>>());
  // It doesn't matter if connect succeds or not, but it needs to be called
  // to create a pipeline
  client->connect(folly::SocketAddress("::1", 8090));

  auto service = (*countingFactory)(client).value();

  // After the first service goes away, the client can be reused
  service = (*countingFactory)(client).value();
  EXPECT_EQ(2, countingFactory->connectionCount);
}

TEST(Wangle, FactoryToService) {
  auto constfactory =
    std::make_shared<ConstFactory<ServicePipeline, std::string, std::string>>(
    std::make_shared<EchoService>());
  FactoryToService<ServicePipeline, std::string, std::string> service(
    constfactory);

  EXPECT_EQ("test", service("test").value());
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  return RUN_ALL_TESTS();
}

} // namespace
