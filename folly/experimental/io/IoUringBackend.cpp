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

#include <signal.h>

#include <folly/Demangle.h>
#include <folly/FileUtil.h>
#include <folly/GLog.h>
#include <folly/Likely.h>
#include <folly/SpinLock.h>
#include <folly/String.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/experimental/io/IoUringBackend.h>
#include <folly/lang/Bits.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/Sockets.h>
#include <folly/portability/SysMman.h>
#include <folly/portability/SysSyscall.h>
#include <folly/synchronization/CallOnce.h>

#if __has_include(<sys/timerfd.h>)
#include <sys/timerfd.h>
#endif

#if FOLLY_HAS_LIBURING

extern "C" FOLLY_ATTR_WEAK void eb_poll_loop_pre_hook(uint64_t* call_time);
extern "C" FOLLY_ATTR_WEAK void eb_poll_loop_post_hook(
    uint64_t call_time, int ret);

// there is no builtin macro we can use in liburing to tell what version we are
// on or if features are supported. We will try and get this into the next
// release but for now in the latest release there was also added defer_taskrun
// - and so we can use it's pressence to suggest that we can safely use newer
// features
#if defined(IORING_SETUP_DEFER_TASKRUN)
#define FOLLY_IO_URING_UP_TO_DATE 1
#else
#define FOLLY_IO_URING_UP_TO_DATE 0
#endif
namespace folly {

namespace {

#if FOLLY_IO_URING_UP_TO_DATE
int ioUringEnableRings([[maybe_unused]] struct io_uring* ring) {
  // Ideally this would call ::io_uring_enable_rings directly which just runs
  // the below however this was missing from a stable version of liburing, which
  // means that some distributions were not able to compile it. see
  // https://github.com/axboe/liburing/issues/773

  // since it is so simple, just implement it here until the fix rolls out to an
  // acceptable number of OSS distributions.
  return ::io_uring_register(
      ring->ring_fd, IORING_REGISTER_ENABLE_RINGS, nullptr, 0);
}
#endif

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

void checkLogOverflow([[maybe_unused]] struct io_uring* ring) {
#if FOLLY_IO_URING_UP_TO_DATE
  if (::io_uring_cq_has_overflow(ring)) {
    FB_LOG_EVERY_MS(ERROR, 10000)
        << "IoUringBackend " << ring << " cq overflow";
  }
#endif
}

} // namespace

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

std::chrono::time_point<std::chrono::steady_clock> getTimerExpireTime(
    const struct timeval& timeout,
    std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now()) {
  using namespace std::chrono;
  microseconds const us = duration_cast<microseconds>(seconds(timeout.tv_sec)) +
      microseconds(timeout.tv_usec);
  return now + us;
}

#if FOLLY_IO_URING_UP_TO_DATE

class ProvidedBuffersBuffer {
 public:
  static constexpr size_t kHugePageMask = (1LLU << 21) - 1; // 2MB
  static constexpr size_t kPageMask = (1LLU << 12) - 1; // 4095
  static size_t calcBufferSize(int bufferShift) {
    if (bufferShift < 5) {
      bufferShift = 5;
    }
    return 1LLU << bufferShift;
  }

  ProvidedBuffersBuffer(
      int count, int bufferShift, int ringCountShift, bool huge_pages)
      : bufferShift_(bufferShift), bufferCount_(count) {
    // space for the ring
    int ringCount = 1 << ringCountShift;
    ringMask_ = ringCount - 1;
    ringSize_ = sizeof(struct io_uring_buf) * ringCount;

    allSize_ = (ringSize_ + 31) & ~32LLU;

    if (bufferShift_ < 5) {
      bufferShift_ = 5; // for alignment
    }

    sizePerBuffer_ = calcBufferSize(bufferShift_);
    bufferSize_ = sizePerBuffer_ * count;
    allSize_ += bufferSize_;

    int pages;
    if (huge_pages) {
      allSize_ = (allSize_ + kHugePageMask) & (~kHugePageMask);
      pages = allSize_ / (1 + kHugePageMask);
    } else {
      allSize_ = (kPageMask + kPageMask) & ~kPageMask;
      pages = allSize_ / (1 + kPageMask);
    }

    buffer_ = mmap(
        nullptr,
        allSize_,
        PROT_READ | PROT_WRITE,
        MAP_ANONYMOUS | MAP_PRIVATE,
        -1,
        0);

    if (buffer_ == MAP_FAILED) {
      auto errnoCopy = errno;
      throw std::runtime_error(folly::to<std::string>(
          "unable to allocate pages of size ",
          allSize_,
          " pages=",
          pages,
          ": ",
          folly::errnoStr(errnoCopy)));
    }

    bufferBuffer_ = ((char*)buffer_) + ringSize_;
    ringPtr_ = (struct io_uring_buf_ring*)buffer_;

    if (huge_pages) {
      int ret = madvise(buffer_, allSize_, MADV_HUGEPAGE);
      PLOG_IF(ERROR, ret) << "cannot enable huge pages";
    }
  }

  struct io_uring_buf_ring* ring() const noexcept {
    return ringPtr_;
  }

  struct io_uring_buf* ringBuf(int idx) const noexcept {
    return &ringPtr_->bufs[idx & ringMask_];
  }

  uint32_t bufferCount() const noexcept { return bufferCount_; }
  uint32_t ringCount() const noexcept { return 1 + ringMask_; }

  char* buffer(uint16_t idx) {
    size_t offset = (size_t)idx << bufferShift_;
    return bufferBuffer_ + offset;
  }

  ~ProvidedBuffersBuffer() { munmap(buffer_, allSize_); }

  size_t sizePerBuffer() const { return sizePerBuffer_; }

 private:
  void* buffer_;
  size_t allSize_;

  size_t ringSize_;
  struct io_uring_buf_ring* ringPtr_;
  int ringMask_;

