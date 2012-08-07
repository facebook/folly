/*
 * Copyright 2012 Facebook, Inc.
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

#ifndef FOLLY_STLALLOCATOR_H_
#define FOLLY_STLALLOCATOR_H_

#include <memory>

namespace folly {

/**
 * Wrap a simple allocator into a STL-compliant allocator.
 *
 * The simple allocator must provide two methods:
 *    void* allocate(size_t size);
 *    void deallocate(void* ptr, size_t size);
 * which, respectively, allocate a block of size bytes (aligned to the maximum
 * alignment required on your system), throwing std::bad_alloc if the
 * allocation can't be satisfied, and free a previously allocated block.
 *
 * Note that the following allocator resembles the standard allocator
 * quite well:
 *
 * class MallocAllocator {
 *  public:
 *   void* allocate(size_t size) {
 *     void* p = malloc(size);
 *     if (!p) throw std::bad_alloc();
 *     return p;
 *   }
 *   void deallocate(void* p) {
 *     free(p);
 *   }
 * };
 */

// This would be so much simpler with std::allocator_traits, but gcc 4.6.2
// doesn't support it
template <class Alloc, class T> class StlAllocator;

template <class Alloc> class StlAllocator<Alloc, void> {
 public:
  typedef void value_type;
  typedef void* pointer;
  typedef const void* const_pointer;
  template <class U> struct rebind {
    typedef StlAllocator<Alloc, U> other;
  };
};

template <class Alloc, class T>
class StlAllocator {
 public:
  typedef T value_type;
  typedef T* pointer;
  typedef const T* const_pointer;
  typedef T& reference;
  typedef const T& const_reference;

  typedef ptrdiff_t difference_type;
  typedef size_t size_type;

  StlAllocator() : alloc_(nullptr) { }
  explicit StlAllocator(Alloc* alloc) : alloc_(alloc) { }

  template <class U> StlAllocator(const StlAllocator<Alloc, U>& other)
    : alloc_(other.alloc()) { }

  T* allocate(size_t n, const void* hint = nullptr) {
    return static_cast<T*>(alloc_->allocate(n * sizeof(T)));
  }

  void deallocate(T* p, size_t n) {
    alloc_->deallocate(p);
  }

  size_t max_size() const {
    return std::numeric_limits<size_t>::max();
  }

  T* address(T& x) const {
    return std::addressof(x);
  }

  const T* address(const T& x) const {
    return std::addressof(x);
  }

  template <class... Args>
  void construct(T* p, Args&&... args) {
    new (p) T(std::forward<Args>(args)...);
  }

  void destroy(T* p) {
    p->~T();
  }

  Alloc* alloc() const {
    return alloc_;
  }

  template <class U> struct rebind {
    typedef StlAllocator<Alloc, U> other;
  };

  bool operator!=(const StlAllocator<Alloc, T>& other) const {
    return alloc_ != other.alloc_;
  }

  bool operator==(const StlAllocator<Alloc, T>& other) const {
    return alloc_ == other.alloc_;
  }

 private:
  Alloc* alloc_;
};

}  // namespace folly

#endif /* FOLLY_STLALLOCATOR_H_ */

