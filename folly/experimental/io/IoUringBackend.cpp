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

#include <folly/FileUtil.h>
#include <folly/Likely.h>
#include <folly/SpinLock.h>
#include <folly/String.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/experimental/io/IoUringBackend.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/Sockets.h>
#include <folly/synchronization/CallOnce.h>

extern "C" FOLLY_ATTR_WEAK void eb_poll_loop_pre_hook(uint64_t* call_time);
extern "C" FOLLY_ATTR_WEAK void eb_poll_loop_post_hook(
    uint64_t call_time, int ret);

namespace {
struct SignalRegistry {
  struct SigInfo {
    struct sigaction sa_ {};
    size_t refs_{0};
  };
  using SignalMap = std::map<int, SigInfo>;

  constexpr SignalRegistry() {}

  void notify(int sig);
  void setNotifyFd(int sig, int fd);

  // lock protecting the signal map
  folly::MicroSpinLock mapLock_ = {0};
  std::unique_ptr<SignalMap> map_;
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
  if (lk.owns_lock()) {
    int fd = notifyFd_.load();
    if (fd >= 0) {
      uint8_t sigNum = static_cast<uint8_t>(sig);
      ::write(fd, &sigNum, 1);
    }
  }
}

void SignalRegistry::setNotifyFd(int sig, int fd) {
  std::lock_guard<folly::MicroSpinLock> g(mapLock_);
  if (fd >= 0) {
    if (!map_) {
      map_ = std::make_unique<SignalMap>();
    }
    // switch the fd
    notifyFd_.store(fd);

    auto iter = (*map_).find(sig);
    if (iter != (*map_).end()) {
      iter->second.refs_++;
    } else {
      auto& entry = (*map_)[sig];
      entry.refs_ = 1;
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
      if ((iter != (*map_).end()) && (--iter->second.refs_ == 0)) {
        auto entry = iter->second;
        (*map_).erase(iter);
        // just restore
        ::sigaction(sig, &entry.sa_, nullptr);
      }
    }
  }
}

} // namespace

static constexpr int64_t kUnlimitedMlock = -1;
DEFINE_int64(
    io_uring_mlock_size,
    kUnlimitedMlock,
    "Maximum bytes to mlock - use 0 for no changes, -1 for unlimited");

namespace {
class SQGroupInfoRegistry {
 private:
  // a group is a collection of io_uring instances
  // that share up to numThreads SQ poll threads
  struct SQGroupInfo {
    struct SQSubGroupInfo {
      folly::F14FastSet<int> fds;
      size_t count{0};

      void add(int fd) {
        CHECK(fds.find(fd) == fds.end());
        fds.insert(fd);
        ++count;
      }

      size_t remove(int fd) {
        auto iter = fds.find(fd);
        CHECK(iter != fds.end());
        fds.erase(fd);
        --count;

        return count;
      }
    };

    SQGroupInfo(size_t num, std::set<uint32_t> const& cpus) : subGroups(num) {
      for (const uint32_t cpu : cpus) {
        nextCpu.emplace_back(cpu);
      }
    }

    // returns the least loaded subgroup
    SQSubGroupInfo* getNextSubgroup() {
      size_t min_idx = 0;
      for (size_t i = 0; i < subGroups.size(); i++) {
        if (subGroups[i].count == 0) {
          return &subGroups[i];
        }

        if (subGroups[i].count < subGroups[min_idx].count) {
          min_idx = i;
        }
      }

      return &subGroups[min_idx];
    }

    size_t add(int fd, SQSubGroupInfo* sg) {
      CHECK(fdSgMap.find(fd) == fdSgMap.end());
      fdSgMap.insert(std::make_pair(fd, sg));
      sg->add(fd);
      ++count;

      return count;
    }

    size_t remove(int fd) {
      auto iter = fdSgMap.find(fd);
      CHECK(fdSgMap.find(fd) != fdSgMap.end());
      iter->second->remove(fd);
      fdSgMap.erase(iter);
      --count;

      return count;
    }

