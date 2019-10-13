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

#include <sstream>

#include <folly/synchronization/detail/AtomicUtils.h>
#include <folly/test/DeterministicSchedule.h>

namespace folly {
namespace test {

template <typename T>
class RecordBuffer {
 private:
  struct Record {
    Record(DSchedTimestamp ts, DSchedThreadId tid, bool sc, T val)
        : acqRelTimestamp_(ts), storingThread_(tid), seqCst_(sc), val_(val) {}
    explicit Record(T val) : val_(val) {}
    Record() = delete;

    DSchedTimestamp acqRelTimestamp_;
    DSchedThreadId storingThread_;
    bool seqCst_;
    T val_;
    ThreadTimestamps acqRelOrder_;
    ThreadTimestamps firstObservedOrder_;
  };

 public:
  RecordBuffer() = default;

  T load(ThreadInfo& threadInfo, std::memory_order mo, bool rmw) {
    DSchedThreadId tid = DeterministicSchedule::getThreadId();
    return load(tid, threadInfo, mo, rmw);
  }

  T load(
      DSchedThreadId tid,
      ThreadInfo& threadInfo,
      std::memory_order mo,
      bool rmw = false) {
    if (!rmw) {
      assert(mo != std::memory_order_release);
      assert(mo != std::memory_order_acq_rel);
    }

    if (!isInitialized()) {
      return 0;
    }

    size_t oldestAllowed =
        rmw ? 0 : getOldestAllowed(mo, threadInfo.acqRelOrder_);

    size_t selected = DeterministicSchedule::getRandNumber(oldestAllowed + 1);

    FOLLY_TEST_DSCHED_VLOG(
        "buffered load, mo: " << folly::detail::memory_order_to_str(mo)
                              << " index " << selected << "/" << oldestAllowed
                              << " allowed."
                              << " current value: " << loadDirect()
                              << " return value: " << history_[selected].val_);

    Record& rec = history_[selected];
    DSchedTimestamp ts = threadInfo.acqRelOrder_.advance(tid);
    rec.firstObservedOrder_.setIfNotPresent(tid, ts);

    bool synch =
        (mo == std::memory_order_acquire || mo == std::memory_order_acq_rel ||
         mo == std::memory_order_seq_cst);
    ThreadTimestamps& dst =
        synch ? threadInfo.acqRelOrder_ : threadInfo.acqFenceOrder_;
    dst.sync(rec.acqRelOrder_);

    return rec.val_;
  }

  T loadDirect() const {
    if (!isInitialized()) {
      return 0;
    }
    return history_[0].val_;
  }

  void storeDirect(T val) {
    if (isInitialized()) {
      history_[0].val_ = val;
    } else {
      history_.emplace_front(val);
    }
  }

  void store(ThreadInfo& threadInfo, T v, std::memory_order mo, bool rmw) {
    DSchedThreadId tid = DeterministicSchedule::getThreadId();
    store(tid, threadInfo, v, mo, rmw);
  }

  void store(
      DSchedThreadId tid,
      ThreadInfo& threadInfo,
      T v,
      std::memory_order mo,
      bool rmw = false) {
    if (!rmw) {
      assert(mo != std::memory_order_acquire);
      assert(mo != std::memory_order_acq_rel);
      assert(mo != std::memory_order_consume);
    }

    DSchedTimestamp ts = threadInfo.acqRelOrder_.advance(tid);
    bool preserve = isInitialized() &&
        (rmw || tid.val == history_.front().storingThread_.val);
    bool sc = (mo == std::memory_order_seq_cst);
    history_.emplace_front(ts, tid, sc, v);
    Record& rec = history_.front();
    rec.firstObservedOrder_.setIfNotPresent(tid, ts);

    bool synch =
        (mo == std::memory_order_release || mo == std::memory_order_acq_rel ||
         mo == std::memory_order_seq_cst);
    ThreadTimestamps& src =
        synch ? threadInfo.acqRelOrder_ : threadInfo.relFenceOrder_;
    if (preserve) {
      rec.acqRelOrder_ = history_.front().acqRelOrder_;
    }
    rec.acqRelOrder_.sync(src);
    if (history_.size() > kMaxRecordBufferSize) {
      history_.pop_back();
    }
  }

