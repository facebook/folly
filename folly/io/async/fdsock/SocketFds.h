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

#pragma once

#include <memory>
#include <optional>
#include <variant>
#include <glog/logging.h>

#include <folly/File.h>

namespace folly {

/**
 * Represents an ordered collection of file descriptors. This union type
 * either contains:
 *  - FDs to be sent on a socket -- with shared ownership, since the sender
 *    may still need them, OR
 *  - FDs just received, with sole ownership.
 *
 * This hides the variant of containers behind a `unique_ptr` so that the
 * normal / fast path, which is not passing FDs, is as light as possible,
 * adding just 8 bytes for a pointer.
 *
 * == Rationale ==
 *
 * In order to send FDs over Unix sockets, Thrift plumbs them through a
 * variety of classes.  Many of these can be used both for send & receive
 * operations (THeader, StreamPayload, etc).
 *
 * This is especially useful in regular Thrift request-response handler
 * methods, where the same THeader is used for consuming the received FDs,
 * and sending back FDs with the response -- necessarily in that order.
 */
class SocketFds final {
 public:
  using Received = std::vector<folly::File>;
  // These `shared_ptr`s are commonly be shared across threads -- and there
  // is no locking -- so `const` ensures thread-safety.  Therefore, if
  // you're going to put your `File` into a `ToSend`, it's best to first
  // make your own copy `const`, too.
  using ToSend = std::vector<std::shared_ptr<const folly::File>>;

  // Must be signed because Thrift lacks unsigned ints, for RpcMetadata.thrift
  using SeqNum = int64_t;
  // FD sequence numbers are nonnegative.  This represents "none was set";
  // also used for some DFATAL errors.
  static constexpr SeqNum kNoSeqNum = -1;

 private:
  using ReceivedPair = std::pair<Received, SeqNum>;
  using ToSendPair = std::pair<ToSend, SeqNum>;
  using FdsVariant = std::variant<ReceivedPair, ToSendPair>;

 public:
  SocketFds() = default;
  SocketFds(SocketFds&& other) = default;
  template <class T>
  explicit SocketFds(T fds) {
    // Representation invariant: when `SocketFds.empty()`, there's no backing
    // allocation.
    if (fds.size() > 0) {
      ptr_ = std::make_unique<FdsVariant>(
          std::make_pair(std::move(fds), kNoSeqNum));
    }
  }

  SocketFds& operator=(SocketFds&& other) = default;

  bool empty() const { return !ptr_; }

  size_t size() const {
    if (!ptr_) {
      return 0;
    }
    return std::visit([](auto&& v) { return v.first.size(); }, *ptr_);
  }

  // These methods help ensure the right kind of data is being plumbed or
  // overwritten:
  //   fds.dcheckEmpty() = otherFds.dcheck{Received,ToSend}();
  SocketFds& dcheckEmpty() {
    DCHECK(!ptr_);
    return *this;
  }
  SocketFds& dcheckReceivedOrEmpty() {
    DCHECK(!ptr_ || std::holds_alternative<ReceivedPair>(*ptr_));
    return *this;
  }
  SocketFds& dcheckToSendOrEmpty() {
    DCHECK(!ptr_ || std::holds_alternative<ToSendPair>(*ptr_));
    return *this;
  }

  // Since ownership of `ToSend` FDs is shared, it is permissible to clone
  // `SocketFds` that are known to be in this state.
  //  - Invariant: `other` must be `ToSend`.
  //  - Cost: Cloning copies a vector into a new heap allocation.
  void cloneToSendFromOrDfatal(const SocketFds& other);

  Received releaseReceived(); // Fatals if `this` is not `Received`
  // Returns `nullopt` if `this` is not `ToSend`
  std::optional<ToSendPair> releaseToSendAndSeqNum();

  // Socket FDs need to be associated with socket data messages. Doing so
  // correctly through the many layers of Folly & Thrift can be tricky --
  // certain code bugs could cause the order in which FDs are sent to
  // deviate from the order in which they are popped off the socket queue
  // by the receiver.
  //
  // Operating on the wrong FD can lead to data loss or data corruption, so
  // in order to detect such bugs, we include FD sequence numbers in the
  // metadata of each message.
  //
  // The semantics of these methods are like so:
  //  - A brand new `SocketFds`, or one that has been `release*`d has
  //    no sequence number -- i.e. `kNoSeqNum`.
  //  - A nonnegative sequence number can be attached to a `SocketFds`
  //    that has none, to be obtained via `AsyncFdSocket`.
  //  - It is a DFATAL error to replace one sequence number by another.
  void setFdSocketSeqNumOnce(SeqNum seqNum);
  SeqNum getFdSocketSeqNum() const; // Non-negative, or `kNoSeqNum`

 private:
  std::unique_ptr<FdsVariant> ptr_;
};

} // namespace folly
