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

#include <Python.h>

#include <queue>
#include <thread>

#include <folly/executors/DrivableExecutor.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/executors/SequencedExecutor.h>
#include <folly/portability/GTest.h>
#include <folly/python/iobuf_ext.h>

namespace folly::python {
namespace {

// Mimics AsyncioExecutor::drop() semantics: after the drive loop exits,
// remaining tasks are NOT drained — they are destroyed with the queue.
// This lets us test that std::move(executor).add() correctly keeps the
// KeepAlive inside the task (so drive() must process it), vs the old
// get()->add() approach where the KeepAlive was released separately.
class DropStyleExecutor : public DrivableExecutor, public SequencedExecutor {
 public:
  void add(Func f) override {
    std::lock_guard g(mu_);
    tasks_.push(std::move(f));
  }

  void drive() noexcept override {
    Func f;
    {
      std::lock_guard g(mu_);
      if (tasks_.empty()) {
        return;
      }
      f = std::move(tasks_.front());
      tasks_.pop();
    }
    f();
  }

  // Like AsyncioExecutor::drop() — drives until keepAliveCount hits 0,
  // then stops. No drain of remaining tasks.
  void drop() {
    keepAliveRelease();
    while (keepAliveCount_.load(std::memory_order_acquire) > 0) {
      drive();
    }
    // No drain — remaining tasks are lost (same as AsyncioExecutor).
  }

  bool keepAliveAcquire() noexcept override {
    keepAliveCount_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  void keepAliveRelease() noexcept override {
    keepAliveCount_.fetch_sub(1, std::memory_order_release);
  }

 private:
  std::mutex mu_;
  std::queue<Func> tasks_;
  std::atomic<int> keepAliveCount_{1}; // starts at 1 like AsyncioExecutor
};

struct IOBufExtTest : public testing::Test {
  void SetUp() override {
    Py_Initialize();
    gstate_ = PyGILState_Ensure();
  }

  void TearDown() override { PyGILState_Release(gstate_); }

 protected:
  PyGILState_STATE gstate_{PyGILState_UNLOCKED};
};

// Regression test for S646339 (use-after-free in iobuf_ext.cpp:55).
// RED without KeepAlive (raw Executor* → ASAN heap-use-after-free).
// GREEN with KeepAlive (executor kept alive by the token).
TEST_F(IOBufExtTest, testFreeCallbackShouldSurviveExecutorDestruction) {
  PyObject* pyBytes = PyBytes_FromStringAndSize("hello", 5);
  ASSERT_NE(pyBytes, nullptr);
  auto initialRefcnt = Py_REFCNT(pyBytes);

  auto executor = std::make_unique<folly::ManualExecutor>();
  auto iobuf = iobuf_from_memoryview(
      executor.get(),
      pyBytes,
      PyBytes_AS_STRING(pyBytes),
      PyBytes_GET_SIZE(pyBytes));
  ASSERT_NE(iobuf, nullptr);
  EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt + 1);

  // Free the IOBuf from a non-GIL thread after a brief delay, concurrently
  // with executor destruction below. This mirrors the production scenario
  // where a ServiceRouter fiber frees the IOBuf during request cleanup.
  std::thread freer([&iobuf]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    iobuf.reset();
  });

  // Destroy the executor. Without the fix (raw Executor*), the destructor
  // completes immediately (keepAliveCount is 0), and the freer thread later
  // hits a use-after-free on executor->add(). With the fix (KeepAlive),
  // the destructor spins in drive() until the freer thread releases the
  // token, then processes the queued Py_DECREF and exits cleanly.
  executor.reset();

  freer.join();

  EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt);
  Py_DECREF(pyBytes);
}

