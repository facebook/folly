/*
 * Copyright 2013-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/test/DeterministicSchedule.h>

#include <assert.h>

#include <algorithm>
#include <list>
#include <mutex>
#include <random>
#include <unordered_map>
#include <utility>

#include <folly/Random.h>

namespace folly {
namespace test {

FOLLY_TLS sem_t* DeterministicSchedule::tls_sem;
FOLLY_TLS DeterministicSchedule* DeterministicSchedule::tls_sched;
FOLLY_TLS bool DeterministicSchedule::tls_exiting;
FOLLY_TLS DSchedThreadId DeterministicSchedule::tls_threadId;
thread_local AuxAct DeterministicSchedule::tls_aux_act;
AuxChk DeterministicSchedule::aux_chk;

// access is protected by futexLock
static std::unordered_map<
    const detail::Futex<DeterministicAtomic>*,
    std::list<std::pair<uint32_t, bool*>>>
    futexQueues;

static std::mutex futexLock;

void ThreadTimestamps::sync(const ThreadTimestamps& src) {
  if (src.timestamps_.size() > timestamps_.size()) {
    timestamps_.resize(src.timestamps_.size());
  }
  for (size_t i = 0; i < src.timestamps_.size(); i++) {
    timestamps_[i].sync(src.timestamps_[i]);
  }
}

DSchedTimestamp ThreadTimestamps::advance(DSchedThreadId tid) {
  assert(timestamps_.size() > tid.val);
  return timestamps_[tid.val].advance();
}

void ThreadTimestamps::setIfNotPresent(DSchedThreadId tid, DSchedTimestamp ts) {
  assert(ts.initialized());
  if (tid.val >= timestamps_.size()) {
    timestamps_.resize(tid.val + 1);
  }
  if (!timestamps_[tid.val].initialized()) {
    timestamps_[tid.val].sync(ts);
  }
}

void ThreadTimestamps::clear() {
  timestamps_.clear();
}

bool ThreadTimestamps::atLeastAsRecentAs(DSchedThreadId tid, DSchedTimestamp ts)
    const {
  // It is not meaningful learn whether any instance is at least
  // as recent as timestamp 0.
  assert(ts.initialized());
  if (tid.val >= timestamps_.size()) {
    return false;
  }
  return timestamps_[tid.val].atLeastAsRecentAs(ts);
}

bool ThreadTimestamps::atLeastAsRecentAsAny(const ThreadTimestamps& src) const {
  size_t min = timestamps_.size() < src.timestamps_.size()
      ? timestamps_.size()
      : src.timestamps_.size();
  for (size_t i = 0; i < min; i++) {
    if (src.timestamps_[i].initialized() &&
        timestamps_[i].atLeastAsRecentAs(src.timestamps_[i])) {
      return true;
    }
  }
  return false;
}

void ThreadSyncVar::acquire() {
  ThreadInfo& threadInfo = DeterministicSchedule::getCurrentThreadInfo();
  DSchedThreadId tid = DeterministicSchedule::getThreadId();
  threadInfo.acqRelOrder_.advance(tid);
  threadInfo.acqRelOrder_.sync(order_);
}

void ThreadSyncVar::release() {
  ThreadInfo& threadInfo = DeterministicSchedule::getCurrentThreadInfo();
  DSchedThreadId tid = DeterministicSchedule::getThreadId();
  threadInfo.acqRelOrder_.advance(tid);
  order_.sync(threadInfo.acqRelOrder_);
}

void ThreadSyncVar::acq_rel() {
  ThreadInfo& threadInfo = DeterministicSchedule::getCurrentThreadInfo();
  DSchedThreadId tid = DeterministicSchedule::getThreadId();
  threadInfo.acqRelOrder_.advance(tid);
  threadInfo.acqRelOrder_.sync(order_);
  order_.sync(threadInfo.acqRelOrder_);
}

DeterministicSchedule::DeterministicSchedule(
    const std::function<size_t(size_t)>& scheduler)
    : scheduler_(scheduler), nextThreadId_(0), step_(0) {
  assert(tls_sem == nullptr);
  assert(tls_sched == nullptr);
  assert(tls_aux_act == nullptr);

  tls_exiting = false;
  tls_sem = new sem_t;
  sem_init(tls_sem, 0, 1);
  sems_.push_back(tls_sem);

  tls_threadId = nextThreadId_++;
  threadInfoMap_.emplace_back(tls_threadId);
  tls_sched = this;
}

DeterministicSchedule::~DeterministicSchedule() {
  assert(tls_sched == this);
  assert(sems_.size() == 1);
  assert(sems_[0] == tls_sem);
  beforeThreadExit();
}

std::function<size_t(size_t)> DeterministicSchedule::uniform(uint64_t seed) {
  auto rand = std::make_shared<std::ranlux48>(seed);
  return [rand](size_t numActive) {
    auto dist = std::uniform_int_distribution<size_t>(0, numActive - 1);
    return dist(*rand);
  };
}

struct UniformSubset {
  UniformSubset(uint64_t seed, size_t subsetSize, size_t stepsBetweenSelect)
      : uniform_(DeterministicSchedule::uniform(seed)),
        subsetSize_(subsetSize),
        stepsBetweenSelect_(stepsBetweenSelect),
        stepsLeft_(0) {}

  size_t operator()(size_t numActive) {
    adjustPermSize(numActive);
    if (stepsLeft_-- == 0) {
      stepsLeft_ = stepsBetweenSelect_ - 1;
      shufflePrefix();
    }
    return perm_[uniform_(std::min(numActive, subsetSize_))];
  }

 private:
  std::function<size_t(size_t)> uniform_;
  const size_t subsetSize_;
  const size_t stepsBetweenSelect_;

  size_t stepsLeft_;
  // only the first subsetSize_ is properly randomized
  std::vector<size_t> perm_;

  void adjustPermSize(size_t numActive) {
    if (perm_.size() > numActive) {
      perm_.erase(
          std::remove_if(
              perm_.begin(),
              perm_.end(),
              [=](size_t x) { return x >= numActive; }),
          perm_.end());
    } else {
      while (perm_.size() < numActive) {
        perm_.push_back(perm_.size());
      }
    }
    assert(perm_.size() == numActive);
  }

  void shufflePrefix() {
    for (size_t i = 0; i < std::min(perm_.size() - 1, subsetSize_); ++i) {
      size_t j = uniform_(perm_.size() - i) + i;
      std::swap(perm_[i], perm_[j]);
    }
  }
};

std::function<size_t(size_t)>
DeterministicSchedule::uniformSubset(uint64_t seed, size_t n, size_t m) {
  auto gen = std::make_shared<UniformSubset>(seed, n, m);
  return [=](size_t numActive) { return (*gen)(numActive); };
}

void DeterministicSchedule::beforeSharedAccess() {
  if (tls_sem) {
    sem_wait(tls_sem);
  }
}

void DeterministicSchedule::afterSharedAccess() {
  auto sched = tls_sched;
  if (!sched) {
    return;
  }
  sem_post(sched->sems_[sched->scheduler_(sched->sems_.size())]);
}

void DeterministicSchedule::afterSharedAccess(bool success) {
  auto sched = tls_sched;
  if (!sched) {
    return;
  }
  sched->callAux(success);
  sem_post(sched->sems_[sched->scheduler_(sched->sems_.size())]);
}

size_t DeterministicSchedule::getRandNumber(size_t n) {
  if (tls_sched) {
    return tls_sched->scheduler_(n);
  }
  return Random::rand32() % n;
}

int DeterministicSchedule::getcpu(
    unsigned* cpu,
    unsigned* node,
    void* /* unused */) {
  if (cpu) {
    *cpu = tls_threadId.val;
  }
  if (node) {
    *node = tls_threadId.val;
  }
  return 0;
}

