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

#include <cassert>
#include <compare>
#include <concepts>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include <folly/CppAttributes.h>
#include <folly/Traits.h>

namespace folly {

namespace detail {

/// indirect_rollback
///
/// A scope-guard for the allocate-then-construct sequences in indirect, which
/// undoes the allocation unless dismissed. folly::makeGuard would be the
/// natural choice but is not usable during constant evaluation, which
/// indirect supports.
template <typename Fn>
class indirect_rollback {
 public:
  constexpr explicit indirect_rollback(Fn fn) noexcept
      : fn_{static_cast<Fn&&>(fn)} {}
  indirect_rollback(indirect_rollback const&) = delete;
  indirect_rollback& operator=(indirect_rollback const&) = delete;
  constexpr ~indirect_rollback() {
    if (live_) {
      fn_();
    }
  }

  constexpr void dismiss() noexcept { live_ = false; }

 private:
  Fn fn_;
  bool live_{true};
};

/// indirect_synth_three_way
///
/// The exposition-only synth-three-way of [expos.only.entity]: a three-way
/// comparison when the operands have one, otherwise synthesized from
/// operator<. indirect's operator<=> declares its return type in terms of
/// this, as [indirect.relops] specifies, which also makes the operator drop
/// out of overload resolution for value types that cannot be ordered.
struct indirect_synth_three_way_fn {
  template <typename T, typename U>
  constexpr auto operator()(const T& t, const U& u) const
    requires requires {
      { t < u } -> std::convertible_to<bool>;
      { u < t } -> std::convertible_to<bool>;
    }
  {
    if constexpr (std::three_way_comparable_with<T, U>) {
      return t <=> u;
    } else if (t < u) {
      return std::weak_ordering::less;
    } else if (u < t) {
      return std::weak_ordering::greater;
    } else {
      return std::weak_ordering::equivalent;
    }
  }
};

inline constexpr indirect_synth_three_way_fn indirect_synth_three_way{};

template <typename T, typename U = T>
using indirect_synth_three_way_result = decltype(indirect_synth_three_way(
    std::declval<const T&>(), std::declval<const U&>()));

} // namespace detail

/// indirect
///
/// An owning, heap-allocated, deep-copy value wrapper for T, usable without a
/// C++26 toolchain. The member set matches [indirect.syn] exactly, so code
/// written against folly::indirect ports to std::indirect unchanged.
///
/// indirect always owns a value (never empty) except for a valueless state
/// produced by moving from an indirect or by constructing or assigning from a
/// valueless source. A valueless indirect supports destruction, assignment,
/// valueless_after_move(), equality/ordering comparisons and hashing;
/// operator* and operator-> assert in debug and are UB in release when
/// valueless.
///
/// Allocator-aware: Alloc must have value_type == T and is used directly
/// without rebinding. Copy/move assignment and swap respect
/// allocator_traits<Alloc>::propagate_on_container_*.
///
/// mimic: std::indirect, C++26, P1950R2, P3019R4
template <typename T, typename Alloc = std::allocator<T>>
class indirect {
 public:
  using value_type = T;
  using allocator_type = Alloc;
  using pointer = typename std::allocator_traits<Alloc>::pointer;
  using const_pointer = typename std::allocator_traits<Alloc>::const_pointer;

 private:
  using traits = std::allocator_traits<Alloc>;

  // [indirect.general]/5. These traits all accept an incomplete T, so they do
  // not defeat the recursive-type use case that motivates indirect.
  static_assert(
      std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> &&
          !std::is_volatile_v<T> && !std::is_same_v<T, std::in_place_t> &&
          !is_instantiation_of_v<std::in_place_type_t, T>,
      "folly::indirect<T>: T must be a non-array, non-cv-qualified object type, "
      "and must not be in_place_t or a specialization of in_place_type_t");

  static_assert(
      std::is_same_v<typename traits::value_type, T>,
      "folly::indirect requires Alloc::value_type to be T; indirect does not rebind");

  [[FOLLY_ATTR_NO_UNIQUE_ADDRESS]] Alloc alloc_{};
  pointer ptr_{nullptr};

  template <typename... Args>
  constexpr void allocate_and_construct(Args&&... args) {
    ptr_ = allocate_construct(alloc_, std::forward<Args>(args)...);
  }

  constexpr void destroy_and_deallocate() noexcept {
    destroy_deallocate(alloc_, ptr_);
    ptr_ = nullptr;
  }

  template <typename... Args>
  static constexpr pointer allocate_construct(Alloc& a, Args&&... args) {
    pointer p = traits::allocate(a, 1);
    detail::indirect_rollback rollback{[&] { traits::deallocate(a, p, 1); }};
    traits::construct(a, std::to_address(p), std::forward<Args>(args)...);
    rollback.dismiss();
    return p;
  }