// Free callback with GIL held — takes the direct Py_DECREF fast path.
TEST_F(IOBufExtTest, testFreeCallbackShouldDecrefDirectlyWhenGILHeld) {
  PyObject* pyBytes = PyBytes_FromStringAndSize("hello", 5);
  ASSERT_NE(pyBytes, nullptr);
  auto initialRefcnt = Py_REFCNT(pyBytes);

  {
    auto iobuf = iobuf_from_memoryview(
        nullptr,
        pyBytes,
        PyBytes_AS_STRING(pyBytes),
        PyBytes_GET_SIZE(pyBytes));
    ASSERT_NE(iobuf, nullptr);
    EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt + 1);
    // IOBuf destroyed here while GIL is held — direct Py_DECREF.
  }

  EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt);
  Py_DECREF(pyBytes);
}

// Free callback with null executor and no GIL — takes the Py_AddPendingCall /
// PyGILState_Ensure fallback path.
TEST_F(IOBufExtTest, testFreeCallbackShouldUsePendingCallWithoutExecutor) {
  PyObject* pyBytes = PyBytes_FromStringAndSize("world", 5);
  ASSERT_NE(pyBytes, nullptr);
  auto initialRefcnt = Py_REFCNT(pyBytes);

  auto iobuf = iobuf_from_memoryview(
      nullptr, pyBytes, PyBytes_AS_STRING(pyBytes), PyBytes_GET_SIZE(pyBytes));
  ASSERT_NE(iobuf, nullptr);
  EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt + 1);

  // Destroy from a non-GIL thread with no executor — exercises the
  // Py_AddPendingCall / PyGILState_Ensure fallback.
  std::thread([&iobuf]() { iobuf.reset(); }).join();

  // Py_AddPendingCall queues the decref; flush it.
  Py_MakePendingCalls();

  EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt);
  Py_DECREF(pyBytes);
}

// Free callback with a live executor and no GIL — exercises the
// executor->add() path when the executor is still valid.
TEST_F(IOBufExtTest, testFreeCallbackShouldScheduleDecrefOnLiveExecutor) {
  PyObject* pyBytes = PyBytes_FromStringAndSize("test!", 5);
  ASSERT_NE(pyBytes, nullptr);
  auto initialRefcnt = Py_REFCNT(pyBytes);

  auto executor = std::make_unique<folly::ManualExecutor>();
  auto iobuf = iobuf_from_memoryview(
      executor.get(),
      pyBytes,
      PyBytes_AS_STRING(pyBytes),
      PyBytes_GET_SIZE(pyBytes));
  ASSERT_NE(iobuf, nullptr);
  EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt + 1);

  // Destroy from a non-GIL thread — executor is alive, so it schedules
  // the Py_DECREF via executor->add().
  std::thread([&iobuf]() { iobuf.reset(); }).join();

  // The decref is queued on the executor, not yet executed.
  EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt + 1);

  // Drain the executor to run the queued Py_DECREF.
  executor->drain();

  EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt);
  Py_DECREF(pyBytes);
}

// Free callback when Py_AddPendingCall fails (queue full) — forces the
// PyGILState_Ensure + Py_DECREF last-resort fallback (lines 75-77).
TEST_F(IOBufExtTest, testFreeCallbackShouldAcquireGILWhenPendingCallQueueFull) {
  PyObject* pyBytes = PyBytes_FromStringAndSize("fallback", 8);
  ASSERT_NE(pyBytes, nullptr);
  auto initialRefcnt = Py_REFCNT(pyBytes);

  auto iobuf = iobuf_from_memoryview(
      nullptr, pyBytes, PyBytes_AS_STRING(pyBytes), PyBytes_GET_SIZE(pyBytes));
  ASSERT_NE(iobuf, nullptr);
  EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt + 1);

  // Saturate the pending calls queue so the next Py_AddPendingCall returns -1.
  auto noop = +[](void*) -> int { return 0; };
  while (Py_AddPendingCall(noop, nullptr) == 0) {
  }

  // Release GIL fully so the destruction thread can acquire it via
  // PyGILState_Ensure when Py_AddPendingCall fails.
  PyGILState_Release(gstate_);
  auto* tstate = PyEval_SaveThread();

  // Destroy from a non-GIL thread. Py_AddPendingCall will fail (queue full),
  // so the callback falls back to PyGILState_Ensure + Py_DECREF.
  std::thread([&iobuf]() { iobuf.reset(); }).join();

  // Re-acquire GIL.
  PyEval_RestoreThread(tstate);
  gstate_ = PyGILState_Ensure();

  // Flush dummy pending calls.
  Py_MakePendingCalls();

  EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt);
  Py_DECREF(pyBytes);
}

