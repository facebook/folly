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
  virtual std::unique_ptr<IOBuf> decode(
    Context* ctx, IOBufQueue& buf, size_t&) {
    return buf.move();
  }
};

class EchoService : public Service<std::string, std::string> {
 public:
  virtual Future<std::string> operator()(std::string req) override {
    return makeFuture<std::string>(std::move(req));
  }
};

class EchoIntService : public Service<std::string, int> {
 public:
  virtual Future<int> operator()(std::string req) override {
    return makeFuture<int>(folly::to<int>(req));
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

  Future<Service<Req, Resp>*> operator()(
      ClientBootstrap<Pipeline>* client) override {
    return makeFuture<Service<Req, Resp>*>(
      new ClientService(client->getPipeline()));
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
  auto service = std::shared_ptr<Service<std::string, std::string>>(
    serviceFactory(client.get()).value());
  auto rep = (*service)("test");

  rep.then([&](std::string value) {
    EXPECT_EQ("test", value);
    EventBaseManager::get()->getEventBase()->terminateLoopSoon();

  });
  EventBaseManager::get()->getEventBase()->loopForever();
  server.stop();
  client.reset();
}

class AppendFilter : public Filter<std::string, std::string> {
 public:
  virtual Future<std::string> operator()(
    Service<std::string, std::string>* service, std::string req) {
    return (*service)(req + "\n");
  }
};

class IntToStringFilter : public Filter<int, int, std::string, std::string> {
 public:
  virtual Future<int> operator()(
    Service<std::string, std::string>* service, int req) {
    return (*service)(folly::to<std::string>(req)).then([](std::string resp) {
      return folly::to<int>(resp);
    });
  }
};

TEST(Wangle, FilterTest) {
  auto service = folly::make_unique<EchoService>();
  auto filter = folly::make_unique<AppendFilter>();
  auto result = (*filter)(service.get(), "test");
  EXPECT_EQ(result.value(), "test\n");

  // Check composition
  auto composed_service = filter->compose(service.get());
  auto result2 = (*composed_service)("test");
  EXPECT_EQ(result2.value(), "test\n");
}

TEST(Wangle, ComplexFilterTest) {
  auto service = folly::make_unique<EchoService>();
  auto filter = folly::make_unique<IntToStringFilter>();
  auto result = (*filter)(service.get(), 1);
  EXPECT_EQ(result.value(), 1);

  // Check composition
  auto composed_service = filter->compose(service.get());
  auto result2 = (*composed_service)(2);
  EXPECT_EQ(result2.value(), 2);
}

class ChangeTypeFilter : public Filter<int, std::string, std::string, int> {
 public:
  virtual Future<std::string> operator()(
    Service<std::string, int>* service, int req) {
    return (*service)(folly::to<std::string>(req)).then([](int resp) {
      return folly::to<std::string>(resp);
    });
  }
};

TEST(Wangle, SuperComplexFilterTest) {
  auto service = folly::make_unique<EchoIntService>();
  auto filter = folly::make_unique<ChangeTypeFilter>();
  auto result = (*filter)(service.get(), 1);
  EXPECT_EQ(result.value(), "1");

  // Check composition
  auto composed_service = filter->compose(service.get());
  auto result2 = (*composed_service)(2);
  EXPECT_EQ(result2.value(), "2");
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  return RUN_ALL_TESTS();
}

} // namespace