  static constexpr void destroy_deallocate(Alloc& a, pointer p) noexcept {
    if (p != nullptr) {
      traits::destroy(a, std::to_address(p));
      traits::deallocate(a, p, 1);
    }
  }

  constexpr T& deref() & noexcept {
    assert(ptr_ != nullptr);
    return *ptr_;
  }
  constexpr const T& deref() const& noexcept {
    assert(ptr_ != nullptr);
    return *ptr_;
  }

  // Centralized valueless handling: valueless < valued, two valueless equal.
  static constexpr std::optional<bool> equal_if_valueless(
      bool av, bool bv) noexcept {
    if (av && bv) {
      return true;
    }
    if (av || bv) {
      return false;
    }
    return std::nullopt;
  }

  static constexpr std::optional<std::strong_ordering> order_if_valueless(
      bool av, bool bv) noexcept {
    if (av && bv) {
      return std::strong_ordering::equal;
    }
    if (av) {
      return std::strong_ordering::less;
    }
    if (bv) {
      return std::strong_ordering::greater;
    }
    return std::nullopt;
  }

 public:
  constexpr bool valueless_after_move() const noexcept {
    return ptr_ == nullptr;
  }

  // Primary ctors - single source of truth for allocation.
  template <typename... Args>
    requires(
        std::is_constructible_v<T, Args...> &&
        std::is_default_constructible_v<Alloc>)
  constexpr explicit indirect(std::in_place_t, Args&&... args) {
    allocate_and_construct(std::forward<Args>(args)...);
  }

  template <typename... Args>
    requires(std::is_constructible_v<T, Args...>)
  constexpr explicit indirect(
      std::allocator_arg_t, const Alloc& alloc, std::in_place_t, Args&&... args)
      : alloc_{alloc} {
    allocate_and_construct(std::forward<Args>(args)...);
  }

  // Delegating ctors - [indirect.ctor]
  constexpr explicit indirect()
    requires(std::is_default_constructible_v<Alloc>)
      : indirect(std::in_place) {
    static_assert(
        std::is_default_constructible_v<T>,
        "folly::indirect<T>: T must be default constructible");
  }

  constexpr explicit indirect(std::allocator_arg_t, const Alloc& alloc)
      : indirect(std::allocator_arg, alloc, std::in_place) {
    static_assert(
        std::is_default_constructible_v<T>,
        "folly::indirect<T>: T must be default constructible");
  }

  template <typename U, typename... Args>
    requires(
        std::is_constructible_v<T, std::initializer_list<U>&, Args...> &&
        std::is_default_constructible_v<Alloc>)
  constexpr explicit indirect(
      std::in_place_t, std::initializer_list<U> il, Args&&... args) {
    allocate_and_construct(il, std::forward<Args>(args)...);
  }

  template <typename U, typename... Args>
    requires(std::is_constructible_v<T, std::initializer_list<U>&, Args...>)
  constexpr explicit indirect(
      std::allocator_arg_t,
      const Alloc& alloc,
      std::in_place_t,
      std::initializer_list<U> il,
      Args&&... args)
      : alloc_{alloc} {
    allocate_and_construct(il, std::forward<Args>(args)...);
  }

  // [indirect.ctor] template<class U = T> explicit indirect(U&& u);
  template <typename U = T>
    requires(
        !std::is_same_v<std::remove_cvref_t<U>, indirect> &&
        !std::is_same_v<std::remove_cvref_t<U>, std::in_place_t> &&
        std::is_constructible_v<T, U> && std::is_default_constructible_v<Alloc>)
  constexpr explicit indirect(U&& u)
      : indirect(std::in_place, std::forward<U>(u)) {}

  template <typename U = T>
    requires(
        !std::is_same_v<std::remove_cvref_t<U>, indirect> &&
        !std::is_same_v<std::remove_cvref_t<U>, std::in_place_t> &&
        std::is_constructible_v<T, U>)
  constexpr explicit indirect(std::allocator_arg_t, const Alloc& alloc, U&& u)
      : indirect(std::allocator_arg, alloc, std::in_place, std::forward<U>(u)) {
  }

  // Copy - [indirect.ctor] states copy-constructibility of T as a Mandates,
  // so these are declared for every T and diagnose only when instantiated.
  constexpr indirect(const indirect& other)
      : indirect(
            std::allocator_arg,
            traits::select_on_container_copy_construction(other.alloc_),
            other) {}

  constexpr indirect(
      std::allocator_arg_t, const Alloc& alloc, const indirect& other)
      : alloc_{alloc} {
    static_assert(
        std::is_copy_constructible_v<T>,
        "folly::indirect<T>: T must be copy constructible to copy an indirect");
    if (!other.valueless_after_move()) {
      allocate_and_construct(*other.ptr_);
    }
  }

