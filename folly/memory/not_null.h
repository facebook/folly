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

#pragma once

/**
 * C++ Core Guideline's not_null PtrT>.
 *
 * not_null<PtrT> holds a pointer-like type PtrT which is not nullptr.
 *
 * not_null<T*> is a drop-in replacement for T* (as long as it's never null).
 * Specializations not_null_unique_ptr<T> and not_null_shared_ptr<T> are
 * drop-in replacements for unique_ptr<T> and shared_ptr<T>, respecitively.
 *
 * Example:
 *    void foo(not_null<int*> nnpi) {
 *      *nnpi = 7; // Safe, since `nnpi` is not null.
 *    }
 *
 *    void bar(not_null_shared_ptr<int> nnspi) {
 *      foo(nnsp.get());
 *    }
 *
 * Notes:
 *    - Constructing a not_null<PtrT> from a nullptr-equivalent argument throws
 *      a std::invalid_argument exception.
 *    - Cannot be used after move.
 *    - In debug mode, not_null checks that it is not null on all accesses,
 *      since use-after-move can cause the underlying PtrT to be null.
 */

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <memory>
#include <type_traits>

namespace folly {

namespace detail {
template <typename T>
struct is_not_null;
template <typename FromT, typename ToT>
struct is_not_null_convertible;
template <typename FromT, typename ToPtrT>
struct is_not_null_nothrow_constructible;
template <typename FromPtrT, typename ToT>
struct is_not_null_castable;
template <typename FromPtrT, typename ToT>
struct is_not_null_move_castable;
} // namespace detail

class guaranteed_not_null_provider {
 protected:
  struct guaranteed_not_null {};
};

/**
 * not_null_base, the common interface for all not_null subclasses.
 *  - Implicitly constructs and casts just like a PtrT.
 *  - Has unwrap() function to access the underlying PtrT.
 */
template <typename PtrT>
class not_null_base : protected guaranteed_not_null_provider {
  template <bool>
  struct implicit_tag {};

 public:
  using pointer = PtrT;
  using element_type = typename std::pointer_traits<PtrT>::element_type;

  /**
   * Construction:
   *  - Throws std::invalid_argument if null.
   *  - Cannot default construct.
   *  - Cannot construct from nullptr.
   *  - Allows implicit construction iff PtrT allows implicit construction.
   *  - Construction from another not_null skips null check (in opt builds).
   */
  not_null_base() = delete;
  /* implicit */ not_null_base(std::nullptr_t) = delete;

  not_null_base(const not_null_base& nn) = default;
  not_null_base(not_null_base&& nn) = default;

  template <
      typename U,
      typename =
          std::enable_if_t<detail::is_not_null_convertible<U&&, PtrT>::value>>
  /* implicit */ not_null_base(U&& u, implicit_tag<true> = {}) noexcept(
      detail::is_not_null_nothrow_constructible<U&&, PtrT>::value);

  template <
      typename U,
      typename =
          std::enable_if_t<!detail::is_not_null_convertible<U&&, PtrT>::value>>
  explicit not_null_base(U&& u, implicit_tag<false> = {}) noexcept(
      detail::is_not_null_nothrow_constructible<U&&, PtrT>::value);

  // Allow construction without a null check for trusted callsites.
  explicit not_null_base(
      PtrT&& ptr, guaranteed_not_null_provider::guaranteed_not_null) noexcept;

  /**
   * Assignment:
   *  - Due to implicit construction, just need to assign from self.
   *  - Cannot assign from nullptr.
   */
  not_null_base& operator=(std::nullptr_t) = delete;
  not_null_base& operator=(const not_null_base& nn) = default;
  not_null_base& operator=(not_null_base&& nn) = default;

  /**
   * Dereferencing:
   *  - Does not return mutable references, since that would allow the
   *    underlying pointer to be assigned to nullptr.
   */
  element_type& operator*() const noexcept;
  const PtrT& operator->() const noexcept;

  /**
   * Casting:
   *  - Implicit casting to PtrT allowed, so that not_null<PtrT> can be used
   *    wherever a PtrT is expected.
   *  - Does not return mutable references, since that would allow the
   *    underlying pointer to be assigned to nullptr.
   *  - Boolean cast is always true.
   */
  operator const PtrT&() const& noexcept;
  operator PtrT&&() && noexcept;

  template <
      typename U,
      typename = std::enable_if_t<detail::is_not_null_castable<PtrT, U>::value>>
  operator U() const& noexcept(std::is_nothrow_constructible_v<U, const PtrT&>);

  template <
      typename U,
      typename =
          std::enable_if_t<detail::is_not_null_move_castable<PtrT, U>::value>>
  operator U() && noexcept(std::is_nothrow_constructible_v<U, PtrT&&>);

  explicit inline operator bool() const noexcept { return true; }

  /**
   * Swap
   */
  void swap(not_null_base& other) noexcept;

