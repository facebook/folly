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
  using ToSend = std::vector<std::shared_ptr<folly::File>>;

 private:
  using FdsVariant = std::variant<Received, ToSend>;

 public:
  SocketFds() = default;
  SocketFds(SocketFds&& other) = default;
  template <class T>
  explicit SocketFds(T fds) {
    ptr_ = std::make_unique<FdsVariant>(std::move(fds));
  }

  SocketFds& operator=(SocketFds&& other) = default;

  bool empty() const { return !ptr_; }

  size_t size() const {
    if (empty()) {
      return 0;
    }
    return std::visit([](auto&& v) { return v.size(); }, *ptr_);
  }

  // These methods help ensure the right kind of data is being plumbed or
  // overwritten:
  //   fds.dcheckEmpty() = otherFds.dcheck{Received,ToSend}();
  SocketFds& dcheckEmpty() {
    DCHECK(!ptr_);
    return *this;
  }
  SocketFds& dcheckReceived() {
    DCHECK(std::holds_alternative<Received>(*ptr_));
    return *this;
  }
  SocketFds& dcheckToSend() {
    DCHECK(std::holds_alternative<ToSend>(*ptr_));
    return *this;
  }

  // Since ownership of `ToSend` FDs is shared, it is permissible to clone
  // `SocketFds` that are known to be in this state.
  //  - Invariant: `other` must be `ToSend`.
  //  - Cost: Cloning copies a vector into a new heap allocation.
  void cloneToSendFrom(const SocketFds& other) {
    if (!other.empty()) {
      auto* fds = CHECK_NOTNULL(std::get_if<ToSend>(other.ptr_.get()));
      ptr_ = std::make_unique<FdsVariant>(*fds);
    }
  }

  Received releaseReceived() {
    auto fds = std::move(*CHECK_NOTNULL(std::get_if<Received>(ptr_.get())));
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

  ToSend releaseToSend() {
    auto fds = std::move(*CHECK_NOTNULL(std::get_if<ToSend>(ptr_.get())));
    ptr_.reset();
    return fds;
  }

 private:
  std::unique_ptr<FdsVariant> ptr_;
};

} // namespace folly
