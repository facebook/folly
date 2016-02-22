/*
 * Copyright 2016 Facebook, Inc.
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

#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <unordered_set>
#include <vector>
#include <boost/noncopyable.hpp>
#include <semaphore.h>
#include <errno.h>
#include <assert.h>
#include <glog/logging.h>

#include <folly/ScopeGuard.h>
#include <folly/detail/CacheLocality.h>
#include <folly/detail/Futex.h>

namespace folly {
namespace test {

// This is ugly, but better perf for DeterministicAtomic translates
// directly to more states explored and tested
#define FOLLY_TEST_DSCHED_VLOG(msg...)                                  \
  do {                                                                  \
    if (false) {                                                        \
      VLOG(2) << std::hex << std::this_thread::get_id() << ": " << msg; \
    }                                                                   \
  } while (false)

/**
 * DeterministicSchedule coordinates the inter-thread communication of a
 * set of threads under test, so that despite concurrency the execution is
 * the same every time.  It works by stashing a reference to the schedule
 * in a thread-local variable, then blocking all but one thread at a time.
 *
 * In order for DeterministicSchedule to work, it needs to intercept
 * all inter-thread communication.  To do this you should use
 * DeterministicAtomic<T> instead of std::atomic<T>, create threads
 * using DeterministicSchedule::thread() instead of the std::thread
 * constructor, DeterministicSchedule::join(thr) instead of thr.join(),
 * and access semaphores via the helper functions in DeterministicSchedule.
 * Locks are not yet supported, although they would be easy to add with
 * the same strategy as the mapping of sem_wait.
 *
 * The actual schedule is defined by a function from n -> [0,n). At
 * each step, the function will be given the number of active threads
 * (n), and it returns the index of the thread that should be run next.
 * Invocations of the scheduler function will be serialized, but will
 * occur from multiple threads.  A good starting schedule is uniform(0).
 */
class DeterministicSchedule : boost::noncopyable {
 public:
  /**
   * Arranges for the current thread (and all threads created by
   * DeterministicSchedule::thread on a thread participating in this
   * schedule) to participate in a deterministic schedule.
   */
  explicit DeterministicSchedule(const std::function<int(int)>& scheduler);

  /** Completes the schedule. */
  ~DeterministicSchedule();

  /**
   * Returns a scheduling function that randomly chooses one of the
   * runnable threads at each step, with no history.  This implements
   * a schedule that is equivalent to one in which the steps between
   * inter-thread communication are random variables following a poisson
   * distribution.
   */
  static std::function<int(int)> uniform(long seed);

  /**
   * Returns a scheduling function that chooses a subset of the active
   * threads and randomly chooses a member of the subset as the next
   * runnable thread.  The subset is chosen with size n, and the choice
   * is made every m steps.
   */
  static std::function<int(int)> uniformSubset(long seed,
                                               int n = 2,
                                               int m = 64);

  /** Obtains permission for the current thread to perform inter-thread
   *  communication. */
  static void beforeSharedAccess();

  /** Releases permission for the current thread to perform inter-thread
   *  communication. */
  static void afterSharedAccess();

  /** Launches a thread that will participate in the same deterministic
   *  schedule as the current thread. */
  template <typename Func, typename... Args>
  static inline std::thread thread(Func&& func, Args&&... args) {
    // TODO: maybe future versions of gcc will allow forwarding to thread
    auto sched = tls_sched;
    auto sem = sched ? sched->beforeThreadCreate() : nullptr;
    auto child = std::thread([=](Args... a) {
      if (sched) {
        sched->afterThreadCreate(sem);
        beforeSharedAccess();
        FOLLY_TEST_DSCHED_VLOG("running");
        afterSharedAccess();
      }
      SCOPE_EXIT {
        if (sched) {
          sched->beforeThreadExit();
        }
      };
      func(a...);
    }, args...);
    if (sched) {
      beforeSharedAccess();
      sched->active_.insert(child.get_id());
      FOLLY_TEST_DSCHED_VLOG("forked " << std::hex << child.get_id());
      afterSharedAccess();
    }
    return child;
  }

  /** Calls child.join() as part of a deterministic schedule. */
  static void join(std::thread& child);

  /** Calls sem_post(sem) as part of a deterministic schedule. */
  static void post(sem_t* sem);

  /** Calls sem_trywait(sem) as part of a deterministic schedule, returning
   *  true on success and false on transient failure. */
  static bool tryWait(sem_t* sem);

  /** Calls sem_wait(sem) as part of a deterministic schedule. */
  static void wait(sem_t* sem);

  /** Used scheduler_ to get a random number b/w [0, n). If tls_sched is
   *  not set-up it falls back to std::rand() */
  static int getRandNumber(int n);

  /** Deterministic implemencation of getcpu */
  static int getcpu(unsigned* cpu, unsigned* node, void* unused);

 private:
  static FOLLY_TLS sem_t* tls_sem;
  static FOLLY_TLS DeterministicSchedule* tls_sched;
  static FOLLY_TLS unsigned tls_threadId;

  std::function<int(int)> scheduler_;
  std::vector<sem_t*> sems_;
  std::unordered_set<std::thread::id> active_;
  unsigned nextThreadId_;

  sem_t* beforeThreadCreate();
  void afterThreadCreate(sem_t*);
  void beforeThreadExit();
};

/**
 * DeterministicAtomic<T> is a drop-in replacement std::atomic<T> that
 * cooperates with DeterministicSchedule.
 */
template <typename T>
struct DeterministicAtomic {
  std::atomic<T> data;

