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

#include <folly/experimental/io/EventBasePoller.h>

#include <atomic>
#include <stdexcept>

#include <boost/polymorphic_cast.hpp>
#include <fmt/format.h>
#include <glog/logging.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/experimental/io/Epoll.h>
#include <folly/experimental/io/Liburing.h>
#include <folly/lang/Align.h>
#include <folly/portability/GFlags.h>
#include <folly/synchronization/Baton.h>
#include <folly/system/ThreadName.h>

#if FOLLY_HAS_EPOLL
#include <poll.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#endif

#if FOLLY_HAS_LIBURING
#include <liburing.h> // @manual
#endif

FOLLY_GFLAGS_DEFINE_string(
    folly_event_base_poller_backend,
    "epoll",
    "Available EventBasePoller backends: \"epoll\", \"io_uring\"");
FOLLY_GFLAGS_DEFINE_uint64(
    folly_event_base_poller_epoll_max_events,
    64,
    "Maximum number of events to process in one iteration when "
    "using the epoll EventBasePoller backend");
FOLLY_GFLAGS_DEFINE_uint64(
    folly_event_base_poller_io_uring_sq_entries,
    128,
    "Minimum number of entries to allocate for the submission queue when "
    "using the io_uring EventBasePoller backend");

namespace folly::detail {

namespace {

template <class T>
class Queue {
 public:
  bool insert(T* t) {
    DCHECK(t->next == nullptr);

    auto oldHead = head_.load(std::memory_order_relaxed);
    bool ret;
    do {
      t->next = (ret = (oldHead == kQueueArmedTag())) ? nullptr : oldHead;
    } while (!head_.compare_exchange_weak(
        oldHead, t, std::memory_order_release, std::memory_order_relaxed));
    return ret;
  }

  T* arm() {
    T* oldHead = head_.load(std::memory_order_relaxed);
    T* newHead;
    T* ret;
    do {
      if (oldHead == nullptr || oldHead == kQueueArmedTag()) {
        newHead = kQueueArmedTag();
        ret = nullptr;
      } else {
        newHead = nullptr;
        ret = oldHead;
      }
    } while (!head_.compare_exchange_weak(
        oldHead,
        newHead,
        std::memory_order_acq_rel,
        std::memory_order_relaxed));

    return ret;
  }

 private:
  static T* kQueueArmedTag() { return reinterpret_cast<T*>(1); }

  std::atomic<T*> head_{nullptr};
};

#if FOLLY_HAS_EPOLL

class EventBasePollerImpl : public EventBasePoller {
  class FdGroupImpl;

 public:
  EventBasePollerImpl()
      : notificationEv_(
            Event::NotificationFd{}, ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
    PCHECK(notificationEv_.fd >= 0);
  }

  ~EventBasePollerImpl() override {
    CHECK_EQ(numGroups_.load(), 0)
        << "All groups must be destroyed before EventBasePoller";
    ::close(notificationEv_.fd);
  }

  std::unique_ptr<FdGroup> makeFdGroup(ReadyCallback readyCallback) override;

 protected:
  void startLoop() {
    returnQueue_.arm();
    addEvent(&notificationEv_);
    loopThread_ = std::make_unique<std::thread>([this]() { loop(); });
  }

  void stopLoop() {
    stop_ = true;
    notifyEvfd();
    loopThread_->join();
  }

  struct Event final : public Handle {
    struct NotificationFd {};

    Event(FdGroupImpl& group_, int fd_, void* userData)
        : Handle(userData), group(&group_), fd(fd_) {}
    // Special internal event to poll the notification eventfd.
    Event(NotificationFd, int fd_) : Handle(nullptr), group(nullptr), fd(fd_) {}

    ~Event() override {
      CHECK(isNotificationFd() || joined_.ready())
          << "Handle must be reclaimed before destruction";
    }

    bool isNotificationFd() const { return group == nullptr; }

    void handoff(bool done) override;

    void handleHandoff();
    void join();

