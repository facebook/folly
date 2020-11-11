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

#include <signal.h>
#include <sys/timerfd.h>

#include <atomic>

#include <folly/FileUtil.h>
#include <folly/Likely.h>
#include <folly/SpinLock.h>
#include <folly/experimental/io/PollIoBackend.h>
#include <folly/portability/Sockets.h>
#include <folly/synchronization/CallOnce.h>

#include <glog/logging.h>

extern "C" FOLLY_ATTR_WEAK void eb_poll_loop_pre_hook(uint64_t* call_time);
extern "C" FOLLY_ATTR_WEAK void eb_poll_loop_post_hook(
    uint64_t call_time,
    int ret);

namespace {
struct SignalRegistry {
  struct SigInfo {
    struct sigaction sa_ {};
    size_t count_{0};
  };
  using SignalMap = std::map<int, SigInfo>;

  constexpr SignalRegistry() {}
  ~SignalRegistry() {}

  void notify(int sig);
  void setNotifyFd(int sig, int fd);

  // lock protecting the signal map
  folly::MicroSpinLock mapLock_ = {0};
  std::unique_ptr<SignalMap> map_;
  std::atomic<int> notifyFd_{-1};
};

SignalRegistry sSignalRegistry;

void evSigHandler(int sig) {
  sSignalRegistry.notify(sig);
}

void SignalRegistry::notify(int sig) {
  // use try_lock in case somebody already has the lock
  if (mapLock_.try_lock()) {
    int fd = notifyFd_.load();
    if (fd >= 0) {
      uint8_t sigNum = static_cast<uint8_t>(sig);
      ::write(fd, &sigNum, 1);
    }
    mapLock_.unlock();
  }
}

void SignalRegistry::setNotifyFd(int sig, int fd) {
  folly::MSLGuard g(mapLock_);
  if (fd >= 0) {
    if (!map_) {
      map_ = std::make_unique<SignalMap>();
    }
    // switch the fd
    notifyFd_.store(fd);

    auto iter = (*map_).find(sig);
    if (iter != (*map_).end()) {
      iter->second.count_++;
    } else {
      auto& entry = (*map_)[sig];
      entry.count_ = 1;
      struct sigaction sa = {};
      sa.sa_handler = evSigHandler;
      sa.sa_flags |= SA_RESTART;
      ::sigfillset(&sa.sa_mask);

      if (::sigaction(sig, &sa, &entry.sa_) == -1) {
        (*map_).erase(sig);
      }
    }
  } else {
    notifyFd_.store(fd);

    if (map_) {
      auto iter = (*map_).find(sig);
      if ((iter != (*map_).end()) && (--iter->second.count_ == 0)) {
        auto entry = iter->second;
        (*map_).erase(iter);
        // just restore
        ::sigaction(sig, &entry.sa_, nullptr);
      }
    }
  }
}

} // namespace

namespace folly {
PollIoBackend::TimerEntry::TimerEntry(
    Event* event,
    const struct timeval& timeout)
    : event_(event) {
  setExpireTime(timeout);
}

PollIoBackend::SocketPair::SocketPair() {
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds_.data())) {
    throw std::runtime_error("socketpair error");
  }

  // set the sockets to non blocking mode
  for (auto fd : fds_) {
    auto flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

PollIoBackend::SocketPair::~SocketPair() {
  for (auto fd : fds_) {
    if (fd >= 0) {
      ::close(fd);
    }
  }
}

PollIoBackend::PollIoBackend(Options options)
    : options_(options), numEntries_(options.capacity) {
  // create the timer fd
  timerFd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerFd_ < 0) {
    throw std::runtime_error("timerfd_create error");
  }
}

PollIoBackend::~PollIoBackend() {
  CHECK(!timerEntry_);
  CHECK(!signalReadEntry_);
  CHECK(freeList_.empty());

  ::close(timerFd_);
}

bool PollIoBackend::addTimerFd() {
  auto* entry = allocSubmissionEntry(); // this can be nullptr
  timerEntry_->prepPollAdd(entry, timerFd_, POLLIN, true /*registerFd*/);
  return (1 == submitOne(timerEntry_.get()));
}

bool PollIoBackend::addSignalFds() {
  auto* entry = allocSubmissionEntry(); // this can be nullptr
  signalReadEntry_->prepPollAdd(
      entry, signalFds_.readFd(), POLLIN, false /*registerFd*/);

  return (1 == submitOne(signalReadEntry_.get()));
}

