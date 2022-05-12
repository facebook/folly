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

#include <folly/ScopeGuard.h>
#include <folly/lang/Exception.h>
#include <folly/portability/PThread.h>
#include <folly/synchronization/SanitizeThread.h>

namespace folly {

void AtForkList::prepare() noexcept {
  mutex.lock();
  while (true) {
    auto task = tasks.rbegin();
    for (; task != tasks.rend(); ++task) {
      if (auto& f = task->prepare) {
        if (!f()) {
          break;
        }
      }
    }
    if (task == tasks.rend()) {
      return;
    }
    for (auto untask = tasks.rbegin(); untask != task; ++untask) {
      if (auto& f = untask->parent) {
        f();
      }
    }
  }
}

void AtForkList::parent() noexcept {
  for (auto& task : tasks) {
    if (auto& f = task.parent) {
      f();
    }
  }
  mutex.unlock();
}

void AtForkList::child() noexcept {
  for (auto& task : tasks) {
    if (auto& f = task.child) {
      f();
    }
  }
  mutex.unlock();
}

void AtForkList::append(
    void const* handle,
    Function<bool()> prepare,
    Function<void()> parent,
    Function<void()> child) {
  std::unique_lock<std::mutex> lg{mutex};
  if (handle && index.count(handle)) {
    throw_exception<std::invalid_argument>("at-fork: append: duplicate");
  }
  auto task =
      Task{handle, std::move(prepare), std::move(parent), std::move(child)};
  auto inserted = tasks.insert(tasks.end(), std::move(task));
  if (handle) {
    index.emplace(handle, inserted);
  }
}

void AtForkList::remove(void const* handle) {
  if (!handle) {
    return;
  }
  std::unique_lock<std::mutex> lg{mutex};
  auto i1 = index.find(handle);
  if (i1 == index.end()) {
    throw_exception<std::out_of_range>("at-fork: remove: missing");
  }
  auto i2 = i1->second;
  index.erase(i1);
  tasks.erase(i2);
}

bool AtForkList::contains( //
    void const* handle) {
  if (!handle) {
    return false;
  }
  std::unique_lock<std::mutex> lg{mutex};
  return index.count(handle) != 0;
}

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

struct AtForkListSingleton {
  static AtForkList& make() {
    auto& instance = *new AtForkList();
    int ret = 0;
#if FOLLY_HAVE_PTHREAD_ATFORK // if no pthread_atfork, probably no fork either
    ret = pthread_atfork(&prepare, &parent, &child);
#endif
    if (ret != 0) {
      throw_exception<std::system_error>(
          ret, std::generic_category(), "pthread_atfork failed");
    }
    return instance;
  }

  static AtForkList& get() {
    static auto& instance = make();
    return instance;
  }

  static void prepare() {
    if (!SkipAtForkHandlers::value) {
      get().prepare();
    }
  }

  static void parent() {
    if (!SkipAtForkHandlers::value) {
      get().parent();
    }
  }

  static void child() {
    if (!SkipAtForkHandlers::value) {
      // if we fork a multithreaded process
      // some of the TSAN mutexes might be locked
      // so we just enable ignores for everything
      // while handling the child callbacks
      // This might still be an issue if we do not exec right away
      annotate_ignore_thread_sanitizer_guard g(__FILE__, __LINE__);
      get().child();
    }
  }
};

} // namespace

void AtFork::init() {
  AtForkListSingleton::get();
}

void AtFork::registerHandler(
    void const* handle,
    Function<bool()> prepare,
    Function<void()> parent,
    Function<void()> child) {
  auto& list = AtForkListSingleton::get();
  list.append(handle, std::move(prepare), std::move(parent), std::move(child));
}

void AtFork::unregisterHandler(void const* handle) {
  auto& list = AtForkListSingleton::get();
  list.remove(handle);
}

pid_t AtFork::forkInstrumented(fork_t forkFn) {
  if (SkipAtForkHandlers::value) {
    return forkFn();
  }
  auto& list = AtForkListSingleton::get();
  list.prepare();
  auto ret = [&] {
    SkipAtForkHandlers::Guard guard;
    return forkFn();
  }();
  if (ret) {
    list.parent();
  } else {
    list.child();
  }
  return ret;
}
} // namespace folly