void DeterministicSchedule::setAuxAct(AuxAct& aux) {
  tls_aux_act = aux;
}

void DeterministicSchedule::setAuxChk(AuxChk& aux) {
  aux_chk = aux;
}

void DeterministicSchedule::clearAuxChk() {
  aux_chk = nullptr;
}

void DeterministicSchedule::reschedule(sem_t* sem) {
  auto sched = tls_sched;
  if (sched) {
    sched->sems_.push_back(sem);
  }
}

sem_t* DeterministicSchedule::descheduleCurrentThread() {
  auto sched = tls_sched;
  if (sched) {
    sched->sems_.erase(
        std::find(sched->sems_.begin(), sched->sems_.end(), tls_sem));
  }
  return tls_sem;
}

sem_t* DeterministicSchedule::beforeThreadCreate() {
  sem_t* s = new sem_t;
  sem_init(s, 0, 0);
  beforeSharedAccess();
  sems_.push_back(s);
  afterSharedAccess();
  return s;
}

void DeterministicSchedule::afterThreadCreate(sem_t* sem) {
  assert(tls_sem == nullptr);
  assert(tls_sched == nullptr);
  tls_exiting = false;
  tls_sem = sem;
  tls_sched = this;
  bool started = false;
  while (!started) {
    beforeSharedAccess();
    if (active_.count(std::this_thread::get_id()) == 1) {
      started = true;
      tls_threadId = nextThreadId_++;
      assert(tls_threadId.val == threadInfoMap_.size());
      threadInfoMap_.emplace_back(tls_threadId);
    }
    afterSharedAccess();
  }
  atomic_thread_fence(std::memory_order_seq_cst);
}