 protected:
  size_t getOldestAllowed(
      std::memory_order mo,
      const ThreadTimestamps& acqRelOrder) {
    assert(isInitialized());
    for (size_t i = 0; i < history_.size() - 1; i++) {
      Record& rec = history_[i];
      if (rec.seqCst_ && (mo == std::memory_order_seq_cst)) {
        return i;
      }

      if (acqRelOrder.atLeastAsRecentAs(
              rec.storingThread_, rec.acqRelTimestamp_)) {
        return i;
      }

      if (acqRelOrder.atLeastAsRecentAsAny(rec.firstObservedOrder_)) {
        return i;
      }
    }
    return history_.size() - 1;
  }
  // index 0 is newest, index size - 1 is oldest
  std::deque<Record> history_;

 private:
  static constexpr size_t kMaxRecordBufferSize = 64;

  bool isInitialized() const {
    return !history_.empty();
  }
};

template <typename T>
struct BufferedAtomic {
  BufferedAtomic() {
    DeterministicSchedule::beforeSharedAccess();
    assert(bufs.count(this) == 0);
    bufs[this];
    DeterministicSchedule::afterSharedAccess();
  }
  ~BufferedAtomic() {
    DeterministicSchedule::beforeSharedAccess();
    assert(bufs.count(this) == 1);
    bufs.erase(this);
    DeterministicSchedule::afterSharedAccess();
  }
  BufferedAtomic(BufferedAtomic<T> const&) = delete;
  BufferedAtomic<T>& operator=(BufferedAtomic<T> const&) = delete;

  using Modification = std::function<T(const T&)>;

  constexpr /* implicit */ BufferedAtomic(T v) noexcept {
    DeterministicSchedule::beforeSharedAccess();
    assert(bufs.count(this) == 0);
    bufs[this];
    doStore(v, std::memory_order_relaxed);
    DeterministicSchedule::afterSharedAccess();
  }

  bool is_lock_free() const noexcept {
    return false;
  }

  bool compare_exchange_strong(
      T& v0,
      T v1,
      std::memory_order mo = std::memory_order_seq_cst) noexcept {
    return compare_exchange_strong(
        v0, v1, mo, folly::detail::default_failure_memory_order(mo));
  }
  bool compare_exchange_strong(
      T& expected,
      T desired,
      std::memory_order success,
      std::memory_order failure) noexcept {
    return doCompareExchange(expected, desired, success, failure, false);
  }

  bool compare_exchange_weak(
      T& v0,
      T v1,
      std::memory_order mo = std::memory_order_seq_cst) noexcept {
    return compare_exchange_weak(
        v0, v1, mo, ::folly::detail::default_failure_memory_order(mo));
  }
  bool compare_exchange_weak(
      T& expected,
      T desired,
      std::memory_order success,
      std::memory_order failure) noexcept {
    return doCompareExchange(expected, desired, success, failure, true);
  }

  T exchange(T v, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    Modification mod = [&](const T& /*prev*/) { return v; };
    return doReadModifyWrite(mod, mo);
  }

  /* implicit */ operator T() const noexcept {
    return doLoad(std::memory_order_seq_cst);
  }

  T load(std::memory_order mo = std::memory_order_seq_cst) const noexcept {
    return doLoad(mo);
  }

  T operator=(T v) noexcept {
    doStore(v, std::memory_order_seq_cst);
    return v;
  }

  void store(T v, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    doStore(v, mo);
  }

  T operator++() noexcept {
    Modification mod = [](const T& prev) { return prev + 1; };
    return doReadModifyWrite(mod, std::memory_order_seq_cst) + 1;
  }

  T operator++(int /* postDummy */) noexcept {
    Modification mod = [](const T& prev) { return prev + 1; };
    return doReadModifyWrite(mod, std::memory_order_seq_cst);
  }

  T operator--() noexcept {
    Modification mod = [](const T& prev) { return prev - 1; };
    return doReadModifyWrite(mod, std::memory_order_seq_cst) - 1;
  }

  T operator--(int /* postDummy */) noexcept {
    Modification mod = [](const T& prev) { return prev - 1; };
    return doReadModifyWrite(mod, std::memory_order_seq_cst);
  }

  T operator+=(T v) noexcept {
    Modification mod = [&](const T& prev) { return prev + v; };
    return doReadModifyWrite(mod, std::memory_order_seq_cst) + v;
  }

  T fetch_add(T v, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    Modification mod = [&](const T& prev) { return prev + v; };
    return doReadModifyWrite(mod, mo);
  }

  T operator-=(T v) noexcept {
    Modification mod = [&](const T& prev) { return prev - v; };
    return doReadModifyWrite(mod, std::memory_order_seq_cst) - v;
  }

  T fetch_sub(T v, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    Modification mod = [&](const T& prev) { return prev - v; };
    return doReadModifyWrite(mod, mo);
  }

