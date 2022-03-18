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

#include <folly/system/AtFork.h>

#include <list>
#include <mutex>

#include <folly/ScopeGuard.h>
#include <folly/lang/Exception.h>
#include <folly/portability/PThread.h>
#include <folly/synchronization/SanitizeThread.h>

namespace folly {

namespace {

struct SkipAtForkHandlers {
  static thread_local bool value;

  struct Guard {
    bool saved = value;
    Guard() { value = true; }
    ~Guard() { value = saved; }
  };
};
thread_local bool SkipAtForkHandlers::value;

struct AtForkTask {
  void const* handle;
  Function<bool()> prepare;
  Function<void()> parent;
  Function<void()> child;
};

class AtForkList {
 public:
  static AtForkList& instance() {
    static auto instance = new AtForkList();
    return *instance;
  }

  static void prepare() noexcept {
    if (SkipAtForkHandlers::value) {
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
    if (SkipAtForkHandlers::value) {
      return;
    }
    auto& tasks = instance().tasks;
    for (auto& task : tasks) {
      task.parent();
    }
    instance().tasksLock.unlock();
  }

  static void child() noexcept {
    if (SkipAtForkHandlers::value) {
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
    int ret = 0;
#if FOLLY_HAVE_PTHREAD_ATFORK // if no pthread_atfork, probably no fork either
    ret = pthread_atfork(&prepare, &parent, &child);
#endif
    if (ret != 0) {
      throw_exception<std::system_error>(
          ret, std::generic_category(), "pthread_atfork failed");
    }
  }
};
} // namespace

void AtFork::init() {
  AtForkList::instance();
}

void AtFork::registerHandler(
    void const* handle,
    Function<bool()> prepare,
    Function<void()> parent,
    Function<void()> child) {
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
  AtForkList::prepare();
  auto ret = [&] {
    SkipAtForkHandlers::Guard guard;
    return forkFn();
  }();
  if (ret) {
    AtForkList::parent();
  } else {
    AtForkList::child();
  }
  return ret;
}
} // namespace folly
