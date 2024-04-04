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

#include <memory>
#include <utility>

#include <folly/Memory.h>
#include <folly/Portability.h>
#include <folly/lang/Exception.h>

namespace folly {

namespace detail {
template <typename T>
inline constexpr bool is_not_null_v = is_instantiation_of_v<not_null, T>;
template <typename T>
struct is_not_null : std::bool_constant<is_not_null_v<T>> {};

template <
    typename T,
    typename = std::enable_if_t<!is_not_null_v<remove_cvref_t<T>>>>
auto maybeUnwrap(T&& t) {
  return std::forward<T>(t);
}
template <typename T>
auto maybeUnwrap(const not_null_base<T>& t) {
  return t.unwrap();
}
template <typename T>
auto maybeUnwrap(not_null_base<T>&& t) {
  return std::move(t).unwrap();
}

template <typename T>
struct maybe_unwrap_not_null {
  using type = T;
};
template <typename T>
struct maybe_unwrap_not_null<const T> {
  using type = const typename maybe_unwrap_not_null<T>::type;
};
template <typename T>
struct maybe_unwrap_not_null<T&> {
  using type = typename maybe_unwrap_not_null<T>::type&;
};
template <typename T>
struct maybe_unwrap_not_null<T&&> {
  using type = typename maybe_unwrap_not_null<T>::type&&;
};
template <typename PtrT>
struct maybe_unwrap_not_null<not_null<PtrT>> {
  using type = PtrT;
};

template <typename FromT, typename ToT>
struct is_not_null_convertible
    : std::is_convertible<
          typename maybe_unwrap_not_null<FromT>::type,
          typename maybe_unwrap_not_null<ToT>::type> {};

template <typename FromT, typename ToPtrT>
struct is_not_null_nothrow_constructible
    : std::integral_constant<
          bool,
          is_not_null_v<FromT> &&
              std::is_nothrow_constructible_v<ToPtrT, FromT>> {};

struct secret_guaranteed_not_null : guaranteed_not_null_provider {
  static guaranteed_not_null get() {
    return guaranteed_not_null_provider::guaranteed_not_null();
  }
};

// In order to be able to cast from not_null<PtrT> to ToT:
//  - It must not already be castable, otherwise the compiler will raise an
//    ambiguity error
//  - PtrT must be castable to ToT
template <typename FromPtrT, typename ToT>
struct is_not_null_castable
    : std::integral_constant<
          bool,
          std::is_convertible_v<const FromPtrT&, ToT> &&
              !std::is_convertible_v<const not_null<FromPtrT>&, ToT>> {};
template <typename FromPtrT, typename ToT>
struct is_not_null_move_castable
    : std::integral_constant<
          bool,
          std::is_convertible_v<FromPtrT&&, ToT> &&
              !std::is_convertible_v<not_null<FromPtrT>&&, ToT>> {};

template <typename T, typename = decltype(*std::declval<T*>() == nullptr)>
inline std::true_type is_comparable_to_nullptr_fn(const T&) {
  return {};
}
inline std::false_type is_comparable_to_nullptr_fn(...) {
  return {};
}
template <typename T>
constexpr bool is_comparable_to_nullptr_v =
    decltype(is_comparable_to_nullptr_fn(*std::declval<T*>()))::value;
} // namespace detail

template <typename PtrT>
template <typename U>
not_null_base<PtrT>::not_null_base(U&& u, private_tag)
    : ptr_(detail::maybeUnwrap(std::forward<U>(u))) {
  if constexpr (!detail::is_not_null_v<U>) {
    throw_if_null();
  }
}

template <typename PtrT>
not_null_base<PtrT>::not_null_base(
    PtrT&& ptr, guaranteed_not_null_provider::guaranteed_not_null) noexcept
    : ptr_(std::move(ptr)) {}

template <typename PtrT>
template <typename U, typename>
not_null_base<PtrT>::not_null_base(U&& u, implicit_tag<true>) noexcept(
    detail::is_not_null_nothrow_constructible<U&&, PtrT>::value)
    : not_null_base(std::forward<U>(u), private_tag{}) {}

template <typename PtrT>
template <typename U, typename>
not_null_base<PtrT>::not_null_base(U&& u, implicit_tag<false>) noexcept(
    detail::is_not_null_nothrow_constructible<U&&, PtrT>::value)
    : not_null_base(std::forward<U>(u), private_tag{}) {}

template <typename PtrT>
typename not_null_base<PtrT>::element_type& not_null_base<PtrT>::operator*()
    const noexcept {
  return *unwrap();
}

template <typename PtrT>
const PtrT& not_null_base<PtrT>::operator->() const noexcept {
  return unwrap();
}

template <typename PtrT>
not_null_base<PtrT>::operator const PtrT&() const& noexcept {
  return unwrap();
}

template <typename PtrT>
not_null_base<PtrT>::operator PtrT&&() && noexcept {
  return std::move(*this).unwrap();
}

template <typename PtrT>
template <typename U, typename>
not_null_base<PtrT>::operator U() const& noexcept(
    std::is_nothrow_constructible_v<U, const PtrT&>) {
  if constexpr (detail::is_not_null_v<U>) {
    return U(*this);
  }
  return U(unwrap());
}

template <typename PtrT>
template <typename U, typename>
not_null_base<PtrT>::operator U() && noexcept(
    std::is_nothrow_constructible_v<U, PtrT&&>) {
  if constexpr (detail::is_not_null_v<U>) {
    return U(std::move(*this));
  }
  return U(std::move(*this).unwrap());
}

template <typename PtrT>
void not_null_base<PtrT>::swap(not_null_base& other) noexcept {
  mutable_unwrap().swap(other.mutable_unwrap());
}

template <typename PtrT>
const PtrT& not_null_base<PtrT>::unwrap() const& noexcept {
  if constexpr (folly::kIsDebug) {
    terminate_if_null(ptr_);
  }
  return ptr_;
}

template <typename PtrT>
PtrT&& not_null_base<PtrT>::unwrap() && noexcept {
  if constexpr (folly::kIsDebug) {
    terminate_if_null(ptr_);
  }
  return std::move(ptr_);
}

template <typename PtrT>
PtrT& not_null_base<PtrT>::mutable_unwrap() noexcept {
  return const_cast<PtrT&>(const_cast<const not_null_base&>(*this).unwrap());
}

template <typename PtrT>
void not_null_base<PtrT>::throw_if_null() const {
  throw_if_null(ptr_);
}

template <typename PtrT>
template <typename T>
void not_null_base<PtrT>::throw_if_null(const T& ptr) {
  if (ptr == nullptr) {
    folly::throw_exception<std::invalid_argument>("non_null<PtrT> is null");
  }
}

template <typename PtrT>
template <typename T>
void not_null_base<PtrT>::terminate_if_null(const T& ptr) {
  if (ptr == nullptr) {
    folly::terminate_with<std::runtime_error>(
        "not_null internal pointer is null");
  }
}

template <typename PtrT>
template <typename Deleter>
Deleter&& not_null_base<PtrT>::forward_or_throw_if_null(Deleter&& deleter) {
  if constexpr (detail::is_comparable_to_nullptr_v<Deleter>) {
    if (deleter == nullptr) {
      folly::throw_exception<std::invalid_argument>(
          "non_null<PtrT> deleter is null");
    }
  }
  return std::forward<Deleter>(deleter);
}

/**
 * not_null<std::unique_ptr<>> specialization.
 */
template <typename T, typename Deleter>
not_null<std::unique_ptr<T, Deleter>>::not_null(pointer p, const Deleter& d)
    : not_null_base<std::unique_ptr<T, Deleter>>(
          std::unique_ptr<T, Deleter>(
              std::move(p).unwrap(), this->forward_or_throw_if_null(d)),
          guaranteed_not_null_provider::guaranteed_not_null()) {}

template <typename T, typename Deleter>
not_null<std::unique_ptr<T, Deleter>>::not_null(pointer p, Deleter&& d)
    : not_null_base<std::unique_ptr<T, Deleter>>(
          std::unique_ptr<T, Deleter>(
              std::move(p).unwrap(),
              this->forward_or_throw_if_null(std::move(d))),
          guaranteed_not_null_provider::guaranteed_not_null()) {}

template <typename T, typename Deleter>
void not_null<std::unique_ptr<T, Deleter>>::reset(pointer ptr) noexcept {
  this->mutable_unwrap().reset(ptr.unwrap());
}

template <typename T, typename Deleter>
typename not_null<std::unique_ptr<T, Deleter>>::pointer
not_null<std::unique_ptr<T, Deleter>>::get() const noexcept {
  return pointer(
      this->unwrap().get(),
      guaranteed_not_null_provider::guaranteed_not_null());
}

template <typename T, typename Deleter>
Deleter& not_null<std::unique_ptr<T, Deleter>>::get_deleter() noexcept {
  return this->mutable_unwrap().get_deleter();
}

template <typename T, typename Deleter>
const Deleter& not_null<std::unique_ptr<T, Deleter>>::get_deleter()
    const noexcept {
  return this->unwrap().get_deleter();
}

template <typename T, typename... Args>
not_null_unique_ptr<T> make_not_null_unique(Args&&... args) {
  return not_null_unique_ptr<T>(
      std::make_unique<T>(std::forward<Args>(args)...),
      detail::secret_guaranteed_not_null::get());
}

/**
 * not_null<std::shared_ptr<>> specialization.
 */
template <typename T>
template <typename U, typename Deleter>
not_null<std::shared_ptr<T>>::not_null(U* ptr, Deleter d)
    : not_null_base<std::shared_ptr<T>>(std::shared_ptr<T>(
          ptr, this->forward_or_throw_if_null(std::move(d)))) {}

template <typename T>
template <typename U, typename Deleter>
not_null<std::shared_ptr<T>>::not_null(not_null<U*> ptr, Deleter d)
    : not_null_base<std::shared_ptr<T>>(
          std::shared_ptr<T>(
              ptr.unwrap(), this->forward_or_throw_if_null(std::move(d))),
          guaranteed_not_null_provider::guaranteed_not_null()) {}

template <typename T>
template <typename U>
not_null<std::shared_ptr<T>>::not_null(
    const std::shared_ptr<U>& r, not_null<element_type*> ptr) noexcept
    : not_null_base<std::shared_ptr<T>>(
          std::shared_ptr<T>(r, ptr.unwrap()),
          guaranteed_not_null_provider::guaranteed_not_null()) {}

template <typename T>
template <typename U>
not_null<std::shared_ptr<T>>::not_null(
    const not_null<std::shared_ptr<U>>& r, not_null<element_type*> ptr) noexcept
    : not_null_base<std::shared_ptr<T>>(
          std::shared_ptr<T>(r.unwrap(), ptr.unwrap()),
          guaranteed_not_null_provider::guaranteed_not_null()) {}

template <typename T>
template <typename U>
not_null<std::shared_ptr<T>>::not_null(
    std::shared_ptr<U>&& r, not_null<element_type*> ptr) noexcept
    : not_null_base<std::shared_ptr<T>>(
          std::shared_ptr<T>(std::move(r), ptr.unwrap()),
          guaranteed_not_null_provider::guaranteed_not_null()) {}

template <typename T>
template <typename U>
not_null<std::shared_ptr<T>>::not_null(
    not_null<std::shared_ptr<U>>&& r, not_null<element_type*> ptr) noexcept
    : not_null_base<std::shared_ptr<T>>(
          std::shared_ptr<T>(std::move(r).unwrap(), ptr.unwrap()),
          guaranteed_not_null_provider::guaranteed_not_null()) {}

template <typename T>
template <typename U>
void not_null<std::shared_ptr<T>>::reset(U* ptr) {
  this->throw_if_null(ptr);
  this->mutable_unwrap().reset(ptr);
}

template <typename T>
template <typename U>
void not_null<std::shared_ptr<T>>::reset(not_null<U*> ptr) noexcept {
  this->mutable_unwrap().reset(ptr.unwrap());
}

template <typename T>
template <typename U, typename Deleter>
void not_null<std::shared_ptr<T>>::reset(U* ptr, Deleter d) {
  this->throw_if_null(ptr);
  this->mutable_unwrap().reset(
      ptr, this->forward_or_throw_if_null(std::move(d)));
}

template <typename T>
template <typename U, typename Deleter>
void not_null<std::shared_ptr<T>>::reset(not_null<U*> ptr, Deleter d) {
  this->mutable_unwrap().reset(
      ptr.unwrap(), this->forward_or_throw_if_null(std::move(d)));
}

template <typename T>
typename not_null<std::shared_ptr<T>>::pointer
not_null<std::shared_ptr<T>>::get() const noexcept {
  return pointer(
      this->unwrap().get(),
      guaranteed_not_null_provider::guaranteed_not_null());
}

template <typename T>
long not_null<std::shared_ptr<T>>::use_count() const noexcept {
  return this->unwrap().use_count();
}

template <typename T>
template <typename U>
bool not_null<std::shared_ptr<T>>::owner_before(
    const std::shared_ptr<U>& other) const noexcept {
  return this->unwrap().owner_before(other);
}

template <typename T>
template <typename U>
bool not_null<std::shared_ptr<T>>::owner_before(
    const not_null<std::shared_ptr<U>>& other) const noexcept {
  return this->unwrap().owner_before(other.unwrap());
}

template <typename T, typename... Args>
not_null_shared_ptr<T> make_not_null_shared(Args&&... args) {
  return not_null_shared_ptr<T>(
      std::make_shared<T>(std::forward<Args>(args)...),
      detail::secret_guaranteed_not_null::get());
}

template <typename T, typename Alloc, typename... Args>
not_null_shared_ptr<T> allocate_not_null_shared(
    const Alloc& alloc, Args&&... args) {
  return not_null_shared_ptr<T>(
      std::allocate_shared<T, Alloc, Args...>(
          alloc, std::forward<Args>(args)...),
      detail::secret_guaranteed_not_null::get());
}

/**
 * Comparators.
 */
#define FB_NOT_NULL_MK_OP(op)                                 \
  template <typename PtrT, typename T>                        \
  bool operator op(const not_null<PtrT>& lhs, const T& rhs) { \
    return lhs.unwrap() op rhs;                               \
  }                                                           \
  template <typename PtrT, typename T, typename>              \
  bool operator op(const T& lhs, const not_null<PtrT>& rhs) { \
    return lhs op rhs.unwrap();                               \
  }

FB_NOT_NULL_MK_OP(==)
FB_NOT_NULL_MK_OP(!=)
FB_NOT_NULL_MK_OP(<)
FB_NOT_NULL_MK_OP(<=)
FB_NOT_NULL_MK_OP(>)
FB_NOT_NULL_MK_OP(>=)
#undef FB_NOT_NULL_MK_OP

/**
 * Output.
 */
template <typename U, typename V, typename PtrT>
std::basic_ostream<U, V>& operator<<(
    std::basic_ostream<U, V>& os, const not_null<PtrT>& ptr) {
  return os << ptr.unwrap();
}

/**
 * Swap
 */
template <typename PtrT>
void swap(not_null<PtrT>& lhs, not_null<PtrT>& rhs) noexcept {
  lhs.swap(rhs);
}

/**
 * Getters
 */
template <typename Deleter, typename T>
Deleter* get_deleter(const not_null_shared_ptr<T>& ptr) {
  return std::get_deleter<Deleter>(ptr.unwrap());
}

/**
 * Casting
 */
template <typename T, typename U>
not_null_shared_ptr<T> static_pointer_cast(const not_null_shared_ptr<U>& r) {
  auto p = std::static_pointer_cast<T, U>(r.unwrap());
  return not_null_shared_ptr<T>(
      std::move(p), detail::secret_guaranteed_not_null::get());
}
template <typename T, typename U>
not_null_shared_ptr<T> static_pointer_cast(not_null_shared_ptr<U>&& r) {
  auto p = std::static_pointer_cast<T, U>(std::move(r).unwrap());
  return not_null_shared_ptr<T>(
      std::move(p), detail::secret_guaranteed_not_null::get());
}
template <typename T, typename U>
std::shared_ptr<T> dynamic_pointer_cast(const not_null_shared_ptr<U>& r) {
  return std::dynamic_pointer_cast<T, U>(r.unwrap());
}
template <typename T, typename U>
std::shared_ptr<T> dynamic_pointer_cast(not_null_shared_ptr<U>&& r) {
  return std::dynamic_pointer_cast<T, U>(std::move(r).unwrap());
}
template <typename T, typename U>
not_null_shared_ptr<T> const_pointer_cast(const not_null_shared_ptr<U>& r) {
  auto p = std::const_pointer_cast<T, U>(r.unwrap());
  return not_null_shared_ptr<T>(
      std::move(p), detail::secret_guaranteed_not_null::get());
}
template <typename T, typename U>
not_null_shared_ptr<T> const_pointer_cast(not_null_shared_ptr<U>&& r) {
  auto p = std::const_pointer_cast<T, U>(std::move(r).unwrap());
  return not_null_shared_ptr<T>(
      std::move(p), detail::secret_guaranteed_not_null::get());
}
template <typename T, typename U>
not_null_shared_ptr<T> reinterpret_pointer_cast(
    const not_null_shared_ptr<U>& r) {
  auto p = std::reinterpret_pointer_cast<T, U>(r.unwrap());
  return not_null_shared_ptr<T>(
      std::move(p), detail::secret_guaranteed_not_null::get());
}
template <typename T, typename U>
not_null_shared_ptr<T> reinterpret_pointer_cast(not_null_shared_ptr<U>&& r) {
  auto p = std::reinterpret_pointer_cast<T, U>(std::move(r).unwrap());
  return not_null_shared_ptr<T>(
      std::move(p), detail::secret_guaranteed_not_null::get());
}

static_assert(
    std::is_same_v<decltype(not_null(std::declval<int*>())), not_null<int*>>);

static_assert(std::is_same_v<
              decltype(not_null(std::declval<std::unique_ptr<int>>())),
              not_null_unique_ptr<int>>);

static_assert(std::is_same_v<
              decltype(not_null(std::declval<std::unique_ptr<int>&&>())),
              not_null_unique_ptr<int>>);

static_assert(std::is_same_v<
              decltype(not_null(std::declval<const std::shared_ptr<int>&>())),
              not_null_shared_ptr<int>>);

} // namespace folly
