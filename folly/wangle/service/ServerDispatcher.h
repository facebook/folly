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

#include <folly/wangle/channel/Handler.h>
#include <folly/wangle/service/Service.h>

namespace folly { namespace wangle {

/**
 * Dispatch requests from pipeline one at a time synchronously.
 * Concurrent requests are queued in the pipeline.
 */
template <typename Req, typename Resp = Req>
class SerialServerDispatcher : public HandlerAdapter<Req, Resp> {
 public:

  typedef typename HandlerAdapter<Req, Resp>::Context Context;

  explicit SerialServerDispatcher(Service<Req, Resp>* service)
      : service_(service) {}

  void read(Context* ctx, Req in) override {
    auto resp = (*service_)(std::move(in)).get();
    ctx->fireWrite(std::move(resp));
  }

 private:

  Service<Req, Resp>* service_;
};

}} // namespace
