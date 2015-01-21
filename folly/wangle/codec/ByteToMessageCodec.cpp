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
#include <folly/wangle/codec/ByteToMessageCodec.h>

namespace folly { namespace wangle {

void ByteToMessageCodec::read(Context* ctx, IOBufQueue& q) {
  size_t needed = 0;
  std::unique_ptr<IOBuf> result;
  while (true) {
    result = decode(ctx, q, needed);
    if (result) {
      q_.append(std::move(result));
      ctx->fireRead(q_);
    } else {
      break;
    }
  }
}

}} // namespace
