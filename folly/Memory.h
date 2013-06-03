/*
 * Copyright 2013 Facebook, Inc.
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

#ifndef FOLLY_MEMORY_H_
#define FOLLY_MEMORY_H_

#include "folly/Traits.h"

#include <memory>
#include <limits>
#include <utility>
#include <exception>
#include <stdexcept>

#include <cstddef>

namespace folly {

/**
 * For exception safety and consistency with make_shared. Erase me when
 * we have std::make_unique().
 *
 * @author Louis Brandy (ldbrandy@fb.com)
 * @author Xu Ning (xning@fb.com)
 */

template<typename T, typename Dp = std::default_delete<T>, typename... Args>
std::unique_ptr<T, Dp> make_unique(Args&&... args) {
  return std::unique_ptr<T, Dp>(new T(std::forward<Args>(args)...));
}

/*
 * StlAllocator wraps a SimpleAllocator into a STL-compliant
 * allocator, maintaining an instance pointer to the simple allocator
 * object.  The underlying SimpleAllocator object must outlive all
 * instances of StlAllocator using it.
 *
 * A SimpleAllocator must provide two methods:
 *
 *    void* allocate(size_t size);
 *    void deallocate(void* ptr);
 *
 * which, respectively, allocate a block of size bytes (aligned to the
 * maximum alignment required on your system), throwing std::bad_alloc
 * if the allocation can't be satisfied, and free a previously
 * allocated block.
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
 *
 * But note that if you pass StlAllocator<MallocAllocator,...> to a
 * standard container it will be larger due to the contained state
 * pointer.
 *
 * author: Tudor Bosman <tudorb@fb.com>
 */

// This would be so much simpler with std::allocator_traits, but gcc 4.6.2
// doesn't support it.
template <class Alloc, class T> class StlAllocator;

template <class Alloc> class StlAllocator<Alloc, void> {
 public:
  typedef void value_type;
  typedef void* pointer;
  typedef const void* const_pointer;

  StlAllocator() : alloc_(nullptr) { }
  explicit StlAllocator(Alloc* alloc) : alloc_(alloc) { }

  Alloc* alloc() const {
    return alloc_;
  }

  template <class U> struct rebind {
    typedef StlAllocator<Alloc, U> other;
  };

  bool operator!=(const StlAllocator<Alloc, void>& other) const {
    return alloc_ != other.alloc_;
  }

  bool operator==(const StlAllocator<Alloc, void>& other) const {
    return alloc_ == other.alloc_;
  }

 private:
  Alloc* alloc_;
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

/**
 * Helper function to obtain rebound allocators
 *
 * @author: Marcelo Juchem <marcelo@fb.com>
 */
template <typename T, typename Allocator>
typename Allocator::template rebind<T>::other rebind_allocator(
  Allocator const& allocator
) {
  return typename Allocator::template rebind<T>::other(allocator);
}

/*
 * Helper classes/functions for creating a unique_ptr using a custom
 * allocator.
 *
 * @author: Marcelo Juchem <marcelo@fb.com>
 */

// Derives from the allocator to take advantage of the empty base
// optimization when possible.
template <typename Allocator>
class allocator_delete
  : private std::remove_reference<Allocator>::type
{
  typedef typename std::remove_reference<Allocator>::type allocator_type;

public:
  typedef typename Allocator::pointer pointer;

  allocator_delete() = default;

  explicit allocator_delete(const allocator_type& allocator)
    : allocator_type(allocator)
  {}

  explicit allocator_delete(allocator_type&& allocator)
    : allocator_type(std::move(allocator))
  {}

  template <typename U>
  allocator_delete(const allocator_delete<U>& other)
    : allocator_type(other.get_allocator())
  {}

  allocator_type& get_allocator() const {
    return *const_cast<allocator_delete*>(this);
  }

  void operator()(pointer p) const {
    if (!p) return;
    const_cast<allocator_delete*>(this)->destroy(p);
    const_cast<allocator_delete*>(this)->deallocate(p, 1);
  }
};

template <typename T, typename Allocator>
class is_simple_allocator {
  FOLLY_CREATE_HAS_MEMBER_FN_TRAITS(has_destroy, destroy);

  typedef typename std::remove_const<
    typename std::remove_reference<Allocator>::type
  >::type allocator;
  typedef typename std::remove_reference<T>::type value_type;
  typedef value_type* pointer;

public:
  constexpr static bool value = !has_destroy<allocator, void(pointer)>::value
    && !has_destroy<allocator, void(void*)>::value;
};

template <typename T, typename Allocator>
struct as_stl_allocator {
  typedef typename std::conditional<
    is_simple_allocator<T, Allocator>::value,
    folly::StlAllocator<
      typename std::remove_reference<Allocator>::type,
      typename std::remove_reference<T>::type
    >,
    typename std::remove_reference<Allocator>::type
  >::type type;
};

template <typename T, typename Allocator>
typename std::enable_if<
  is_simple_allocator<T, Allocator>::value,
  folly::StlAllocator<
    typename std::remove_reference<Allocator>::type,
    typename std::remove_reference<T>::type
  >
>::type make_stl_allocator(Allocator&& allocator) {
  return folly::StlAllocator<
    typename std::remove_reference<Allocator>::type,
    typename std::remove_reference<T>::type
  >(&allocator);
}

template <typename T, typename Allocator>
typename std::enable_if<
  !is_simple_allocator<T, Allocator>::value,
  typename std::remove_reference<Allocator>::type
>::type make_stl_allocator(Allocator&& allocator) {
  return std::move(allocator);
}

/**
 * AllocatorUniquePtr: a unique_ptr that supports both STL-style
 * allocators and SimpleAllocator
 *
 * @author: Marcelo Juchem <marcelo@fb.com>
 */

template <typename T, typename Allocator>
struct AllocatorUniquePtr {
  typedef std::unique_ptr<T,
    folly::allocator_delete<
      typename std::conditional<
        is_simple_allocator<T, Allocator>::value,
        folly::StlAllocator<typename std::remove_reference<Allocator>::type, T>,
        typename std::remove_reference<Allocator>::type
      >::type
    >
  > type;
};

/**
 * Functions to allocate a unique_ptr / shared_ptr, supporting both
 * STL-style allocators and SimpleAllocator, analog to std::allocate_shared
 *
 * @author: Marcelo Juchem <marcelo@fb.com>
 */

template <typename T, typename Allocator, typename ...Args>
typename AllocatorUniquePtr<T, Allocator>::type allocate_unique(
  Allocator&& allocator, Args&&... args
) {
  auto stlAllocator = folly::make_stl_allocator<T>(
    std::forward<Allocator>(allocator)
  );
  auto p = stlAllocator.allocate(1);

  try {
    stlAllocator.construct(p, std::forward<Args>(args)...);

    return {p,
      folly::allocator_delete<decltype(stlAllocator)>(std::move(stlAllocator))
    };
  } catch (...) {
    stlAllocator.deallocate(p, 1);
    throw;
  }
}

template <typename T, typename Allocator, typename ...Args>
std::shared_ptr<T> allocate_shared(Allocator&& allocator, Args&&... args) {
  return std::allocate_shared<T>(
    folly::make_stl_allocator<T>(std::forward<Allocator>(allocator)),
    std::forward<Args>(args)...
  );
}

}  // namespace folly

#endif /* FOLLY_MEMORY_H_ */
