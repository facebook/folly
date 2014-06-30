/*
 * Copyright 2014 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/mman.h>

#include <cstddef>
#include <map>
#include <stdexcept>

#include <folly/AtomicHashArray.h>
#include <folly/Hash.h>
#include <folly/Conv.h>
#include <folly/Memory.h>
#include <gtest/gtest.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

using namespace std;
using namespace folly;

template <class T>
class MmapAllocator {
 public:
  typedef T value_type;
  typedef T* pointer;
  typedef const T* const_pointer;
  typedef T& reference;
  typedef const T& const_reference;

  typedef ptrdiff_t difference_type;
  typedef size_t size_type;

  T* address(T& x) const {
    return std::addressof(x);
  }

  const T* address(const T& x) const {
    return std::addressof(x);
  }

  size_t max_size() const {
    return std::numeric_limits<size_t>::max();
  }

  template <class U> struct rebind {
    typedef MmapAllocator<U> other;
  };

  bool operator!=(const MmapAllocator<T>& other) const {
    return !(*this == other);
  }

  bool operator==(const MmapAllocator<T>& other) const {
    return true;
  }

  template <class... Args>
  void construct(T* p, Args&&... args) {
    new (p) T(std::forward<Args>(args)...);
  }

  void destroy(T* p) {
    p->~T();
  }

  T *allocate(size_t n) {
    void *p = mmap(nullptr, n * sizeof(T), PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (!p) throw std::bad_alloc();
    return (T *)p;
  }

  void deallocate(T *p, size_t n) {
    munmap(p, n * sizeof(T));
  }
};

template<class KeyT, class ValueT>
pair<KeyT,ValueT> createEntry(int i) {
  return pair<KeyT,ValueT>(to<KeyT>(folly::hash::jenkins_rev_mix32(i) % 1000),
                           to<ValueT>(i + 3));
}

template<class KeyT, class ValueT, class Allocator = std::allocator<char>>
void testMap() {
  typedef AtomicHashArray<KeyT, ValueT, std::hash<KeyT>,
                          std::equal_to<KeyT>, Allocator> MyArr;
  auto arr = MyArr::create(150);
  map<KeyT, ValueT> ref;
  for (int i = 0; i < 100; ++i) {
    auto e = createEntry<KeyT, ValueT>(i);
    auto ret = arr->insert(e);
    EXPECT_EQ(!ref.count(e.first), ret.second);  // succeed iff not in ref
    ref.insert(e);
    EXPECT_EQ(ref.size(), arr->size());
    if (ret.first == arr->end()) {
      EXPECT_FALSE("AHA should not have run out of space.");
      continue;
    }
    EXPECT_EQ(e.first, ret.first->first);
    EXPECT_EQ(ref.find(e.first)->second, ret.first->second);
  }

  for (int i = 125; i > 0; i -= 10) {
    auto e = createEntry<KeyT, ValueT>(i);
    auto ret = arr->erase(e.first);
    auto refRet = ref.erase(e.first);
    EXPECT_EQ(ref.size(), arr->size());
    EXPECT_EQ(refRet, ret);
  }

  for (int i = 155; i > 0; i -= 10) {
    auto e = createEntry<KeyT, ValueT>(i);
    auto ret = arr->insert(e);
    auto refRet = ref.insert(e);
    EXPECT_EQ(ref.size(), arr->size());
    EXPECT_EQ(*refRet.first, *ret.first);
    EXPECT_EQ(refRet.second, ret.second);
  }

  for (const auto& e : ref) {
    auto ret = arr->find(e.first);
    if (ret == arr->end()) {
      EXPECT_FALSE("Key was not in AHA");
      continue;
    }
    EXPECT_EQ(e.first, ret->first);
    EXPECT_EQ(e.second, ret->second);
  }
}

template<class KeyT, class ValueT, class Allocator = std::allocator<char>>
void testNoncopyableMap() {
  typedef AtomicHashArray<KeyT, std::unique_ptr<ValueT>, std::hash<KeyT>,
                          std::equal_to<KeyT>, Allocator> MyArr;
  auto arr = MyArr::create(150);
  for (int i = 0; i < 100; i++) {
    arr->insert(make_pair(i,std::unique_ptr<ValueT>(new ValueT(i))));
  }
  for (int i = 0; i < 100; i++) {
    auto ret = arr->find(i);
    EXPECT_EQ(*(ret->second), i);
  }
}


TEST(Aha, InsertErase_i32_i32) {
  testMap<int32_t, int32_t>();
  testMap<int32_t, int32_t, MmapAllocator<char>>();
  testNoncopyableMap<int32_t, int32_t>();
  testNoncopyableMap<int32_t, int32_t, MmapAllocator<char>>();
}
TEST(Aha, InsertErase_i64_i32) {
  testMap<int64_t, int32_t>();
  testMap<int64_t, int32_t, MmapAllocator<char>>();
  testNoncopyableMap<int64_t, int32_t>();
  testNoncopyableMap<int64_t, int32_t, MmapAllocator<char>>();
}
TEST(Aha, InsertErase_i64_i64) {
  testMap<int64_t, int64_t>();
  testMap<int64_t, int64_t, MmapAllocator<char>>();
  testNoncopyableMap<int64_t, int64_t>();
  testNoncopyableMap<int64_t, int64_t, MmapAllocator<char>>();
}
TEST(Aha, InsertErase_i32_i64) {
  testMap<int32_t, int64_t>();
  testMap<int32_t, int64_t, MmapAllocator<char>>();
  testNoncopyableMap<int32_t, int64_t>();
  testNoncopyableMap<int32_t, int64_t, MmapAllocator<char>>();
}
TEST(Aha, InsertErase_i32_str) {
  testMap<int32_t, string>();
  testMap<int32_t, string, MmapAllocator<char>>();
}
TEST(Aha, InsertErase_i64_str) {
  testMap<int64_t, string>();
  testMap<int64_t, string, MmapAllocator<char>>();
}