void DeterministicSchedule::beforeThreadExit() {
  assert(tls_sched == this);

  atomic_thread_fence(std::memory_order_seq_cst);
  beforeSharedAccess();
  auto parent = joins_.find(std::this_thread::get_id());
  if (parent != joins_.end()) {
    reschedule(parent->second);
    joins_.erase(parent);
  }
  sems_.erase(std::find(sems_.begin(), sems_.end(), tls_sem));
  active_.erase(std::this_thread::get_id());
  if (sems_.size() > 0) {
    FOLLY_TEST_DSCHED_VLOG("exiting");
    /* Wait here so that parent thread can control when the thread
     * enters the thread local destructors. */
    exitingSems_[std::this_thread::get_id()] = tls_sem;
    afterSharedAccess();
    sem_wait(tls_sem);
  }
  tls_sched = nullptr;
  tls_aux_act = nullptr;
  tls_exiting = true;
  sem_destroy(tls_sem);
  delete tls_sem;
  tls_sem = nullptr;
}

void DeterministicSchedule::waitForBeforeThreadExit(std::thread& child) {
  assert(tls_sched == this);
  beforeSharedAccess();
  assert(tls_sched->joins_.count(child.get_id()) == 0);
  if (tls_sched->active_.count(child.get_id())) {
    sem_t* sem = descheduleCurrentThread();
    tls_sched->joins_.insert({child.get_id(), sem});
    afterSharedAccess();
    // Wait to be scheduled by exiting child thread
    beforeSharedAccess();
    assert(!tls_sched->active_.count(child.get_id()));
  }
  afterSharedAccess();
}

void DeterministicSchedule::joinAll(std::vector<std::thread>& children) {
  auto sched = tls_sched;
  if (sched) {
    // Wait until all children are about to exit
    for (auto& child : children) {
      sched->waitForBeforeThreadExit(child);
    }
  }
  atomic_thread_fence(std::memory_order_seq_cst);
  /* Let each child thread proceed one at a time to protect
   * shared access during thread local destructors.*/
  for (auto& child : children) {
    if (sched) {
      sem_post(sched->exitingSems_[child.get_id()]);
    }
    child.join();
  }
}

void DeterministicSchedule::join(std::thread& child) {
  auto sched = tls_sched;
  if (sched) {
    sched->waitForBeforeThreadExit(child);
  }
  atomic_thread_fence(std::memory_order_seq_cst);
  FOLLY_TEST_DSCHED_VLOG("joined " << std::hex << child.get_id());
  if (sched) {
    sem_post(sched->exitingSems_[child.get_id()]);
  }
  child.join();
}

void DeterministicSchedule::callAux(bool success) {
  ++step_;
  if (tls_aux_act) {
    tls_aux_act(success);
    tls_aux_act = nullptr;
  }
  if (aux_chk) {
    aux_chk(step_);
  }
}

static std::unordered_map<sem_t*, std::unique_ptr<ThreadSyncVar>> semSyncVar;

