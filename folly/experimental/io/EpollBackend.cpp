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

#include <folly/experimental/io/Epoll.h> // @manual

#if FOLLY_HAS_EPOLL

#include <signal.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include <folly/IntrusiveList.h>
#include <folly/String.h>
#include <folly/experimental/io/EpollBackend.h>

#include <folly/FileUtil.h>

extern "C" FOLLY_ATTR_WEAK void eb_poll_loop_pre_hook(uint64_t* call_time);
extern "C" FOLLY_ATTR_WEAK void eb_poll_loop_post_hook(
    uint64_t call_time, int ret);

namespace folly {
namespace {

struct EventInfo {
  folly::IntrusiveListHook listHook_;
  folly::EventBaseEvent* event_{nullptr};
  int what_{0};

  void resetEvent() {
    // remove it from the list
    listHook_.unlink();
    if (event_) {
      event_ = nullptr;
    }
  }
};

void eventInfoFreeFunction(void* v) {
  delete (EventInfo*)(v);
}

using EventInfoList = folly::IntrusiveList<EventInfo, &EventInfo::listHook_>;

struct SignalRegistry {
  struct SigInfo {
    struct sigaction sa_ {};
    size_t refs_{0};
  };
  using SignalMap = std::map<int, SigInfo>;

  SignalRegistry() {}

  void notify(int sig);
  void setNotifyFd(int sig, int fd);

  // lock protecting the signal map
  // we use a spinlock because we need
  // it to async-signal-safe
  folly::MicroSpinLock mapLock_ = {0};
  SignalMap map_;
  std::atomic<int> notifyFd_{-1};
};

SignalRegistry& getSignalRegistry() {
  static auto& sInstance = *new SignalRegistry();
  return sInstance;
}

void evSigHandler(int sig) {
  getSignalRegistry().notify(sig);
}

void SignalRegistry::notify(int sig) {
  // use try_lock in case somebody already has the lock
  std::unique_lock<folly::MicroSpinLock> lk(mapLock_, std::try_to_lock);
  if (!lk.owns_lock()) {
    return;
  }

  int fd = notifyFd_.load();
  if (fd >= 0) {
    uint8_t sigNum = static_cast<uint8_t>(sig);
    ::write(fd, &sigNum, 1);
  }
}

void SignalRegistry::setNotifyFd(int sig, int fd) {
  std::lock_guard<folly::MicroSpinLock> g(mapLock_);
  if (fd >= 0) {
    // switch the fd
    notifyFd_.store(fd);

    auto& entry = map_[sig];

    if (entry.refs_++ == 0) {
      struct sigaction sa = {};
      sa.sa_handler = evSigHandler;
      sa.sa_flags |= SA_RESTART;
      ::sigfillset(&sa.sa_mask);

      if (::sigaction(sig, &sa, &entry.sa_) == -1) {
        map_.erase(sig);
      }
    }
  } else {
    notifyFd_.store(fd);
    auto iter = map_.find(sig);
    if ((iter != map_.end()) && (--iter->second.refs_ == 0)) {
      auto entry = iter->second;
      map_.erase(iter);
      // just restore
      ::sigaction(sig, &entry.sa_, nullptr);
    }
  }
}

std::chrono::time_point<std::chrono::steady_clock> getTimerExpireTime(
    const struct timeval& timeout,
    std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now()) {
  return now + std::chrono::seconds{timeout.tv_sec} +
      std::chrono::microseconds{timeout.tv_usec};
}

struct TimerUserData {
  std::multimap<std::chrono::steady_clock::time_point, EpollBackend::Event*>::
      const_iterator iter;
};

void timerUserDataFreeFunction(void* v) {
  delete (TimerUserData*)(v);
}

uint32_t getPollFlags(short events) {
  uint32_t ret = 0;
  if (events & EV_READ) {
    ret |= POLLIN;
  }

  if (events & EV_WRITE) {
    ret |= POLLOUT;
  }

  return ret;
}
} // namespace

EpollBackend::SocketPair::SocketPair() {
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds_.data())) {
    throw std::runtime_error(folly::errnoStr(errno));
  }

  // set the sockets to non blocking mode
  for (auto fd : fds_) {
    auto flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

EpollBackend::SocketPair::~SocketPair() {
  for (auto fd : fds_) {
    if (fd >= 0) {
      ::close(fd);
    }
  }
}

EpollBackend::EpollBackend(Options options) : options_(options) {
  epollFd_ = ::epoll_create1(EPOLL_CLOEXEC);

  if (epollFd_ == -1) {
    throw std::runtime_error(folly::errnoStr(errno));
  }

  timerFd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerFd_ == -1) {
    auto errnoCopy = errno;
    ::close(epollFd_);
    throw std::runtime_error(folly::errnoStr(errnoCopy));
  }

  auto callback = [](libevent_fd_t /*fd*/, short /*events*/, void* arg) {
    static_cast<EpollBackend*>(arg)->processTimers();
  };

  timerFdEvent_.eb_event_set(timerFd_, EV_READ | EV_PERSIST, callback, this);
  event_ref_flags(timerFdEvent_.getEvent()) |= EVLIST_INTERNAL;

  eb_event_add(timerFdEvent_, nullptr);

  events_.resize(options_.numLoopEvents);
}

