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

#include <folly/io/async/Epoll.h> // @manual

#if FOLLY_HAS_EPOLL

#include <signal.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include <folly/IntrusiveList.h>
#include <folly/MapUtil.h>
#include <folly/String.h>
#include <folly/io/async/EpollBackend.h>

#include <folly/FileUtil.h>

extern "C" FOLLY_ATTR_WEAK void eb_poll_loop_pre_hook(uint64_t* call_time);
extern "C" FOLLY_ATTR_WEAK void eb_poll_loop_post_hook(
    uint64_t call_time, int ret);

namespace folly {
namespace {

struct EventInfo {
  static void freeFunction(void* v) { delete static_cast<EventInfo*>(v); }

  void resetEvent() {
    listHook.unlink(); // Remove from the info list.
    ev = nullptr;
  }

  folly::IntrusiveListHook listHook;
  struct event* ev{nullptr};
  int what_{0};
};

using EventInfoList = folly::IntrusiveList<EventInfo, &EventInfo::listHook>;

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

uint32_t getPollFlags(short events) {
  uint32_t ret = 0;
  if (events & EV_READ) {
    ret |= EPOLLIN;
  }

  if (events & EV_WRITE) {
    ret |= EPOLLOUT;
  }

  return ret;
}

} // namespace

struct EpollBackend::TimerInfo : public IntrusiveHeapNode<> {
  bool operator<(const TimerInfo& other) const {
    // IntrusiveHeap is a max-heap.
    return expiration > other.expiration;
  }

  static void freeFunction(void* v) { delete static_cast<TimerInfo*>(v); }

  ~TimerInfo() { DCHECK(!isLinked()); }

  std::chrono::steady_clock::time_point expiration;
  struct event* ev;
};

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

  {
    timerFd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerFd_ == -1) {
      auto errnoCopy = errno;
      ::close(epollFd_);
      throw std::runtime_error(folly::errnoStr(errnoCopy));
    }
    struct epoll_event epev = {};
    epev.events = EPOLLIN;
    // epoll_data is a union, so we need to use pointers for all events. We can
    // use any unique pointer to distinguish the timerfd event, use the address
    // of the fd variable.
    epev.data.ptr = &timerFd_;
    PCHECK(::epoll_ctl(epollFd_, EPOLL_CTL_ADD, timerFd_, &epev) == 0);
  }

  {
    struct epoll_event epev = {};
    epev.events = EPOLLIN;
    epev.data.ptr = &signalFds_;
    PCHECK(
        ::epoll_ctl(epollFd_, EPOLL_CTL_ADD, signalFds_.readFd(), &epev) == 0);
  }

  events_.resize(options_.numLoopEvents);
}

EpollBackend::~EpollBackend() {
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
    bool shouldProcessSignals = false;
    // Callbacks may delete other active events, so we accumulate active events
    // first into an intrusive list that is updated if events in it are deleted.
    EventInfoList infoList;
    for (int i = 0; i < numEvents; ++i) {
      if (events_[i].data.ptr == &timerFd_) {
        shouldProcessTimers = true;
        continue;
      } else if (events_[i].data.ptr == &signalFds_) {
        shouldProcessSignals = true;
        continue;
      }

      auto* info = static_cast<EventInfo*>(events_[i].data.ptr);
      auto* event = info->ev;
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
          }

          PCHECK(
              ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, event->ev_fd, nullptr) == 0);
        }
      }

      event_ref_flags(event) |= EVLIST_ACTIVE;
      infoList.push_back(*info);
    }

    // Process timers and signals first.
    if (shouldProcessTimers) {
      processTimers();
    }
    if (shouldProcessSignals) {
      processSignals();
    }

    while (!infoList.empty()) {
      auto* info = &infoList.front();
      infoList.pop_front();

      struct event* event = info->ev;

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
  CHECK(ev != nullptr);
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
  }

  event_ref_flags(ev) |= EVLIST_INSERTED;
  numInsertedEvents_++;

  EventInfo* info = static_cast<EventInfo*>(event.getUserData());
  if (!info) {
    info = new EventInfo();
    event.setUserData(info, EventInfo::freeFunction);
  }
  info->ev = ev;

  struct epoll_event epev = {};
  epev.events = getPollFlags(ev->ev_events & (EV_READ | EV_WRITE));
  epev.data.ptr = info;

  return ::epoll_ctl(epollFd_, EPOLL_CTL_ADD, ev->ev_fd, &epev);
}