    FdGroupImpl* group;
    const int fd;
    bool registered = false; // Managed by addEvent()/delEvent().
    Event* next{nullptr}; // Managed by Queue.

   private:
    bool joining_ = false;
    Baton<> joined_;
  };

 private:
  virtual void addEvent(Event* event) = 0;
  virtual void delEvent(Event* event) = 0;
  virtual bool waitForEvents() = 0;

  void notifyEvfd();
  void returnEvent(Event* event);

  void handleNotification();
  void handleReadyEvents();
  void loop();

 protected:
  std::vector<Event*> readyEvents_;

 private:
  std::atomic<size_t> numGroups_{0};
  Event notificationEv_;
  std::vector<std::unique_ptr<Event>> events_;
  std::unique_ptr<std::thread> loopThread_;
  std::vector<Handle*> readyHandles_; // Cache allocation.
  std::atomic<bool> stop_{false};
  Queue<Event> returnQueue_;
};

class EventBasePollerImpl::FdGroupImpl final : public FdGroup {
 public:
  FdGroupImpl(EventBasePollerImpl& parent_, ReadyCallback readyCallback_)
      : parent(parent_), readyCallback(std::move(readyCallback_)) {
    ++parent.numGroups_;
  }

  ~FdGroupImpl() override {
    CHECK_EQ(numHandles_, 0)
        << "All EventBases must be reclaimed before group destruction";
    --parent.numGroups_;
  }

  std::unique_ptr<Handle> add(int fd, void* userData) override {
    auto handle = std::make_unique<Event>(*this, fd, userData);
    ++numHandles_;
    // Run the first iteration to register the fd.
    Handle* handlePtr = handle.get();
    readyCallback({&handlePtr, 1});
    return handle;
  }

  void reclaim(std::unique_ptr<Handle> handle) override {
    boost::polymorphic_downcast<Event*>(handle.get())->join();
    --numHandles_;
  }

  EventBasePollerImpl& parent;
  const ReadyCallback readyCallback;

 private:
  size_t numHandles_ = 0;
};

void EventBasePollerImpl::Event::handoff(bool done) {
  DCHECK(!isNotificationFd());
  CHECK(!joining_);
  joining_ = done;
  group->parent.returnEvent(this);
}

void EventBasePollerImpl::Event::handleHandoff() {
  DCHECK(!isNotificationFd());
  if (joining_) {
    group->parent.delEvent(this);
    joined_.post();
    return;
  }

  group->parent.addEvent(this);
}

void EventBasePollerImpl::Event::join() {
  DCHECK(!isNotificationFd());
  joined_.wait();
}

void EventBasePollerImpl::notifyEvfd() {
  uint64_t val = 1;
  auto ret = writeNoInt(notificationEv_.fd, &val, sizeof(val));
  PCHECK(ret == sizeof(val)) << ret;
}

void EventBasePollerImpl::returnEvent(Event* event) {
  if (returnQueue_.insert(event)) {
    notifyEvfd();
  }
}

std::unique_ptr<EventBasePoller::FdGroup> EventBasePollerImpl::makeFdGroup(
    ReadyCallback readyCallback) {
  return std::make_unique<FdGroupImpl>(*this, std::move(readyCallback));
}

void EventBasePollerImpl::handleNotification() {
  while (auto* event = returnQueue_.arm()) {
    while (event) {
      auto* next = std::exchange(event->next, nullptr);
      event->handleHandoff();
      event = next;
    }
  }
  addEvent(&notificationEv_);
}

void EventBasePollerImpl::handleReadyEvents() {
  CHECK(!readyEvents_.empty());
  // Sort by group so we can call readyCallback on each group.
  std::sort(
      readyEvents_.begin(),
      readyEvents_.end(),
      [](const Event* lhs, const Event* rhs) {
        // Sort backwards so the notification event (group == nullptr) is
        // processed last.
        return std::greater<>{}(lhs->group, rhs->group);
      });

  auto it = readyEvents_.begin();
  FdGroupImpl* curGroup = (*it)->group;
  while (true) {
    if (it == readyEvents_.end() || (*it)->group != curGroup) {
      if (curGroup == nullptr) {
        // There can only be one notification event.
        CHECK_EQ(readyHandles_.size(), 1);
        handleNotification();
      } else {
        curGroup->readyCallback(folly::range(readyHandles_));
      }
      readyHandles_.clear();
      if (it == readyEvents_.end()) {
        break;
      }
      curGroup = (*it)->group;
    }
    readyHandles_.push_back(*it++);
  }

  readyEvents_.clear();
}

void EventBasePollerImpl::loop() {
  setThreadName("EventBasePoller");

  auto lastLoopTs = std::chrono::steady_clock::now();
  while (!stop_.load()) {
    if (!waitForEvents()) {
      continue;
    }
    auto waitEndTs = std::chrono::steady_clock::now();
    handleReadyEvents();
    auto busyEndTs = std::chrono::steady_clock::now();
    stats_.wlock()->update(
        readyEvents_.size(), waitEndTs - lastLoopTs, busyEndTs - waitEndTs);
    lastLoopTs = busyEndTs;
  }
}

class EventBasePollerEpoll final : public EventBasePollerImpl {
 public:
  EventBasePollerEpoll() {
    epFd_ = ::epoll_create1(EPOLL_CLOEXEC);
    PCHECK(epFd_ > 0);
    startLoop();
  }

