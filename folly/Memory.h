/*
 * Copyright 2013-present Facebook, Inc.
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

#pragma once

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <folly/ConstexprMath.h>
#include <folly/Likely.h>
#include <folly/Traits.h>
#include <folly/functional/Invoke.h>
#include <folly/lang/Align.h>
#include <folly/lang/Exception.h>
#include <folly/portability/Config.h>
#include <folly/portability/Malloc.h>

namespace folly {

#if _POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600 || \
    (defined(__ANDROID__) && (__ANDROID_API__ > 15)) ||   \
    (defined(__APPLE__) &&                                \
     (__MAC_OS_X_VERSION_MIN_REQUIRED >= __MAC_10_6 ||    \
      __IPHONE_OS_VERSION_MIN_REQUIRED >= __IPHONE_3_0))

inline void* aligned_malloc(size_t size, size_t align) {
  // use posix_memalign, but mimic the behaviour of memalign
  void* ptr = nullptr;
  int rc = posix_memalign(&ptr, align, size);
  return rc == 0 ? (errno = 0, ptr) : (errno = rc, nullptr);
}

inline void aligned_free(void* aligned_ptr) {
  free(aligned_ptr);
}

#elif defined(_WIN32)

inline void* aligned_malloc(size_t size, size_t align) {
  return _aligned_malloc(size, align);
}

inline void aligned_free(void* aligned_ptr) {
  _aligned_free(aligned_ptr);
}

#else

inline void* aligned_malloc(size_t size, size_t align) {
  return memalign(align, size);
}

inline void aligned_free(void* aligned_ptr) {
  free(aligned_ptr);
}

#endif

/**
 * For exception safety and consistency with make_shared. Erase me when
 * we have std::make_unique().
 *
 * @author Louis Brandy (ldbrandy@fb.com)
 * @author Xu Ning (xning@fb.com)
 */

#if __cplusplus >= 201402L || __cpp_lib_make_unique >= 201304L || \
    (__ANDROID__ && __cplusplus >= 201300L) || _MSC_VER >= 1900

/* using override */ using std::make_unique;

#else

