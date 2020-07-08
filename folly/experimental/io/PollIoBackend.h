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
#include <set>
#include <vector>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/slist.hpp>

#include <folly/CPortability.h>
#include <folly/CppAttributes.h>
#include <folly/io/async/EventBaseBackendBase.h>

namespace folly {

class PollIoBackend : public EventBaseBackendBase {
 public:
  struct Options {
    Options() = default;

    Options& setCapacity(size_t v) {
      capacity = v;

      return *this;
    }

    Options& setMaxSubmit(size_t v) {
      maxSubmit = v;

      return *this;
    }

    Options& setMaxGet(size_t v) {
      maxGet = v;

      return *this;
    }

    Options& setUseRegisteredFds(bool v) {
      useRegisteredFds = v;

      return *this;
    }

    size_t capacity{0};
    size_t maxSubmit{128};
    size_t maxGet{static_cast<size_t>(-1)};
    bool useRegisteredFds{false};
  };

  explicit PollIoBackend(Options options);
  ~PollIoBackend() override;

  // from EventBaseBackendBase
  event_base* getEventBase() override {
    return nullptr;
  }

  int eb_event_base_loop(int flags) override;
  int eb_event_base_loopbreak() override;

  int eb_event_add(Event& event, const struct timeval* timeout) override;
  int eb_event_del(Event& event) override;

  struct FdRegistrationRecord : public boost::intrusive::slist_base_hook<
                                    boost::intrusive::cache_last<false>> {
    int count_{0};
    int fd_{-1};
    size_t idx_{0};
  };

  virtual FdRegistrationRecord* registerFd(int /*fd*/) {
    return nullptr;
  }

  virtual bool unregisterFd(FdRegistrationRecord* /*rec*/) {
    return false;
  }

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

    virtual void processSubmit(void* entry) = 0;

    virtual void processActive() {}

    PollIoBackend* backend_;
    BackendCb* backendCb_{nullptr};
    const bool poolAlloc_;
    Event* event_{nullptr};
    FdRegistrationRecord* fdRecord_{nullptr};
    size_t useCount_{0};

    FOLLY_ALWAYS_INLINE void resetEvent() {
      // remove it from the list
      unlink();
      if (event_) {
        event_->setUserData(nullptr);
        event_ = nullptr;
      }
    }

    virtual void
    prepPollAdd(void* entry, int fd, uint32_t events, bool registerFd) = 0;

    struct EventCallbackData {
      EventCallback::Type type_{EventCallback::Type::TYPE_NONE};
      union {
        EventReadCallback::IoVec* ioVec_;
        EventRecvmsgCallback::MsgHdr* msgHdr_;
      };

      void set(EventReadCallback::IoVec* ioVec) {
        type_ = EventCallback::Type::TYPE_READ;
        ioVec_ = ioVec;
      }

      void set(EventRecvmsgCallback::MsgHdr* msgHdr) {
        type_ = EventCallback::Type::TYPE_RECVMSG;
        msgHdr_ = msgHdr;
      }

      void reset() {
        type_ = EventCallback::Type::TYPE_NONE;
      }

      bool processCb(int res) {
        bool ret = false;
        switch (type_) {
          case EventCallback::Type::TYPE_READ: {
            ret = true;
            auto cbFunc = ioVec_->cbFunc_;
            cbFunc(ioVec_, res);
            break;
          }
          case EventCallback::Type::TYPE_RECVMSG: {
            ret = true;
            auto cbFunc = msgHdr_->cbFunc_;
            cbFunc(msgHdr_, res);
            break;
          }
          case EventCallback::Type::TYPE_NONE:
            break;
        }
        type_ = EventCallback::Type::TYPE_NONE;

        return ret;
      }

      void releaseData() {
        switch (type_) {
          case EventCallback::Type::TYPE_READ: {
            auto freeFunc = ioVec_->freeFunc_;
            freeFunc(ioVec_);
            break;
          }
          case EventCallback::Type::TYPE_RECVMSG: {
            auto freeFunc = msgHdr_->freeFunc_;
            freeFunc(msgHdr_);
            break;
          }
          case EventCallback::Type::TYPE_NONE:
            break;
        }
        type_ = EventCallback::Type::TYPE_NONE;
      }
    };

    EventCallbackData cbData_;
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

  class SocketPair {
   public:
    SocketPair();

    SocketPair(const SocketPair&) = delete;
    SocketPair& operator=(const SocketPair&) = delete;

    ~SocketPair();

    int readFd() const {
      return fds_[1];
    }

    int writeFd() const {
      return fds_[0];
    }

   private:
    std::array<int, 2> fds_{{-1, -1}};
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

  // signal handling
  void addSignalEvent(Event& event);
  void removeSignalEvent(Event& event);
  bool addSignalFds();
  size_t processSignals();
  FOLLY_ALWAYS_INLINE void setProcessSignals() {
    processSignals_ = true;
  }

  static void processSignalReadIoCb(
      PollIoBackend* backend,
      IoCb* /*unused*/,
      int64_t /*unused*/) {
    backend->setProcessSignals();
  }

  void processPollIo(IoCb* ioCb, int64_t res) noexcept;

  IoCb* FOLLY_NULLABLE allocIoCb(const EventCallback& cb);
  void releaseIoCb(IoCb* aioIoCb);
  void incNumIoCbInUse() {
    numIoCbInUse_++;
  }

  virtual IoCb* allocNewIoCb(const EventCallback& cb) = 0;

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

  Options options_;
  size_t numEntries_;
  std::unique_ptr<IoCb> timerEntry_;
  std::unique_ptr<IoCb> signalReadEntry_;
  IoCbList freeList_;

  // timer related
  int timerFd_{-1};
  bool timerChanged_{false};
  std::map<
      std::chrono::time_point<std::chrono::steady_clock>,
      std::vector<TimerEntry>>
      timers_;
  std::map<Event*, std::chrono::time_point<std::chrono::steady_clock>>
      eventToTimers_;

  // signal related
  SocketPair signalFds_;
  std::map<int, std::set<Event*>> signals_;

  // submit
  IoCbList submitList_;

  // loop related
  bool loopBreak_{false};
  bool shuttingDown_{false};
  bool processTimers_{false};
  bool processSignals_{false};
  size_t numInsertedEvents_{0};
  IoCbList activeEvents_;
  // number of IoCb instances in use
  size_t numIoCbInUse_{0};
};
} // namespace folly
