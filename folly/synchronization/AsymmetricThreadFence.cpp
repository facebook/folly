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

#include <folly/synchronization/AsymmetricThreadFence.h>

#include <mutex>

#include <folly/Exception.h>
#include <folly/Indestructible.h>
#include <folly/portability/SysMembarrier.h>
#include <folly/portability/SysMman.h>
#include <folly/synchronization/RelaxedAtomic.h>

namespace folly {

namespace {

// The intention is to force a memory barrier in every core running any of the
// process's threads. There is not a wide selection of options, but we do have
// one trick: force a TLB shootdown. There are multiple scenarios in which a TLB
// shootdown occurs, two of which are relevant: (1) when a resident page is
// swapped out, and (2) when the protection on a resident page is downgraded.
// We cannot force (1) and we cannot force (2). But we can force at least one of
// the outcomes (1) or (2) to happen!
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
    FOLLY_SAFE_PCHECK(MAP_FAILED != dummyPage);
  }

  // We want to downgrade the page while it is resident. To do that, it must
  // first be upgraded and forced to be resident.
  FOLLY_SAFE_PCHECK(-1 != mprotect(dummyPage, 1, PROT_READ | PROT_WRITE));

  // Force the page to be resident. If it is already resident, almost no-op.
  *static_cast<char volatile*>(dummyPage) = 0;

  // Downgrade the page. Forces a memory barrier in every core running any
  // of the process's threads, if the page is resident. On a sane platform.
  // If the page has been swapped out and is no longer resident, then the
  // memory barrier has already occurred.
  FOLLY_SAFE_PCHECK(-1 != mprotect(dummyPage, 1, PROT_READ));
}

bool sysMembarrierAvailableCached() {
  // Optimistic concurrency variation on static local variable
  static relaxed_atomic<char> cache{0};
  char value = cache;
  if (value == 0) {
    value = detail::sysMembarrierPrivateExpeditedAvailable() ? 1 : -1;
    cache = value;
  }
  return value == 1;
}

} // namespace

void asymmetric_thread_fence_heavy_fn::impl_(std::memory_order order) noexcept {
  if (kIsLinux) {
    if (sysMembarrierAvailableCached()) {
      FOLLY_SAFE_PCHECK(-1 != detail::sysMembarrierPrivateExpedited());
    } else {
      mprotectMembarrier();
    }
  } else {
    std::atomic_thread_fence(order);
  }
}

} // namespace folly
