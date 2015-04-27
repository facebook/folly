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
#pragma once

#include <folly/futures/Future.h>
#include <folly/Memory.h>

#include <folly/wangle/bootstrap/ServerBootstrap.h>
#include <folly/wangle/bootstrap/ClientBootstrap.h>
#include <folly/wangle/channel/Pipeline.h>
#include <folly/wangle/channel/AsyncSocketHandler.h>

namespace folly {

/**
 * A Service is an asynchronous function from Request to
 * Future<Response>. It is the basic unit of the RPC interface.
 */
template <typename Req, typename Resp = Req>
class Service {
 public:
  virtual Future<Resp> operator()(Req request) = 0;
  virtual ~Service() {}
};

/**
 * A Filter acts as a decorator/transformer of a service. It may apply
 * transformations to the input and output of that service:
 *
 *          class MyService
 *
 * ReqA  -> |
 *          | -> ReqB
 *          | <- RespB
 * RespA <- |
 *
 * For example, you may have a service that takes Strings and parses
 * them as Ints.  If you want to expose this as a Network Service via
 * Thrift, it is nice to isolate the protocol handling from the
 * business rules. Hence you might have a Filter that converts back
 * and forth between Thrift structs:
 *
 * [ThriftIn -> (String  ->  Int) -> ThriftOut]
 */
template <typename ReqA, typename RespA,
          typename ReqB = ReqA, typename RespB = RespA>
class Filter {
  public:
  virtual Future<RespA> operator()(
    Service<ReqB, RespB>* service, ReqA request) = 0;
  std::unique_ptr<Service<ReqA, RespA>> compose(
    Service<ReqB, RespB>* service);
  virtual ~Filter() {}
};

template <typename ReqA, typename RespA,
          typename ReqB = ReqA, typename RespB = RespA>
class ComposedService : public Service<ReqA, RespA> {
  public:
  ComposedService(Service<ReqB, RespB>* service,
                  Filter<ReqA, RespA, ReqB, RespB>* filter)
    : service_(service)
    , filter_(filter) {}
  virtual Future<RespA> operator()(ReqA request) override {
    return (*filter_)(service_, request);
  }

  ~ComposedService(){}
  private:
  Service<ReqB, RespB>* service_;
  Filter<ReqA, RespA, ReqB, RespB>* filter_;
};

template <typename ReqA, typename RespA,
          typename ReqB, typename RespB>
  std::unique_ptr<Service<ReqA, RespA>>
    Filter<ReqA, RespA, ReqB, RespB>::compose(Service<ReqB, RespB>* service) {
  return  folly::make_unique<ComposedService<ReqA, RespA, ReqB, RespB>>(
    service, this);
}

/**
 * A factory that creates services, given a client.  This lets you
 * make RPC calls on the Service interface over a client's pipeline.
 *
 * Clients can be reused after you are done using the service.
 */
template <typename Pipeline, typename Req, typename Resp>
class ServiceFactory {
 public:
  virtual Future<Service<Req, Resp>*> operator()(
    ClientBootstrap<Pipeline>* client) = 0;

  virtual ~ServiceFactory() = default;
};

} // namespace