  // Move - valueless source propagates valueless (std::indirect semantics).
  // [indirect.ctor] constexpr indirect(indirect&& other) noexcept;
  constexpr indirect(indirect&& other) noexcept
      : alloc_{std::move(other.alloc_)},
        ptr_{std::exchange(other.ptr_, nullptr)} {}

  // [indirect.ctor] constexpr indirect(allocator_arg_t, const Alloc& a,
  // indirect&& other) noexcept(is_always_equal)
  constexpr indirect(
      std::allocator_arg_t,
      const Alloc& alloc,
      indirect&& other) noexcept(traits::is_always_equal::value)
      : alloc_{alloc} {
    if (other.valueless_after_move()) {
      return;
    }
    if (alloc_ == other.alloc_) {
      ptr_ = other.ptr_;
      other.ptr_ = nullptr;
    } else {
      allocate_and_construct(std::move(*other.ptr_));
      other.destroy_and_deallocate();
    }
  }

  constexpr ~indirect() { destroy_and_deallocate(); }

  // Copy assignment - requires only copy-constructible T.
  // Strong guarantee on the allocate-new path; basic guarantee on the
  // assign-through fast path.
  constexpr indirect& operator=(const indirect& other) {
    static_assert(
        std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>,
        "folly::indirect<T>: T must be copy constructible and copy assignable "
        "to copy-assign an indirect");
    if (this == &other) {
      return *this;
    }
    if (other.valueless_after_move()) {
      destroy_and_deallocate();
      if constexpr (traits::propagate_on_container_copy_assignment::value) {
        // Cpp17Allocator requires allocator copy assignment not to throw;
        // enforce it here, since a throw would leave *this valueless.
        std::invoke([&]() noexcept { alloc_ = other.alloc_; });
      }
      return *this;
    }
    if constexpr (traits::propagate_on_container_copy_assignment::value) {
      if (alloc_ != other.alloc_) {
        Alloc old_alloc = alloc_;
        pointer old_ptr = ptr_;
        Alloc new_alloc = other.alloc_;
        pointer new_ptr = allocate_construct(new_alloc, *other.ptr_);
        // Cpp17Allocator requires allocator copy assignment not to throw;
        // enforce it here, since a throw would orphan new_ptr.
        std::invoke([&]() noexcept { alloc_ = new_alloc; });
        destroy_deallocate(old_alloc, old_ptr);
        ptr_ = new_ptr;
        return *this;
      }
      // Cpp17Allocator requires allocator copy assignment not to throw;
      // enforce it here too, though the allocators already compare equal.
      std::invoke([&]() noexcept { alloc_ = other.alloc_; });
    }
    // [indirect.assign]/3: equal allocators and a valued destination assign
    // through, which avoids an allocation and preserves pointer stability.
    if (alloc_ == other.alloc_ && !valueless_after_move()) {
      *ptr_ = *other.ptr_;
      return *this;
    }
    // [indirect.assign]/4: otherwise construct a new owned object, which also
    // gives the strong guarantee.
    pointer new_ptr = allocate_construct(alloc_, *other.ptr_);
    destroy_and_deallocate();
    ptr_ = new_ptr;
    return *this;
  }

  // Move assignment.
  constexpr indirect& operator=(indirect&& other) noexcept(
      traits::propagate_on_container_move_assignment::value ||
      traits::is_always_equal::value) {
    static_assert(
        traits::propagate_on_container_move_assignment::value ||
            traits::is_always_equal::value || std::is_move_constructible_v<T>,
        "folly::indirect<T, Alloc>: T must be move constructible when Alloc "
        "neither propagates on move assignment nor is always equal");
    if (this == &other) {
      return *this;
    }
    if (other.valueless_after_move()) {
      destroy_and_deallocate();
      if constexpr (traits::propagate_on_container_move_assignment::value) {
        alloc_ = std::move(other.alloc_);
      }
      return *this;
    }
    if constexpr (traits::propagate_on_container_move_assignment::value) {
      destroy_and_deallocate();
      alloc_ = std::move(other.alloc_);
      ptr_ = other.ptr_;
      other.ptr_ = nullptr;
      return *this;
    } else {
      if (alloc_ == other.alloc_) {
        assert(other.ptr_ != nullptr);
        destroy_and_deallocate();
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
      } else {
        // [indirect.assign]/4: unequal non-propagating allocators construct a
        // new owned object rather than assigning through the existing one.
        pointer new_ptr = allocate_construct(alloc_, std::move(*other.ptr_));
        destroy_and_deallocate();
        ptr_ = new_ptr;
        other.destroy_and_deallocate();
      }
      return *this;
    }
  }

