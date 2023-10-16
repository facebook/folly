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

#include <folly/File.h>
#include <folly/experimental/io/IoUringBackend.h>
#include <folly/experimental/io/Liburing.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>

namespace folly {

#if FOLLY_HAS_LIBURING

class IoUringEvent : public EventHandler, public EventBase::LoopCallback {
 public:
  IoUringEvent(
      folly::EventBase* eventBase,
      IoUringBackend::Options const& o,
      bool use_event_fd = true);
  ~IoUringEvent() override;

  // cannot move/copy due to postLoopCallback
  IoUringEvent const& operator=(IoUringEvent const&) = delete;
  IoUringEvent&& operator=(IoUringEvent&&) = delete;
  IoUringEvent(IoUringEvent&&) = delete;
  IoUringEvent(IoUringEvent const&) = delete;

  void handlerReady(uint16_t events) noexcept override;

  void runLoopCallback() noexcept override;

  IoUringBackend& backend() { return backend_; }

 private:
  bool hasWork();
  EventBase* eventBase_;
  IoUringBackend backend_;

  bool lastWasResignalled_ = false;
  bool edgeTriggered_ = false;
  std::optional<File> eventFd_;
};

#endif

} // namespace folly