int EpollBackend::eb_event_del(Event& event) {
  if (!event.eb_ev_base()) {
    errno = EINVAL;
    return -1;
  }

  auto* ev = event.getEvent();
  if (event_ref_flags(ev) & EVLIST_TIMEOUT) {
    event_ref_flags(ev) &= ~EVLIST_TIMEOUT;
    return removeTimerEvent(event);
  }

  if (!(event_ref_flags(ev) & (EVLIST_ACTIVE | EVLIST_INSERTED))) {
    errno = EINVAL;
    return -1;
  }

  if (ev->ev_events & EV_SIGNAL) {
    event_ref_flags(ev) &= ~(EVLIST_INSERTED | EVLIST_ACTIVE);
    return removeSignalEvent(event);
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
    }

    return ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, ev->ev_fd, nullptr);
  }

  errno = EINVAL;
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
  TimerInfo* info = static_cast<TimerInfo*>(event.getUserData());
  if (info == nullptr) {
    info = new TimerInfo;
    info->ev = event.getEvent();
    event.setUserData(info, TimerInfo::freeFunction);
  }

  info->expiration = std::chrono::steady_clock::now() +
      std::chrono::seconds{timeout->tv_sec} +
      std::chrono::microseconds{timeout->tv_usec};
  if (info->isLinked()) {
    timers_.update(info);
  } else {
    timers_.push(info);
  }

  updateTimerFd();
}

int EpollBackend::removeTimerEvent(Event& event) {
  auto* info = static_cast<TimerInfo*>(event.getUserData());
  if (info == nullptr || !info->isLinked()) {
    errno = EINVAL;
    return -1;
  }

  DCHECK_EQ(event.getFreeFunction(), TimerInfo::freeFunction);
  timers_.erase(info);
  updateTimerFd();
  return 0;
}

void EpollBackend::updateTimerFd() {
  std::optional<std::chrono::steady_clock::time_point> expiration;
  if (auto* earliest = timers_.top()) {
    expiration = earliest->expiration;
  }
  if (expiration == timerFdExpiration_) {
    return; // Nothing to do.
  }

  if (!expiration) {
    struct itimerspec val = {}; // Disable.
    PCHECK(::timerfd_settime(timerFd_, 0, &val, nullptr) == 0);
  } else {
    auto delta = std::chrono::duration_cast<std::chrono::microseconds>(
        *expiration - std::chrono::steady_clock::now());
    if (delta < std::chrono::microseconds(1000)) {
      delta = std::chrono::microseconds(1000);
    }

    struct itimerspec val;
    val.it_interval = {0, 0};
    val.it_value.tv_sec =
        std::chrono::duration_cast<std::chrono::seconds>(delta).count();
    val.it_value.tv_nsec =
        std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count() %
        1'000'000'000LL;

    PCHECK(::timerfd_settime(timerFd_, 0, &val, nullptr) == 0);
  }

  timerFdExpiration_ = expiration;
}

void EpollBackend::processTimers() {
  // Consume the event.
  uint64_t data = 0;
  PCHECK(folly::readNoInt(timerFd_, &data, sizeof(data)) == sizeof(data));

  while (!timers_.empty() &&
         timers_.top()->expiration <= std::chrono::steady_clock::now()) {
    auto* info = timers_.pop();
    auto* ev = info->ev;
    ev->ev_res = EV_TIMEOUT;
    event_ref_flags(ev).get() = EVLIST_INIT;
    // NOTE: The callback might change the set of registered timers.
    (*event_ref_callback(ev))((int)ev->ev_fd, ev->ev_res, event_ref_arg(ev));
  }

  updateTimerFd();
}

void EpollBackend::addSignalEvent(Event& event) {
  auto* ev = event.getEvent();
  signals_[ev->ev_fd].insert(event.getEvent());

  // we pass the write fd for notifications
  getSignalRegistry().setNotifyFd(ev->ev_fd, signalFds_.writeFd());
}

int EpollBackend::removeSignalEvent(Event& event) {
  auto* ev = event.getEvent();
  auto* set = get_ptr(signals_, ev->ev_fd);
  if (set == nullptr || set->erase(ev) == 0) {
    errno = EINVAL;
    return -1;
  }
  getSignalRegistry().setNotifyFd(ev->ev_fd, -1);
  return 0;
}

void EpollBackend::processSignals() {
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
    if (signum < 0 || signum >= NSIG || processed[signum]) {
      continue;
    }
    processed[signum] = true;
    auto* events = get_ptr(signals_, signum);
    if (events == nullptr) {
      continue;
    }
    for (auto* ev : *events) {
      ev->ev_res = 0;
      event_ref_flags(ev) |= EVLIST_ACTIVE;
      (*event_ref_callback(ev))((int)ev->ev_fd, ev->ev_res, event_ref_arg(ev));
      event_ref_flags(ev) &= ~EVLIST_ACTIVE;
    }
  }
}

} // namespace folly
#endif