    // file descriptor to sub group index map
    folly::F14FastMap<int, SQSubGroupInfo*> fdSgMap;
    // array of subgoups
    std::vector<SQSubGroupInfo> subGroups;
    // number of entries
    size_t count{0};
    // Set of CPUs we will bind threads to.
    std::vector<uint32_t> nextCpu;
    int nextCpuIndex{0};
  };

  using SQGroupInfoMap = folly::F14FastMap<std::string, SQGroupInfo>;
  SQGroupInfoMap map_;
  std::mutex mutex_;

 public:
  SQGroupInfoRegistry() = default;
  ~SQGroupInfoRegistry() = default;

  using FDCreateFunc = folly::Function<int(struct io_uring_params&)>;
  using FDCloseFunc = folly::Function<void()>;

  size_t addTo(
      const std::string& groupName,
      size_t groupNumThreads,
      FDCreateFunc& createFd,
      struct io_uring_params& params,
      std::set<uint32_t> const& cpus) {
    if (groupName.empty()) {
      createFd(params);
      return 0;
    }

    std::lock_guard g(mutex_);

    SQGroupInfo::SQSubGroupInfo* sg = nullptr;
    SQGroupInfo* info = nullptr;
    auto iter = map_.find(groupName);
    if (iter != map_.end()) {
      info = &iter->second;
    } else {
      // First use of this group.
      SQGroupInfo gr(groupNumThreads, cpus);
      info =
          &map_.insert(std::make_pair(groupName, std::move(gr))).first->second;
    }
    sg = info->getNextSubgroup();
    if (sg->count) {
      // we're adding to a non empty subgroup
      params.wq_fd = *(sg->fds.begin());
      params.flags |= IORING_SETUP_ATTACH_WQ;
    } else {
      // First use of this subgroup, pin thread to CPU if specified.
      if (info->nextCpu.size()) {
        uint32_t cpu = info->nextCpu[info->nextCpuIndex];
        info->nextCpuIndex = (info->nextCpuIndex + 1) % info->nextCpu.size();

        params.sq_thread_cpu = cpu;
        params.flags |= IORING_SETUP_SQ_AFF;
      }
    }

    auto fd = createFd(params);
    if (fd < 0) {
      return 0;
    }

    return info->add(fd, sg);
  }

  size_t removeFrom(const std::string& groupName, int fd, FDCloseFunc& func) {
    if (groupName.empty()) {
      func();
      return 0;
    }

    size_t ret;

    std::lock_guard g(mutex_);

    func();

    auto iter = map_.find(groupName);
    CHECK(iter != map_.end());
    // check for empty group
    if ((ret = iter->second.remove(fd)) == 0) {
      map_.erase(iter);
    }

    return ret;
  }
};

static folly::Indestructible<SQGroupInfoRegistry> sSQGroupInfoRegistry;

} // namespace