EpollBackend::~EpollBackend() {
  eb_event_del(timerFdEvent_);
  ::close(epollFd_);
  ::close(timerFd_);
}

int EpollBackend::eb_event_base_loop(int flags) {
  const bool waitForEvents = (flags & EVLOOP_NONBLOCK) == 0;
  while (true) {
    if (loopBreak_) {
      loopBreak_ = false;
      return 0;
    }

    if (numInternalEvents_ == numInsertedEvents_ && timers_.empty() &&
        signals_.empty()) {
      return 1;
    }

    uint64_t call_time = 0;
    if (eb_poll_loop_pre_hook) {
      eb_poll_loop_pre_hook(&call_time);
    }

    int numEvents;
    do {
      numEvents = ::epoll_wait(
          epollFd_, events_.data(), events_.size(), waitForEvents ? -1 : 0);
    } while (numEvents == -1 && errno == EINTR);

    if (eb_poll_loop_post_hook) {
      eb_poll_loop_post_hook(call_time, numEvents);
    }

    if (numEvents < 0) {
      return -1;
    } else if (numEvents == 0) {
      CHECK(!waitForEvents);
      return 2;
    }

    bool shouldProcessTimers = false;
    EventInfoList infoList;
    for (int i = 0; i < numEvents; ++i) {
      auto* info = static_cast<EventInfo*>(events_[i].data.ptr);
      auto* event = info->event_->getEvent();
      info->what_ = events_[i].events;
      // if not persistent we need to remove it
      if (~event->ev_events & EV_PERSIST) {
        if (event_ref_flags(event) & EVLIST_INSERTED) {
          event_ref_flags(event) &= ~EVLIST_INSERTED;

          DCHECK_GT(numInsertedEvents_, 0);
          numInsertedEvents_--;

          if (event_ref_flags(event) & EVLIST_INTERNAL) {
            DCHECK_GT(numInternalEvents_, 0);
            numInternalEvents_--;
          } else {
            DCHECK_GT(numEvents_, 0);
            numEvents_--;
          }

          int ret = ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, event->ev_fd, nullptr);
          CHECK_EQ(ret, 0);
        }
      }

      if (event->ev_fd == timerFd_) {
        shouldProcessTimers = true;
      } else {
        event_ref_flags(event) |= EVLIST_ACTIVE;
        infoList.push_back(*info);
      }
    }

    // process timers first
    if (shouldProcessTimers) {
      processTimers();
    }

    while (!infoList.empty()) {
      auto* info = &infoList.front();
      infoList.pop_front();

      struct event* event = info->event_->getEvent();

      int what = info->what_;
      short ev = 0;

      bool evRead = (event->ev_events & EV_READ) != 0;
      bool evWrite = (event->ev_events & EV_WRITE) != 0;

      if (what & EPOLLERR) {
        if (evRead) {
          ev |= EV_READ;
        }
        if (evWrite) {
          ev |= EV_WRITE;
        }
      } else if ((what & EPOLLHUP) && !(what & EPOLLRDHUP)) {
        if (evRead) {
          ev |= EV_READ;
        }
        if (evWrite) {
          ev |= EV_WRITE;
        }
      } else {
        if (evRead && (what & EPOLLIN)) {
          ev |= EV_READ;
        }
        if (evWrite && (what & EPOLLOUT)) {
          ev |= EV_WRITE;
        }
      }

      event_ref_flags(event) &= ~EVLIST_ACTIVE;
      event->ev_res = ev;
      if (event->ev_res) {
        (*event_ref_callback(event))(
            (int)event->ev_fd, event->ev_res, event_ref_arg(event));
      }
    }

    if (flags & EVLOOP_ONCE) {
      return 0;
    }
  }
}

