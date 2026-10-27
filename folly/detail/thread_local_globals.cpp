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

#include <folly/detail/thread_local_globals.h>

#include <stdexcept>
#include <system_error>

#include <folly/detail/StaticSingletonManager.h>
#include <folly/lang/Exception.h>
#include <folly/synchronization/MicroSpinLock.h>

namespace folly::detail {

namespace {

// Constant-initialized and trivially destructible, so that keys may be
// registered at any point in static initialization or destruction.
class pthread_key_registry {
 public:
  constexpr pthread_key_registry() noexcept : lock_(), size_(0), keys_() {}

  void add(pthread_key_t key) {
    MSLGuard lg(lock_);
    if (size_ == kCapacity) {
      throw_exception<std::logic_error>("too many pthread keys to register");
    }
    keys_[size_++] = key;
  }

  void delete_all() {
    MSLGuard lg(lock_);
    while (size_) {
      pthread_key_delete(keys_[--size_]);
    }
  }

 private:
  static constexpr size_t kCapacity = 1024;

  MicroSpinLock lock_;
  size_t size_;
  pthread_key_t keys_[kCapacity];
};

pthread_key_registry pthread_keys;

/// thread_is_dying_global
///
/// This is a hack. The C runtime library provides no indication that a thread
/// has ended the phase where thread_local variables may be destroyed. Were it
/// to provide that indication, that would be used instead. Relies on library
/// facilities which have signal to mark that a thread is dying.
///
/// In glibc, in the shutdown phase, start_thread first calls __call_tls_dtors,
/// which runs destructors of thread_local variables, and then immediately calls
/// __nptl_deallocate_tsd, which runs destructors of pthread thread-specific
/// variables. There is no other indication of state change in the current
/// thread.
///
/// https://github.com/bminor/glibc/blob/glibc-2.39/nptl/pthread_create.c#L451-L455
struct thread_is_dying_global {
  pthread_key_t key{};

  thread_is_dying_global() {
    int ret = pthread_key_create(&key, nullptr);
    if (ret != 0) {
      throw_exception<std::system_error>(
          ret, std::generic_category(), "pthread_key_create failed");
    }
    thread_local_register_pthread_key(key);
  }
};

} // namespace

bool thread_is_dying() {
  auto& global = createGlobal<thread_is_dying_global, void>();
  return !!pthread_getspecific(global.key);
}

void thread_is_dying_mark() {
  auto& global = createGlobal<thread_is_dying_global, void>();
  if (!pthread_getspecific(global.key)) {
    pthread_setspecific(global.key, &global.key);
  }
}

void thread_local_register_pthread_key(pthread_key_t key) {
  pthread_keys.add(key);
}

void thread_local_delete_pthread_keys() {
  pthread_keys.delete_all();
}

} // namespace folly::detail
