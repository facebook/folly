/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/io/IOBufIovecBuilder.h>
#include <folly/portability/IOVec.h>

namespace folly {
size_t IOBufIovecBuilder::allocateBuffers(IoVecVec& iovs, size_t len) {
  iovs.clear();
  size_t total = 0;
  for (size_t i = 0; (i < IOV_MAX) && (total < len); ++i) {
    DCHECK_LE(i, buffers_.size());
    if (i == buffers_.size()) {
      // TODO - allocate RefCountMem and the main buffer together
      // if it does not cause the allocation to spill into the next
      // jemalloc allocation class
      buffers_.push_back(new RefCountMem(options_.blockSize_));
    }
    DCHECK_LT(i, buffers_.size());

    struct iovec iov;
    iov.iov_base = buffers_[i]->usableMem();
    iov.iov_len = buffers_[i]->usableSize();
    iovs.emplace_back(iov);

    total += buffers_[i]->usableSize();
  }

  return total;
}

std::unique_ptr<folly::IOBuf> IOBufIovecBuilder::extractIOBufChain(size_t len) {
  std::unique_ptr<folly::IOBuf> ioBuf;
  std::unique_ptr<folly::IOBuf> tmp;

  while (len > 0) {
    CHECK(!buffers_.empty());
    auto* buf = buffers_.front();
    auto size = buf->usableSize();

    if (len >= size) {
      // no need to inc the ref count since we're transferring ownership
      tmp = folly::IOBuf::takeOwnership(
          buf->usableMem(), size, RefCountMem::freeMem, buf);
      buffers_.pop_front();
      len -= size;
    } else {
      buf->addRef();
      tmp = folly::IOBuf::takeOwnership(
          buf->usableMem(), len, RefCountMem::freeMem, buf);
      buf->incUsedMem(len);
      len = 0;
    }

    CHECK(!tmp->isShared());

    if (ioBuf) {
      ioBuf->prependChain(std::move(tmp));
    } else {
      ioBuf = std::move(tmp);
    }
  }

  return ioBuf;
}
} // namespace folly
