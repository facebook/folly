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

#include <folly/system/HardwareConcurrency.h>

#include <thread>

#include <folly/Utility.h>
#include <folly/lang/SafeAssert.h>
#include <folly/portability/Sched.h>

namespace folly {

#if defined(__linux__)

cpu_set_state cpu_set_state::ask(cpu_set_t* stackset) noexcept {
  static std::atomic<std::size_t> setszc{0}; // monotonic, like in the kernel
  auto setszcv = setszc.load(std::memory_order_relaxed);
  auto setsz = setszcv ? setszcv : CPU_SETSIZE;
  auto set = stackset && setsz == CPU_SETSIZE ? stackset : CPU_ALLOC(setsz);
  while (true) {
    FOLLY_SAFE_CHECK(!!set);
    /// sched_getaffinity:
    /// * returns 0 on success and -1 on failure
    /// * fails with EINVAL if the provided set is too small
    int const ret = sched_getaffinity(0, CPU_ALLOC_SIZE(setsz), set);
    if (ret == 0) {
      break;
    }
    FOLLY_SAFE_PCHECK(errno == EINVAL);
    CPU_FREE(set == stackset ? nullptr : set);
    setsz *= 2;
    set = CPU_ALLOC(setsz);
  }
  /// atomic-max: setszc <- max(setszc, setsz)
  while (setsz > setszcv) {
    if (setszc.compare_exchange_strong(
            setszcv, setsz, std::memory_order_relaxed)) {
      setszcv = setsz;
    }
  }
  FOLLY_SAFE_CHECK(setsz <= setszcv);
  return {set, setsz, set != stackset};
}

cpu_set_state::~cpu_set_state() {
  CPU_FREE(!alloc_ ? nullptr : set_);
  set_ = nullptr;
  size_ = 0;
  alloc_ = false;
}

#endif

unsigned int hardware_concurrency() noexcept {
#if defined(__linux__)
  cpu_set_t stackset;
  return to_narrow(cpu_set_state::ask(&stackset).cpu_count());
#endif

  return std::thread::hardware_concurrency();
}

} // namespace folly
