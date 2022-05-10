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

#include <folly/synchronization/AsymmetricMemoryBarrier.h>

#include <mutex>

#include <folly/Exception.h>
#include <folly/Indestructible.h>
#include <folly/portability/SysMembarrier.h>
#include <folly/portability/SysMman.h>

namespace folly {

namespace {

void mprotectMembarrier() {
  // This function is required to be safe to call on shutdown,
  // so we must leak the mutex.
  static Indestructible<std::mutex> mprotectMutex;
  std::lock_guard<std::mutex> lg(*mprotectMutex);

  // Ensure that we have a dummy page. The page is not used to store data;
  // rather, it is used only for the side-effects of page operations.
  static void* dummyPage = nullptr;
  if (dummyPage == nullptr) {
    dummyPage = mmap(nullptr, 1, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    checkUnixError(reinterpret_cast<ssize_t>(dummyPage), "mmap");
  }

  int r = 0;

  // We want to downgrade the page while it is resident. To do that, it must
  // first be upgraded and forced to be resident.
  r = mprotect(dummyPage, 1, PROT_READ | PROT_WRITE);
  checkUnixError(r, "mprotect");

  // Force the page to be resident. If it is already resident, almost no-op.
  *static_cast<char*>(dummyPage) = 0;

  // Downgrade the page. Forces a memory barrier in every core running any
  // of the process's threads. On a sane platform.
  r = mprotect(dummyPage, 1, PROT_READ);
  checkUnixError(r, "mprotect");
}

} // namespace

void asymmetricHeavyBarrier() {
  if (kIsLinux) {
    static const bool useSysMembarrier =
        detail::sysMembarrierPrivateExpeditedAvailable();
    if (useSysMembarrier) {
      auto r = detail::sysMembarrierPrivateExpedited();
      checkUnixError(r, "membarrier");
    } else {
      mprotectMembarrier();
    }
  } else {
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }
}

} // namespace folly