int EpollBackend::eb_event_base_loopbreak() {
  loopBreak_ = true;
  return 0;
}

int EpollBackend::eb_event_add(Event& event, const struct timeval* timeout) {
  auto* ev = event.getEvent();
  CHECK(ev);
  CHECK(!(event_ref_flags(ev) & ~EVLIST_ALL));
  // we do not support read/write timeouts
  if (timeout) {
    event_ref_flags(ev) |= EVLIST_TIMEOUT;
    addTimerEvent(event, timeout);
    return 0;
  }

  if (ev->ev_events & EV_SIGNAL) {
    event_ref_flags(ev) |= EVLIST_INSERTED;
    addSignalEvent(event);
    return 0;
  }

  if (event_ref_flags(ev) & EVLIST_INTERNAL) {
    numInternalEvents_++;
  } else {
    numEvents_++;
  }
  event_ref_flags(ev) |= EVLIST_INSERTED;
  numInsertedEvents_++;

  EventInfo* info = static_cast<EventInfo*>(event.getUserData());
  if (!info) {
    info = new EventInfo();
    event.setUserData(info, eventInfoFreeFunction);
  }
  info->event_ = &event;

  struct epoll_event epev = {};
  epev.events = getPollFlags(ev->ev_events & (EV_READ | EV_WRITE));
  epev.data.ptr = info;

  int ret = ::epoll_ctl(epollFd_, EPOLL_CTL_ADD, ev->ev_fd, &epev);
  return ret == 0;
}

int EpollBackend::eb_event_del(Event& event) {
  if (!event.eb_ev_base()) {
    return -1;
  }

  auto* ev = event.getEvent();
  if (event_ref_flags(ev) & EVLIST_TIMEOUT) {
    event_ref_flags(ev) &= ~EVLIST_TIMEOUT;
    removeTimerEvent(event);
    return 1;
  }

  if (!(event_ref_flags(ev) & (EVLIST_ACTIVE | EVLIST_INSERTED))) {
    return -1;
  }

  if (ev->ev_events & EV_SIGNAL) {
    event_ref_flags(ev) &= ~(EVLIST_INSERTED | EVLIST_ACTIVE);
    removeSignalEvent(event);
    return 0;
  }

  auto* info = static_cast<EventInfo*>(event.getUserData());
  if (info) {
    info->resetEvent();
  }

  // if the event is on the active list, we just clear the flags
  // and reset the event_ ptr
  if (event_ref_flags(ev) & EVLIST_ACTIVE) {
    event_ref_flags(ev) &= ~EVLIST_ACTIVE;
  }

  if (event_ref_flags(ev) & EVLIST_INSERTED) {
    event_ref_flags(ev) &= ~EVLIST_INSERTED;

    DCHECK_GT(numInsertedEvents_, 0);
    numInsertedEvents_--;

    if (event_ref_flags(ev) & EVLIST_INTERNAL) {
      DCHECK_GT(numInternalEvents_, 0);
      numInternalEvents_--;
    } else {
      DCHECK_GT(numEvents_, 0);
      numEvents_--;
    }

    int ret = ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, ev->ev_fd, nullptr);
    return ret == 0;
  }
  return -1;
}

bool EpollBackend::setEdgeTriggered(Event& event) {
  auto* ev = event.getEvent();
  CHECK(ev);

  EventInfo* info = static_cast<EventInfo*>(event.getUserData());
  if (info == nullptr) {
    return false;
  }

  struct epoll_event epev = {};
  epev.events = getPollFlags(ev->ev_events & (EV_READ | EV_WRITE)) | EPOLLET;
  epev.data.ptr = info;

  int ret = ::epoll_ctl(epollFd_, EPOLL_CTL_MOD, ev->ev_fd, &epev);
  return ret == 0;
}

void EpollBackend::addTimerEvent(Event& event, const struct timeval* timeout) {
  auto expire = getTimerExpireTime(*timeout);

  TimerUserData* td = (TimerUserData*)event.getUserData();
  if (td) {
    CHECK_EQ(event.getFreeFunction(), timerUserDataFreeFunction);
    if (td->iter == timers_.end()) {
      td->iter = timers_.emplace(expire, &event);
    } else {
      auto ex = timers_.extract(td->iter);
      ex.key() = expire;
      td->iter = timers_.insert(std::move(ex));
    }
  } else {
    auto it = timers_.emplace(expire, &event);
    td = new TimerUserData();
    td->iter = it;
    event.setUserData(td, timerUserDataFreeFunction);
  }

  if (td->iter == timers_.begin()) {
    scheduleTimeout();
  }
}

