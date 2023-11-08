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

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>
#include <folly/io/IOBuf.h>
#include <folly/io/async/DelayedDestruction.h>

struct io_uring_sqe;

namespace folly {

class IoUringBackend;

struct IoSqeBase
    : boost::intrusive::list_base_hook<
          boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {
  enum class Type {
    Unknown,
    Read,
    Write,
    Open,
    Close,
    Connect,
    Cancel,
  };

  IoSqeBase() : IoSqeBase(Type::Unknown) {}
  explicit IoSqeBase(Type type) : type_(type) {}
  // use raw addresses, so disallow copy/move
  IoSqeBase(IoSqeBase&&) = delete;
  IoSqeBase(const IoSqeBase&) = delete;
  IoSqeBase& operator=(IoSqeBase&&) = delete;
  IoSqeBase& operator=(const IoSqeBase&) = delete;

  virtual ~IoSqeBase() = default;
  virtual void processSubmit(struct io_uring_sqe* sqe) noexcept = 0;
  virtual void callback(int res, uint32_t flags) noexcept = 0;
  virtual void callbackCancelled(int res, uint32_t flags) noexcept = 0;
  IoSqeBase::Type type() const { return type_; }
  bool inFlight() const { return inFlight_; }
  bool cancelled() const { return cancelled_; }
  void markCancelled() { cancelled_ = true; }

 protected:
  // This is used if you want to prepare this sqe for reuse, but will manage the
  // lifetime. For example for zerocopy send, you might want to reuse the sqe
  // but still have a notification inbound.
  void prepareForReuse() { internalUnmarkInflight(); }

 private:
  friend class IoUringBackend;
  void internalSubmit(struct io_uring_sqe* sqe) noexcept;
  void internalCallback(int res, uint32_t flags) noexcept;
  void internalUnmarkInflight() { inFlight_ = false; }

  bool inFlight_ = false;
  bool cancelled_ = false;
  Type type_;
};

class IoUringBufferProviderBase {
 protected:
  uint16_t const gid_;
  size_t const sizePerBuffer_;

 public:
  struct Deleter {
    void operator()(IoUringBufferProviderBase* base) {
      if (base) {
        base->destroy();
      }
    }
  };

  using UniquePtr = std::unique_ptr<IoUringBufferProviderBase, Deleter>;
  explicit IoUringBufferProviderBase(uint16_t gid, size_t sizePerBuffer)
      : gid_(gid), sizePerBuffer_(sizePerBuffer) {}
  virtual ~IoUringBufferProviderBase() = default;

  IoUringBufferProviderBase(IoUringBufferProviderBase&&) = delete;
  IoUringBufferProviderBase(IoUringBufferProviderBase const&) = delete;
  IoUringBufferProviderBase& operator=(IoUringBufferProviderBase&&) = delete;
  IoUringBufferProviderBase& operator=(IoUringBufferProviderBase const&) =
      delete;

  size_t sizePerBuffer() const { return sizePerBuffer_; }
  uint16_t gid() const { return gid_; }

  virtual uint32_t count() const noexcept = 0;
  virtual void unusedBuf(uint16_t i) noexcept = 0;
  virtual std::unique_ptr<IOBuf> getIoBuf(
      uint16_t i, size_t length) noexcept = 0;
  virtual void enobuf() noexcept = 0;
  virtual bool available() const noexcept = 0;
  virtual void destroy() noexcept = 0;
};

struct IoUringFdRegistrationRecord : public boost::intrusive::slist_base_hook<
                                         boost::intrusive::cache_last<false>> {
  int count_{0};
  int fd_{-1};
  int idx_{0};
};

} // namespace folly