template <typename T, typename... Args>
typename std::enable_if<!std::is_array<T>::value, std::unique_ptr<T>>::type
make_unique(Args&&... args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

// Allows 'make_unique<T[]>(10)'. (N3690 s20.9.1.4 p3-4)
template <typename T>
typename std::enable_if<std::is_array<T>::value, std::unique_ptr<T>>::type
make_unique(const size_t n) {
  return std::unique_ptr<T>(new typename std::remove_extent<T>::type[n]());
}

// Disallows 'make_unique<T[10]>()'. (N3690 s20.9.1.4 p5)
template <typename T, typename... Args>
typename std::enable_if<
  std::extent<T>::value != 0, std::unique_ptr<T>>::type
make_unique(Args&&...) = delete;

#endif

/**
 * static_function_deleter
 *
 * So you can write this:
 *
 *      using RSA_deleter = folly::static_function_deleter<RSA, &RSA_free>;
 *      auto rsa = std::unique_ptr<RSA, RSA_deleter>(RSA_new());
 *      RSA_generate_key_ex(rsa.get(), bits, exponent, nullptr);
 *      rsa = nullptr;  // calls RSA_free(rsa.get())
 *
 * This would be sweet as well for BIO, but unfortunately BIO_free has signature
 * int(BIO*) while we require signature void(BIO*). So you would need to make a
 * wrapper for it:
 *
 *      inline void BIO_free_fb(BIO* bio) { CHECK_EQ(1, BIO_free(bio)); }
 *      using BIO_deleter = folly::static_function_deleter<BIO, &BIO_free_fb>;
 *      auto buf = std::unique_ptr<BIO, BIO_deleter>(BIO_new(BIO_s_mem()));
 *      buf = nullptr;  // calls BIO_free(buf.get())
 */

template <typename T, void(*f)(T*)>
struct static_function_deleter {
  void operator()(T* t) const {
    f(t);
  }
};

/**
 *  to_shared_ptr
 *
 *  Convert unique_ptr to shared_ptr without specifying the template type
 *  parameter and letting the compiler deduce it.
 *
 *  So you can write this:
 *
 *      auto sptr = to_shared_ptr(getSomethingUnique<T>());
 *
 *  Instead of this:
 *
 *      auto sptr = shared_ptr<T>(getSomethingUnique<T>());
 *
 *  Useful when `T` is long, such as:
 *
 *      using T = foobar::FooBarAsyncClient;
 */
template <typename T, typename D>
std::shared_ptr<T> to_shared_ptr(std::unique_ptr<T, D>&& ptr) {
  return std::shared_ptr<T>(std::move(ptr));
}

/**
 *  to_weak_ptr
 *
 *  Make a weak_ptr and return it from a shared_ptr without specifying the
 *  template type parameter and letting the compiler deduce it.
 *
 *  So you can write this:
 *
 *      auto wptr = to_weak_ptr(getSomethingShared<T>());
 *
 *  Instead of this:
 *
 *      auto wptr = weak_ptr<T>(getSomethingShared<T>());
 *
 *  Useful when `T` is long, such as:
 *
 *      using T = foobar::FooBarAsyncClient;
 */
template <typename T>
std::weak_ptr<T> to_weak_ptr(const std::shared_ptr<T>& ptr) {
  return std::weak_ptr<T>(ptr);
}

namespace detail {
template <typename T>
struct lift_void_to_char {
  using type = T;
};
template <>
struct lift_void_to_char<void> {
  using type = char;
};
}

/**
 * SysAllocator
 *
 * Resembles std::allocator, the default Allocator, but wraps std::malloc and
 * std::free.
 */
template <typename T>
class SysAllocator {
 private:
  using Self = SysAllocator<T>;

 public:
  using value_type = T;

  T* allocate(size_t count) {
    using lifted = typename detail::lift_void_to_char<T>::type;
    auto const p = std::malloc(sizeof(lifted) * count);
    if (!p) {
      throw_exception<std::bad_alloc>();
    }
    return static_cast<T*>(p);
  }
  void deallocate(T* p, size_t /* count */) {
    std::free(p);
  }

  friend bool operator==(Self const&, Self const&) noexcept {
    return true;
  }
  friend bool operator!=(Self const&, Self const&) noexcept {
    return false;
  }
};

class DefaultAlign {
 private:
  using Self = DefaultAlign;
  std::size_t align_;

 public:
  explicit DefaultAlign(std::size_t align) noexcept : align_(align) {
    assert(!(align_ < sizeof(void*)) && bool("bad align: too small"));
    assert(!(align_ & (align_ - 1)) && bool("bad align: not power-of-two"));
  }
  std::size_t operator()() const noexcept {
    return align_;
  }

  friend bool operator==(Self const& a, Self const& b) noexcept {
    return a.align_ == b.align_;
  }
  friend bool operator!=(Self const& a, Self const& b) noexcept {
    return a.align_ != b.align_;
  }
};

template <std::size_t Align>
class FixedAlign {
 private:
  static_assert(!(Align < sizeof(void*)), "bad align: too small");
  static_assert(!(Align & (Align - 1)), "bad align: not power-of-two");
  using Self = FixedAlign<Align>;

 public:
  constexpr std::size_t operator()() const noexcept {
    return Align;
  }

  friend bool operator==(Self const&, Self const&) noexcept {
    return true;
  }
  friend bool operator!=(Self const&, Self const&) noexcept {
    return false;
  }
};

/**
 * AlignedSysAllocator
 *
 * Resembles std::allocator, the default Allocator, but wraps aligned_malloc and
 * aligned_free.
 *
 * Accepts a policy parameter for providing the alignment, which must:
 *   * be invocable as std::size_t() noexcept, returning the alignment
 *   * be noexcept-copy-constructible
 *   * have noexcept operator==
 *   * have noexcept operator!=
 *   * not be final
 *
 * DefaultAlign and FixedAlign<std::size_t>, provided above, are valid policies.
 */
template <typename T, typename Align = DefaultAlign>
class AlignedSysAllocator : private Align {
 private:
  using Self = AlignedSysAllocator<T, Align>;

  constexpr Align const& align() const {
    return *this;
  }

 public:
  static_assert(std::is_nothrow_copy_constructible<Align>::value, "");
  static_assert(is_nothrow_invocable_r<std::size_t, Align>::value, "");

  using value_type = T;

  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;

  using Align::Align;

  // TODO: remove this ctor, which is required only by gcc49
  template <
      typename S = Align,
      _t<std::enable_if<std::is_default_constructible<S>::value, int>> = 0>
  constexpr AlignedSysAllocator() noexcept(noexcept(Align())) : Align() {}

  template <typename U>
  constexpr explicit AlignedSysAllocator(
      AlignedSysAllocator<U, Align> const& other) noexcept
      : Align(other.align()) {}

  T* allocate(size_t count) {
    using lifted = typename detail::lift_void_to_char<T>::type;
    auto const p = aligned_malloc(sizeof(lifted) * count, align()());
    if (!p) {
      if (FOLLY_UNLIKELY(errno != ENOMEM)) {
        std::terminate();
      }
      throw_exception<std::bad_alloc>();
    }
    return static_cast<T*>(p);
  }
  void deallocate(T* p, size_t /* count */) {
    aligned_free(p);
  }

  friend bool operator==(Self const& a, Self const& b) noexcept {
    return a.align() == b.align();
  }
  friend bool operator!=(Self const& a, Self const& b) noexcept {
    return a.align() != b.align();
  }
};

/**
 * CxxAllocatorAdaptor
 *
 * A type conforming to C++ concept Allocator, delegating operations to an
 * unowned Inner which has this required interface:
 *
 *   void* allocate(std::size_t)
 *   void deallocate(void*, std::size_t)
 *
 * Note that Inner is *not* a C++ Allocator.
 */
template <typename T, class Inner>
class CxxAllocatorAdaptor {
 private:
  using Self = CxxAllocatorAdaptor<T, Inner>;

  template <typename U, typename UAlloc>
  friend class CxxAllocatorAdaptor;

  std::reference_wrapper<Inner> ref_;

 public:
  using value_type = T;

  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;

  explicit CxxAllocatorAdaptor(Inner& ref) : ref_(ref) {}

  template <typename U>
  explicit CxxAllocatorAdaptor(CxxAllocatorAdaptor<U, Inner> const& other)
      : ref_(other.ref_) {}

  T* allocate(std::size_t n) {
    using lifted = typename detail::lift_void_to_char<T>::type;
    return static_cast<T*>(ref_.get().allocate(sizeof(lifted) * n));
  }
  void deallocate(T* p, std::size_t n) {
    using lifted = typename detail::lift_void_to_char<T>::type;
    ref_.get().deallocate(p, sizeof(lifted) * n);
  }

  friend bool operator==(Self const& a, Self const& b) noexcept {
    return std::addressof(a.ref_.get()) == std::addressof(b.ref_.get());
  }
  friend bool operator!=(Self const& a, Self const& b) noexcept {
    return std::addressof(a.ref_.get()) != std::addressof(b.ref_.get());
  }
};

/*
 * allocator_delete
 *
 * A deleter which automatically works with a given allocator.
 *
 * Derives from the allocator to take advantage of the empty base
 * optimization when possible.
 */
template <typename Alloc>
class allocator_delete : private std::remove_reference<Alloc>::type {
 private:
  using allocator_type = typename std::remove_reference<Alloc>::type;
  using allocator_traits = std::allocator_traits<allocator_type>;
  using value_type = typename allocator_traits::value_type;
  using pointer = typename allocator_traits::pointer;

 public:
  allocator_delete() = default;
  allocator_delete(allocator_delete const&) = default;
  allocator_delete(allocator_delete&&) = default;
  allocator_delete& operator=(allocator_delete const&) = default;
  allocator_delete& operator=(allocator_delete&&) = default;

  explicit allocator_delete(const allocator_type& allocator)
      : allocator_type(allocator) {}

  explicit allocator_delete(allocator_type&& allocator)
      : allocator_type(std::move(allocator)) {}

  template <typename U>
  allocator_delete(const allocator_delete<U>& other)
      : allocator_type(other.get_allocator()) {}

  allocator_type const& get_allocator() const {
    return *this;
  }

  void operator()(pointer p) const {
    auto alloc = get_allocator();
    allocator_traits::destroy(alloc, p);
    allocator_traits::deallocate(alloc, p, 1);
  }
};

/**
 * allocate_unique, like std::allocate_shared but for std::unique_ptr
 */
template <typename T, typename Alloc, typename... Args>
std::unique_ptr<T, allocator_delete<Alloc>> allocate_unique(
    Alloc const& alloc,
    Args&&... args) {
  using traits = std::allocator_traits<Alloc>;
  auto copy = alloc;
  auto const p = traits::allocate(copy, 1);
  try {
    traits::construct(copy, p, static_cast<Args&&>(args)...);
    return {p, allocator_delete<Alloc>(std::move(copy))};
  } catch (...) {
    traits::deallocate(copy, p, 1);
    throw;
  }
}

struct SysBufferDeleter {
  void operator()(void* ptr) {
    std::free(ptr);
  }
};
using SysBufferUniquePtr = std::unique_ptr<void, SysBufferDeleter>;

inline SysBufferUniquePtr allocate_sys_buffer(std::size_t size) {
  auto p = std::malloc(size);
  if (!p) {
    throw_exception<std::bad_alloc>();
  }
  return {p, {}};
}

/**
 * AllocatorHasTrivialDeallocate
 *
 * Unambiguously inherits std::integral_constant<bool, V> for some bool V.
 *
 * Describes whether a C++ Aallocator has trivial, i.e. no-op, deallocate().
 *
 * Also may be used to describe types which may be used with
 * CxxAllocatorAdaptor.
 */
template <typename Alloc>
struct AllocatorHasTrivialDeallocate : std::false_type {};

template <typename T, class Alloc>
struct AllocatorHasTrivialDeallocate<CxxAllocatorAdaptor<T, Alloc>>
    : AllocatorHasTrivialDeallocate<Alloc> {};

/*
 * folly::enable_shared_from_this
 *
 * To be removed once C++17 becomes a minimum requirement for folly.
 */
#if __cplusplus >= 201700L || \
    __cpp_lib_enable_shared_from_this >= 201603L

// Guaranteed to have std::enable_shared_from_this::weak_from_this(). Prefer
// type alias over our own class.
/* using override */ using std::enable_shared_from_this;

#else

/**
 * Extends std::enabled_shared_from_this. Offers weak_from_this() to pre-C++17
 * code. Use as drop-in replacement for std::enable_shared_from_this.
 *
 * C++14 has no direct means of creating a std::weak_ptr, one must always
 * create a (temporary) std::shared_ptr first. C++17 adds weak_from_this() to
 * std::enable_shared_from_this to avoid that overhead. Alas code that must
 * compile under different language versions cannot call
 * std::enable_shared_from_this::weak_from_this() directly. Hence this class.
 *
 * @example
 *   class MyClass : public folly::enable_shared_from_this<MyClass> {};
 *
 *   int main() {
 *     std::shared_ptr<MyClass> sp = std::make_shared<MyClass>();
 *     std::weak_ptr<MyClass> wp = sp->weak_from_this();
 *   }
 */
template <typename T>
class enable_shared_from_this : public std::enable_shared_from_this<T> {
 public:
  constexpr enable_shared_from_this() noexcept = default;

  std::weak_ptr<T> weak_from_this() noexcept {
    return weak_from_this_<T>(this);
  }

  std::weak_ptr<T const> weak_from_this() const noexcept {
    return weak_from_this_<T>(this);
  }

 private:
  // Uses SFINAE to detect and call
  // std::enable_shared_from_this<T>::weak_from_this() if available. Falls
  // back to std::enable_shared_from_this<T>::shared_from_this() otherwise.
  template <typename U>
  auto weak_from_this_(std::enable_shared_from_this<U>* base_ptr)
  noexcept -> decltype(base_ptr->weak_from_this()) {
    return base_ptr->weak_from_this();
  }

  template <typename U>
  auto weak_from_this_(std::enable_shared_from_this<U> const* base_ptr)
  const noexcept -> decltype(base_ptr->weak_from_this()) {
    return base_ptr->weak_from_this();
  }

  template <typename U>
  std::weak_ptr<U> weak_from_this_(...) noexcept {
    try {
      return this->shared_from_this();
    } catch (std::bad_weak_ptr const&) {
      // C++17 requires that weak_from_this() on an object not owned by a
      // shared_ptr returns an empty weak_ptr. Sadly, in C++14,
      // shared_from_this() on such an object is undefined behavior, and there
      // is nothing we can do to detect and handle the situation in a portable
      // manner. But in case a compiler is nice enough to implement C++17
      // semantics of shared_from_this() and throws a bad_weak_ptr, we catch it
      // and return an empty weak_ptr.
      return std::weak_ptr<U>{};
    }
  }

  template <typename U>
  std::weak_ptr<U const> weak_from_this_(...) const noexcept {
    try {
      return this->shared_from_this();
    } catch (std::bad_weak_ptr const&) {
      return std::weak_ptr<U const>{};
    }
  }
};

#endif

} // namespace folly
