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

#include <thread>

#include <folly/executors/ManualExecutor.h>
#include <folly/portability/GTest.h>
#include <folly/python/iobuf_ext.h>

namespace folly::python {
namespace {

struct IOBufExtTest : public testing::Test {
  void SetUp() override {
    Py_Initialize();
    gstate_ = PyGILState_Ensure();
  }

  void TearDown() override { PyGILState_Release(gstate_); }

 protected:
  PyGILState_STATE gstate_{PyGILState_UNLOCKED};
};

// Regression test: When the executor passed to
// iobuf_from_memoryview is destroyed before the IOBuf, the free callback
// dereferences a dangling Executor* pointer.
// DISABLED: This test crashes (ASAN heap-use-after-free) until the fix lands.
TEST_F(
    IOBufExtTest, DISABLED_testFreeCallbackShouldSurviveExecutorDestruction) {
  // Create a Python bytes object to serve as the backing buffer.
  PyObject* pyBytes = PyBytes_FromStringAndSize("hello", 5);
  ASSERT_NE(pyBytes, nullptr);
  auto initialRefcnt = Py_REFCNT(pyBytes);

  std::unique_ptr<folly::IOBuf> iobuf;
  {
    // Create an executor with a limited lifetime.
    auto executor = std::make_unique<folly::ManualExecutor>();

    // Wrap the Python buffer in an IOBuf, passing the executor.
    // iobuf_from_memoryview does Py_INCREF internally.
    iobuf = iobuf_from_memoryview(
        executor.get(),
        pyBytes,
        PyBytes_AS_STRING(pyBytes),
        PyBytes_GET_SIZE(pyBytes));
    ASSERT_NE(iobuf, nullptr);
    EXPECT_EQ(Py_REFCNT(pyBytes), initialRefcnt + 1);

    // Executor is destroyed here while IOBuf still holds a raw pointer to it.
  }

  // Destroy the IOBuf from a thread that has never held the GIL, just like
  // the ServiceRouter fiber thread in production. Before the fix, this
  // crashed (SIGSEGV) via executor->add() on the dangling pointer. After
  // the fix, it falls through to Py_AddPendingCall.
  std::thread([&iobuf]() { iobuf.reset(); }).join();

  // Py_AddPendingCall queues the decref; flush it.
  Py_MakePendingCalls();

  // The Python object's refcount should be back to the initial value.
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

} // namespace
} // namespace folly::python
