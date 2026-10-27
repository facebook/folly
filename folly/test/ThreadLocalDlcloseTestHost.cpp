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

// Loads a library with its own copy of folly, uses a folly::ThreadLocal from
// worker threads, prepares the library for unloading and closes it, and then
// lets the last worker exit. This
// binary must not link folly itself, so that the library cannot bind to another
// copy and closing it can really unmap it.
//
// Exit status: 0 on success, 3 if the library stays mapped once the worker has
// exited, 4 if loading and unloading the library leaks pthread keys.

#include <dlfcn.h>
// This binary must not link folly, so it cannot use
// folly/portability/PThread.h.
// @lint-ignore CLANGTIDY facebook-hte-PortabilityInclude-pthread.h
#include <pthread.h>

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
// The library's folly calls the TSAN annotations and relies on this binary to
// provide them. Referencing one links the runtime's annotation support, which
// --export-dynamic then exports.
extern "C" void AnnotateRWLockCreate(
    char const* f, int l, const volatile void* addr);
[[gnu::used]] static auto const keepTsanAnnotations = &AnnotateRWLockCreate;
#endif
#endif

namespace {

constexpr int kLeakCheckCycles = 10;

void* load(const char* path) {
  void* handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
  if (handle == nullptr) {
    std::fprintf(stderr, "dlopen(%s): %s\n", path, dlerror());
    std::exit(2);
  }
  return handle;
}

using Fn = void (*)();

Fn lookup(void* handle, const char* name) {
  auto const fn = reinterpret_cast<Fn>(dlsym(handle, name));
  if (fn == nullptr) {
    std::fprintf(stderr, "dlsym(%s): %s\n", name, dlerror());
    std::exit(2);
  }
  return fn;
}

bool isLoaded(const char* path) {
  void* handle = dlopen(path, RTLD_LAZY | RTLD_NOLOAD);
  if (handle != nullptr) {
    dlclose(handle);
  }
  return handle != nullptr;
}

// Returns whether the library is unmapped once the worker has exited.
bool runCycle(const char* path, bool verbose) {
  // glibc does not unmap a library while any thread still owes it a C++
  // thread_local destructor, and folly registers one on the thread that runs
  // its static constructors. Load from a thread that exits straight away so
  // the main thread does not pin the library.
  void* handle = nullptr;
  std::thread([&] { handle = load(path); }).join();

  auto const use = lookup(handle, "useThreadLocal");
  auto const prepareForUnload = lookup(handle, "prepareForUnload");

  // A thread that exits while the library is loaded runs folly's thread exit
  // handling, which creates keys of its own.
  std::thread(use).join();

  std::mutex mutex;
  std::condition_variable cv;
  bool used = false;
  bool closed = false;

  std::thread worker([&] {
    use();
    {
      std::lock_guard lock(mutex);
      used = true;
    }
    cv.notify_all();
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return closed; });
    // Returning runs the thread's pthread key destructors, which folly set to
    // functions inside the library.
  });

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return used; });
  }

  prepareForUnload();
  dlclose(handle);
  if (verbose) {
    std::fprintf(
        stderr,
        "library %s after dlclose\n",
        isLoaded(path) ? "still mapped" : "unmapped");
  }

  {
    std::lock_guard lock(mutex);
    closed = true;
  }
  cv.notify_all();
  worker.join();

  bool const loaded = isLoaded(path);
  if (verbose) {
    std::fprintf(
        stderr,
        "worker exited; library %s\n",
        loaded ? "still mapped" : "unmapped");
  }
  return !loaded;
}

size_t freeKeyCount() {
  std::vector<pthread_key_t> keys;
  pthread_key_t key;
  while (pthread_key_create(&key, nullptr) == 0) {
    keys.push_back(key);
  }
  for (auto k : keys) {
    pthread_key_delete(k);
  }
  return keys.size();
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <library>\n", argv[0]);
    return 2;
  }
  const char* path = argv[1];

  // The first cycle also creates any keys the C++ runtime allocates once per
  // process.
  if (!runCycle(path, /*verbose=*/true)) {
    return 3;
  }

  auto const before = freeKeyCount();
  for (int i = 0; i < kLeakCheckCycles; ++i) {
    if (!runCycle(path, /*verbose=*/false)) {
      return 3;
    }
  }
  auto const after = freeKeyCount();
  if (after != before) {
    std::fprintf(
        stderr,
        "%d load cycles leaked %zu pthread keys\n",
        kLeakCheckCycles,
        before - after);
    return 4;
  }
  return 0;
}
