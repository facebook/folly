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

#include <atomic>
#include <functional>
#include <utility>

#ifdef Py_GIL_DISABLED
#include <chrono>
#include <latch>
#include <thread>
#include <vector>
#endif

#include <folly/ScopeGuard.h>
#include <folly/portability/GTest.h>
#include <folly/python/import.h>

namespace folly::python::test {
namespace {

class PythonThreadState {
 public:
  PythonThreadState() : state_{PyGILState_Ensure()} {}
  ~PythonThreadState() { PyGILState_Release(state_); }

 private:
  PyGILState_STATE state_;
};

class MockCythonImport {
 public:
  using ImportHandler = std::function<int()>;

  static MockCythonImport& getInstance() {
    static MockCythonImport mock;
    return mock;
  }

  static int import() { return getInstance().call(); }

  void reset() {
    callCount_.store(0);
    resetImportHandler();
  }

  void resetImportHandler() {
    importHandler_ = [] { return 0; };
  }

  void onImport(ImportHandler handler) { importHandler_ = std::move(handler); }

  size_t callCount() const { return callCount_.load(); }

 private:
  int call() {
    ++callCount_;
    return importHandler_();
  }

  std::atomic<size_t> callCount_{0};
  ImportHandler importHandler_{[] { return 0; }};
};

class ImportCacheTest : public testing::Test {
 public:
  void SetUp() override {
    Py_Initialize();
    mainThreadState_ = PyEval_SaveThread();
    MockCythonImport::getInstance().reset();
  }

  void TearDown() override { PyEval_RestoreThread(mainThreadState_); }

 private:
  PyThreadState* mainThreadState_{nullptr};
};

TEST_F(ImportCacheTest, allowsRecursiveImportFromSameThread) {
  PythonThreadState gil;

  auto& mock = MockCythonImport::getInstance();
  import_cache_nocapture cache{MockCythonImport::import};

  constexpr size_t kRecursiveImports = 3;
  size_t recursiveImportsRemaining = kRecursiveImports;
  mock.onImport([&] {
    if (recursiveImportsRemaining != 0) {
      --recursiveImportsRemaining;
      // Re-enter the same cache while the import function is still active.
      return cache() ? 0 : -1;
    }
    return 0;
  });
  auto resetImportHandler = folly::makeGuard([&] {
    mock.resetImportHandler();
  });

  EXPECT_TRUE(cache());
  EXPECT_EQ(0, recursiveImportsRemaining);
  EXPECT_EQ(kRecursiveImports + 1, mock.callCount());

  EXPECT_TRUE(cache());
  EXPECT_EQ(kRecursiveImports + 1, mock.callCount());
}

#ifdef Py_GIL_DISABLED
// Only meaningful without the GIL: with it, the second caller cannot enter the
// import while the first is still inside it.
TEST_F(ImportCacheTest, initializesOnceWhenThreadsRace) {
  auto& mock = MockCythonImport::getInstance();
  import_cache_nocapture cache{MockCythonImport::import};

  // Stay inside the import long enough for the other callers to pile up.
  mock.onImport([] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 0;
  });

  constexpr size_t kRacingCallers = 4;
  std::latch allReady{kRacingCallers};
  std::vector<std::thread> callers;
  for (size_t i = 0; i < kRacingCallers; ++i) {
    callers.emplace_back([&] {
      // Attached, like a real cython import, so waiters exercise the detach in
      // ScopedGILRelease.
      PythonThreadState gil;
      // Nobody calls cache() until every caller is attached and ready.
      allReady.arrive_and_wait();
      EXPECT_TRUE(cache());
    });
  }
  for (auto& caller : callers) {
    caller.join();
  }

  EXPECT_EQ(1, mock.callCount());
}
#endif

} // namespace
} // namespace folly::python::test