void DeterministicSchedule::post(sem_t* sem) {
  beforeSharedAccess();
  if (semSyncVar.count(sem) == 0) {
    semSyncVar[sem] = std::make_unique<ThreadSyncVar>();
  }
  semSyncVar[sem]->release();
  sem_post(sem);
  FOLLY_TEST_DSCHED_VLOG("sem_post(" << sem << ")");
  afterSharedAccess();
}

bool DeterministicSchedule::tryWait(sem_t* sem) {
  beforeSharedAccess();
  if (semSyncVar.count(sem) == 0) {
    semSyncVar[sem] = std::make_unique<ThreadSyncVar>();
  }

  int rv = sem_trywait(sem);
  int e = rv == 0 ? 0 : errno;
  FOLLY_TEST_DSCHED_VLOG(
      "sem_trywait(" << sem << ") = " << rv << " errno=" << e);
  if (rv == 0) {
    semSyncVar[sem]->acq_rel();
  } else {
    semSyncVar[sem]->acquire();
  }

  afterSharedAccess();
  if (rv == 0) {
    return true;
  } else {
    assert(e == EAGAIN);
    return false;
  }
}

void DeterministicSchedule::wait(sem_t* sem) {
  while (!tryWait(sem)) {
    // we're not busy waiting because this is a deterministic schedule
  }
}

ThreadInfo& DeterministicSchedule::getCurrentThreadInfo() {
  auto sched = tls_sched;
  assert(sched);
  assert(tls_threadId.val < sched->threadInfoMap_.size());
  return sched->threadInfoMap_[tls_threadId.val];
}

void DeterministicSchedule::atomic_thread_fence(std::memory_order mo) {
  if (!tls_sched) {
    std::atomic_thread_fence(mo);
    return;
  }
  beforeSharedAccess();
  ThreadInfo& threadInfo = getCurrentThreadInfo();
  switch (mo) {
    case std::memory_order_relaxed:
      assert(false);
      break;
    case std::memory_order_consume:
    case std::memory_order_acquire:
      threadInfo.acqRelOrder_.sync(threadInfo.acqFenceOrder_);
      break;
    case std::memory_order_release:
      threadInfo.relFenceOrder_.sync(threadInfo.acqRelOrder_);
      break;
    case std::memory_order_acq_rel:
      threadInfo.acqRelOrder_.sync(threadInfo.acqFenceOrder_);
      threadInfo.relFenceOrder_.sync(threadInfo.acqRelOrder_);
      break;
    case std::memory_order_seq_cst:
      threadInfo.acqRelOrder_.sync(threadInfo.acqFenceOrder_);
      threadInfo.acqRelOrder_.sync(tls_sched->seqCstFenceOrder_);
      tls_sched->seqCstFenceOrder_ = threadInfo.acqRelOrder_;
      threadInfo.relFenceOrder_.sync(threadInfo.acqRelOrder_);
      break;
  }
  FOLLY_TEST_DSCHED_VLOG("fence: " << folly::detail::memory_order_to_str(mo));
  afterSharedAccess();
}

detail::FutexResult futexWaitImpl(
    const detail::Futex<DeterministicAtomic>* futex,
    uint32_t expected,
    std::chrono::system_clock::time_point const* absSystemTimeout,
    std::chrono::steady_clock::time_point const* absSteadyTimeout,
    uint32_t waitMask) {
  return deterministicFutexWaitImpl<DeterministicAtomic>(
      futex,
      futexLock,
      futexQueues,
      expected,
      absSystemTimeout,
      absSteadyTimeout,
      waitMask);
}

int futexWakeImpl(
    const detail::Futex<DeterministicAtomic>* futex,
    int count,
    uint32_t wakeMask) {
  return deterministicFutexWakeImpl<DeterministicAtomic>(
      futex, futexLock, futexQueues, count, wakeMask);
}

} // namespace test
} // namespace folly

namespace folly {

template <>
CacheLocality const& CacheLocality::system<test::DeterministicAtomic>() {
  static CacheLocality cache(CacheLocality::uniform(16));
  return cache;
}

template <>
Getcpu::Func AccessSpreader<test::DeterministicAtomic>::pickGetcpuFunc() {
  return &test::DeterministicSchedule::getcpu;
}
} // namespace folly
