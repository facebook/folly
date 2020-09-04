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

#include <folly/detail/AtFork.h>

#include <list>
#include <mutex>

#include <folly/ScopeGuard.h>
#include <folly/lang/Exception.h>
#include <folly/portability/PThread.h>
#include <folly/synchronization/SanitizeThread.h>

namespace folly {

namespace detail {

namespace {

#if !FOLLY_MOBILE && !defined(__APPLE__) && !defined(_MSC_VER)
#define FOLLY_ATFORK_USE_FOLLY_TLS 1
#else
#define FOLLY_ATFORK_USE_FOLLY_TLS 0
#endif

template <bool enabled = FOLLY_ATFORK_USE_FOLLY_TLS>
struct SkipAtForkHandlers;

template <>
struct SkipAtForkHandlers<false> {
  static bool available() {
    return false;
  }

  static bool get() {
    return false;
  }

  [[noreturn]] static void set(bool) {
    std::terminate();
  }
};

#if FOLLY_ATFORK_USE_FOLLY_TLS
template <>
struct SkipAtForkHandlers<true> {
  static bool available() {
    return true;
  }

  static bool get() {
    return value_;
  }

  static void set(bool value) {
    value_ = value;
  }

 private:
  static FOLLY_TLS bool value_;
};
FOLLY_TLS bool SkipAtForkHandlers<true>::value_{false};
#endif

struct AtForkTask {
  void const* handle;
  folly::Function<bool()> prepare;
  folly::Function<void()> parent;
  folly::Function<void()> child;
};

class AtForkList {
 public:
  static AtForkList& instance() {
    static auto instance = new AtForkList();
    return *instance;
  }

  static void prepare() noexcept {
    if (SkipAtForkHandlers<>::get()) {
      return;
    }
    instance().tasksLock.lock();
    while (true) {
      auto& tasks = instance().tasks;
      auto task = tasks.rbegin();
      for (; task != tasks.rend(); ++task) {
        if (!task->prepare()) {
          break;
        }
      }
      if (task == tasks.rend()) {
        return;
      }
      for (auto untask = tasks.rbegin(); untask != task; ++untask) {
        untask->parent();
      }
    }
  }

  static void parent() noexcept {
    if (SkipAtForkHandlers<>::get()) {
      return;
    }
    auto& tasks = instance().tasks;
    for (auto& task : tasks) {
      task.parent();
    }
    instance().tasksLock.unlock();
  }

  static void child() noexcept {
    if (SkipAtForkHandlers<>::get()) {
      return;
    }
    // if we fork a multithreaded process
    // some of the TSAN mutexes might be locked
    // so we just enable ignores for everything
    // while handling the child callbacks
    // This might still be an issue if we do not exec right away
    annotate_ignore_thread_sanitizer_guard g(__FILE__, __LINE__);

    auto& tasks = instance().tasks;
    for (auto& task : tasks) {
      task.child();
    }
    instance().tasksLock.unlock();
  }

  std::mutex tasksLock;
  std::list<AtForkTask> tasks;

 private:
  AtForkList() {
#if FOLLY_HAVE_PTHREAD_ATFORK
    int ret = pthread_atfork(
        &AtForkList::prepare, &AtForkList::parent, &AtForkList::child);
    if (ret != 0) {
      throw_exception<std::system_error>(
          ret, std::generic_category(), "pthread_atfork failed");
    }
#elif !__ANDROID__ && !defined(_MSC_VER)
// pthread_atfork is not part of the Android NDK at least as of n9d. If
// something is trying to call native fork() directly at all with Android's
// process management model, this is probably the least of the problems.
//
// But otherwise, this is a problem.
#warning pthread_atfork unavailable
#endif
  }
};
} // namespace

void AtFork::init() {
  AtForkList::instance();
}

void AtFork::registerHandler(
    void const* handle,
    folly::Function<bool()> prepare,
    folly::Function<void()> parent,
    folly::Function<void()> child) {
  std::lock_guard<std::mutex> lg(AtForkList::instance().tasksLock);
  AtForkList::instance().tasks.push_back(
      {handle, std::move(prepare), std::move(parent), std::move(child)});
}

void AtFork::unregisterHandler(void const* handle) {
  if (!handle) {
    return;
  }
  auto& list = AtForkList::instance();
  std::lock_guard<std::mutex> lg(list.tasksLock);
  for (auto it = list.tasks.begin(); it != list.tasks.end(); ++it) {
    if (it->handle == handle) {
      list.tasks.erase(it);
      return;
    }
  }
}

pid_t AtFork::forkInstrumented(fork_t forkFn) {
  if (SkipAtForkHandlers<>::available()) {
    AtForkList::prepare();
    SkipAtForkHandlers<>::set(true);
    auto ret = forkFn();
    SkipAtForkHandlers<>::set(false);
    if (ret) {
      AtForkList::parent();
    } else {
      AtForkList::child();
    }
    return ret;
  } else {
    return forkFn();
  }
}
} // namespace detail
} // namespace folly