  size_t bufferSize_;
  size_t bufferShift_;
  size_t sizePerBuffer_;
  char* bufferBuffer_;
  uint32_t bufferCount_;
};

class ProvidedBufferRing : public IoUringBufferProviderBase {
 public:
  ProvidedBufferRing(
      IoUringBackend* backend,
      uint16_t gid,
      int count,
      int bufferShift,
      int ringSizeShift)
      : IoUringBufferProviderBase(
            gid, ProvidedBuffersBuffer::calcBufferSize(bufferShift)),
        backend_(backend),
        buffer_(count, bufferShift, ringSizeShift, true) {
    if (count > std::numeric_limits<uint16_t>::max()) {
      throw std::runtime_error("too many buffers");
    }
    if (count <= 0) {
      throw std::runtime_error("not enough buffers");
    }

    ioBufCallbacks_.assign((count + (sizeof(void*) - 1)) / sizeof(void*), this);

    initialRegister();

    gottenBuffers_ += count;
    for (int i = 0; i < count; i++) {
      returnBuffer(i);
    }
  }

  void enobuf() noexcept override {
    {
      // what we want to do is something like
      // if (cachedTail_ != localTail_) {
      //   publish();
      //   enobuf_ = false;
      // }
      // but if we are processing a batch it doesn't really work
      // because we'll likely get an ENOBUF straight after
      enobuf_.store(true, std::memory_order_relaxed);
    }
    VLOG_EVERY_N(1, 500) << "enobuf";
  }

  void unusedBuf(uint16_t i) noexcept override {
    gottenBuffers_++;
    returnBuffer(i);
  }

  uint32_t count() const noexcept override { return buffer_.bufferCount(); }

  void destroy() noexcept override {
    ::io_uring_unregister_buf_ring(backend_->ioRingPtr(), gid());
    shutdownReferences_ = 1;
    auto returned = returnedBuffers_.load();
    {
      std::lock_guard<std::mutex> guard(shutdownMutex_);
      wantsShutdown_ = true;
      // add references for every missing one
      // we can assume that there will be no more from the kernel side.
      // there is a race condition here between reading wantsShutdown_ and
      // a return incrementing the number of returned references, but it is very
      // unlikely to trigger as everything is shutting down, so there should not
      // actually be any buffer returns. Worse case this will leak, but since
      // everything is shutting down anyway it should not be a problem.
      uint64_t const gotten = gottenBuffers_;
      DCHECK(gottenBuffers_ >= returned);
      uint32_t outstanding = (gotten - returned);
      shutdownReferences_ += outstanding;
    }
    if (shutdownReferences_.fetch_sub(1) == 1) {
      delete this;
    }
  }

  std::unique_ptr<IOBuf> getIoBuf(uint16_t i, size_t length) noexcept override {
    std::unique_ptr<IOBuf> ret;
    DCHECK(!wantsShutdown_);

    // use a weird convention: userData = ioBufCallbacks_.data() + i
    // ioBufCallbacks_ is just a list of the same pointer, to this
    // so we don't need to malloc anything
    auto free_fn = [](void*, void* userData) {
      size_t pprov = (size_t)userData & ~((size_t)(sizeof(void*) - 1));
      ProvidedBufferRing* prov = *(ProvidedBufferRing**)pprov;
      uint16_t idx = (size_t)userData - (size_t)prov->ioBufCallbacks_.data();
      prov->returnBuffer(idx);
    };

    ret = IOBuf::takeOwnership(
        (void*)getData(i),
        sizePerBuffer_,
        length,
        free_fn,
        (void*)(((size_t)ioBufCallbacks_.data()) + i));
    gottenBuffers_++;
    return ret;
  }

  bool available() const noexcept override {
    return !enobuf_.load(std::memory_order_relaxed);
  }

 private:
  void initialRegister() {
    struct io_uring_buf_reg reg;
    memset(&reg, 0, sizeof(reg));
    reg.ring_addr = (__u64)buffer_.ring();
    reg.ring_entries = buffer_.ringCount();
    reg.bgid = gid();

    int ret = ::io_uring_register_buf_ring(backend_->ioRingPtr(), &reg, 0);

    if (ret) {
      throw IoUringBackend::NotAvailable(folly::to<std::string>(
          "unable to register provided buffer ring ",
          -ret,
          ": ",
          folly::errnoStr(-ret)));
    }
  }

  std::atomic<uint16_t>* sharedTail() {
    return reinterpret_cast<std::atomic<uint16_t>*>(&buffer_.ring()->tail);
  }

  bool tryPublish(uint16_t expected, uint16_t value) noexcept {
    return sharedTail()->compare_exchange_strong(
        expected, value, std::memory_order_release);
  }

  void returnBufferInShutdown() noexcept {
    { std::lock_guard<std::mutex> guard(shutdownMutex_); }
    if (shutdownReferences_.fetch_sub(1) == 1) {
      delete this;
    }
  }

  void returnBuffer(uint16_t i) noexcept {
    if (FOLLY_UNLIKELY(wantsShutdown_)) {
      returnBufferInShutdown();
      return;
    }
    uint16_t this_idx = static_cast<uint16_t>(returnedBuffers_++);
    __u64 addr = (__u64)buffer_.buffer(i);
    uint16_t next_tail = this_idx + 1;
    auto* r = buffer_.ringBuf(this_idx);
    r->addr = addr;
    r->len = buffer_.sizePerBuffer();
    r->bid = i;

    if (tryPublish(this_idx, next_tail)) {
      enobuf_.store(false, std::memory_order_relaxed);
    }
    DVLOG(9) << "returnBuffer(" << i << ")@" << this_idx;
  }

  char const* getData(uint16_t i) { return buffer_.buffer(i); }

  IoUringBackend* backend_;
  ProvidedBuffersBuffer buffer_;
  std::atomic<bool> enobuf_{false};
  std::vector<ProvidedBufferRing*> ioBufCallbacks_;

  uint64_t gottenBuffers_{0};
  std::atomic<uint64_t> returnedBuffers_{0};

  std::atomic<bool> wantsShutdown_{false};
  std::atomic<uint32_t> shutdownReferences_;
  std::mutex shutdownMutex_;
};

template <class... Args>
ProvidedBufferRing::UniquePtr makeProvidedBufferRing(Args&&... args) {
  return ProvidedBufferRing::UniquePtr(
      new ProvidedBufferRing(std::forward<Args>(args)...));
}

#else

template <class... Args>
IoUringBufferProviderBase::UniquePtr makeProvidedBufferRing(Args&&...) {
  throw IoUringBackend::NotAvailable(
      "Provided buffer rings not compiled into this binary");
}

#endif

} // namespace

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
    : ioRing_(ioRing), files_(n, -1), inUse_(n), records_(n) {
  if (n > std::numeric_limits<int>::max()) {
    throw std::runtime_error("too many registered files");
  }
}