  T operator&=(T v) noexcept {
    Modification mod = [&](const T& prev) { return prev & v; };
    return doReadModifyWrite(mod, std::memory_order_seq_cst) & v;
  }

  T fetch_and(T v, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    Modification mod = [&](const T& prev) { return prev & v; };
    return doReadModifyWrite(mod, mo);
  }

  T operator|=(T v) noexcept {
    Modification mod = [&](const T& prev) { return prev | v; };
    return doReadModifyWrite(mod, std::memory_order_seq_cst) | v;
  }

  T fetch_or(T v, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    Modification mod = [&](const T& prev) { return prev | v; };
    return doReadModifyWrite(mod, mo);
  }

  T operator^=(T v) noexcept {
    Modification mod = [&](const T& prev) { return prev ^ v; };
    return doReadModifyWrite(mod, std::memory_order_seq_cst) ^ v;
  }

  T fetch_xor(T v, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    Modification mod = [&](const T& prev) { return prev ^ v; };
    return doReadModifyWrite(mod, mo);
  }

 private:
  T doLoad(std::memory_order mo, bool rmw = false) const {
    // Static destructors that outlive DSched instance may load atomics
    if (!DeterministicSchedule::isActive()) {
      if (!DeterministicSchedule::isCurrentThreadExiting()) {
        auto prev = prevUnguardedAccess.exchange(std::this_thread::get_id());
        CHECK(prev == std::thread::id() || prev == std::this_thread::get_id());
      }
      return getBuf().loadDirect();
    }
    ThreadInfo& threadInfo = DeterministicSchedule::getCurrentThreadInfo();
    T rv = getBuf().load(threadInfo, mo, rmw);
    return rv;
  }

  void doStore(T val, std::memory_order mo, bool rmw = false) {
    // Static destructors that outlive DSched instance may store to atomics
    if (!DeterministicSchedule::isActive()) {
      if (!DeterministicSchedule::isCurrentThreadExiting()) {
        auto prev = prevUnguardedAccess.exchange(std::this_thread::get_id());
        CHECK(prev == std::thread::id() || prev == std::this_thread::get_id());
      }
      getBuf().storeDirect(val);
      return;
    }
    ThreadInfo& threadInfo = DeterministicSchedule::getCurrentThreadInfo();
    getBuf().store(threadInfo, val, mo, rmw);
    FOLLY_TEST_DSCHED_VLOG(
        "\tstore mo: " << folly::detail::memory_order_to_str(mo)
                       << " rmw: " << rmw);
  }

  T doReadModifyWrite(Modification mod, std::memory_order mo) {
    T prev = doLoad(mo, true);
    T next = mod(prev);
    doStore(next, mo, true);
    return prev;
  }

  bool doCompareExchange(
      T& expected,
      T desired,
      std::memory_order success,
      std::memory_order failure,
      bool spuriousFailures) {
    T current = getBuf().loadDirect();
    if (current == expected) {
      if (!spuriousFailures || DeterministicSchedule::getRandNumber(2)) {
        Modification mod = [&](const T& /*prev*/) { return desired; };
        doReadModifyWrite(mod, success);
        return true;
      }
    }
    expected = doLoad(failure, true);
    assert(expected == current);
    return false;
  }

  RecordBuffer<T>& getBuf() const {
    assert(bufs.count(this) == 1);
    return bufs.at(this);
  }

  static std::unordered_map<const BufferedAtomic<T>*, RecordBuffer<T>> bufs;
  mutable std::atomic<std::thread::id> prevUnguardedAccess;
};

template <typename T>
std::unordered_map<const BufferedAtomic<T>*, RecordBuffer<T>>
    BufferedAtomic<T>::bufs =
        std::unordered_map<const BufferedAtomic<T>*, RecordBuffer<T>>();

template <typename T>
using BufferedDeterministicAtomic =
    DeterministicAtomicImpl<T, DeterministicSchedule, BufferedAtomic>;

/* Futex extensions for DeterministicSchedule based Futexes */
int futexWakeImpl(
    const folly::detail::Futex<test::BufferedDeterministicAtomic>* futex,
    int count,
    uint32_t wakeMask);

folly::detail::FutexResult futexWaitImpl(
    const folly::detail::Futex<test::BufferedDeterministicAtomic>* futex,
    uint32_t expected,
    std::chrono::system_clock::time_point const* absSystemTime,
    std::chrono::steady_clock::time_point const* absSteadyTime,
    uint32_t waitMask);

} // namespace test

} // namespace folly
