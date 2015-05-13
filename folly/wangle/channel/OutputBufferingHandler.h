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

#include <folly/futures/SharedPromise.h>
#include <folly/wangle/channel/Handler.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>

namespace folly { namespace wangle {

/*
 * OutputBufferingHandler buffers writes in order to minimize syscalls. The
 * transport will be written to once per event loop instead of on every write.
 *
 * This handler may only be used in a single Pipeline.
 */
class OutputBufferingHandler : public OutboundBytesToBytesHandler,
                               protected EventBase::LoopCallback {
 public:
  Future<void> write(Context* ctx, std::unique_ptr<IOBuf> buf) override {
    CHECK(buf);
    if (!queueSends_) {
      return ctx->fireWrite(std::move(buf));
    } else {
      // Delay sends to optimize for fewer syscalls
      if (!sends_) {
        DCHECK(!isLoopCallbackScheduled());
        // Buffer all the sends, and call writev once per event loop.
        sends_ = std::move(buf);
        ctx->getTransport()->getEventBase()->runInLoop(this);
      } else {
        DCHECK(isLoopCallbackScheduled());
        sends_->prependChain(std::move(buf));
      }
      return sharedPromise_.getFuture();
    }
  }

  void runLoopCallback() noexcept override {
    MoveWrapper<SharedPromise<void>> sharedPromise;
    std::swap(*sharedPromise, sharedPromise_);
    getContext()->fireWrite(std::move(sends_))
      .then([sharedPromise](Try<void> t) mutable {
        sharedPromise->setTry(std::move(t));
      });
  }

  Future<void> close(Context* ctx) override {
    if (isLoopCallbackScheduled()) {
      cancelLoopCallback();
    }

    // If there are sends queued, cancel them
    sharedPromise_.setException(
      folly::make_exception_wrapper<std::runtime_error>(
        "close() called while sends still pending"));
    sends_.reset();
    sharedPromise_ = SharedPromise<void>();
    return ctx->fireClose();
  }

  SharedPromise<void> sharedPromise_;
  std::unique_ptr<IOBuf> sends_{nullptr};
  bool queueSends_{true};
};

}}
