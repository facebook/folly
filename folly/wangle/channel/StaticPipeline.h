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

#include <folly/wangle/channel/Pipeline.h>

namespace folly { namespace wangle {

/*
 * StaticPipeline allows you to create a Pipeline with minimal allocations.
 * Specify your handlers after the input/output types of your Pipeline in order
 * from front to back, and construct with either H&&, H*, or std::shared_ptr<H>
 * for each handler. The pipeline will be finalized for you at the end of
 * construction. For example:
 *
 * StringToStringHandler stringHandler1;
 * auto stringHandler2 = std::make_shared<StringToStringHandler>();
 *
 * StaticPipeline<int, std::string,
 *   IntToStringHandler,
 *   StringToStringHandler,
 *   StringToStringHandler>(
 *     IntToStringHandler(),  // H&&
 *     &stringHandler1,       // H*
 *     stringHandler2)        // std::shared_ptr<H>
 * pipeline;
 *
 * You can then use pipeline just like any Pipeline. See Pipeline.h.
 */
template <class R, class W, class... Handlers>
class StaticPipeline;

template <class R, class W>
class StaticPipeline<R, W> : public Pipeline<R, W> {
 protected:
  explicit StaticPipeline(bool) : Pipeline<R, W>(true) {}
};

template <class R, class W, class Handler, class... Handlers>
class StaticPipeline<R, W, Handler, Handlers...>
    : public StaticPipeline<R, W, Handlers...> {
 public:
  template <class... HandlerArgs>
  explicit StaticPipeline(HandlerArgs&&... handlers)
    : StaticPipeline(true, std::forward<HandlerArgs>(handlers)...) {
    isFirst_ = true;
  }

  ~StaticPipeline() {
    if (isFirst_) {
      Pipeline<R, W>::detachHandlers();
    }
  }

 protected:
  typedef ContextImpl<Pipeline<R, W>, Handler> Context;

  template <class HandlerArg, class... HandlerArgs>
  StaticPipeline(
      bool isFirst,
      HandlerArg&& handler,
      HandlerArgs&&... handlers)
    : StaticPipeline<R, W, Handlers...>(
          false,
          std::forward<HandlerArgs>(handlers)...) {
    isFirst_ = isFirst;
    setHandler(std::forward<HandlerArg>(handler));
    CHECK(handlerPtr_);
    ctx_.initialize(this, handlerPtr_);
    Pipeline<R, W>::addContextFront(&ctx_);
    if (isFirst_) {
      Pipeline<R, W>::finalize();
    }
  }

 private:
  template <class HandlerArg>
  typename std::enable_if<std::is_same<
    typename std::remove_reference<HandlerArg>::type,
    Handler
  >::value>::type
  setHandler(HandlerArg&& arg) {
    handler_.emplace(std::forward<HandlerArg>(arg));
    handlerPtr_ = std::shared_ptr<Handler>(&(*handler_), [](Handler*){});
  }

  template <class HandlerArg>
  typename std::enable_if<std::is_same<
    typename std::decay<HandlerArg>::type,
    std::shared_ptr<Handler>
  >::value>::type
  setHandler(HandlerArg&& arg) {
    handlerPtr_ = std::forward<HandlerArg>(arg);
  }

  template <class HandlerArg>
  typename std::enable_if<std::is_same<
    typename std::decay<HandlerArg>::type,
    Handler*
  >::value>::type
  setHandler(HandlerArg&& arg) {
    handlerPtr_ = std::shared_ptr<Handler>(arg, [](Handler*){});
  }

  bool isFirst_;
  folly::Optional<Handler> handler_;
  std::shared_ptr<Handler> handlerPtr_;
  ContextImpl<Pipeline<R, W>, Handler> ctx_;
};

}} // folly::wangle