// Free callback during Python finalization — skips all refcount management,
// just deletes the PyBufferData (line 49 false branch, line 81).
TEST_F(IOBufExtTest, testFreeCallbackShouldSkipDecrefDuringFinalization) {
  PyObject* pyBytes = PyBytes_FromStringAndSize("final", 5);
  ASSERT_NE(pyBytes, nullptr);

  auto iobuf = iobuf_from_memoryview(
      nullptr, pyBytes, PyBytes_AS_STRING(pyBytes), PyBytes_GET_SIZE(pyBytes));
  ASSERT_NE(iobuf, nullptr);

  // Release GIL state before finalizing.
  PyGILState_Release(gstate_);

  // Begin finalization — Py_IsFinalizing() will return true.
  // pyBytes is invalidated during finalization; we cannot check refcounts.
  Py_FinalizeEx();

  // Destroy the IOBuf while Python is finalizing. The free callback should
  // see Py_IsFinalizing() == true and skip Py_DECREF, just deleting py_data.
  iobuf.reset(); // Must not crash.

  // Re-initialize Python for subsequent tests and TearDown.
  Py_Initialize();
  gstate_ = PyGILState_Ensure();
}

// Regression test for the std::move fix (prevents repeat of D61615594 revert).
// RED with get()->add() (KeepAlive released too early → Py_DECREF task lost).
// GREEN with std::move().add() (KeepAlive in task → drive() must run it).
//
// Uses DropStyleExecutor which mimics AsyncioExecutor::drop() — no drain
// after the drive loop, so tasks not driven before keepAliveCount hits 0
// are silently lost.
TEST_F(IOBufExtTest, testDropStyleExecutorShouldDecrefViaDrive) {
  PyObject* pyBytes = PyBytes_FromStringAndSize("drop!", 5);
  ASSERT_NE(pyBytes, nullptr);
  auto initialRefcnt = Py_REFCNT(pyBytes);

  auto executor = std::make_unique<DropStyleExecutor>();
  auto iobuf = iobuf_from_memoryview(
      executor.get(),
      pyBytes,
      PyBytes_AS_STRING(pyBytes),
      PyBytes_GET_SIZE(pyBytes));
  ASSERT_NE(iobuf, nullptr);
  EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt + 1);

  // Free the IOBuf from a non-GIL thread first (deterministic, no race).
  // The free callback enqueues Py_DECREF on the executor and either:
  //   (a) moves the KeepAlive into the task (std::move path), or
  //   (b) releases the KeepAlive via delete py_data (get()->add() path)
  std::thread([&iobuf]() { iobuf.reset(); }).join();

  // Now call drop(). It does keepAliveRelease (own ref), then drives
  // while keepAliveCount > 0.
  // With std::move: count is 1 (task holds KeepAlive) → drive() processes
  //   the Py_DECREF task → KeepAlive released → count 0 → exits.
  // Without std::move: count is 0 (KeepAlive already released by delete
  //   py_data) → loop doesn't execute → task never driven → Py_DECREF lost.
  executor->drop();

  // If Py_DECREF ran (std::move path), refcount is back to initial.
  // If Py_DECREF was lost (get()->add() path), refcount is still initial+1.
  EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt);
  Py_DECREF(pyBytes);
}

} // namespace
} // namespace folly::python
