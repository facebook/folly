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
 * Dispatch a request, satisfying Promise `p` with the response;
 * the returned Future is satisfied when the response is received:
 * only one request is allowed at a time.
 */
template <typename Pipeline, typename Req, typename Resp = Req>
class SerialClientDispatcher : public HandlerAdapter<Req, Resp>
                             , public Service<Req, Resp> {
 public:

  typedef typename HandlerAdapter<Req, Resp>::Context Context;

  void setPipeline(Pipeline* pipeline) {
    pipeline_ = pipeline;
    pipeline->addBack(this);
    pipeline->finalize();
  }

  void read(Context* ctx, Req in) override {
    DCHECK(p_);
    p_->setValue(std::move(in));
    p_ = none;
  }

  virtual Future<Resp> operator()(Req arg) override {
    CHECK(!p_);
    DCHECK(pipeline_);

    p_ = Promise<Resp>();
    auto f = p_->getFuture();
    pipeline_->write(std::move(arg));
    return f;
  }

 private:
  Pipeline* pipeline_{nullptr};
  folly::Optional<Promise<Resp>> p_;
};

}} // namespace