  ~EventBasePollerEpoll() override {
    stopLoop();
    ::close(epFd_);
  }

  void addEvent(Event* event) override {
    if (event->isNotificationFd() && event->registered) {
      return; // notificationEv_ is persistent.
    }

    epoll_event ev = {};
    ev.data.ptr = event;
    ev.events = EPOLLIN;

    int op = EPOLL_CTL_ADD;
    if (!event->isNotificationFd()) {
      ev.events |= EPOLLONESHOT;
      if (FOLLY_UNLIKELY(!event->registered)) {
        event->registered = true;
      } else {
        op = EPOLL_CTL_MOD;
      }
    } else {
      // Use edge triggering, so we don't need to drain the eventfd when ready.
      ev.events |= EPOLLET;
      event->registered = true;
    }

    auto ret = ::epoll_ctl(epFd_, op, event->fd, &ev);
    PCHECK(ret == 0);
  }

  void delEvent(Event* event) override {
    CHECK(!event->isNotificationFd());
    if (!event->registered) {
      return;
    }

    auto ret = ::epoll_ctl(epFd_, EPOLL_CTL_DEL, event->fd, nullptr);
    PCHECK(ret == 0);
  }

  bool waitForEvents() override {
    auto ret = ::epoll_wait(epFd_, epollEvents_.data(), kMaxEvents, -1);
    if (ret <= 0) {
      return false;
    }
    for (int i = 0; i < ret; ++i) {
      readyEvents_.push_back(
          CHECK_NOTNULL(reinterpret_cast<Event*>(epollEvents_[i].data.ptr)));
    }
    return true;
  }

 private:
  const size_t kMaxEvents = FLAGS_folly_event_base_poller_epoll_max_events;
  int epFd_{-1};
  std::vector<struct epoll_event> epollEvents_{kMaxEvents};
};

#if FOLLY_HAS_LIBURING

class EventBasePollerIoUring final : public EventBasePollerImpl {
 public:
  EventBasePollerIoUring() {
    ::memset(&ring_, 0, sizeof(ring_));
    struct io_uring_params params;
    ::memset(&params, 0, sizeof(params));

    // TODO(ott): Consider following flags once widely available:
    // IORING_SETUP_COOP_TASKRUN
    // IORING_SETUP_TASKRUN_FLAG
    // IORING_SETUP_SINGLE_ISSUER
    // IORING_SETUP_DEFER_TASKRUN
    int ret = ::io_uring_queue_init_params(
        FLAGS_folly_event_base_poller_io_uring_sq_entries, &ring_, &params);
    CHECK(ret == 0) << "Error creating io_uring: " << folly::errnoStr(-ret);

    startLoop();
  }