void PollIoBackend::scheduleTimeout() {
  if (!timerChanged_) {
    return;
  }

  // reset
  timerChanged_ = false;
  if (!timers_.empty()) {
    auto delta = timers_.begin()->second[0].getRemainingTime();
    if (delta.count() < 1000) {
      delta = std::chrono::microseconds(1000);
    }
    scheduleTimeout(delta);
  } else {
    scheduleTimeout(std::chrono::microseconds(0)); // disable
  }

  // we do not call addTimerFd() here
  // since it has to be added only once, after
  // we process a poll callback
}

void PollIoBackend::scheduleTimeout(const std::chrono::microseconds& us) {
  struct itimerspec val;
  val.it_interval = {0, 0};
  val.it_value.tv_sec =
      std::chrono::duration_cast<std::chrono::seconds>(us).count();
  val.it_value.tv_nsec =
      std::chrono::duration_cast<std::chrono::nanoseconds>(us).count() %
      1000000000LL;

  CHECK_EQ(::timerfd_settime(timerFd_, 0, &val, nullptr), 0);
}

void PollIoBackend::addTimerEvent(Event& event, const struct timeval* timeout) {
  // first try to remove if already existing
  auto iter1 = eventToTimers_.find(&event);
  if (iter1 != eventToTimers_.end()) {
    // no neeed to remove it from eventToTimers_
    auto expireTime = iter1->second;
    auto iter2 = timers_.find(expireTime);
    for (auto iter = iter2->second.begin(), last = iter2->second.end();
         iter != last;
         ++iter) {
      if (iter->event_ == &event) {
        iter2->second.erase(iter);
        break;
      }
    }

    if (iter2->second.empty()) {
      timers_.erase(iter2);
    }
  }

  TimerEntry entry(&event, *timeout);
  if (!timerChanged_) {
    timerChanged_ =
        timers_.empty() || (entry.expireTime_ < timers_.begin()->first);
  }
  timers_[entry.expireTime_].push_back(entry);
  eventToTimers_[&event] = entry.expireTime_;
}

void PollIoBackend::removeTimerEvent(Event& event) {
  auto iter1 = eventToTimers_.find(&event);
  CHECK(iter1 != eventToTimers_.end());
  auto expireTime = iter1->second;
  eventToTimers_.erase(iter1);

  auto iter2 = timers_.find(expireTime);
  CHECK(iter2 != timers_.end());

  for (auto iter = iter2->second.begin(), last = iter2->second.end();
       iter != last;
       ++iter) {
    if (iter->event_ == &event) {
      iter2->second.erase(iter);
      break;
    }
  }

  if (iter2->second.empty()) {
    if (!timerChanged_) {
      timerChanged_ = (iter2 == timers_.begin());
    }
    timers_.erase(iter2);
  }
}

size_t PollIoBackend::processTimers() {
  size_t ret = 0;
  uint64_t data = 0;
  // this can fail with but it is OK since the fd
  // will still be readable
  folly::readNoInt(timerFd_, &data, sizeof(data));

  auto now = std::chrono::steady_clock::now();
  while (!timers_.empty() && (now >= timers_.begin()->first)) {
    if (!timerChanged_) {
      timerChanged_ = true;
    }
    auto vec = std::move(timers_.begin()->second);
    timers_.erase(timers_.begin());
    for (auto& entry : vec) {
      ret++;
      eventToTimers_.erase(entry.event_);
      auto* ev = entry.event_->getEvent();
      ev->ev_res = EV_TIMEOUT;
      event_ref_flags(ev).get() = EVLIST_INIT;
      (*event_ref_callback(ev))((int)ev->ev_fd, ev->ev_res, event_ref_arg(ev));
    }
  }

  return ret;
}

void PollIoBackend::addSignalEvent(Event& event) {
  auto* ev = event.getEvent();
  signals_[ev->ev_fd].insert(&event);

  // we pass the write fd for notifications
  sSignalRegistry.setNotifyFd(ev->ev_fd, signalFds_.writeFd());
}

void PollIoBackend::removeSignalEvent(Event& event) {
  auto* ev = event.getEvent();
  auto iter = signals_.find(ev->ev_fd);
  if (iter != signals_.end()) {
    sSignalRegistry.setNotifyFd(ev->ev_fd, -1);
  }
}