  // [indirect.assign] template<class U = T> indirect& operator=(U&& u);
  // Assigns into the existing object when there is one, so assignment
  // preserves pointer stability.
  template <typename U = T>
    requires(
        !std::is_same_v<std::remove_cvref_t<U>, indirect> &&
        std::is_constructible_v<T, U> && std::is_assignable_v<T&, U>)
  constexpr indirect& operator=(U&& u) {
    if (valueless_after_move()) {
      allocate_and_construct(std::forward<U>(u));
    } else {
      *ptr_ = std::forward<U>(u);
    }
    return *this;
  }

  // Observers.
  constexpr T& operator*() & noexcept { return deref(); }
  constexpr const T& operator*() const& noexcept { return deref(); }
  constexpr T&& operator*() && noexcept { return std::move(deref()); }
  constexpr const T&& operator*() const&& noexcept {
    return std::move(deref());
  }

  constexpr pointer operator->() noexcept {
    assert(ptr_ != nullptr);
    return ptr_;
  }
  constexpr const_pointer operator->() const noexcept {
    assert(ptr_ != nullptr);
    return ptr_;
  }

  constexpr Alloc get_allocator() const noexcept { return alloc_; }

  // Swap - [indirect.swap] makes equal allocators a precondition when
  // propagate_on_container_swap is false, so unequal allocators assert in
  // debug and are UB in release, as with a valueless operator*.
  constexpr void swap(indirect& other) noexcept(
      traits::propagate_on_container_swap::value ||
      traits::is_always_equal::value) {
    using std::swap;
    if constexpr (traits::propagate_on_container_swap::value) {
      swap(alloc_, other.alloc_);
    } else {
      assert(alloc_ == other.alloc_);
    }
    swap(ptr_, other.ptr_);
  }

  friend constexpr void swap(indirect& a, indirect& b) noexcept(
      noexcept(a.swap(b))) {
    a.swap(b);
  }

  // Comparisons - a valueless operand compares less than a valued one, and
  // two valueless operands compare equal. Following the standard, the
  // well-formedness of the underlying comparison is a Mandates rather than a
  // constraint: constraining operator== here would make every ADL lookup that
  // reaches indirect re-enter overload resolution on T.

  // [indirect.relops]
  template <typename U, typename AA>
  friend constexpr bool operator==(
      const indirect& lhs,
      const indirect<U, AA>& rhs) noexcept(noexcept(bool(*lhs == *rhs))) {
    if (auto eq = equal_if_valueless(
            lhs.valueless_after_move(), rhs.valueless_after_move())) {
      return *eq;
    }
    return *lhs == *rhs;
  }

  template <typename U, typename AA>
  friend constexpr detail::indirect_synth_three_way_result<T, U> operator<=>(
      const indirect& lhs, const indirect<U, AA>& rhs) {
    using R = detail::indirect_synth_three_way_result<T, U>;
    if (auto ord = order_if_valueless(
            lhs.valueless_after_move(), rhs.valueless_after_move())) {
      return static_cast<R>(*ord);
    }
    return detail::indirect_synth_three_way(*lhs, *rhs);
  }

  // [indirect.comp.with.t]
  template <typename U>
    requires(!std::is_same_v<std::remove_cvref_t<U>, indirect>)
  friend constexpr bool operator==(const indirect& lhs, const U& rhs) noexcept(
      noexcept(bool(*lhs == rhs))) {
    if (lhs.valueless_after_move()) {
      return false;
    }
    return *lhs == rhs;
  }

  template <typename U>
    requires(!std::is_same_v<std::remove_cvref_t<U>, indirect>)
  friend constexpr detail::indirect_synth_three_way_result<T, U> operator<=>(
      const indirect& lhs, const U& rhs) {
    using R = detail::indirect_synth_three_way_result<T, U>;
    if (lhs.valueless_after_move()) {
      return static_cast<R>(std::strong_ordering::less);
    }
    return detail::indirect_synth_three_way(*lhs, rhs);
  }
};

template <typename T>
indirect(T) -> indirect<T>;

template <typename Alloc, typename Value>
indirect(std::allocator_arg_t, Alloc, Value) -> indirect<
    Value,
    typename std::allocator_traits<Alloc>::template rebind_alloc<Value>>;

} // namespace folly

// std::hash specialization - [indirect.hash]
// Enabled iff hash<T> is enabled. Not guaranteed noexcept per spec.
namespace std {
template <typename T, typename Alloc>
  requires requires(const T& t) { std::hash<T>{}(t); }
struct hash<folly::indirect<T, Alloc>> {
  size_t operator()(const folly::indirect<T, Alloc>& v) const {
    if (v.valueless_after_move()) {
      return 0;
    }
    return std::hash<T>{}(*v);
  }
};
} // namespace std
