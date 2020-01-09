/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <poll.h>
#include <sys/types.h>

#include <chrono>
#include <map>
#include <vector>

#include <boost/intrusive/list.hpp>

#include <folly/CPortability.h>
#include <folly/CppAttributes.h>
#include <folly/io/async/EventBaseBackendBase.h>

namespace folly {

class PollIoBackend : public EventBaseBackendBase {
 public:
  explicit PollIoBackend(size_t capacity, size_t maxSubmit, size_t maxGet);
  ~PollIoBackend() override;

  // from EventBaseBackendBase
  event_base* getEventBase() override {
    return nullptr;
  }

  int eb_event_base_loop(int flags) override;
  int eb_event_base_loopbreak() override;

  int eb_event_add(Event& event, const struct timeval* timeout) override;
  int eb_event_del(Event& event) override;

 protected:
  enum class WaitForEventsMode { WAIT, DONT_WAIT };
  struct IoCb;

  struct IoCb
      : public boost::intrusive::list_base_hook<
            boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {
    using BackendCb = void(PollIoBackend*, IoCb*, int64_t);

    explicit IoCb(PollIoBackend* backend, bool poolAlloc = true)
        : backend_(backend), poolAlloc_(poolAlloc) {}
    virtual ~IoCb() = default;

    PollIoBackend* backend_;
    BackendCb* backendCb_{nullptr};
    const bool poolAlloc_;
    IoCb* next_{nullptr}; // this is for the free list
    Event* event_{nullptr};
    size_t useCount_{0};

    FOLLY_ALWAYS_INLINE void resetEvent() {
      // remove it from the list
      unlink();
      if (event_) {
        event_->setUserData(nullptr);
        event_ = nullptr;
      }
    }

    virtual void prepPollAdd(void* entry, int fd, uint32_t events) = 0;
  };

  using IoCbList =
      boost::intrusive::list<IoCb, boost::intrusive::constant_time_size<false>>;

  struct TimerEntry {
    explicit TimerEntry(Event* event) : event_(event) {}
    TimerEntry(Event* event, const struct timeval& timeout);
    Event* event_{nullptr};
    std::chrono::time_point<std::chrono::steady_clock> expireTime_;

    bool operator==(const TimerEntry& other) {
      return event_ == other.event_;
    }

    std::chrono::microseconds getRemainingTime() const {
      auto now = std::chrono::steady_clock::now();
      if (expireTime_ > now) {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            expireTime_ - now);
      }

      return std::chrono::microseconds(0);
    }

    static bool isExpired(
        const std::chrono::time_point<std::chrono::steady_clock>& timestamp) {
      return (std::chrono::steady_clock::now() >= timestamp);
    }

    void setExpireTime(const struct timeval& timeout) {
      uint64_t us = static_cast<uint64_t>(timeout.tv_sec) *
              static_cast<uint64_t>(1000000) +
          static_cast<uint64_t>(timeout.tv_usec);

      expireTime_ =
          std::chrono::steady_clock::now() + std::chrono::microseconds(us);
    }
  };

  static FOLLY_ALWAYS_INLINE uint32_t getPollFlags(short events) {
    uint32_t ret = 0;
    if (events & EV_READ) {
      ret |= POLLIN;
    }

    if (events & EV_WRITE) {
      ret |= POLLOUT;
    }

    return ret;
  }

  static FOLLY_ALWAYS_INLINE short getPollEvents(uint32_t flags, short events) {
    short ret = 0;
    if (flags & POLLIN) {
      ret |= EV_READ;
    }

    if (flags & POLLOUT) {
      ret |= EV_WRITE;
    }

    if (flags & (POLLERR | POLLHUP)) {
      ret |= (EV_READ | EV_WRITE);
    }

    ret &= events;

    return ret;
  }

  // timer processing
  bool addTimerFd();
  void scheduleTimeout();
  void scheduleTimeout(const std::chrono::microseconds& us);
  void addTimerEvent(Event& event, const struct timeval* timeout);
  void removeTimerEvent(Event& event);
  size_t processTimers();
  FOLLY_ALWAYS_INLINE void setProcessTimers() {
    processTimers_ = true;
  }

  size_t processActiveEvents();

  static void processPollIoCb(PollIoBackend* backend, IoCb* ioCb, int64_t res) {
    backend->processPollIo(ioCb, res);
  }

  static void processTimerIoCb(
      PollIoBackend* backend,
      IoCb* /*unused*/,
      int64_t /*unused*/) {
    backend->setProcessTimers();
  }

  void processPollIo(IoCb* ioCb, int64_t res) noexcept;

  IoCb* FOLLY_NULLABLE allocIoCb();
  void releaseIoCb(IoCb* aioIoCb);

  virtual IoCb* allocNewIoCb() = 0;

  virtual void* allocSubmissionEntry() = 0;
  virtual int getActiveEvents(WaitForEventsMode waitForEvents) = 0;
  virtual size_t submitList(
      IoCbList& ioCbs,
      WaitForEventsMode waitForEvents) = 0;
  virtual int submitOne(IoCb* ioCb) = 0;
  virtual int cancelOne(IoCb* ioCb) = 0;

  int eb_event_modify_inserted(Event& event, IoCb* ioCb);

  FOLLY_ALWAYS_INLINE size_t numIoCbInUse() const {
    return numIoCbInUse_;
  }

  size_t capacity_;
  size_t numEntries_;
  IoCb* timerEntry_{nullptr};
  IoCb* freeHead_{nullptr};

  // timer related
  int timerFd_{-1};
  bool timerChanged_{false};
  std::map<
      std::chrono::time_point<std::chrono::steady_clock>,
      std::vector<TimerEntry>>
      timers_;
  std::map<Event*, std::chrono::time_point<std::chrono::steady_clock>>
      eventToTimers_;

  // submit
  size_t maxSubmit_;
  IoCbList submitList_;

  // process
  size_t maxGet_;

  // loop related
  bool loopBreak_{false};
  bool shuttingDown_{false};
  bool processTimers_{false};
  size_t numInsertedEvents_{0};
  IoCbList activeEvents_;
  // number of IoCb instances in use
  size_t numIoCbInUse_{0};
};
} // namespace folly