int IoUringBackend::FdRegistry::init() {
  if (inUse_) {
    int ret = ::io_uring_register_files(&ioRing_, files_.data(), inUse_);

    if (!ret) {
      // build and set the free list head if we succeed
      for (int i = 0; i < (int)records_.size(); i++) {
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

IoUringFdRegistrationRecord* IoUringBackend::FdRegistry::alloc(
    int fd) noexcept {
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

bool IoUringBackend::FdRegistry::free(IoUringFdRegistrationRecord* record) {
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

FOLLY_ALWAYS_INLINE struct io_uring_sqe* IoUringBackend::getUntrackedSqe() {
  struct io_uring_sqe* ret = ::io_uring_get_sqe(&ioRing_);
  // if running with SQ poll enabled
  // we might have to wait for an sq entry to available
  // before we can submit another one
  while (!ret) {
    if (options_.flags & Options::Flags::POLL_SQ) {
      asm_volatile_pause();
      ret = ::io_uring_get_sqe(&ioRing_);
    } else {
      submitEager();
      ret = ::io_uring_get_sqe(&ioRing_);
      CHECK(ret != nullptr);
    }
  }

  ++waitingToSubmit_;
  return ret;
}

FOLLY_ALWAYS_INLINE struct io_uring_sqe* IoUringBackend::getSqe() {
  ++numInsertedEvents_;
  return getUntrackedSqe();
}

void IoSqeBase::internalSubmit(struct io_uring_sqe* sqe) noexcept {
  if (inFlight_) {
    LOG(ERROR) << "cannot resubmit an IoSqe. type="
               << folly::demangle(typeid(*this));
    return;
  }
  inFlight_ = true;
  processSubmit(sqe);
  ::io_uring_sqe_set_data(sqe, this);
}

void IoSqeBase::internalCallback(int res, uint32_t flags) noexcept {
  if (!(flags & IORING_CQE_F_MORE)) {
    inFlight_ = false;
  }
  if (cancelled_) {
    callbackCancelled(res, flags);
  } else {
    callback(res, flags);
  }
}

FOLLY_ALWAYS_INLINE void IoUringBackend::setProcessTimers() {
  processTimers_ = true;
  --numInternalEvents_;
}

FOLLY_ALWAYS_INLINE void IoUringBackend::setProcessSignals() {
  processSignals_ = true;
  --numInternalEvents_;
}

void IoUringBackend::processPollIoSqe(
    IoUringBackend* backend, IoSqe* ioSqe, int res, uint32_t flags) {
  backend->processPollIo(ioSqe, res, flags);
}

void IoUringBackend::processTimerIoSqe(
    IoUringBackend* backend,
    IoSqe* /*sqe*/,
    int /*res*/,
    uint32_t /* flags */) {
  backend->setProcessTimers();
}

void IoUringBackend::processSignalReadIoSqe(
    IoUringBackend* backend,
    IoSqe* /*sqe*/,
    int /*res*/,
    uint32_t /* flags */) {
  backend->setProcessSignals();
}

IoUringBackend::IoUringBackend(Options options)
    : options_(options),
      numEntries_(options.capacity),
      fdRegistry_(ioRing_, options.registeredFds) {
  // create the timer fd
  timerFd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerFd_ < 0) {
    throw std::runtime_error("timerfd_create error");
  }

  ::memset(&ioRing_, 0, sizeof(ioRing_));
  ::memset(&params_, 0, sizeof(params_));

  params_.flags |= IORING_SETUP_CQSIZE;
  params_.cq_entries = options.capacity;

#if FOLLY_IO_URING_UP_TO_DATE
  if (options_.taskRunCoop) {
    params_.flags |= IORING_SETUP_COOP_TASKRUN;
  }
  if (options_.deferTaskRun && kernelSupportsDeferTaskrun()) {
    params_.flags |= IORING_SETUP_SINGLE_ISSUER;
    params_.flags |= IORING_SETUP_DEFER_TASKRUN;
    params_.flags |= IORING_SETUP_SUBMIT_ALL;
    params_.flags |= IORING_SETUP_R_DISABLED;
    usingDeferTaskrun_ = true;
  }
#endif

  // poll SQ options
  if (options.flags & Options::Flags::POLL_SQ) {
    params_.flags |= IORING_SETUP_SQPOLL;
    params_.sq_thread_idle = options.sqIdle.count();
  }

  SQGroupInfoRegistry::FDCreateFunc func = [&](struct io_uring_params& params) {
    while (true) {
      // allocate entries both for poll add and cancel
      size_t sqeSize =
          options_.sqeSize > 0 ? options_.sqeSize : 2 * options_.maxSubmit;
      int ret = ::io_uring_queue_init_params(sqeSize, &ioRing_, &params);
      if (ret) {
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
                     << "failed ret = " << ret << ":\"" << folly::errnoStr(ret)
                     << "\" " << this;

          if (ret == -ENOMEM) {
            throw std::runtime_error("io_uring_queue_init error out of memory");
          }
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
  // delay adding the timer and signal fds until running the loop first time

  if (!usingDeferTaskrun_) {
    // can do this now, no need to delay if not using IORING_SETUP_SINGLE_ISSUER
    initSubmissionLinked();
  }
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
  if (ioRing_.ring_fd <= 0) {
    return;
  }

  // release the nonsubmitted items from the submitList
  auto processSubmitList = [&]() {
    IoSqeBaseList mustSubmitList;
    while (!submitList_.empty()) {
      auto* ioSqe = &submitList_.front();
      submitList_.pop_front();
      if (IoSqe* i = dynamic_cast<IoSqe*>(ioSqe); i) {
        releaseIoSqe(i);
      } else {
        mustSubmitList.push_back(*ioSqe);
      }
    }
    prepList(mustSubmitList);
  };

  // wait for the outstanding events to finish
  processSubmitList();
  while (isWaitingToSubmit() || numInsertedEvents_ > numInternalEvents_) {
    struct io_uring_cqe* cqe = nullptr;
    processSubmitList();
    int ret = submitEager();
    if (ret == -EEXIST) {
      LOG(ERROR) << "using DeferTaskrun, but submitting from the wrong thread";
      break;
    }

    bool const canContinue = ret != -EEXIST && ret != -EBADR;
    if (canContinue) {
      ::io_uring_wait_cqe(&ioRing_, &cqe);
    }
    internalProcessCqe(
        std::numeric_limits<unsigned int>::max(),
        InternalProcessCqeMode::CANCEL_ALL);

    if (!canContinue) {
      LOG(ERROR) << "Submit resulted in : " << folly::errnoStr(-ret)
                 << " not cleanly shutting down IoUringBackend";
      break;
    }
  }

  // release the active events
  while (!activeEvents_.empty()) {
    auto* ioSqe = &activeEvents_.front();
    activeEvents_.pop_front();
    releaseIoSqe(ioSqe);
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
  submitOutstanding();
  auto* entry = getSqe();
  timerEntry_->prepPollAdd(entry, timerFd_, POLLIN);
  ++numInternalEvents_;
  return (1 == submitOne());
}

bool IoUringBackend::addSignalFds() {
  submitOutstanding();
  auto* entry = getSqe();
  signalReadEntry_->prepPollAdd(entry, signalFds_.readFd(), POLLIN);
  ++numInternalEvents_;
  return (1 == submitOne());
}

void IoUringBackend::scheduleTimeout() {
  if (!timerChanged_) {
    return;
  }

  // reset
  timerChanged_ = false;
  if (!timers_.empty()) {
    auto delta = std::chrono::duration_cast<std::chrono::microseconds>(
        timers_.begin()->first - std::chrono::steady_clock::now());
    if (delta < std::chrono::microseconds(1000)) {
      delta = std::chrono::microseconds(1000);
    }
    scheduleTimeout(delta);
  } else if (timerSet_) {
    scheduleTimeout(std::chrono::microseconds(0)); // disable
  }

  // we do not call addTimerFd() here
  // since it has to be added only once, after
  // we process a poll callback
}

void IoUringBackend::scheduleTimeout(const std::chrono::microseconds& us) {
  struct itimerspec val;
  timerSet_ = us.count() != 0;
  val.it_interval = {0, 0};
  val.it_value.tv_sec =
      std::chrono::duration_cast<std::chrono::seconds>(us).count();
  val.it_value.tv_nsec =
      std::chrono::duration_cast<std::chrono::nanoseconds>(us).count() %
      1000000000LL;

  CHECK_EQ(::timerfd_settime(timerFd_, 0, &val, nullptr), 0);
}

namespace {

struct TimerUserData {
  std::multimap<std::chrono::steady_clock::time_point, IoUringBackend::Event*>::
      const_iterator iter;
};

void timerUserDataFreeFunction(void* v) {
  delete (TimerUserData*)(v);
}

} // namespace

void IoUringBackend::addTimerEvent(
    Event& event, const struct timeval* timeout) {
  auto expire = getTimerExpireTime(*timeout);

  TimerUserData* td = (TimerUserData*)event.getUserData();
  DVLOG(6) << "addTimerEvent this=" << this << " event=" << &event
           << " td=" << td << " changed_=" << timerChanged_
           << " u=" << timeout->tv_usec;
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
    DVLOG(6) << "addTimerEvent::alloc " << td << " event=" << &event;
    event.setUserData(td, timerUserDataFreeFunction);
  }
  timerChanged_ |= td->iter == timers_.begin();
}

void IoUringBackend::removeTimerEvent(Event& event) {
  TimerUserData* td = (TimerUserData*)event.getUserData();
  DVLOG(6) << "removeTimerEvent this=" << this << " event=" << &event
           << " td=" << td;
  CHECK(td && event.getFreeFunction() == timerUserDataFreeFunction);
  timerChanged_ |= td->iter == timers_.begin();
  timers_.erase(td->iter);
  td->iter = timers_.end();
  event.setUserData(nullptr, nullptr);
  delete td;
}

size_t IoUringBackend::processTimers() {
  DVLOG(3) << "IoUringBackend::processTimers " << timers_.size();
  size_t ret = 0;
  uint64_t data = 0;
  // this can fail with but it is OK since the fd
  // will still be readable
  folly::readNoInt(timerFd_, &data, sizeof(data));

  auto now = std::chrono::steady_clock::now();
  while (true) {
    auto it = timers_.begin();
    if (it == timers_.end() || now < it->first) {
      break;
    }
    timerChanged_ = true;
    Event* e = it->second;
    TimerUserData* td = (TimerUserData*)e->getUserData();
    DVLOG(5) << "processTimer " << e << " td=" << td;
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

  DVLOG(3) << "IoUringBackend::processTimers done, changed= " << timerChanged_
           << " count=" << ret;
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
    return ret;
  }

  // alloc a new IoSqe
  return allocNewIoSqe(cb);
}

void IoUringBackend::releaseIoSqe(IoUringBackend::IoSqe* aioIoSqe) noexcept {
  aioIoSqe->cbData_.releaseData();
  // unregister the file dsecriptor record
  if (aioIoSqe->fdRecord_) {
    unregisterFd(aioIoSqe->fdRecord_);
    aioIoSqe->fdRecord_ = nullptr;
  }

  if (FOLLY_LIKELY(aioIoSqe->poolAlloc_)) {
    aioIoSqe->event_ = nullptr;
    freeList_.push_front(*aioIoSqe);
  } else if (!aioIoSqe->persist_) {
    delete aioIoSqe;
  }
}

void IoUringBackend::IoSqe::release() noexcept {
  backend_->releaseIoSqe(this);
}

void IoUringBackend::processPollIo(
    IoSqe* ioSqe, int res, uint32_t flags) noexcept {
  auto* ev = ioSqe->event_ ? (ioSqe->event_->getEvent()) : nullptr;
  if (ev) {
    if (flags & IORING_CQE_F_MORE) {
      ioSqe->useCount_++;
      SCOPE_EXIT { ioSqe->useCount_--; };
      if (ioSqe->cbData_.processCb(this, res, flags)) {
        return;
      }
    }

    // if this is not a persistent event
    // remove the EVLIST_INSERTED flags
    if (!(ev->ev_events & EV_PERSIST)) {
      event_ref_flags(ev) &= ~EVLIST_INSERTED;
    }

    if (event_ref_flags(ev) & EVLIST_INTERNAL) {
      DCHECK_GT(numInternalEvents_, 0);
      --numInternalEvents_;
    }

    // add it to the active list
    event_ref_flags(ev) |= EVLIST_ACTIVE;

    // only clamp upper bound, as no error codes are smaller than short min
    ev->ev_res = static_cast<short>(
        std::min<int64_t>(res, std::numeric_limits<short>::max()));

    ioSqe->res_ = res;
    ioSqe->cqeFlags_ = flags;
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
      if (!ioSqe->cbData_.processCb(this, ioSqe->res_, ioSqe->cqeFlags_)) {
        // adjust the ev_res for the poll case
        ev->ev_res = getPollEvents(ioSqe->res_, ev->ev_events);
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

void IoUringBackend::submitOutstanding() {
  delayedInit();
  prepList(submitList_);
  submitEager();
}

unsigned int IoUringBackend::processCompleted() {
  return internalProcessCqe(
      options_.maxGet, InternalProcessCqeMode::AVAILABLE_ONLY);
}

size_t IoUringBackend::loopPoll() {
  delayedInit();
  prepList(submitList_);
  size_t ret = getActiveEvents(WaitForEventsMode::DONT_WAIT);
  processActiveEvents();
  return ret;
}

void IoUringBackend::dCheckSubmitTid() {
  if (!kIsDebug) {
    // lets only check this in DEBUG
    return;
  }
  if (!usingDeferTaskrun_) {
    // only care in defer_taskrun mode
    return;
  }
  if (!submitTid_) {
    submitTid_ = std::this_thread::get_id();
  } else {
    DCHECK_EQ(*submitTid_, std::this_thread::get_id())
        << "Cannot change submit/reap threads with DeferTaskrun";
  }
}

void IoUringBackend::initSubmissionLinked() {
  // we need to call the init before adding the timer fd
  // so we avoid a deadlock - waiting for the queue to be drained
  if (options_.registeredFds > 0) {
    // now init the file registry
    // if this fails, we still continue since we
    // can run without registered fds
    fdRegistry_.init();
  }

  if (options_.initalProvidedBuffersCount) {
    auto get_shift = [](int x) -> int {
      int shift = findLastSet(x) - 1;
      if (x != (1 << shift)) {
        shift++;
      }
      return shift;
    };

    int sizeShift =
        std::max<int>(get_shift(options_.initalProvidedBuffersEachSize), 5);
    int ringShift =
        std::max<int>(get_shift(options_.initalProvidedBuffersCount), 1);

    bufferProvider_ = makeProvidedBufferRing(
        this,
        nextBufferProviderGid(),
        options_.initalProvidedBuffersCount,
        sizeShift,
        ringShift);
  }
}

void IoUringBackend::delayedInit() {
  dCheckSubmitTid();

  if (FOLLY_LIKELY(!needsDelayedInit_)) {
    return;
  }

  needsDelayedInit_ = false;

  if (usingDeferTaskrun_) {
    // usingDeferTaskrun_ is guarded already on having an up to date liburing
#if FOLLY_IO_URING_UP_TO_DATE
    int ret = ioUringEnableRings(&ioRing_);
    if (ret) {
      LOG(ERROR) << "io_uring_enable_rings gave " << folly::errnoStr(-ret);
    }
#else
    LOG(ERROR)
        << "Unexpectedly usingDeferTaskrun_=true, but liburing does not support it?";
#endif
    initSubmissionLinked();
  }

  if (options_.registerRingFd) {
    // registering just has some perf impact, so no need to fall back
#if FOLLY_IO_URING_UP_TO_DATE
    if (io_uring_register_ring_fd(&ioRing_) < 0) {
      LOG(ERROR) << "unable to register io_uring ring fd";
    }
#endif
  }
  if (!addTimerFd() || !addSignalFds()) {
    cleanup();
    throw NotAvailable("io_uring_submit error");
  }
}

int IoUringBackend::eb_event_base_loop(int flags) {
  delayedInit();

  const auto waitForEvents = (flags & EVLOOP_NONBLOCK)
      ? WaitForEventsMode::DONT_WAIT
      : WaitForEventsMode::WAIT;

  bool hadEvents = true;
  for (bool done = false; !done;) {
    scheduleTimeout();

    // check if we need to break here
    if (loopBreak_) {
      loopBreak_ = false;
      return 0;
    }

    prepList(submitList_);

    if (numInternalEvents_ == numInsertedEvents_ && timers_.empty() &&
        signals_.empty()) {
      VLOG(2) << "IoUringBackend::eb_event_base_loop nothing to do";
      return 1;
    }

    uint64_t call_time = 0;
    if (eb_poll_loop_pre_hook) {
      eb_poll_loop_pre_hook(&call_time);
    }

    // do not wait for events if EVLOOP_NONBLOCK is set
    size_t processedEvents = getActiveEvents(waitForEvents);

    if (eb_poll_loop_post_hook) {
      eb_poll_loop_post_hook(call_time, static_cast<int>(processedEvents));
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

    hadEvents = numProcessedTimers || numProcessedSignals || processedEvents;
    if (hadEvents && (flags & EVLOOP_ONCE)) {
      done = true;
    }

    VLOG(2) << "IoUringBackend::eb_event_base_loop processedEvents="
            << processedEvents << " numProcessedSignals=" << numProcessedSignals
            << " numProcessedTimers=" << numProcessedTimers << " done=" << done;

    if (processTimersFlag) {
      addTimerFd();
    }
  }

  return hadEvents ? 0 : 2;
}

int IoUringBackend::eb_event_base_loopbreak() {
  loopBreak_ = true;

  return 0;
}

int IoUringBackend::eb_event_add(Event& event, const struct timeval* timeout) {
  DVLOG(4) << "Add event " << &event;
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
    if (event_ref_flags(ev) & EVLIST_INTERNAL) {
      numInternalEvents_++;
    }
    event_ref_flags(ev) |= EVLIST_INSERTED;
    event.setUserData(ioSqe);
  }

  return 0;
}

int IoUringBackend::eb_event_del(Event& event) {
  DVLOG(4) << "Del event " << &event;
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

    if (event_ref_flags(ev) & EVLIST_INTERNAL) {
      DCHECK_GT(numInternalEvents_, 0);
      numInternalEvents_--;
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
  DVLOG(4) << "Modify event " << &event;
  // unlink and append
  ioSqe->unlink();
  if (event_ref_flags(event.getEvent()) & EVLIST_INTERNAL) {
    numInternalEvents_++;
  }
  submitList_.push_back(*ioSqe);
  event.setUserData(ioSqe);

  return 0;
}

void IoUringBackend::submitNextLoop(IoSqeBase& ioSqe) noexcept {
  submitList_.push_back(ioSqe);
}

void IoUringBackend::submitImmediateIoSqe(IoSqeBase& ioSqe) {
  if (options_.flags &
      (Options::Flags::POLL_SQ | Options::Flags::POLL_SQ_IMMEDIATE_IO)) {
    submitNow(ioSqe);
  } else {
    submitList_.push_back(ioSqe);
  }
}

int IoUringBackend::submitOne() {
  return submitBusyCheck(1, WaitForEventsMode::DONT_WAIT);
}

void IoUringBackend::submitNow(IoSqeBase& ioSqe) {
  internalSubmit(ioSqe);
  submitBusyCheck(waitingToSubmit_, WaitForEventsMode::DONT_WAIT);
}

void IoUringBackend::internalSubmit(IoSqeBase& ioSqe) noexcept {
  auto* sqe = getSqe();
  setSubmitting();
  ioSqe.internalSubmit(sqe);
  if (ioSqe.type() == IoSqeBase::Type::Write) {
    numSendEvents_++;
  }
  doneSubmitting();
}

void IoUringBackend::submitSoon(IoSqeBase& ioSqe) noexcept {
  internalSubmit(ioSqe);
  if (waitingToSubmit_ >= options_.maxSubmit) {
    submitBusyCheck(waitingToSubmit_, WaitForEventsMode::DONT_WAIT);
  }
}

void IoUringBackend::cancel(IoSqeBase* ioSqe) {
  bool skip = false;
  ioSqe->markCancelled();
  auto* sqe = getUntrackedSqe();
  ::io_uring_prep_cancel64(sqe, (uint64_t)ioSqe, 0);
  ::io_uring_sqe_set_data(sqe, nullptr);
#if FOLLY_IO_URING_UP_TO_DATE
  if (params_.features & IORING_FEAT_CQE_SKIP) {
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    skip = true;
  }
#endif
  DVLOG(4) << "Cancel " << ioSqe << " skip=" << skip;
}

int IoUringBackend::cancelOne(IoSqe* ioSqe) {
  auto* rentry = static_cast<IoSqe*>(allocIoSqe(EventCallback()));
  if (!rentry) {
    return 0;
  }

  auto* sqe = getSqe();
  rentry->prepCancel(sqe, ioSqe); // prev entry

  int ret = submitBusyCheck(waitingToSubmit_, WaitForEventsMode::DONT_WAIT);

  if (ret < 0) {
    // release the sqe
    releaseIoSqe(rentry);
  }

  return ret;
}

size_t IoUringBackend::getActiveEvents(WaitForEventsMode waitForEvents) {
  struct io_uring_cqe* cqe;

  setGetActiveEvents();
  SCOPE_EXIT { doneGetActiveEvents(); };

  auto inner_do_wait = [&]() -> int {
    if (waitingToSubmit_) {
      submitBusyCheck(waitingToSubmit_, WaitForEventsMode::WAIT);
      int ret = ::io_uring_peek_cqe(&ioRing_, &cqe);
      return ret;
    } else if (useReqBatching()) {
      struct __kernel_timespec timeout;
      timeout.tv_sec = 0;
      timeout.tv_nsec = options_.timeout * 1000;
      int ret = ::io_uring_wait_cqes(
          &ioRing_, &cqe, options_.batchSize, &timeout, nullptr);
      return ret;
    } else {
      int ret = ::io_uring_wait_cqe(&ioRing_, &cqe);
      return ret;
    }
  };
  auto do_wait = [&]() -> int {
    if (kIsDebug && VLOG_IS_ON(1)) {
      std::chrono::steady_clock::time_point start;
      start = std::chrono::steady_clock::now();
      unsigned was = ::io_uring_cq_ready(&ioRing_);
      auto was_submit = waitingToSubmit_;
      int ret = inner_do_wait();
      auto end = std::chrono::steady_clock::now();
      std::chrono::microseconds const micros =
          std::chrono::duration_cast<std::chrono::microseconds>(end - start);
      if (micros.count()) {
        VLOG(1) << "wait took " << micros.count()
                << "us have=" << ::io_uring_cq_ready(&ioRing_) << " was=" << was
                << " submit_len=" << was_submit;
      }
      return ret;
    } else {
      return inner_do_wait();
    }
  };
  auto do_peek = [&]() -> int {
    if (usingDeferTaskrun_) {
#if FOLLY_IO_URING_UP_TO_DATE
      return ::io_uring_get_events(&ioRing_);
#endif
    }
    return ::io_uring_peek_cqe(&ioRing_, &cqe);
  };

  int ret;
  // we can be called from the submitList() method
  // or with non blocking flags
  do {
    if (waitForEvents == WaitForEventsMode::WAIT) {
      // if polling the CQ, busy wait for one entry
      if (options_.flags & Options::Flags::POLL_CQ) {
        if (waitingToSubmit_) {
          submitBusyCheck(waitingToSubmit_, WaitForEventsMode::DONT_WAIT);
        }
        do {
          ret = ::io_uring_peek_cqe(&ioRing_, &cqe);
          asm_volatile_pause();
          // call the loop callback if installed
          // we call it every time we poll for a CQE
          // regardless of the io_uring_peek_cqe result
          if (cqPollLoopCallback_) {
            cqPollLoopCallback_();
          }
        } while (ret);
      } else {
        ret = do_wait();
      }
    } else {
      if (waitingToSubmit_) {
        ret = submitBusyCheck(waitingToSubmit_, WaitForEventsMode::DONT_WAIT);
      } else {
        ret = do_peek();
      }
    }
  } while (ret == -EINTR);

  if (ret == -EBADR) {
    // cannot recover from droped CQE
    folly::terminate_with<std::runtime_error>("BADR");
  } else if (ret == -EAGAIN) {
    return 0;
  } else if (ret == -ETIME) {
    if (cqe == nullptr) {
      return 0;
    }
  } else if (ret < 0) {
    LOG(ERROR) << "wait_cqe error: " << ret;
    return 0;
  }

  return internalProcessCqe(options_.maxGet, InternalProcessCqeMode::NORMAL);
}

unsigned int IoUringBackend::internalProcessCqe(
    unsigned int maxGet, InternalProcessCqeMode mode) noexcept {
  struct io_uring_cqe* cqe;

  unsigned int count_more = 0;
  unsigned int count = 0;
  unsigned int count_send = 0;

  checkLogOverflow(&ioRing_);
  do {
    unsigned int head;
    unsigned int loop_count = 0;
    io_uring_for_each_cqe(&ioRing_, head, cqe) {
      loop_count++;
      if (cqe->flags & IORING_CQE_F_MORE) {
        count_more++;
      }
      IoSqeBase* sqe = reinterpret_cast<IoSqeBase*>(cqe->user_data);
      if (cqe->user_data) {
        count++;
        if (sqe->type() == IoSqeBase::Type::Write) {
          count_send++;
        }
        if (FOLLY_UNLIKELY(mode == InternalProcessCqeMode::CANCEL_ALL)) {
          sqe->markCancelled();
        }
        sqe->internalCallback(cqe->res, cqe->flags);
      } else {
        // untracked, do not increment count
      }
      if (count >= options_.maxGet) {
        break;
      }
    }
    if (!loop_count) {
      break;
    }
    io_uring_cq_advance(&ioRing_, loop_count);
    if (count >= maxGet) {
      break;
    }

    // io_uring_peek_cqe will check for any overflows and copy them to the cq
    // ring.
    if (mode != InternalProcessCqeMode::AVAILABLE_ONLY) {
      int ret = ::io_uring_peek_cqe(&ioRing_, &cqe);
      if (ret == -EBADR) {
        // cannot recover from droped CQE
        folly::terminate_with<std::runtime_error>("BADR");
      } else if (ret) {
        break;
      }
    }
  } while (true);
  numInsertedEvents_ -= (count - count_more);
  numSendEvents_ -= count_send;
  return count;
}

int IoUringBackend::submitEager() {
  int res;
  DCHECK(!isSubmitting()) << "mid processing a submit, cannot submit";
  do {
    res = ::io_uring_submit(&ioRing_);
  } while (res == -EINTR);
  VLOG(2) << "IoUringBackend::submitEager() " << waitingToSubmit_;
  if (res >= 0) {
    DCHECK((int)waitingToSubmit_ >= res);
    waitingToSubmit_ -= res;
  }
  return res;
}

int IoUringBackend::submitBusyCheck(
    int num, WaitForEventsMode waitForEvents) noexcept {
  int i = 0;
  int res;
  DCHECK(!isSubmitting()) << "mid processing a submit, cannot submit";
  while (i < num) {
    VLOG(2) << "IoUringBackend::submit() " << waitingToSubmit_;

    if (waitForEvents == WaitForEventsMode::WAIT) {
      if (options_.flags & Options::Flags::POLL_CQ) {
        res = ::io_uring_submit(&ioRing_);
      } else {
        if (useReqBatching()) {
          struct io_uring_cqe* cqe;
          struct __kernel_timespec timeout;
          timeout.tv_sec = 0;
          timeout.tv_nsec = options_.timeout * 1000;
          res = ::io_uring_submit_and_wait_timeout(
              &ioRing_,
              &cqe,
              options_.batchSize + numSendEvents_,
              &timeout,
              nullptr);
        } else {
          res = ::io_uring_submit_and_wait(&ioRing_, 1);
        }
        if (res >= 0) {
          // no more waiting
          waitForEvents = WaitForEventsMode::DONT_WAIT;
        }
      }
    } else {
#if FOLLY_IO_URING_UP_TO_DATE
      if (usingDeferTaskrun_) {
        // usingDeferTaskrun_ implies SUBMIT_ALL, and we definitely
        // want to do get_events() to process outstanding work
        res = ::io_uring_submit_and_get_events(&ioRing_);
        if (res >= 0) {
          // this is ok since we are using SUBMIT_ALL
          i = waitingToSubmit_;
          break;
        }
      } else {
        res = ::io_uring_submit(&ioRing_);
      }
#else
      res = ::io_uring_submit(&ioRing_);
#endif
    }

    if (res < 0) {
      // continue if interrupted
      if (res == -EINTR || errno == EINTR) {
        continue;
      }
      CHECK_NE(res, -EBADR);
      if (res == -EEXIST) {
        FB_LOG_EVERY_MS(ERROR, 1000)
            << "Received -EEXIST, likely calling get_events/submit "
            << " from the wrong thread with DeferTaskrun enabled";
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

  DCHECK((int)waitingToSubmit_ >= i);
  waitingToSubmit_ -= i;
  return num;
}

size_t IoUringBackend::prepList(IoSqeBaseList& ioSqes) {
  int i = 0;

  while (!ioSqes.empty()) {
    if (static_cast<size_t>(i) == options_.maxSubmit) {
      int num = submitBusyCheck(i, WaitForEventsMode::DONT_WAIT);
      CHECK_EQ(num, i);
      i = 0;
    }

    auto* entry = &ioSqes.front();
    ioSqes.pop_front();
    auto* sqe = getSqe();
    entry->internalSubmit(sqe);
    i++;
  }

  return i;
}

void IoUringBackend::queueRead(
    int fd, void* buf, unsigned int nbytes, off_t offset, FileOpCallback&& cb) {
  struct iovec iov {
    buf, nbytes
  };
  auto* ioSqe = new ReadIoSqe(this, fd, &iov, offset, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;

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

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueReadv(
    int fd,
    Range<const struct iovec*> iovecs,
    off_t offset,
    FileOpCallback&& cb) {
  auto* ioSqe = new ReadvIoSqe(this, fd, iovecs, offset, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueWritev(
    int fd,
    Range<const struct iovec*> iovecs,
    off_t offset,
    FileOpCallback&& cb) {
  auto* ioSqe = new WritevIoSqe(this, fd, iovecs, offset, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;

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

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueOpenat(
    int dfd, const char* path, int flags, mode_t mode, FileOpCallback&& cb) {
  auto* ioSqe = new FOpenAtIoSqe(this, dfd, path, flags, mode, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueOpenat2(
    int dfd, const char* path, struct open_how* how, FileOpCallback&& cb) {
  auto* ioSqe = new FOpenAt2IoSqe(this, dfd, path, how, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueClose(int fd, FileOpCallback&& cb) {
  auto* ioSqe = new FCloseIoSqe(this, fd, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueStatx(
    int dirfd,
    const char* pathname,
    int flags,
    unsigned int mask,
    struct statx* statxbuf,
    FileOpCallback&& cb) {
  auto* ioSqe = new FStatxIoSqe(
      this, dirfd, pathname, flags, mask, statxbuf, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueFallocate(
    int fd, int mode, off_t offset, off_t len, FileOpCallback&& cb) {
  auto* ioSqe = new FAllocateIoSqe(this, fd, mode, offset, len, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueSendmsg(
    int fd, const struct msghdr* msg, unsigned int flags, FileOpCallback&& cb) {
  auto* ioSqe = new SendmsgIoSqe(this, fd, msg, flags, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::queueRecvmsg(
    int fd, struct msghdr* msg, unsigned int flags, FileOpCallback&& cb) {
  auto* ioSqe = new RecvmsgIoSqe(this, fd, msg, flags, std::move(cb));
  ioSqe->backendCb_ = processFileOpCB;

  submitImmediateIoSqe(*ioSqe);
}

void IoUringBackend::processFileOp(IoSqe* sqe, int res) noexcept {
  auto* ioSqe = reinterpret_cast<FileOpIoSqe*>(sqe);
  // save the res
  ioSqe->res_ = res;
  activeEvents_.push_back(*ioSqe);
}

bool IoUringBackend::kernelHasNonBlockWriteFixes() const {
#if FOLLY_IO_URING_UP_TO_DATE
  // this was fixed in 5.18, which introduced linked file
  // fixed in "io_uring: only wake when the correct events are set"
  return params_.features & IORING_FEAT_LINKED_FILE;
#else
  // this indicates that sockets have to manually remove O_NONBLOCK
  // which is a bit slower but shouldnt cause any functional changes
  return false;
#endif
}

namespace {

static bool doKernelSupportsRecvmsgMultishot() {
  try {
    struct S : IoSqeBase {
      explicit S(IoUringBufferProviderBase* bp) : bp_(bp) {
        fd = open("/dev/null", O_RDONLY);
        memset(&msg, 0, sizeof(msg));
      }
      ~S() override {
        if (fd >= 0) {
          close(fd);
        }
      }
      void processSubmit(struct io_uring_sqe* sqe) noexcept override {
        io_uring_prep_recvmsg(sqe, fd, &msg, 0);

        sqe->buf_group = bp_->gid();
        sqe->flags |= IOSQE_BUFFER_SELECT;

        // see note in prepRecvmsgMultishot
        constexpr uint16_t kMultishotFlag = 1U << 1;
        sqe->ioprio |= kMultishotFlag;
      }

      void callback(int res, uint32_t) noexcept override {
        supported = res != -EINVAL;
      }

      void callbackCancelled(int, uint32_t) noexcept override { delete this; }

      IoUringBufferProviderBase* bp_;
      bool supported = false;
      struct msghdr msg;
      int fd = -1;
    };

    std::unique_ptr<S> s;
    IoUringBackend io(
        IoUringBackend::Options().setInitialProvidedBuffers(1024, 1));
    if (!io.bufferProvider()) {
      return false;
    }
    s = std::make_unique<S>(io.bufferProvider());
    io.submitNow(*s);
    io.eb_event_base_loop(EVLOOP_NONBLOCK);
    bool ret = s->supported;
    if (s->inFlight()) {
      LOG(ERROR) << "Unexpectedly sqe still in flight";
      ret = false;
    }
    return ret;
  } catch (IoUringBackend::NotAvailable const&) {
    return false;
  }
}

static bool doKernelSupportsDeferTaskrun() {
#if FOLLY_IO_URING_UP_TO_DATE
  struct io_uring ring;
  int ret = io_uring_queue_init(
      1, &ring, IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN);
  if (ret == 0) {
    io_uring_queue_exit(&ring);
    return true;
  }
#endif

  // fallthrough
  return false;
}

static bool doKernelSupportsSendZC() {
#if FOLLY_IO_URING_UP_TO_DATE
  struct io_uring ring;

  int ret = io_uring_queue_init(4, &ring, 0);
  if (ret) {
    LOG(ERROR)
        << "doKernelSupportsSendZC: Unexpectedly io_uring_queue_init failed";
    return false;
  }
  SCOPE_EXIT { io_uring_queue_exit(&ring); };

  auto* sqe = ::io_uring_get_sqe(&ring);
  if (!sqe) {
    LOG(ERROR) << "doKernelSupportsSendZC: no sqe?";
    return false;
  }

  io_uring_prep_sendmsg_zc(sqe, -1, nullptr, 0);
  ret = ::io_uring_submit(&ring);
  if (ret != 1) {
    return false;
  }

  struct io_uring_cqe* cqe = nullptr;
  ret = ::io_uring_wait_cqe(&ring, &cqe);
  if (ret) {
    return false;
  }

  if (!(cqe->flags & IORING_CQE_F_MORE)) {
    return false; // zerocopy sends two notifications
  }

  return (cqe->flags & IORING_CQE_F_NOTIF) || (cqe->res == -EBADF);
#endif

  // fallthrough
  return false;
}

} // namespace

bool IoUringBackend::kernelSupportsRecvmsgMultishot() {
  static bool const ret = doKernelSupportsRecvmsgMultishot();
  return ret;
}

bool IoUringBackend::kernelSupportsDeferTaskrun() {
  static bool const ret = doKernelSupportsDeferTaskrun();
  return ret;
}

bool IoUringBackend::kernelSupportsSendZC() {
  static bool const ret = doKernelSupportsSendZC();
  return ret;
}

} // namespace folly

#endif