  ~EventBasePollerIoUring() override {
    stopLoop();
    ::io_uring_queue_exit(&ring_);
  }

  void addEvent(Event* event) override {
    auto* sqe = ::io_uring_get_sqe(&ring_);
    if (sqe == nullptr) {
      submitPendingSqes();
      sqe = ::io_uring_get_sqe(&ring_);
      // Single issuer, so if we can't get a sqe after a submit we have a
      // problem.
      CHECK(sqe != nullptr);
    }
    ++numPendingSqes_;

    ::io_uring_sqe_set_data(sqe, event);
    if (event->isNotificationFd()) {
      ::io_uring_prep_read(
          sqe, event->fd, &eventFdBuf_, sizeof(eventFdBuf_), 0);
    } else {
      ::io_uring_prep_poll_add(sqe, event->fd, POLLIN);
    }
  }

  void delEvent(Event* /* event */) override {
    // Nothing to do, no events are persistent.
  }

  bool waitForEvents() override {
    if (numPendingSqes_ > 0) {
      submitPendingSqes();
    }

    struct io_uring_cqe* cqe = nullptr;
    auto ret = ::io_uring_wait_cqe(&ring_, &cqe);
    if (ret < 0 || cqe == nullptr) {
      return false;
    }

    DCHECK_EQ(readyEvents_.size(), 0);
    unsigned head;
    io_uring_for_each_cqe(&ring_, head, cqe) {
      auto* event =
          CHECK_NOTNULL(static_cast<Event*>(io_uring_cqe_get_data(cqe)));
      if (event->isNotificationFd()) {
        CHECK_EQ(cqe->res, sizeof(eventFdBuf_)) << errnoStr(-cqe->res);
      } else {
        CHECK_GE(cqe->res, 0) << errnoStr(-cqe->res);
      }
      readyEvents_.push_back(event);
    }
    ::io_uring_cq_advance(&ring_, readyEvents_.size());

    return true;
  }

 private:
  void submitPendingSqes() {
    auto ret = ::io_uring_submit(&ring_);
    CHECK_EQ(ret, numPendingSqes_);
    numPendingSqes_ = 0;
  }

  struct io_uring ring_;
  size_t numPendingSqes_ = 0;
  alignas(cacheline_align_v) uint64_t eventFdBuf_;
};

#endif // FOLLY_HAS_LIBURING

#endif // FOLLY_HAS_EPOLL

} // namespace

void EventBasePoller::Stats::update(
    int numEvents, Duration wait, Duration busy) {
  minNumEvents = std::min(minNumEvents, numEvents);
  maxNumEvents = std::max(maxNumEvents, numEvents);
  totalNumEvents += static_cast<size_t>(numEvents);
  totalWakeups += 1;

  totalWait += wait;
  minWait = std::min(minWait, wait);
  maxWait = std::max(maxWait, wait);

  totalBusy += busy;
  minBusy = std::min(minBusy, busy);
  maxBusy = std::max(maxBusy, busy);
}

EventBasePoller::Handle::~Handle() = default;

EventBasePoller::FdGroup::~FdGroup() = default;

EventBasePoller::~EventBasePoller() = default;

/* static */ EventBasePoller& EventBasePoller::get() {
  static auto instance = []() -> std::unique_ptr<EventBasePoller> {
#if FOLLY_HAS_EPOLL
    if (FLAGS_folly_event_base_poller_backend == "epoll") {
      return std::make_unique<EventBasePollerEpoll>();
    }
#endif
#if FOLLY_HAS_EPOLL && FOLLY_HAS_LIBURING
    if (FLAGS_folly_event_base_poller_backend == "io_uring") {
      return std::make_unique<EventBasePollerIoUring>();
    }
#endif
    throw std::invalid_argument(fmt::format(
        "Unsupported EventBasePoller backend: {}",
        FLAGS_folly_event_base_poller_backend));
  }();
  return *instance;
}

} // namespace folly::detail