namespace folly {
IoUringBackend::TimerEntry::TimerEntry(
    Event* event, const struct timeval& timeout)
    : event_(event) {
  setExpireTime(timeout, std::chrono::steady_clock::now());
}

IoUringBackend::SocketPair::SocketPair() {
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds_.data())) {
    throw std::runtime_error("socketpair error");
  }

  // set the sockets to non blocking mode
  for (auto fd : fds_) {
    auto flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

IoUringBackend::SocketPair::~SocketPair() {
  for (auto fd : fds_) {
    if (fd >= 0) {
      ::close(fd);
    }
  }
}

IoUringBackend::FdRegistry::FdRegistry(struct io_uring& ioRing, size_t n)
    : ioRing_(ioRing), files_(n, -1), inUse_(n), records_(n) {}

int IoUringBackend::FdRegistry::init() {
  if (inUse_) {
    int ret = ::io_uring_register_files(&ioRing_, files_.data(), inUse_);

    if (!ret) {
      // build and set the free list head if we succeed
      for (size_t i = 0; i < records_.size(); i++) {
        records_[i].idx_ = i;
        free_.push_front(records_[i]);
      }
    } else {
      LOG(ERROR) << "io_uring_register_files(" << inUse_ << ") "
                 << "failed errno = " << errno << ":\""
                 << folly::errnoStr(errno) << "\" " << this;
    }

    return ret;
  }

  return 0;
}

IoUringBackend::FdRegistrationRecord* IoUringBackend::FdRegistry::alloc(
    int fd) {
  if (FOLLY_UNLIKELY(err_ || free_.empty())) {
    return nullptr;
  }

  auto& record = free_.front();

  // now we have an idx
  int ret = ::io_uring_register_files_update(&ioRing_, record.idx_, &fd, 1);
  if (ret != 1) {
    // set the err_ flag so we do not retry again
    // this usually happens when we hit the file desc limit
    // and retrying this operation for every request is expensive
    err_ = true;
    LOG(ERROR) << "io_uring_register_files(1) "
               << "failed errno = " << errno << ":\"" << folly::errnoStr(errno)
               << "\" " << this;
    return nullptr;
  }

  record.fd_ = fd;
  record.count_ = 1;
  free_.pop_front();

  return &record;
}

bool IoUringBackend::FdRegistry::free(
    IoUringBackend::FdRegistrationRecord* record) {
  if (record && (--record->count_ == 0)) {
    record->fd_ = -1;
    int ret = ::io_uring_register_files_update(
        &ioRing_, record->idx_, &record->fd_, 1);

    // we add it to the free list anyway here
    free_.push_front(*record);

    return (ret == 1);
  }

  return false;
}

IoUringBackend::IoUringBackend(Options options)
    : options_(options),
      numEntries_(options.capacity),
      fdRegistry_(ioRing_, options.useRegisteredFds ? options.capacity : 0) {
  // create the timer fd
  timerFd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerFd_ < 0) {
    throw std::runtime_error("timerfd_create error");
  }
  FOLLY_MAYBE_UNUSED static bool sMlockInit = []() {
    int ret = 0;
    if (FLAGS_io_uring_mlock_size) {
      struct rlimit rlim;
      if (FLAGS_io_uring_mlock_size == kUnlimitedMlock) {
        rlim.rlim_cur = RLIM_INFINITY;
        rlim.rlim_max = RLIM_INFINITY;
      } else {
        rlim.rlim_cur = FLAGS_io_uring_mlock_size;
        rlim.rlim_max = FLAGS_io_uring_mlock_size;
      }
      ret = setrlimit(RLIMIT_MEMLOCK, &rlim); // best effort
    }

    return ret ? false : true;
  }();

  ::memset(&ioRing_, 0, sizeof(ioRing_));
  ::memset(&params_, 0, sizeof(params_));

  params_.flags |= IORING_SETUP_CQSIZE;
  params_.cq_entries = options.capacity;

  // poll SQ options
  if (options.flags & Options::Flags::POLL_SQ) {
    params_.flags |= IORING_SETUP_SQPOLL;
    params_.sq_thread_idle = options.sqIdle.count();
  }

  SQGroupInfoRegistry::FDCreateFunc func = [&](struct io_uring_params& params) {
    while (true) {
      // allocate entries both for poll add and cancel
      if (::io_uring_queue_init_params(
              2 * options_.maxSubmit, &ioRing_, &params)) {
        options.capacity /= 2;
        if (options.minCapacity && (options.capacity >= options.minCapacity)) {
          LOG(INFO) << "io_uring_queue_init_params(" << 2 * options_.maxSubmit
                    << "," << params.cq_entries << ") "
                    << "failed errno = " << errno << ":\""
                    << folly::errnoStr(errno) << "\" " << this
                    << " retrying with capacity = " << options.capacity;

          params_.cq_entries = options.capacity;
          numEntries_ = options.capacity;
        } else {
          LOG(ERROR) << "io_uring_queue_init_params(" << 2 * options_.maxSubmit
                     << "," << params.cq_entries << ") "
                     << "failed errno = " << errno << ":\""
                     << folly::errnoStr(errno) << "\" " << this;

          throw NotAvailable("io_uring_queue_init error");
        }
      } else {
        // success - break
        break;
      }
    }

    return ioRing_.ring_fd;
  };

  auto ret = sSQGroupInfoRegistry->addTo(
      options_.sqGroupName,
      options_.sqGroupNumThreads,
      func,
      params_,
      options.sqCpus);

  if (!options_.sqGroupName.empty()) {
    LOG(INFO) << "Adding to SQ poll group \"" << options_.sqGroupName
              << "\" ret = " << ret << " fd = " << ioRing_.ring_fd;
  }

  numEntries_ *= 2;

  // timer entry
  timerEntry_ = std::make_unique<IoSqe>(this, false, true /*persist*/);
  timerEntry_->backendCb_ = IoUringBackend::processTimerIoSqe;

  // signal entry
  signalReadEntry_ = std::make_unique<IoSqe>(this, false, true /*persist*/);
  signalReadEntry_->backendCb_ = IoUringBackend::processSignalReadIoSqe;

  // we need to call the init before adding the timer fd
  // so we avoid a deadlock - waiting for the queue to be drained
  if (options.useRegisteredFds) {
    // now init the file registry
    // if this fails, we still continue since we
    // can run without registered fds
    fdRegistry_.init();
  }

  // delay adding the timer and signal fds until running the loop first time
}