  DeterministicAtomic() = default;
  ~DeterministicAtomic() = default;
  DeterministicAtomic(DeterministicAtomic<T> const&) = delete;
  DeterministicAtomic<T>& operator=(DeterministicAtomic<T> const&) = delete;

  constexpr /* implicit */ DeterministicAtomic(T v) noexcept : data(v) {}

  bool is_lock_free() const noexcept { return data.is_lock_free(); }

  bool compare_exchange_strong(
      T& v0, T v1, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    auto orig = v0;
    bool rv = data.compare_exchange_strong(v0, v1, mo);
    FOLLY_TEST_DSCHED_VLOG(this << ".compare_exchange_strong(" << std::hex
                                << orig << ", " << std::hex << v1 << ") -> "
                                << rv << "," << std::hex << v0);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  bool compare_exchange_weak(
      T& v0, T v1, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    auto orig = v0;
    bool rv = data.compare_exchange_weak(v0, v1, mo);
    FOLLY_TEST_DSCHED_VLOG(this << ".compare_exchange_weak(" << std::hex << orig
                                << ", " << std::hex << v1 << ") -> " << rv
                                << "," << std::hex << v0);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T exchange(T v, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = data.exchange(v, mo);
    FOLLY_TEST_DSCHED_VLOG(this << ".exchange(" << std::hex << v << ") -> "
                                << std::hex << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  /* implicit */ operator T() const noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = data;
    FOLLY_TEST_DSCHED_VLOG(this << "() -> " << std::hex << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T load(std::memory_order mo = std::memory_order_seq_cst) const noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = data.load(mo);
    FOLLY_TEST_DSCHED_VLOG(this << ".load() -> " << std::hex << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T operator=(T v) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = (data = v);
    FOLLY_TEST_DSCHED_VLOG(this << " = " << std::hex << v);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  void store(T v, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    data.store(v, mo);
    FOLLY_TEST_DSCHED_VLOG(this << ".store(" << std::hex << v << ")");
    DeterministicSchedule::afterSharedAccess();
  }

  T operator++() noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = ++data;
    FOLLY_TEST_DSCHED_VLOG(this << " pre++ -> " << std::hex << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T operator++(int /* postDummy */) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = data++;
    FOLLY_TEST_DSCHED_VLOG(this << " post++ -> " << std::hex << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T operator--() noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = --data;
    FOLLY_TEST_DSCHED_VLOG(this << " pre-- -> " << std::hex << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T operator--(int /* postDummy */) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = data--;
    FOLLY_TEST_DSCHED_VLOG(this << " post-- -> " << std::hex << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T operator+=(T v) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = (data += v);
    FOLLY_TEST_DSCHED_VLOG(this << " += " << std::hex << v << " -> " << std::hex
                                << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T fetch_add(T v,
              std::memory_order /* mo */ = std::memory_order_seq_cst) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = data;
    data += v;
    FOLLY_TEST_DSCHED_VLOG(this << ".fetch_add(" << std::hex << v << ") -> "
                                << std::hex << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T operator-=(T v) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = (data -= v);
    FOLLY_TEST_DSCHED_VLOG(this << " -= " << std::hex << v << " -> " << std::hex
                                << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T fetch_sub(T v,
              std::memory_order /* mo */ = std::memory_order_seq_cst) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = data;
    data -= v;
    FOLLY_TEST_DSCHED_VLOG(this << ".fetch_sub(" << std::hex << v << ") -> "
                                << std::hex << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T operator&=(T v) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = (data &= v);
    FOLLY_TEST_DSCHED_VLOG(this << " &= " << std::hex << v << " -> " << std::hex
                                << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T fetch_and(T v,
              std::memory_order /* mo */ = std::memory_order_seq_cst) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = data;
    data &= v;
    FOLLY_TEST_DSCHED_VLOG(this << ".fetch_and(" << std::hex << v << ") -> "
                                << std::hex << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T operator|=(T v) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = (data |= v);
    FOLLY_TEST_DSCHED_VLOG(this << " |= " << std::hex << v << " -> " << std::hex
                                << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T fetch_or(T v,
             std::memory_order /* mo */ = std::memory_order_seq_cst) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = data;
    data |= v;
    FOLLY_TEST_DSCHED_VLOG(this << ".fetch_or(" << std::hex << v << ") -> "
                                << std::hex << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T operator^=(T v) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = (data ^= v);
    FOLLY_TEST_DSCHED_VLOG(this << " ^= " << std::hex << v << " -> " << std::hex
                                << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }

  T fetch_xor(T v,
              std::memory_order /* mo */ = std::memory_order_seq_cst) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    T rv = data;
    data ^= v;
    FOLLY_TEST_DSCHED_VLOG(this << ".fetch_xor(" << std::hex << v << ") -> "
                                << std::hex << rv);
    DeterministicSchedule::afterSharedAccess();
    return rv;
  }
};
}
} // namespace folly::test

/* Specialization declarations */

namespace folly {
namespace detail {

template <>
int Futex<test::DeterministicAtomic>::futexWake(int count, uint32_t wakeMask);

template <>
FutexResult Futex<test::DeterministicAtomic>::futexWaitImpl(
    uint32_t expected,
    std::chrono::time_point<std::chrono::system_clock>* absSystemTime,
    std::chrono::time_point<std::chrono::steady_clock>* absSteadyTime,
    uint32_t waitMask);

template <>
Getcpu::Func AccessSpreader<test::DeterministicAtomic>::pickGetcpuFunc();
}
} // namespace folly::detail
