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

#include <folly/memory/SanitizeLeak.h>

#include <mutex>
#include <unordered_map>

#include <folly/lang/Extern.h>

//  Leak Sanitizer interface may be found at:
//    https://github.com/llvm/llvm-project/blob/main/compiler-rt/include/sanitizer/lsan_interface.h
extern "C" void __lsan_ignore_object(void const*);
extern "C" void __lsan_register_root_region(void const*, std::size_t);
extern "C" void __lsan_unregister_root_region(void const*, std::size_t);

namespace {

FOLLY_CREATE_EXTERN_ACCESSOR( //
    lsan_ignore_object_access_v,
    __lsan_ignore_object);
FOLLY_CREATE_EXTERN_ACCESSOR( //
    lsan_register_root_region_access_v,
    __lsan_register_root_region);
FOLLY_CREATE_EXTERN_ACCESSOR( //
    lsan_unregister_root_region_access_v,
    __lsan_unregister_root_region);

constexpr bool E = folly::kIsLibrarySanitizeAddress;

} // namespace

namespace folly {

namespace detail {

FOLLY_STORAGE_CONSTEXPR lsan_ignore_object_t* const //
    lsan_ignore_object_v = lsan_ignore_object_access_v<E>;
FOLLY_STORAGE_CONSTEXPR lsan_register_root_region_t* const //
    lsan_register_root_region_v = lsan_register_root_region_access_v<E>;
FOLLY_STORAGE_CONSTEXPR lsan_unregister_root_region_t* const //
    lsan_unregister_root_region_v = lsan_unregister_root_region_access_v<E>;

namespace {
struct LeakedPtrs {
  std::mutex mutex;
  std::unordered_map<void const*, size_t> map;

  static LeakedPtrs& instance() {
    static auto* ptrs = new LeakedPtrs();
    return *ptrs;
  }
};
} // namespace

void annotate_object_leaked_impl(void const* ptr) {
  if (ptr == nullptr) {
    return;
  }
  auto& ptrs = LeakedPtrs::instance();
  std::lock_guard<std::mutex> lg(ptrs.mutex);
  ++ptrs.map[ptr];
}

void annotate_object_collected_impl(void const* ptr) {
  if (ptr == nullptr) {
    return;
  }
  auto& ptrs = LeakedPtrs::instance();
  std::lock_guard<std::mutex> lg(ptrs.mutex);
  if (!--ptrs.map[ptr]) {
    ptrs.map.erase(ptr);
  }
}

} // namespace detail
} // namespace folly