IoUringBackend::~IoUringBackend() {
  shuttingDown_ = true;

  cleanup();

  CHECK(!timerEntry_);
  CHECK(!signalReadEntry_);
  CHECK(freeList_.empty());

  ::close(timerFd_);
}

void IoUringBackend::cleanup() {
  if (ioRing_.ring_fd > 0) {
    // release the nonsubmitted items from the submitList
    while (!submitList_.empty()) {
      auto* ioSqe = &submitList_.front();
      submitList_.pop_front();
      releaseIoSqe(ioSqe);
    }

    // release the active events
    while (!activeEvents_.empty()) {
      auto* ioSqe = &activeEvents_.front();
      activeEvents_.pop_front();
      releaseIoSqe(ioSqe);
    }

    // wait for the outstanding events to finish
    while (numIoSqeInUse()) {
      struct io_uring_cqe* cqe = nullptr;
      ::io_uring_wait_cqe(&ioRing_, &cqe);
      if (cqe) {
        IoSqe* sqe = reinterpret_cast<IoSqe*>(io_uring_cqe_get_data(cqe));
        releaseIoSqe(sqe);
        ::io_uring_cqe_seen(&ioRing_, cqe);
      }
    }

    // free the entries
    timerEntry_.reset();
    signalReadEntry_.reset();
    freeList_.clear_and_dispose([](auto _) { delete _; });

    int fd = ioRing_.ring_fd;
    SQGroupInfoRegistry::FDCloseFunc func = [&]() {
      // exit now
      ::io_uring_queue_exit(&ioRing_);
      ioRing_.ring_fd = -1;
    };

    auto ret = sSQGroupInfoRegistry->removeFrom(
        options_.sqGroupName, ioRing_.ring_fd, func);

    if (!options_.sqGroupName.empty()) {
      LOG(INFO) << "Removing from SQ poll group \"" << options_.sqGroupName
                << "\" ret = " << ret << " fd = " << fd;
    }
  }
}

bool IoUringBackend::isAvailable() {
  static bool sAvailable = true;

  static folly::once_flag initFlag;
  folly::call_once(initFlag, [&]() {
    try {
      Options options;
      options.setCapacity(1024);
      IoUringBackend backend(options);
    } catch (const NotAvailable&) {
      sAvailable = false;
    }
  });

  return sAvailable;
}

