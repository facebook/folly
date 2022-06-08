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

/*
 *
 * Author: Eric Niebler <eniebler@fb.com>
 */

#include <folly/Portability.h>

namespace folly {

template <class Fn>
struct exception_wrapper::arg_type_
    : public arg_type_<decltype(&Fn::operator())> {};
template <class Ret, class Class, class Arg>
struct exception_wrapper::arg_type_<Ret (Class::*)(Arg)> {
  using type = Arg;
};
template <class Ret, class Class, class Arg>
struct exception_wrapper::arg_type_<Ret (Class::*)(Arg) const> {
  using type = Arg;
};
template <class Ret, class Arg>
struct exception_wrapper::arg_type_<Ret(Arg)> {
  using type = Arg;
};
template <class Ret, class Arg>
struct exception_wrapper::arg_type_<Ret (*)(Arg)> {
  using type = Arg;
};
template <class Ret, class Class>
struct exception_wrapper::arg_type_<Ret (Class::*)(...)> {
  using type = AnyException;
};
template <class Ret, class Class>
struct exception_wrapper::arg_type_<Ret (Class::*)(...) const> {
  using type = AnyException;
};
template <class Ret>
struct exception_wrapper::arg_type_<Ret(...)> {
  using type = AnyException;
};
template <class Ret>
struct exception_wrapper::arg_type_<Ret (*)(...)> {
  using type = AnyException;
};

#ifdef FOLLY_HAVE_NOEXCEPT_FUNCTION_TYPE
template <class Ret, class Class, class Arg>
struct exception_wrapper::arg_type_<Ret (Class::*)(Arg) noexcept> {
  using type = Arg;
};
template <class Ret, class Class, class Arg>
struct exception_wrapper::arg_type_<Ret (Class::*)(Arg) const noexcept> {
  using type = Arg;
};
template <class Ret, class Arg>
struct exception_wrapper::arg_type_<Ret(Arg) noexcept> {
  using type = Arg;
};
template <class Ret, class Arg>
struct exception_wrapper::arg_type_<Ret (*)(Arg) noexcept> {
  using type = Arg;
};
template <class Ret, class Class>
struct exception_wrapper::arg_type_<Ret (Class::*)(...) noexcept> {
  using type = AnyException;
};
template <class Ret, class Class>
struct exception_wrapper::arg_type_<Ret (Class::*)(...) const noexcept> {
  using type = AnyException;
};
template <class Ret>
struct exception_wrapper::arg_type_<Ret(...) noexcept> {
  using type = AnyException;
};
template <class Ret>
struct exception_wrapper::arg_type_<Ret (*)(...) noexcept> {
  using type = AnyException;
};
#endif

struct exception_wrapper::with_exception_from_fn_ {
  template <typename, typename Fn>
  using apply = arg_type<Fn>;
};

struct exception_wrapper::with_exception_from_ex_ {
  template <typename Ex, typename>
  using apply = Ex;
};

template <class Ret, class... Args>
inline Ret exception_wrapper::noop_(Args...) {
  return Ret();
}

inline std::type_info const* exception_wrapper::uninit_type_(
    exception_wrapper const*) {
  return &typeid(void);
}

template <class Ex, typename... As>
inline exception_wrapper::Buffer::Buffer(in_place_type_t<Ex>, As&&... as_) {
  ::new (static_cast<void*>(&buff_)) Ex(std::forward<As>(as_)...);
}

template <class Ex>
inline Ex& exception_wrapper::Buffer::as() noexcept {
  return *static_cast<Ex*>(static_cast<void*>(&buff_));
}
template <class Ex>
inline Ex const& exception_wrapper::Buffer::as() const noexcept {
  return *static_cast<Ex const*>(static_cast<void const*>(&buff_));
}

inline std::exception const* exception_wrapper::as_exception_or_null_(
    std::exception const& ex) {
  return &ex;
}
inline std::exception const* exception_wrapper::as_exception_or_null_(
    AnyException) {
  return nullptr;
}

inline void exception_wrapper::ExceptionPtr::copy_(
    exception_wrapper const* from, exception_wrapper* to) {
  ::new (static_cast<void*>(&to->eptr_)) ExceptionPtr(from->eptr_);
}
inline void exception_wrapper::ExceptionPtr::move_(
    exception_wrapper* from, exception_wrapper* to) {
  ::new (static_cast<void*>(&to->eptr_)) ExceptionPtr(std::move(from->eptr_));
  delete_(from);
}
inline void exception_wrapper::ExceptionPtr::delete_(exception_wrapper* that) {
  that->eptr_.~ExceptionPtr();
  that->vptr_ = &uninit_;
}
[[noreturn]] inline void exception_wrapper::ExceptionPtr::throw_(
    exception_wrapper const* that) {
  std::rethrow_exception(that->eptr_.ptr_);
}
inline std::type_info const* exception_wrapper::ExceptionPtr::type_(
    exception_wrapper const* that) {
  return exception_ptr_get_type(that->eptr_.ptr_);
}
inline std::exception const* exception_wrapper::ExceptionPtr::get_exception_(
    exception_wrapper const* that) {
  return exception_ptr_get_object<std::exception>(that->eptr_.ptr_);
}
inline exception_wrapper exception_wrapper::ExceptionPtr::get_exception_ptr_(
    exception_wrapper const* that) {
  return *that;
}

template <class Ex>
inline void exception_wrapper::InPlace<Ex>::copy_(
    exception_wrapper const* from, exception_wrapper* to) {
  ::new (static_cast<void*>(std::addressof(to->buff_.as<Ex>())))
      Ex(from->buff_.as<Ex>());
}
template <class Ex>
inline void exception_wrapper::InPlace<Ex>::move_(
    exception_wrapper* from, exception_wrapper* to) {
  ::new (static_cast<void*>(std::addressof(to->buff_.as<Ex>())))
      Ex(std::move(from->buff_.as<Ex>()));
  delete_(from);
}
template <class Ex>
inline void exception_wrapper::InPlace<Ex>::delete_(exception_wrapper* that) {
  that->buff_.as<Ex>().~Ex();
  that->vptr_ = &uninit_;
}
template <class Ex>
[[noreturn]] inline void exception_wrapper::InPlace<Ex>::throw_(
    exception_wrapper const* that) {
  throw that->buff_.as<Ex>();
}
template <class Ex>
inline std::type_info const* exception_wrapper::InPlace<Ex>::type_(
    exception_wrapper const*) {
  return &typeid(Ex);
}
template <class Ex>
inline std::exception const* exception_wrapper::InPlace<Ex>::get_exception_(
    exception_wrapper const* that) {
  return as_exception_or_null_(that->buff_.as<Ex>());
}
template <class Ex>
inline exception_wrapper exception_wrapper::InPlace<Ex>::get_exception_ptr_(
    exception_wrapper const* that) {
  try {
    throw_(that);
  } catch (...) {
    return exception_wrapper{std::current_exception()};
  }
}

template <class Ex>
[[noreturn]] inline void exception_wrapper::SharedPtr::Impl<Ex>::throw_()
    const {
  throw ex_;
}
template <class Ex>
inline std::exception const*
exception_wrapper::SharedPtr::Impl<Ex>::get_exception_() const noexcept {
  return as_exception_or_null_(ex_);
}
template <class Ex>
inline exception_wrapper
exception_wrapper::SharedPtr::Impl<Ex>::get_exception_ptr_() const noexcept {
  try {
    throw_();
  } catch (...) {
    return exception_wrapper{std::current_exception()};
  }
}
inline void exception_wrapper::SharedPtr::copy_(
    exception_wrapper const* from, exception_wrapper* to) {
  ::new (static_cast<void*>(std::addressof(to->sptr_))) SharedPtr(from->sptr_);
}
inline void exception_wrapper::SharedPtr::move_(
    exception_wrapper* from, exception_wrapper* to) {
  ::new (static_cast<void*>(std::addressof(to->sptr_)))
      SharedPtr(std::move(from->sptr_));
  delete_(from);
}
inline void exception_wrapper::SharedPtr::delete_(exception_wrapper* that) {
  that->sptr_.~SharedPtr();
  that->vptr_ = &uninit_;
}
[[noreturn]] inline void exception_wrapper::SharedPtr::throw_(
    exception_wrapper const* that) {
  that->sptr_.ptr_->throw_();
  folly::assume_unreachable();
}
inline std::type_info const* exception_wrapper::SharedPtr::type_(
    exception_wrapper const* that) {
  return that->sptr_.ptr_->info_;
}
inline std::exception const* exception_wrapper::SharedPtr::get_exception_(
    exception_wrapper const* that) {
  return that->sptr_.ptr_->get_exception_();
}
inline exception_wrapper exception_wrapper::SharedPtr::get_exception_ptr_(
    exception_wrapper const* that) {
  return that->sptr_.ptr_->get_exception_ptr_();
}

template <class Ex, typename... As>
inline exception_wrapper::exception_wrapper(
    ThrownTag, in_place_type_t<Ex>, As&&... as)
    : eptr_{std::make_exception_ptr(Ex(std::forward<As>(as)...))},
      vptr_(&ExceptionPtr::ops_) {}

template <class Ex, typename... As>
inline exception_wrapper::exception_wrapper(
    OnHeapTag, in_place_type_t<Ex>, As&&... as)
    : sptr_{std::make_shared<SharedPtr::Impl<Ex>>(std::forward<As>(as)...)},
      vptr_(&SharedPtr::ops_) {}

template <class Ex, typename... As>
inline exception_wrapper::exception_wrapper(
    InSituTag, in_place_type_t<Ex>, As&&... as)
    : buff_{in_place_type<Ex>, std::forward<As>(as)...},
      vptr_(&InPlace<Ex>::ops_) {}

inline exception_wrapper::exception_wrapper(exception_wrapper&& that) noexcept
    : exception_wrapper{} {
  (vptr_ = that.vptr_)->move_(&that, this); // Move into *this, won't throw
}

inline exception_wrapper::exception_wrapper(
    exception_wrapper const& that) noexcept
    : exception_wrapper{} {
  that.vptr_->copy_(&that, this); // Copy into *this, won't throw
  vptr_ = that.vptr_;
}

// If `this == &that`, this move assignment operator leaves the object in a
// valid but unspecified state.
inline exception_wrapper& exception_wrapper::operator=(
    exception_wrapper&& that) noexcept {
  vptr_->delete_(this); // Free the current exception
  (vptr_ = that.vptr_)->move_(&that, this); // Move into *this, won't throw
  return *this;
}

inline exception_wrapper& exception_wrapper::operator=(
    exception_wrapper const& that) noexcept {
  exception_wrapper(that).swap(*this);
  return *this;
}

inline exception_wrapper::~exception_wrapper() {
  reset();
}

template <class Ex>
inline exception_wrapper::exception_wrapper(
    std::exception_ptr const& ptr, Ex& ex) noexcept
    : exception_wrapper{folly::copy(ptr), ex} {}

template <class Ex>
inline exception_wrapper::exception_wrapper(
    std::exception_ptr&& ptr, Ex& ex) noexcept
    : eptr_{std::move(ptr)}, vptr_(&ExceptionPtr::ops_) {
  assert(eptr_.ptr_);
  (void)ex;
  assert(exception_ptr_get_object<Ex>(eptr_.ptr_));
  assert(exception_ptr_get_object<Ex>(eptr_.ptr_) == &ex || kIsWindows);
}

namespace exception_wrapper_detail {
template <class Ex>
Ex&& dont_slice(Ex&& ex) {
  assert(
      (typeid(ex) == typeid(std::decay_t<Ex>)) &&
      "Dynamic and static exception types don't match. Exception would "
      "be sliced when storing in exception_wrapper.");
  return std::forward<Ex>(ex);
}
} // namespace exception_wrapper_detail

template <
    class Ex,
    class Ex_,
    FOLLY_REQUIRES_DEF(Conjunction<
                       exception_wrapper::IsStdException<Ex_>,
                       exception_wrapper::IsRegularExceptionType<Ex_>>::value)>
inline exception_wrapper::exception_wrapper(Ex&& ex)
    : exception_wrapper{
          PlacementOf<Ex_>{},
          in_place_type<Ex_>,
          exception_wrapper_detail::dont_slice(std::forward<Ex>(ex))} {}

template <
    class Ex,
    class Ex_,
    FOLLY_REQUIRES_DEF(exception_wrapper::IsRegularExceptionType<Ex_>::value)>
inline exception_wrapper::exception_wrapper(in_place_t, Ex&& ex)
    : exception_wrapper{
          PlacementOf<Ex_>{},
          in_place_type<Ex_>,
          exception_wrapper_detail::dont_slice(std::forward<Ex>(ex))} {}

template <
    class Ex,
    typename... As,
    FOLLY_REQUIRES_DEF(exception_wrapper::IsRegularExceptionType<Ex>::value)>
inline exception_wrapper::exception_wrapper(in_place_type_t<Ex>, As&&... as)
    : exception_wrapper{
          PlacementOf<Ex>{}, in_place_type<Ex>, std::forward<As>(as)...} {}

inline void exception_wrapper::swap(exception_wrapper& that) noexcept {
  exception_wrapper tmp(std::move(that));
  that = std::move(*this);
  *this = std::move(tmp);
}

inline exception_wrapper::operator bool() const noexcept {
  return vptr_ != &uninit_;
}

inline bool exception_wrapper::operator!() const noexcept {
  return !static_cast<bool>(*this);
}

inline void exception_wrapper::reset() {
  vptr_->delete_(this);
}

inline bool exception_wrapper::has_exception_ptr() const noexcept {
  return vptr_ == &ExceptionPtr::ops_;
}

inline std::exception* exception_wrapper::get_exception() noexcept {
  return const_cast<std::exception*>(vptr_->get_exception_(this));
}
inline std::exception const* exception_wrapper::get_exception() const noexcept {
  return vptr_->get_exception_(this);
}

template <typename Ex>
inline Ex* exception_wrapper::get_exception() noexcept {
  constexpr auto stdexcept = std::is_base_of<std::exception, Ex>::value;
  if (vptr_ == &ExceptionPtr::ops_) {
    return exception_ptr_get_object<Ex>(eptr_.ptr_);
  } else if (!stdexcept || vptr_ == &uninit_) {
    return nullptr;
  } else {
    using Target = conditional_t<stdexcept, Ex, std::exception>;
    auto const ptr = dynamic_cast<Target*>(get_exception());
    return reinterpret_cast<Ex*>(ptr);
  }
}

template <typename Ex>
inline Ex const* exception_wrapper::get_exception() const noexcept {
  constexpr auto stdexcept = std::is_base_of<std::exception, Ex>::value;
  if (vptr_ == &ExceptionPtr::ops_) {
    return exception_ptr_get_object<Ex>(eptr_.ptr_);
  } else if (!stdexcept || vptr_ == &uninit_) {
    return nullptr;
  } else {
    using Target = conditional_t<stdexcept, Ex, std::exception>;
    auto const ptr = dynamic_cast<Target const*>(get_exception());
    return reinterpret_cast<Ex const*>(ptr);
  }
}

inline std::exception_ptr exception_wrapper::to_exception_ptr() noexcept {
  if (*this) {
    // Computing an exception_ptr is expensive so cache the result.
    return (*this = vptr_->get_exception_ptr_(this)).eptr_.ptr_;
  }
  return {};
}
inline std::exception_ptr exception_wrapper::to_exception_ptr() const noexcept {
  return vptr_->get_exception_ptr_(this).eptr_.ptr_;
}

inline std::type_info const& exception_wrapper::none() noexcept {
  return typeid(void);
}

inline std::type_info const& exception_wrapper::type() const noexcept {
  return *vptr_->type_(this);
}

inline folly::fbstring exception_wrapper::what() const {
  if (auto e = get_exception()) {
    return class_name() + ": " + e->what();
  }
  return class_name();
}

inline folly::fbstring exception_wrapper::class_name() const {
  auto& ti = type();
  return ti == none() ? "" : folly::demangle(ti);
}

template <class Ex>
inline bool exception_wrapper::is_compatible_with() const noexcept {
  return get_exception<Ex>();
}

[[noreturn]] inline void exception_wrapper::throw_exception() const {
  vptr_->throw_(this);
  onNoExceptionError(__func__);
}

template <class Ex>
[[noreturn]] inline void exception_wrapper::throw_with_nested(Ex&& ex) const {
  try {
    throw_exception();
  } catch (...) {
    std::throw_with_nested(std::forward<Ex>(ex));
  }
}

template <class This, class Fn>
inline bool exception_wrapper::with_exception_(
    This&, Fn fn_, tag_t<AnyException>) {
  return void(fn_()), true;
}

template <class This, class Fn, typename Ex>
inline bool exception_wrapper::with_exception_(This& this_, Fn fn_, tag_t<Ex>) {
  auto ptr = this_.template get_exception<remove_cvref_t<Ex>>();
  return ptr && (void(fn_(static_cast<Ex&>(*ptr))), true);
}

template <class Ex, class This, class Fn>
inline bool exception_wrapper::with_exception_(This& this_, Fn fn_) {
  using from_fn = with_exception_from_fn_;
  using from_ex = with_exception_from_ex_;
  using from = conditional_t<std::is_void<Ex>::value, from_fn, from_ex>;
  using type = typename from::template apply<Ex, Fn>;
  return with_exception_(this_, std::move(fn_), tag<type>);
}

template <class This, class... CatchFns>
inline void exception_wrapper::handle_(
    This& this_, char const* name, CatchFns&... fns) {
  using _ = bool[];
  if (!this_) {
    onNoExceptionError(name);
  }
  bool handled = false;
  void(_{false, (handled = handled || with_exception_<void>(this_, fns))...});
  if (!handled) {
    this_.throw_exception();
  }
}

template <class Ex, class Fn>
inline bool exception_wrapper::with_exception(Fn fn) {
  return with_exception_<Ex>(*this, std::move(fn));
}
template <class Ex, class Fn>
inline bool exception_wrapper::with_exception(Fn fn) const {
  return with_exception_<Ex const>(*this, std::move(fn));
}

template <class... CatchFns>
inline void exception_wrapper::handle(CatchFns... fns) {
  handle_(*this, __func__, fns...);
}
template <class... CatchFns>
inline void exception_wrapper::handle(CatchFns... fns) const {
  handle_(*this, __func__, fns...);
}

} // namespace folly