size_t PollIoBackend::processSignals() {
  size_t ret = 0;
  static constexpr auto kNumEntries = NSIG * 2;
  static_assert(
      NSIG < 256, "Use a different data type to cover all the signal values");
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
  // add the signal fd(s) back
  addSignalFds();
  return ret;
}

PollIoBackend::IoCb* PollIoBackend::allocIoCb(const EventCallback& cb) {
  // try to allocate from the pool first
  if ((cb.type_ == EventCallback::Type::TYPE_NONE) && (!freeList_.empty())) {
    auto* ret = &freeList_.front();
    freeList_.pop_front();
    numIoCbInUse_++;
    return ret;
  }

  // alloc a new IoCb
  auto* ret = allocNewIoCb(cb);
  if (FOLLY_LIKELY(!!ret)) {
    numIoCbInUse_++;
  }

  return ret;
}

void PollIoBackend::releaseIoCb(PollIoBackend::IoCb* aioIoCb) {
  CHECK_GT(numIoCbInUse_, 0);
  numIoCbInUse_--;
  aioIoCb->cbData_.releaseData();
  // unregister the file descriptor record
  if (aioIoCb->fdRecord_) {
    unregisterFd(aioIoCb->fdRecord_);
    aioIoCb->fdRecord_ = nullptr;
  }

  if (FOLLY_LIKELY(aioIoCb->poolAlloc_)) {
    aioIoCb->event_ = nullptr;
    freeList_.push_front(*aioIoCb);
  } else {
    delete aioIoCb;
  }
}

void PollIoBackend::processPollIo(IoCb* ioCb, int64_t res) noexcept {
  auto* ev = ioCb->event_ ? (ioCb->event_->getEvent()) : nullptr;
  if (ev) {
    if (~event_ref_flags(ev) & EVLIST_INTERNAL) {
      // if this is not a persistent event
      // remove the EVLIST_INSERTED flags
      // and dec the numInsertedEvents_
      if (~ev->ev_events & EV_PERSIST) {
        DCHECK(numInsertedEvents_ > 0);
        numInsertedEvents_--;
        event_ref_flags(ev) &= ~EVLIST_INSERTED;
      }
    }

    // add it to the active list
    event_ref_flags(ev) |= EVLIST_ACTIVE;
    ev->ev_res = res;
    activeEvents_.push_back(*ioCb);
  } else {
    releaseIoCb(ioCb);
  }
}

size_t PollIoBackend::processActiveEvents() {
  size_t ret = 0;
  IoCb* ioCb;

  while (!activeEvents_.empty() && !loopBreak_) {
    bool release = true;
    ioCb = &activeEvents_.front();
    activeEvents_.pop_front();
    ret++;
    auto* event = ioCb->event_;
    auto* ev = event ? event->getEvent() : nullptr;
    if (ev) {
      // remove it from the active list
      event_ref_flags(ev) &= ~EVLIST_ACTIVE;
      bool inserted = (event_ref_flags(ev) & EVLIST_INSERTED);

      // prevent the callback from freeing the aioIoCb
      ioCb->useCount_++;
      if (!ioCb->cbData_.processCb(ev->ev_res)) {
        // adjust the ev_res for the poll case
        ev->ev_res = getPollEvents(ev->ev_res, ev->ev_events);
        // handle spurious poll events that return 0
        // this can happen during high load on process startup
        if (ev->ev_res) {
          (*event_ref_callback(ev))(
              (int)ev->ev_fd, ev->ev_res, event_ref_arg(ev));
        }
      }
      // get the event again
      event = ioCb->event_;
      ev = event ? event->getEvent() : nullptr;
      if (ev && inserted && event_ref_flags(ev) & EVLIST_INSERTED &&
          !shuttingDown_) {
        release = false;
        eb_event_modify_inserted(*event, ioCb);
      }
      ioCb->useCount_--;
    } else {
      ioCb->processActive();
    }
    if (release) {
      releaseIoCb(ioCb);
    }
  }

  return ret;
}

