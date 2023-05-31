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

#include <folly/detail/StaticSingletonManager.h>

#include <map>
#include <mutex>
#include <typeindex>

#include <folly/memory/ReentrantAllocator.h>

namespace folly {
namespace detail {

namespace {

class StaticSingletonManagerWithRttiImpl {
 public:
  using Make = void*();

  template <typename Arg>
  static void* create(Arg& arg) {
    // This Leaky Meyers Singleton must always live in the .cpp file.
    static Indestructible<StaticSingletonManagerWithRttiImpl> instance;
    auto const ptr = instance->entry(*arg.key).get(*arg.make, arg.debug);
    arg.cache.store(ptr, std::memory_order_release);
    return ptr;
  }

 private:
  struct Entry {
    void* ptr{};
    std::mutex mutex;

    void* get(Make& make, void** debug) {
      std::unique_lock<std::mutex> lock(mutex);
      return ptr ? ptr : (*debug = ptr = make());
    }
  };

  Entry& entry(std::type_info const& key) {
    std::unique_lock<std::mutex> lock(mutex_);
    return map_[key];
  }

  // using reentrant_allocator to permit new/delete hooks to use this class
  // using std::map over std::unordered_map to reduce number of mmap regions
  // since reentrant_allocator creates creates mmap regions to avoid malloc/free
  using map_value_t = std::pair<std::type_index const, Entry>;
  using map_less_t = std::less<std::type_index>;
  using map_alloc_t = reentrant_allocator<map_value_t>;
  using map_t = std::map<std::type_index, Entry, map_less_t, map_alloc_t>;

  map_t map_{map_alloc_t{reentrant_allocator_options{}}};
  std::mutex mutex_;
};

} // namespace

template <bool Noexcept>
void* StaticSingletonManagerWithRtti::create_(Arg& arg) noexcept(Noexcept) {
  return StaticSingletonManagerWithRttiImpl::create(arg);
}

template void* StaticSingletonManagerWithRtti::create_<false>(Arg& arg);
template void* StaticSingletonManagerWithRtti::create_<true>(Arg& arg) noexcept;

} // namespace detail
} // namespace folly