  /**
   * Accessor:
   *  - Can explicitly access the underlying type via `unwrap`.
   *  - Does not return mutable references, since that would allow the
   *    underlying pointer to be assigned to nullptr.
   */
  const PtrT& unwrap() const& noexcept;
  PtrT&& unwrap() && noexcept;

 protected:
  void throw_if_null() const;
  template <typename T>
  static void throw_if_null(const T& ptr);
  template <typename T>
  static void terminate_if_null(const T& ptr);
  template <typename Deleter>
  static Deleter&& forward_or_throw_if_null(Deleter&& deleter);

  // Non-const accessor.
  PtrT& mutable_unwrap() noexcept;

 private:
  struct private_tag {};
  template <typename U>
  not_null_base(U&& u, private_tag);

  PtrT ptr_;
};

/**
 * not_null specializable class.
 *
 * Default implementation is not_null_base.
 */
template <typename PtrT>
class not_null : public not_null_base<PtrT> {
 public:
  using pointer = typename not_null_base<PtrT>::pointer;
  using element_type = typename not_null_base<PtrT>::element_type;
  using not_null_base<PtrT>::not_null_base;
};

/**
 * not_null<std::unique_ptr<>> specialization.
 *
 * alias: not_null_unique_ptr
 *
 * Provides API compatibility with unique_ptr, except:
 *  - Pointer arguments must be non-null.
 *  - Cannot reset().
 *  - Functions are not noexcept, since debug-mode checks can throw exceptions.
 *  - Promotes returned pointers to be not_null pointers. Implicit casting
 *    allows these to be used in place of regular pointers.
 *
 * Notes:
 *  - Has make_not_null_unique, equivalent to std::make_unique
 */
template <typename T, typename Deleter>
class not_null<std::unique_ptr<T, Deleter>>
    : public not_null_base<std::unique_ptr<T, Deleter>> {
 public:
  using pointer = not_null<typename std::unique_ptr<T, Deleter>::pointer>;
  using element_type = typename std::unique_ptr<T, Deleter>::element_type;
  using deleter_type = typename std::unique_ptr<T, Deleter>::deleter_type;

  /**
   * Constructors. Most are inherited from not_null_base.
   */
  using not_null_base<std::unique_ptr<T, Deleter>>::not_null_base;

  not_null(pointer p, const Deleter& d);
  not_null(pointer p, Deleter&& d);

  /**
   * not_null_unique_ptr cannot be released - that would cause it to be null.
   */
  pointer release() = delete;

  /**
   * not_null_unique_ptr can only be reset to a non-null pointer.
   */
  void reset(std::nullptr_t) = delete;
  void reset(pointer ptr) noexcept;

  /**
   * get() returns a not_null (pointer type is not_null<T*>).
   *
   * Due to implicit casting, can still capture the result of get() as a regular
   * pointer type:
   *
   *   int* ptr = not_null_unique_ptr<int>(...).get(); // valid
   */
  pointer get() const noexcept;

  /**
   * get_deleter(): same as for unique_ptr.
   */
  Deleter& get_deleter() noexcept;
  const Deleter& get_deleter() const noexcept;
};

template <typename T, typename Deleter = std::default_delete<T>>
using not_null_unique_ptr = not_null<std::unique_ptr<T, Deleter>>;

template <typename T, typename... Args>
not_null_unique_ptr<T> make_not_null_unique(Args&&... args);

/**
 * not_null<std::shared_ptr<>> specialization.
 *
 * alias: not_null_shared_ptr
 *
 * Provides API compatibility with shared_ptr, except:
 *  - Pointer arguments must be non-null.
 *  - Cannot reset().
 *  - Functions are not noexcept, since debug-mode checks can throw exceptions.
 *  - Promotes returned pointers to be not_null pointers. Implicit casting
 *    allows these to be used in place of regular pointers.
 *
 * Notes:
 *  - Has make_not_null_shared, equivalent to std::make_shared.
 */
template <typename T>
class not_null<std::shared_ptr<T>> : public not_null_base<std::shared_ptr<T>> {
 public:
  using element_type = typename std::shared_ptr<T>::element_type;
  using pointer = not_null<element_type*>;
  using weak_type = typename std::shared_ptr<T>::weak_type;

  /**
   * Constructors. Most are inherited from not_null_base.
   */
  using not_null_base<std::shared_ptr<T>>::not_null_base;

  template <typename U, typename Deleter>
  not_null(U* ptr, Deleter d);
  template <typename U, typename Deleter>
  not_null(not_null<U*> ptr, Deleter d);

  /**
   * Aliasing constructors.
   *
   * Note:
   *  - The aliased shared_ptr argument, @r, is allowed to be null. The
   *    constructed object is not null iff @ptr is.
   */
  template <typename U>
  not_null(const std::shared_ptr<U>& r, not_null<element_type*> ptr) noexcept;
  template <typename U>
  not_null(
      const not_null<std::shared_ptr<U>>& r,
      not_null<element_type*> ptr) noexcept;
  template <typename U>
  not_null(std::shared_ptr<U>&& r, not_null<element_type*> ptr) noexcept;
  template <typename U>
  not_null(
      not_null<std::shared_ptr<U>>&& r, not_null<element_type*> ptr) noexcept;

