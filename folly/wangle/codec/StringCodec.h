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

namespace folly { namespace wangle {

/*
 * StringCodec converts a pipeline from IOBufs to std::strings.
 */
class StringCodec : public Handler<IOBufQueue&, std::string,
                                   std::string, std::unique_ptr<IOBuf>> {
 public:
  typedef typename Handler<
   IOBufQueue&, std::string,
   std::string, std::unique_ptr<IOBuf>>::Context Context;

  void read(Context* ctx, IOBufQueue& q) override {
    auto buf = q.pop_front();
    buf->coalesce();
    std::string data((const char*)buf->data(), buf->length());

    ctx->fireRead(data);
  }

  Future<void> write(Context* ctx, std::string msg) override {
    auto buf = IOBuf::copyBuffer(msg.data(), msg.length());
    return ctx->fireWrite(std::move(buf));
  }
};

}} // namespace