int PollIoBackend::eb_event_base_loop(int flags) {
  // schedule the timers
  bool done = false;
  auto waitForEvents = (flags & EVLOOP_NONBLOCK) ? WaitForEventsMode::DONT_WAIT
                                                 : WaitForEventsMode::WAIT;
  while (!done) {
    scheduleTimeout();
    // check if we need to break here
    if (loopBreak_) {
      loopBreak_ = false;
      break;
    }

    submitList(submitList_, waitForEvents);

    if (!numInsertedEvents_ && timers_.empty() && signals_.empty()) {
      return 1;
    }

    uint64_t call_time = 0;
    if (eb_poll_loop_pre_hook) {
      eb_poll_loop_pre_hook(&call_time);
    }

    // do not wait for events if EVLOOP_NONBLOCK is set
    int ret = getActiveEvents(waitForEvents);

    if (eb_poll_loop_post_hook) {
      eb_poll_loop_post_hook(call_time, ret);
    }

    size_t numProcessedTimers = 0;

    // save the processTimers_
    // this means we've received a notification
    // and we need to add the timer fd back
    bool processTimersFlag = processTimers_;
    if (processTimers_ && !loopBreak_) {
      numProcessedTimers = processTimers();
      processTimers_ = false;
    }

    size_t numProcessedSignals = 0;

    if (processSignals_ && !loopBreak_) {
      numProcessedSignals = processSignals();
      processSignals_ = false;
    }

    if (!activeEvents_.empty() && !loopBreak_) {
      processActiveEvents();
      if (flags & EVLOOP_ONCE) {
        done = true;
      }
    } else if (flags & EVLOOP_NONBLOCK) {
      if (signals_.empty()) {
        done = true;
      }
    }

    if (!done && (numProcessedTimers || numProcessedSignals) &&
        (flags & EVLOOP_ONCE)) {
      done = true;
    }

    if (processTimersFlag) {
      addTimerFd();
    }
  }

  return 0;
}

int PollIoBackend::eb_event_base_loopbreak() {
  loopBreak_ = true;

  return 0;
}

int PollIoBackend::eb_event_add(Event& event, const struct timeval* timeout) {
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

  if ((ev->ev_events & (EV_READ | EV_WRITE)) &&
      !(event_ref_flags(ev) & (EVLIST_INSERTED | EVLIST_ACTIVE))) {
    auto* iocb = allocIoCb(event.getCallback());
    CHECK(iocb);
    iocb->event_ = &event;

    // just append it
    submitList_.push_back(*iocb);
    if (~event_ref_flags(ev) & EVLIST_INTERNAL) {
      numInsertedEvents_++;
    }
    event_ref_flags(ev) |= EVLIST_INSERTED;
    event.setUserData(iocb);
  }

  return 0;
}

int PollIoBackend::eb_event_del(Event& event) {
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

  auto* iocb = reinterpret_cast<IoCb*>(event.getUserData());
  bool wasLinked = iocb->is_linked();
  iocb->resetEvent();

  // if the event is on the active list, we just clear the flags
  // and reset the event_ ptr
  if (event_ref_flags(ev) & EVLIST_ACTIVE) {
    event_ref_flags(ev) &= ~EVLIST_ACTIVE;
  }

  if (event_ref_flags(ev) & EVLIST_INSERTED) {
    event_ref_flags(ev) &= ~EVLIST_INSERTED;

    // not in use  - we can cancel it
    if (!iocb->useCount_ && !wasLinked) {
      // io_cancel will attempt to cancel the event. the result is
      // EINVAL - usually the event has already been delivered
      // EINPROGRESS - cancellation in progress
      // EFAULT - bad ctx
      // regardless, we want to dec the numInsertedEvents_
      // since even if the events get delivered, the event ptr is nullptr
      int ret = cancelOne(iocb);
      if (ret < 0) {
        // release the iocb
        releaseIoCb(iocb);
      }
    } else {
      if (!iocb->useCount_) {
        releaseIoCb(iocb);
      }
    }

    if (~event_ref_flags(ev) & EVLIST_INTERNAL) {
      CHECK_GT(numInsertedEvents_, 0);
      numInsertedEvents_--;
    }

    return 0;
  } else {
    // we can have an EVLIST_ACTIVE event
    // which does not have the EVLIST_INSERTED flag set
    // so we need to release it here
    releaseIoCb(iocb);
  }

  return -1;
}

int PollIoBackend::eb_event_modify_inserted(Event& event, IoCb* ioCb) {
  // unlink and append
  ioCb->unlink();
  submitList_.push_back(*ioCb);
  event.setUserData(ioCb);

  return 0;
}

} // namespace folly