void EpollBackend::removeTimerEvent(Event& event) {
  TimerUserData* td = (TimerUserData*)event.getUserData();
  CHECK(!!td);
  CHECK_EQ(event.getFreeFunction(), timerUserDataFreeFunction);
  bool timerChanged = td->iter == timers_.begin();
  timers_.erase(td->iter);
  td->iter = timers_.end();
  event.setUserData(nullptr, nullptr);
  delete td;

  if (timerChanged) {
    scheduleTimeout();
  }
}

void EpollBackend::scheduleTimeout() {
  if (!timers_.empty()) {
    auto delta = std::chrono::duration_cast<std::chrono::microseconds>(
        timers_.begin()->first - std::chrono::steady_clock::now());
    if (delta < std::chrono::microseconds(1000)) {
      delta = std::chrono::microseconds(1000);
    }
    struct itimerspec val;
    timerSet_ = true;
    val.it_interval = {0, 0};
    val.it_value.tv_sec =
        std::chrono::duration_cast<std::chrono::seconds>(delta).count();
    val.it_value.tv_nsec =
        std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count() %
        1'000'000'000LL;

    CHECK_EQ(::timerfd_settime(timerFd_, 0, &val, nullptr), 0);
  } else if (timerSet_) {
    timerSet_ = false;
    // disable
    struct itimerspec val = {};
    CHECK_EQ(::timerfd_settime(timerFd_, 0, &val, nullptr), 0);
  }
}

size_t EpollBackend::processTimers() {
  size_t ret = 0;

  // consume the event
  uint64_t data = 0;
  folly::readNoInt(timerFd_, &data, sizeof(data));
  bool timerChanged = false;

  while (true) {
    auto it = timers_.begin();
    auto now = std::chrono::steady_clock::now();
    if (it == timers_.end() || now < it->first) {
      break;
    }
    timerChanged = true;
    Event* e = it->second;
    TimerUserData* td = (TimerUserData*)e->getUserData();
    CHECK(td && e->getFreeFunction() == timerUserDataFreeFunction);
    td->iter = timers_.end();
    timers_.erase(it);
    auto* ev = e->getEvent();
    ev->ev_res = EV_TIMEOUT;
    event_ref_flags(ev).get() = EVLIST_INIT;
    // might change the lists
    (*event_ref_callback(ev))((int)ev->ev_fd, ev->ev_res, event_ref_arg(ev));
    ++ret;
  }

  if (timerChanged) {
    scheduleTimeout();
  }
  return ret;
}

// signal related
void EpollBackend::addSignalEvent(Event& event) {
  auto* ev = event.getEvent();
  signals_[ev->ev_fd].insert(&event);

  // we pass the write fd for notifications
  getSignalRegistry().setNotifyFd(ev->ev_fd, signalFds_.writeFd());
}

void EpollBackend::removeSignalEvent(Event& event) {
  auto* ev = event.getEvent();
  auto iter = signals_.find(ev->ev_fd);
  if (iter != signals_.end()) {
    getSignalRegistry().setNotifyFd(ev->ev_fd, -1);
  }
}

size_t EpollBackend::processSignals() {
  size_t ret = 0;
  static constexpr auto kNumEntries = NSIG * 2;
  static_assert(
      NSIG < std::numeric_limits<uint8_t>::max(),
      "Use a different data type to cover all the signal values");
  std::array<bool, NSIG> processed{};
  std::array<uint8_t, kNumEntries> signals;

  ssize_t num =
      folly::readNoInt(signalFds_.readFd(), signals.data(), signals.size());
  for (ssize_t i = 0; i < num; i++) {
    int signum = static_cast<int>(signals[i]);
    if ((signum >= 0) && (signum < static_cast<int>(processed.size())) &&
        !processed[signum]) {
      processed[signum] = true;
      auto iter = signals_.find(signum);
      if (iter != signals_.end()) {
        auto& set = iter->second;
        for (auto& event : set) {
          auto* ev = event->getEvent();
          ev->ev_res = 0;
          event_ref_flags(ev) |= EVLIST_ACTIVE;
          (*event_ref_callback(ev))(
              (int)ev->ev_fd, ev->ev_res, event_ref_arg(ev));
          event_ref_flags(ev) &= ~EVLIST_ACTIVE;
        }
      }
    }
  }
  return ret;
}
} // namespace folly
#endif
