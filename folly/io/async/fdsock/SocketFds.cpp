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

#include "SocketFds.h"

namespace folly {

void SocketFds::cloneToSendFromOrDfatal(const SocketFds& other) {
  if (other.empty()) {
    ptr_.reset();
  } else {
    auto* fds = std::get_if<ToSendPair>(other.ptr_.get());
    if (UNLIKELY(fds == nullptr)) {
      LOG(DFATAL) << "SocketFds was in 'received' state, not cloning";
      ptr_.reset();
    } else {
      // Cloning is only for "multi-publisher" scenarios.  In these cases
      // we must first clone, and then bind a socket sequence number.
      DCHECK_EQ(kNoSeqNum, fds->second)
          << "Cannot clone SocketFds once it was bound to a socket";
      ptr_ = std::make_unique<FdsVariant>(*fds);
    }
  }
}

SocketFds::Received SocketFds::releaseReceived() {
  auto fds =
      std::move(CHECK_NOTNULL(std::get_if<ReceivedPair>(ptr_.get()))->first);
  // NB: In the case of a Thrift server handler method that is receiving
  // and then sending back FDs using the same `SocketFds` object, this
  // deallocation (and subsequent allocation) could be avoided, e.g. by:
  //  - Without changing the API by having an additional `std::monostate`
  //    representing the variant being empty. This has the downside of
  //    holding on to allocations unnecessarily in other cases.
  //  - By adding `std::pair<Received, ToSend&> releaseReceivedAndSend()`.
  //    This complicates the user experience.
  ptr_.reset();
  return fds;
}

SocketFds::ToSend SocketFds::releaseToSend() {
  auto fds =
      std::move(CHECK_NOTNULL(std::get_if<ToSendPair>(ptr_.get()))->first);
  ptr_.reset();
  return fds;
}

void SocketFds::setFdSocketSeqNumOnce(int64_t seqNum) {
  // The type has to be `int64_t` because Thrift IDL only supports signed.
  DCHECK_GE(seqNum, 0) << "Sequence number must be nonnegative";
  if (LIKELY(ptr_ != nullptr)) {
    std::visit(
        [seqNum](auto&& v) {
          DCHECK_EQ(kNoSeqNum, v.second) << "Can only set sequence number once";
          v.second = seqNum;
        },
        *ptr_);
  } else {
    LOG(DFATAL) << "Cannot set sequence number on empty SocketFds";
  }
}

int64_t SocketFds::getFdSocketSeqNum() const {
  if (LIKELY(ptr_ != nullptr)) {
    auto seqNum = std::visit([](auto&& v) { return v.second; }, *ptr_);
    if (seqNum >= 0) {
      return seqNum;
    }
    if (seqNum != kNoSeqNum) {
      LOG(DFATAL) << "Sequence number in invalid state: " << seqNum;
    }
  } else {
    LOG(DFATAL) << "Cannot query sequence number of empty SocketFds";
  }
  return kNoSeqNum;
}

} // namespace folly