bool IoUringBackend::addTimerFd() {
  auto* entry = allocSubmissionEntry(); // this can be nullptr
  timerEntry_->prepPollAdd(entry, timerFd_, POLLIN, true /*registerFd*/);
  return (1 == submitOne());
}

bool IoUringBackend::addSignalFds() {
  auto* entry = allocSubmissionEntry(); // this can be nullptr
  signalReadEntry_->prepPollAdd(
      entry, signalFds_.readFd(), POLLIN, false /*registerFd*/);

  return (1 == submitOne());
}

void IoUringBackend::scheduleTimeout() {
  if (!timerChanged_) {
    return;
  }

  // reset
  timerChanged_ = false;
  if (!timers_.empty()) {
    auto delta = timers_.begin()->second[0].getRemainingTime(
        std::chrono::steady_clock::now());
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

void IoUringBackend::scheduleTimeout(const std::chrono::microseconds& us) {
  struct itimerspec val;
  val.it_interval = {0, 0};
  val.it_value.tv_sec =
      std::chrono::duration_cast<std::chrono::seconds>(us).count();
  val.it_value.tv_nsec =
      std::chrono::duration_cast<std::chrono::nanoseconds>(us).count() %
      1000000000LL;

  CHECK_EQ(::timerfd_settime(timerFd_, 0, &val, nullptr), 0);
}

void IoUringBackend::addTimerEvent(
    Event& event, const struct timeval* timeout) {
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

void IoUringBackend::removeTimerEvent(Event& event) {
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

size_t IoUringBackend::processTimers() {
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

void IoUringBackend::addSignalEvent(Event& event) {
  auto* ev = event.getEvent();
  signals_[ev->ev_fd].insert(&event);

  // we pass the write fd for notifications
  getSignalRegistry().setNotifyFd(ev->ev_fd, signalFds_.writeFd());
}

void IoUringBackend::removeSignalEvent(Event& event) {
  auto* ev = event.getEvent();
  auto iter = signals_.find(ev->ev_fd);
  if (iter != signals_.end()) {
    getSignalRegistry().setNotifyFd(ev->ev_fd, -1);
  }
}

size_t IoUringBackend::processSignals() {
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

IoUringBackend::IoSqe* IoUringBackend::allocIoSqe(const EventCallback& cb) {
  // try to allocate from the pool first
  if ((cb.type_ == EventCallback::Type::TYPE_NONE) && (!freeList_.empty())) {
    auto* ret = &freeList_.front();
    freeList_.pop_front();
    numIoSqeInUse_++;
    return ret;
  }

  // alloc a new IoSqe
  auto* ret = allocNewIoSqe(cb);
  if (FOLLY_LIKELY(!!ret)) {
    numIoSqeInUse_++;
  }

  return ret;
}

void IoUringBackend::releaseIoSqe(IoUringBackend::IoSqe* aioIoSqe) {
  CHECK_GT(numIoSqeInUse_, 0);
  aioIoSqe->cbData_.releaseData();
  // unregister the file descriptor record
  if (aioIoSqe->fdRecord_) {
    unregisterFd(aioIoSqe->fdRecord_);
    aioIoSqe->fdRecord_ = nullptr;
  }

  if (FOLLY_LIKELY(aioIoSqe->poolAlloc_)) {
    numIoSqeInUse_--;
    aioIoSqe->event_ = nullptr;
    freeList_.push_front(*aioIoSqe);
  } else {
    if (!aioIoSqe->persist_) {
      numIoSqeInUse_--;
      delete aioIoSqe;
    }
  }
}

void IoUringBackend::processPollIo(IoSqe* ioSqe, int64_t res) noexcept {
  auto* ev = ioSqe->event_ ? (ioSqe->event_->getEvent()) : nullptr;
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
    activeEvents_.push_back(*ioSqe);
  } else {
    releaseIoSqe(ioSqe);
  }
}

size_t IoUringBackend::processActiveEvents() {
  size_t ret = 0;
  IoSqe* ioSqe;

  while (!activeEvents_.empty() && !loopBreak_) {
    bool release = true;
    ioSqe = &activeEvents_.front();
    activeEvents_.pop_front();
    ret++;
    auto* event = ioSqe->event_;
    auto* ev = event ? event->getEvent() : nullptr;
    if (ev) {
      // remove it from the active list
      event_ref_flags(ev) &= ~EVLIST_ACTIVE;
      bool inserted = (event_ref_flags(ev) & EVLIST_INSERTED);

      // prevent the callback from freeing the aioIoSqe
      ioSqe->useCount_++;
      if (!ioSqe->cbData_.processCb(ev->ev_res)) {
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
      event = ioSqe->event_;
      ev = event ? event->getEvent() : nullptr;
      if (ev && inserted && event_ref_flags(ev) & EVLIST_INSERTED &&
          !shuttingDown_) {
        release = false;
        eb_event_modify_inserted(*event, ioSqe);
      }
      ioSqe->useCount_--;
    } else {
      ioSqe->processActive();
    }
    if (release) {
      releaseIoSqe(ioSqe);
    }
  }

  return ret;
}

int IoUringBackend::eb_event_base_loop(int flags) {
  if (registerDefaultFds_) {
    registerDefaultFds_ = false;
    if (!addTimerFd() || !addSignalFds()) {
      cleanup();
      throw NotAvailable("io_uring_submit error");
    }
  }

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

int IoUringBackend::eb_event_base_loopbreak() {
  loopBreak_ = true;

  return 0;
}

int IoUringBackend::eb_event_add(Event& event, const struct timeval* timeout) {
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
    auto* ioSqe = allocIoSqe(event.getCallback());
    CHECK(ioSqe);
    ioSqe->event_ = &event;

    // just append it
    submitList_.push_back(*ioSqe);
    if (~event_ref_flags(ev) & EVLIST_INTERNAL) {
      numInsertedEvents_++;
    }
    event_ref_flags(ev) |= EVLIST_INSERTED;
    event.setUserData(ioSqe);
  }

  return 0;
}

int IoUringBackend::eb_event_del(Event& event) {
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

  auto* ioSqe = reinterpret_cast<IoSqe*>(event.getUserData());
  bool wasLinked = ioSqe->is_linked();
  ioSqe->resetEvent();

  // if the event is on the active list, we just clear the flags
  // and reset the event_ ptr
  if (event_ref_flags(ev) & EVLIST_ACTIVE) {
    event_ref_flags(ev) &= ~EVLIST_ACTIVE;
  }

  if (event_ref_flags(ev) & EVLIST_INSERTED) {
    event_ref_flags(ev) &= ~EVLIST_INSERTED;

    // not in use  - we can cancel it
    if (!ioSqe->useCount_ && !wasLinked) {
      // io_cancel will attempt to cancel the event. the result is
      // EINVAL - usually the event has already been delivered
      // EINPROGRESS - cancellation in progress
      // EFAULT - bad ctx
      // regardless, we want to dec the numInsertedEvents_
      // since even if the events get delivered, the event ptr is nullptr
      int ret = cancelOne(ioSqe);
      if (ret < 0) {
        // release the ioSqe
        releaseIoSqe(ioSqe);
      }
    } else {
      if (!ioSqe->useCount_) {
        releaseIoSqe(ioSqe);
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
    releaseIoSqe(ioSqe);
  }

  return -1;
}

int IoUringBackend::eb_event_modify_inserted(Event& event, IoSqe* ioSqe) {
  // unlink and append
  ioSqe->unlink();
  submitList_.push_back(*ioSqe);
  event.setUserData(ioSqe);

  return 0;
}

int IoUringBackend::submitOne() {
  return submitBusyCheck(1, WaitForEventsMode::DONT_WAIT);
}

int IoUringBackend::cancelOne(IoSqe* ioSqe) {
  auto* rentry = static_cast<IoSqe*>(allocIoSqe(EventCallback()));
  if (!rentry) {
    return 0;
  }

  auto* sqe = get_sqe();
  CHECK(sqe);

  rentry->prepCancel(sqe, ioSqe); // prev entry

  int ret = submitBusyCheck(1, WaitForEventsMode::DONT_WAIT);

  if (ret < 0) {
    // release the sqe
    releaseIoSqe(rentry);
  }

  return ret;
}

int IoUringBackend::getActiveEvents(WaitForEventsMode waitForEvents) {
  size_t i = 0;
  struct io_uring_cqe* cqe = nullptr;
  // we can be called from the submitList() method
  // or with non blocking flags
  if (FOLLY_LIKELY(waitForEvents == WaitForEventsMode::WAIT)) {
    // if polling the CQ, busy wait for one entry
    if (options_.flags & Options::Flags::POLL_CQ) {
      do {
        ::io_uring_peek_cqe(&ioRing_, &cqe);
        asm_volatile_pause();
        // call the loop callback if installed
        // we call it every time we poll for a CQE
        // regardless of the io_uring_peek_cqe result
        if (cqPollLoopCallback_) {
          cqPollLoopCallback_();
        }
      } while (!cqe);
    } else {
      ::io_uring_wait_cqe(&ioRing_, &cqe);
    }
  } else {
    ::io_uring_peek_cqe(&ioRing_, &cqe);
  }
  while (cqe && (i < options_.maxGet)) {
    i++;
    IoSqe* sqe = reinterpret_cast<IoSqe*>(io_uring_cqe_get_data(cqe));
    sqe->backendCb_(this, sqe, cqe->res);
    ::io_uring_cqe_seen(&ioRing_, cqe);
    cqe = nullptr;
    ::io_uring_peek_cqe(&ioRing_, &cqe);
  }

  return static_cast<int>(i);
}

int IoUringBackend::submitBusyCheck(int num, WaitForEventsMode waitForEvents) {
  int i = 0;
  int res;
  while (i < num) {
    if (waitForEvents == WaitForEventsMode::WAIT) {
      if (options_.flags & Options::Flags::POLL_CQ) {
        res = ::io_uring_submit(&ioRing_);
      } else {
        res = ::io_uring_submit_and_wait(&ioRing_, 1);
      }
    } else {
      res = ::io_uring_submit(&ioRing_);
    }
    if (res == -EBUSY) {
      // if we get EBUSY, try to consume some CQ entries
      getActiveEvents(WaitForEventsMode::DONT_WAIT);
      continue;
    }
    if (res < 0) {
      // continue if interrupted
      if (errno == EINTR) {
        continue;
      }

      return res;
    }

    // we do not have any other entries to submit
    if (res == 0) {
      break;
    }

    i += res;

    // if polling the CQ, busy wait for one entry
    if (waitForEvents == WaitForEventsMode::WAIT &&
        options_.flags & Options::Flags::POLL_CQ && i == num) {
      struct io_uring_cqe* cqe = nullptr;
      while (!cqe) {
        ::io_uring_peek_cqe(&ioRing_, &cqe);
      }
    }
  }

  return num;
}

size_t IoUringBackend::submitList(
    IoSqeList& ioSqes, WaitForEventsMode waitForEvents) {
  int i = 0;
  size_t ret = 0;

  while (!ioSqes.empty()) {
    auto* entry = &ioSqes.front();
    ioSqes.pop_front();
    auto* sqe = get_sqe();
    CHECK(sqe); // this should not happen

    entry->processSubmit(sqe);
    i++;

    if (ioSqes.empty()) {
      int num = submitBusyCheck(i, waitForEvents);
      CHECK_EQ(num, i);
      ret += i;
    } else {
      if (static_cast<size_t>(i) == options_.maxSubmit) {
        int num = submitBusyCheck(i, WaitForEventsMode::DONT_WAIT);
        CHECK_EQ(num, i);
        ret += i;
        i = 0;
      }
    }
  }

  return ret;
}

void IoUringBackend::queueRead(
    int fd, void* buf, unsigned int nbytes, off_t offset, FileOpCallback&& cb) {
  struct iovec iov {
    buf, nbytes
  };
  auto* ioSqe = new ReadIoSqe(this, fd, &iov, offset, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;
  incNumIoSqeInUse();

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueWrite(
    int fd,
    const void* buf,
    unsigned int nbytes,
    off_t offset,
    FileOpCallback&& cb) {
  struct iovec iov {
    const_cast<void*>(buf), nbytes
  };
  auto* ioSqe = new WriteIoSqe(this, fd, &iov, offset, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;
  incNumIoSqeInUse();

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueReadv(
    int fd,
    Range<const struct iovec*> iovecs,
    off_t offset,
    FileOpCallback&& cb) {
  auto* ioSqe = new ReadvIoSqe(this, fd, iovecs, offset, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;
  incNumIoSqeInUse();

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueWritev(
    int fd,
    Range<const struct iovec*> iovecs,
    off_t offset,
    FileOpCallback&& cb) {
  auto* ioSqe = new WritevIoSqe(this, fd, iovecs, offset, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;
  incNumIoSqeInUse();

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueFsync(int fd, FileOpCallback&& cb) {
  queueFsync(fd, FSyncFlags::FLAGS_FSYNC, std::move(cb));
}

void IoUringBackend::queueFdatasync(int fd, FileOpCallback&& cb) {
  queueFsync(fd, FSyncFlags::FLAGS_FDATASYNC, std::move(cb));
}

void IoUringBackend::queueFsync(int fd, FSyncFlags flags, FileOpCallback&& cb) {
  auto* ioSqe = new FSyncIoSqe(this, fd, flags, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;
  incNumIoSqeInUse();

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueOpenat(
    int dfd, const char* path, int flags, mode_t mode, FileOpCallback&& cb) {
  auto* ioSqe = new FOpenAtIoSqe(this, dfd, path, flags, mode, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;
  incNumIoSqeInUse();

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueOpenat2(
    int dfd, const char* path, struct open_how* how, FileOpCallback&& cb) {
  auto* ioSqe = new FOpenAt2IoSqe(this, dfd, path, how, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;
  incNumIoSqeInUse();

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueClose(int fd, FileOpCallback&& cb) {
  auto* ioSqe = new FCloseIoSqe(this, fd, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;
  incNumIoSqeInUse();

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueFallocate(
    int fd, int mode, off_t offset, off_t len, FileOpCallback&& cb) {
  auto* ioSqe = new FAllocateIoSqe(this, fd, mode, offset, len, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;
  incNumIoSqeInUse();

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueSendmsg(
    int fd, const struct msghdr* msg, unsigned int flags, FileOpCallback&& cb) {
  auto* ioSqe = new SendmsgIoSqe(this, fd, msg, flags, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;
  incNumIoSqeInUse();

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueRecvmsg(
    int fd, struct msghdr* msg, unsigned int flags, FileOpCallback&& cb) {
  auto* ioSqe = new RecvmsgIoSqe(this, fd, msg, flags, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;
  incNumIoSqeInUse();

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::processFileOp(IoSqe* sqe, int64_t res) noexcept {
  auto* ioSqe = reinterpret_cast<FileOpIoSqe*>(sqe);
  // save the res
  ioSqe->res_ = res;
  activeEvents_.push_back(*ioSqe);
  numInsertedEvents_--;
}

} // namespace folly
