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

#include <mutex>
#include <typeindex>
#include <unordered_map>

#include <folly/memory/ReentrantAllocator.h>

namespace folly {
namespace detail {

namespace {

class StaticSingletonManagerWithRttiImpl {
 public:
  using Self = StaticSingletonManagerWithRttiImpl;
  using Make = void*();

  static Self& instance() {
    // This Leaky Meyers Singleton must always live in the .cpp file.
    static Indestructible<StaticSingletonManagerWithRttiImpl> instance;
    return instance;
  }

  template <typename Arg>
  static void* get_existing(Arg& arg) {
    auto const* const entry = instance().get_existing_entry(*arg.key);
    auto const ptr = entry ? entry->get_existing() : nullptr;
    if (ptr) {
      arg.cache.store(ptr, std::memory_order_release);
    }
    return ptr;
  }

  template <typename Arg>
  static void* create(Arg& arg) {
    auto& entry = instance().create_entry(*arg.key);
    auto const ptr = entry.create(*arg.make, *arg.debug);
    arg.cache.store(ptr, std::memory_order_release);
    return ptr;
  }

 private:
  struct Entry {
    std::atomic<void*> ptr{};
    std::mutex mutex;

    void* get_existing() const { return ptr.load(std::memory_order_acquire); }

    void* create(Make& make, void*& debug) {
      if (auto const v = ptr.load(std::memory_order_acquire)) {
        return v;
      }
      std::unique_lock lock(mutex);
      if (auto const v = ptr.load(std::memory_order_acquire)) {
        return v;
      }
      auto const v = make();
      ptr.store(v, std::memory_order_release);
      debug = ptr;
      return v;
    }
  };

  Entry* get_existing_entry(std::type_info const& key) {
    std::unique_lock lock(mutex_);
    auto const it = map_.find(key);
    return it == map_.end() ? nullptr : &it->second;
  }

  Entry& create_entry(std::type_info const& key) {
    std::unique_lock lock(mutex_);
    return map_[key];
  }

  // Using reentrant_allocator to permit new/delete hooks to use this class.
  // std::map would be preferred over std::unordered_map to reduce number of
  // mmap regions, since reentrant_allocator creates mmap regions to avoid
  // malloc/free. However, std::map surfaced address sanitizer issues in
  // std::type_info::before when defining
  // _LIBCPP_TYPEINFO_COMPARISON_IMPLEMENTATION=2 on Mac builds.
  using map_value_t = std::pair<std::type_index const, Entry>;
  using map_alloc_t = reentrant_allocator<map_value_t>;
  using map_t = std::unordered_map<
      std::type_index,
      Entry,
      std::hash<std::type_index>,
      std::equal_to<std::type_index>,
      map_alloc_t>;

  map_t map_{map_alloc_t{reentrant_allocator_options{}}};
  std::mutex mutex_;
};

} // namespace

void* StaticSingletonManagerWithRtti::get_existing_(Arg& arg) noexcept {
  return StaticSingletonManagerWithRttiImpl::get_existing(arg);
}

template <bool Noexcept>
void* StaticSingletonManagerWithRtti::create_(Arg& arg) noexcept(Noexcept) {
  return StaticSingletonManagerWithRttiImpl::create(arg);
}

template void* StaticSingletonManagerWithRtti::create_<false>(Arg& arg);
template void* StaticSingletonManagerWithRtti::create_<true>(Arg& arg) noexcept;

} // namespace detail
} // namespace folly