  /**
   * not_null_shared_ptr can only be reset to a non-null pointer.
   */
  void reset() = delete;
  template <typename U>
  void reset(U* ptr);
  template <typename U>
  void reset(not_null<U*> ptr) noexcept;
  template <typename U, typename Deleter>
  void reset(U* ptr, Deleter d);
  template <typename U, typename Deleter>
  void reset(not_null<U*> ptr, Deleter d);

  /**
   * get() returns a not_null.
   *
   * Due to implicit casting, can still capture the result of get() as a regular
   * pointer type:
   *
   *   int* ptr = not_null_shared_ptr<int>(...).get(); // valid
   */
  pointer get() const noexcept;

  /**
   * use_count()
   * owner_before()
   *
   * Same as shared_ptr.
   *
   * Notes:
   *  - unique() is deprecated in c++17, so is not implemented here. Can call
   *    not_null_shared_ptr.unwrap().unique() as a workaround, until unique()
   *    is removed in C++20.
   */
  long use_count() const noexcept;
  template <typename U>
  bool owner_before(const std::shared_ptr<U>& other) const noexcept;
  template <typename U>
  bool owner_before(const not_null<std::shared_ptr<U>>& other) const noexcept;
};

template <typename T>
using not_null_shared_ptr = not_null<std::shared_ptr<T>>;

template <typename T, typename... Args>
not_null_shared_ptr<T> make_not_null_shared(Args&&... args);

template <typename T, typename Alloc, typename... Args>
not_null_shared_ptr<T> allocate_not_null_shared(
    const Alloc& alloc, Args&&... args);

/**
 * Comparison:
 *  - Forwards to underlying PtrT.
 *  - Works when one of the operands is not not_null.
 *  - Works when one of the operands is nullptr.
 */
#define FB_NOT_NULL_MK_OP(op)                                      \
  template <typename PtrT, typename T>                             \
  bool operator op(const not_null<PtrT>& lhs, const T& rhs);       \
  template <                                                       \
      typename PtrT,                                               \
      typename T,                                                  \
      typename = std::enable_if_t<!detail::is_not_null<T>::value>> \
  bool operator op(const T& lhs, const not_null<PtrT>& rhs);
FB_NOT_NULL_MK_OP(==)
FB_NOT_NULL_MK_OP(!=)
FB_NOT_NULL_MK_OP(<)
FB_NOT_NULL_MK_OP(<=)
FB_NOT_NULL_MK_OP(>)
FB_NOT_NULL_MK_OP(>=)
#undef FB_NOT_NULL_MK_OP

/**
 * Output:
 *  - Forwards to underlying PtrT.
 */
template <typename U, typename V, typename PtrT>
std::basic_ostream<U, V>& operator<<(
    std::basic_ostream<U, V>& os, const not_null<PtrT>& ptr);

/**
 * Swap
 */
template <typename PtrT>
void swap(not_null<PtrT>& lhs, not_null<PtrT>& rhs) noexcept;

/**
 * Getters
 */
template <typename Deleter, typename T>
Deleter* get_deleter(const not_null_shared_ptr<T>& ptr);

/**
 * Casting
 */
template <typename T, typename U>
not_null_shared_ptr<T> static_pointer_cast(const not_null_shared_ptr<U>& r);
template <typename T, typename U>
not_null_shared_ptr<T> static_pointer_cast(not_null_shared_ptr<U>&& r);
template <typename T, typename U>
std::shared_ptr<T> dynamic_pointer_cast(const not_null_shared_ptr<U>& r);
template <typename T, typename U>
std::shared_ptr<T> dynamic_pointer_cast(not_null_shared_ptr<U>&& r);
template <typename T, typename U>
not_null_shared_ptr<T> const_pointer_cast(const not_null_shared_ptr<U>& r);
template <typename T, typename U>
not_null_shared_ptr<T> const_pointer_cast(not_null_shared_ptr<U>&& r);
template <typename T, typename U>
not_null_shared_ptr<T> reinterpret_pointer_cast(
    const not_null_shared_ptr<U>& r);
template <typename T, typename U>
not_null_shared_ptr<T> reinterpret_pointer_cast(not_null_shared_ptr<U>&& r);

template <typename PtrT>
not_null(PtrT&&) -> not_null<std::remove_cv_t<std::remove_reference_t<PtrT>>>;

} // namespace folly

namespace std {
/**
 * Hashing:
 *  - Forwards to underlying PtrT.
 */
template <typename PtrT>
struct hash<::folly::not_null<PtrT>> : hash<PtrT> {};
} // namespace std

#include <folly/memory/not_null-inl.h>
