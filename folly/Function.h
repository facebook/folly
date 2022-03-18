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
 * @author Eric Niebler (eniebler@fb.com), Sven Over (over@fb.com)
 * Acknowledgements: Giuseppe Ottaviano (ott@fb.com)
 */

/**
 * @class Function
 *
 * @brief A polymorphic function wrapper that is not copyable and does not
 *    require the wrapped function to be copy constructible.
 *
 * `folly::Function` is a polymorphic function wrapper, similar to
 * `std::function`. The template parameters of the `folly::Function` define
 * the parameter signature of the wrapped callable, but not the specific
 * type of the embedded callable. E.g. a `folly::Function<int(int)>`
 * can wrap callables that return an `int` when passed an `int`. This can be a
 * function pointer or any class object implementing one or both of
 *
 *     int operator(int);
 *     int operator(int) const;
 *
 * If both are defined, the non-const one takes precedence.
 *
 * Unlike `std::function`, a `folly::Function` can wrap objects that are not
 * copy constructible. As a consequence of this, `folly::Function` itself
 * is not copyable, either.
 *
 * Another difference is that, unlike `std::function`, `folly::Function` treats
 * const-ness of methods correctly. While a `std::function` allows to wrap
 * an object that only implements a non-const `operator()` and invoke
 * a const-reference of the `std::function`, `folly::Function` requires you to
 * declare a function type as const in order to be able to execute it on a
 * const-reference.
 *
 * For example:
 *
 *     class Foo {
 *      public:
 *       void operator()() {
 *         // mutates the Foo object
 *       }
 *     };
 *
 *     class Bar {
 *       std::function<void(void)> foo_; // wraps a Foo object
 *      public:
 *       void mutateFoo() const
 *       {
 *         foo_();
 *       }
 *     };
 *
 * Even though `mutateFoo` is a const-method, so it can only reference `foo_`
 * as const, it is able to call the non-const `operator()` of the Foo
 * object that is embedded in the foo_ function.
 *
 * `folly::Function` will not allow you to do that. You will have to decide
 * whether you need to invoke your wrapped callable from a const reference
 * (like in the example above), in which case it will only wrap a
 * `operator() const`. If your functor does not implement that,
 * compilation will fail. If you do not require to be able to invoke the
 * wrapped function in a const context, you can wrap any functor that
 * implements either or both of const and non-const `operator()`.
 *
 * The template parameter of `folly::Function`, the `FunctionType`, can be
 * const-qualified. Be aware that the const is part of the function signature.
 * It does not mean that the function type is a const type.
 *
 *   using FunctionType = R(Args...);
 *   using ConstFunctionType = R(Args...) const;
 *
 * In this example, `FunctionType` and `ConstFunctionType` are different
 * types. `ConstFunctionType` is not the same as `const FunctionType`.
 * As a matter of fact, trying to use the latter should emit a compiler
 * warning or error, because it has no defined meaning.
 *
 *     // This will not compile:
 *     folly::Function<void(void) const> func = Foo();
 *     // because Foo does not have a member function of the form:
 *     //   void operator()() const;
 *
 *     // This will compile just fine:
 *     folly::Function<void(void)> func = Foo();
 *     // and it will wrap the existing member function:
 *     //   void operator()();
 *
 * When should a const function type be used? As a matter of fact, you will
 * probably not need to use const function types very often. See the following
 * example:
 *
 *     class Bar {
 *       folly::Function<void()> func_;
 *       folly::Function<void() const> constFunc_;
 *
 *       void someMethod() {
 *         // Can call func_.
 *         func_();
 *         // Can call constFunc_.
 *         constFunc_();
 *       }
 *
 *       void someConstMethod() const {
 *         // Can call constFunc_.
 *         constFunc_();
 *         // However, cannot call func_ because a non-const method cannot
 *         // be called from a const one.
 *       }
 *     };
 *
 * As you can see, whether the `folly::Function`'s function type should
 * be declared const or not is identical to whether a corresponding method
 * would be declared const or not.
 *
 * You only require a `folly::Function` to hold a const function type, if you
 * intend to invoke it from within a const context. This is to ensure that
 * you cannot mutate its inner state when calling in a const context.
 *
 * This is how the const/non-const choice relates to lambda functions:
 *
 *     // Non-mutable lambdas: can be stored in a non-const...
 *     folly::Function<void(int)> print_number =
 *       [] (int number) { std::cout << number << std::endl; };
 *
 *     // ...as well as in a const folly::Function
 *     folly::Function<void(int) const> print_number_const =
 *       [] (int number) { std::cout << number << std::endl; };
 *
 *     // Mutable lambda: can only be stored in a non-const folly::Function:
 *     int number = 0;
 *     folly::Function<void()> print_number =
 *       [number] () mutable { std::cout << ++number << std::endl; };
 *     // Trying to store the above mutable lambda in a
 *     // `folly::Function<void() const>` would lead to a compiler error:
 *     // error: no viable conversion from '(lambda at ...)' to
 *     // 'folly::Function<void () const>'
 *
 * Casting between const and non-const `folly::Function`s:
 * conversion from const to non-const signatures happens implicitly. Any
 * function that takes a `folly::Function<R(Args...)>` can be passed
 * a `folly::Function<R(Args...) const>` without explicit conversion.
 * This is safe, because casting from const to non-const only entails giving
 * up the ability to invoke the function from a const context.
 * Casting from a non-const to a const signature is potentially dangerous,
 * as it means that a function that may change its inner state when invoked
 * is made possible to call from a const context. Therefore this cast does
 * not happen implicitly. The function `folly::constCastFunction` can
 * be used to perform the cast.
 *
 *     // Mutable lambda: can only be stored in a non-const folly::Function:
 *     int number = 0;
 *     folly::Function<void()> print_number =
 *       [number] () mutable { std::cout << ++number << std::endl; };
 *
 *     // const-cast to a const folly::Function:
 *     folly::Function<void() const> print_number_const =
 *       constCastFunction(std::move(print_number));
 *
 * When to use const function types?
 * Generally, only when you need them. When you use a `folly::Function` as a
 * member of a struct or class, only use a const function signature when you
 * need to invoke the function from const context.
 * When passing a `folly::Function` to a function, the function should accept
 * a non-const `folly::Function` whenever possible, i.e. when it does not
 * need to pass on or store a const `folly::Function`. This is the least
 * possible constraint: you can always pass a const `folly::Function` when
 * the function accepts a non-const one.
 *
 * How does the const behaviour compare to `std::function`?
 * `std::function` can wrap object with non-const invocation behaviour but
 * exposes them as const. The equivalent behaviour can be achieved with
 * `folly::Function` like so:
 *
 *     std::function<void(void)> stdfunc = someCallable;
 *
 *     folly::Function<void(void) const> uniqfunc = constCastFunction(
 *       folly::Function<void(void)>(someCallable)
 *     );
 *
 * You need to wrap the callable first in a non-const `folly::Function` to
 * select a non-const invoke operator (or the const one if no non-const one is
 * present), and then move it into a const `folly::Function` using
 * `constCastFunction`.
 * The name of `constCastFunction` should warn you that something
 * potentially dangerous is happening. As a matter of fact, using
 * `std::function` always involves this potentially dangerous aspect, which
 * is why it is not considered fully const-safe or even const-correct.
 * However, in most of the cases you will not need the dangerous aspect at all.
 * Either you do not require invocation of the function from a const context,
 * in which case you do not need to use `constCastFunction` and just
 * use the inner `folly::Function` in the example above, i.e. just use a
 * non-const `folly::Function`. Or, you may need invocation from const, but
 * the callable you are wrapping does not mutate its state (e.g. it is a class
 * object and implements `operator() const`, or it is a normal,
 * non-mutable lambda), in which case you can wrap the callable in a const
 * `folly::Function` directly, without using `constCastFunction`.
 * Only if you require invocation from a const context of a callable that
 * may mutate itself when invoked you have to go through the above procedure.
 * However, in that case what you do is potentially dangerous and requires
 * the equivalent of a `const_cast`, hence you need to call
 * `constCastFunction`.
 */

#pragma once

#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include <folly/CppAttributes.h>
#include <folly/Portability.h>
#include <folly/Traits.h>
#include <folly/functional/Invoke.h>
#include <folly/lang/Align.h>
#include <folly/lang/Exception.h>

namespace folly {

template <typename FunctionType>
class Function;

template <typename ReturnType, typename... Args>
Function<ReturnType(Args...) const> constCastFunction(
    Function<ReturnType(Args...)>&&) noexcept;

#if FOLLY_HAVE_NOEXCEPT_FUNCTION_TYPE
template <typename ReturnType, typename... Args>
Function<ReturnType(Args...) const noexcept> constCastFunction(
    Function<ReturnType(Args...) noexcept>&&) noexcept;
#endif

namespace detail {
namespace function {

enum class Op { MOVE, NUKE, HEAP };

union Data {
  Data() {}
  void* big;
  std::aligned_storage<6 * sizeof(void*)>::type tiny;
};

struct CoerceTag {};

template <typename T>
using FunctionNullptrTest =
    decltype(static_cast<bool>(static_cast<T const&>(T(nullptr)) == nullptr));

template <
    typename T,
    std::enable_if_t<!is_detected_v<FunctionNullptrTest, T>, int> = 0>
constexpr bool isEmptyFunction(T const&) {
  return false;
}
template <
    typename T,
    std::enable_if_t<is_detected_v<FunctionNullptrTest, T>, int> = 0>
constexpr bool isEmptyFunction(T const& t) {
  return static_cast<bool>(t == nullptr);
}

template <typename F, typename... Args>
using CallableResult = decltype(FOLLY_DECLVAL(F &&)(FOLLY_DECLVAL(Args &&)...));

template <
    typename From,
    typename To,
    typename = typename std::enable_if<
        !std::is_reference<To>::value || std::is_reference<From>::value>::type>
using IfSafeResultImpl = decltype(void(static_cast<To>(FOLLY_DECLVAL(From))));

#if defined(_MSC_VER)
//  Need a workaround for MSVC to avoid the inscrutable error:
//
//      folly\function.h(...) : fatal error C1001: An internal error has
//          occurred in the compiler.
//      (compiler file 'f:\dd\vctools\compiler\utc\src\p2\main.c', line 258)
//      To work around this problem, try simplifying or changing the program
//          near the locations listed above.
template <typename T>
using CallArg = T&&;
#else
template <typename T>
using CallArg = conditional_t<is_register_pass_v<T>, T, T&&>;
#endif

template <typename F, typename R, typename... A>
class FunctionTraitsSharedProxy {
  std::shared_ptr<Function<F>> sp_;

 public:
  explicit FunctionTraitsSharedProxy(std::nullptr_t) noexcept {}
  explicit FunctionTraitsSharedProxy(Function<F>&& func)
      : sp_(func ? std::make_shared<Function<F>>(std::move(func))
                 : std::shared_ptr<Function<F>>()) {}
  R operator()(A&&... args) const {
    if (!sp_) {
      throw_exception<std::bad_function_call>();
    }
    return (*sp_)(static_cast<A&&>(args)...);
  }

  explicit operator bool() const noexcept { return sp_ != nullptr; }

  friend bool operator==(
      FunctionTraitsSharedProxy<F, R, A...> const& proxy,
      std::nullptr_t) noexcept {
    return proxy.sp_ == nullptr;
  }
  friend bool operator!=(
      FunctionTraitsSharedProxy<F, R, A...> const& proxy,
      std::nullptr_t) noexcept {
    return proxy.sp_ != nullptr;
  }

  friend bool operator==(
      std::nullptr_t,
      FunctionTraitsSharedProxy<F, R, A...> const& proxy) noexcept {
    return proxy.sp_ == nullptr;
  }
  friend bool operator!=(
      std::nullptr_t,
      FunctionTraitsSharedProxy<F, R, A...> const& proxy) noexcept {
    return proxy.sp_ != nullptr;
  }
};

template <typename FunctionType>
struct FunctionTraits;

template <typename ReturnType, typename... Args>
struct FunctionTraits<ReturnType(Args...)> {
  using Call = ReturnType (*)(CallArg<Args>..., Data&);
  using ConstSignature = ReturnType(Args...) const;
  using NonConstSignature = ReturnType(Args...);
  using OtherSignature = ConstSignature;

  template <typename F, typename R = CallableResult<F&, Args...>>
  using IfSafeResult = IfSafeResultImpl<R, ReturnType>;

  template <typename Fun>
  static ReturnType callSmall(CallArg<Args>... args, Data& p) {
    auto& fn = *static_cast<Fun*>(static_cast<void*>(&p.tiny));
#if __cpp_if_constexpr >= 201606L
    if constexpr (std::is_void<ReturnType>::value) {
      fn(static_cast<Args&&>(args)...);
    } else {
      return fn(static_cast<Args&&>(args)...);
    }
#else
    return static_cast<ReturnType>(fn(static_cast<Args&&>(args)...));
#endif
  }

  template <typename Fun>
  static ReturnType callBig(CallArg<Args>... args, Data& p) {
    auto& fn = *static_cast<Fun*>(p.big);
#if __cpp_if_constexpr >= 201606L
    if constexpr (std::is_void<ReturnType>::value) {
      fn(static_cast<Args&&>(args)...);
    } else {
      return fn(static_cast<Args&&>(args)...);
    }
#else
    return static_cast<ReturnType>(fn(static_cast<Args&&>(args)...));
#endif
  }

  static ReturnType uninitCall(CallArg<Args>..., Data&) {
    throw_exception<std::bad_function_call>();
  }

  ReturnType operator()(Args... args) {
    auto& fn = *static_cast<Function<NonConstSignature>*>(this);
    return fn.call_(static_cast<Args&&>(args)..., fn.data_);
  }

  using SharedProxy =
      FunctionTraitsSharedProxy<NonConstSignature, ReturnType, Args...>;
};

template <typename ReturnType, typename... Args>
struct FunctionTraits<ReturnType(Args...) const> {
  using Call = ReturnType (*)(CallArg<Args>..., Data&);
  using ConstSignature = ReturnType(Args...) const;
  using NonConstSignature = ReturnType(Args...);
  using OtherSignature = NonConstSignature;

  template <typename F, typename R = CallableResult<const F&, Args...>>
  using IfSafeResult = IfSafeResultImpl<R, ReturnType>;

  template <typename Fun>
  static ReturnType callSmall(CallArg<Args>... args, Data& p) {
    auto& fn = *static_cast<const Fun*>(static_cast<void*>(&p.tiny));
#if __cpp_if_constexpr >= 201606L
    if constexpr (std::is_void<ReturnType>::value) {
      fn(static_cast<Args&&>(args)...);
    } else {
      return fn(static_cast<Args&&>(args)...);
    }
#else
    return static_cast<ReturnType>(fn(static_cast<Args&&>(args)...));
#endif
  }

  template <typename Fun>
  static ReturnType callBig(CallArg<Args>... args, Data& p) {
    auto& fn = *static_cast<const Fun*>(p.big);
#if __cpp_if_constexpr >= 201606L
    if constexpr (std::is_void<ReturnType>::value) {
      fn(static_cast<Args&&>(args)...);
    } else {
      return fn(static_cast<Args&&>(args)...);
    }
#else
    return static_cast<ReturnType>(fn(static_cast<Args&&>(args)...));
#endif
  }

  static ReturnType uninitCall(CallArg<Args>..., Data&) {
    throw_exception<std::bad_function_call>();
  }

  ReturnType operator()(Args... args) const {
    auto& fn = *static_cast<const Function<ConstSignature>*>(this);
    return fn.call_(static_cast<Args&&>(args)..., fn.data_);
  }

  using SharedProxy =
      FunctionTraitsSharedProxy<ConstSignature, ReturnType, Args...>;
};

#if FOLLY_HAVE_NOEXCEPT_FUNCTION_TYPE
template <typename ReturnType, typename... Args>
struct FunctionTraits<ReturnType(Args...) noexcept> {
  using Call = ReturnType (*)(CallArg<Args>..., Data&) noexcept;
  using ConstSignature = ReturnType(Args...) const noexcept;
  using NonConstSignature = ReturnType(Args...) noexcept;
  using OtherSignature = ConstSignature;

  template <typename F, typename R = CallableResult<F&, Args...>>
  using IfSafeResult = IfSafeResultImpl<R, ReturnType>;

  template <typename Fun>
  static ReturnType callSmall(CallArg<Args>... args, Data& p) noexcept {
    auto& fn = *static_cast<Fun*>(static_cast<void*>(&p.tiny));
#if __cpp_if_constexpr >= 201606L
    if constexpr (std::is_void<ReturnType>::value) {
      fn(static_cast<Args&&>(args)...);
    } else {
      return fn(static_cast<Args&&>(args)...);
    }
#else
    return static_cast<ReturnType>(fn(static_cast<Args&&>(args)...));
#endif
  }

  template <typename Fun>
  static ReturnType callBig(CallArg<Args>... args, Data& p) noexcept {
    auto& fn = *static_cast<Fun*>(p.big);
#if __cpp_if_constexpr >= 201606L
    if constexpr (std::is_void<ReturnType>::value) {
      fn(static_cast<Args&&>(args)...);
    } else {
      return fn(static_cast<Args&&>(args)...);
    }
#else
    return static_cast<ReturnType>(fn(static_cast<Args&&>(args)...));
#endif
  }

  static ReturnType uninitCall(CallArg<Args>..., Data&) noexcept {
    terminate_with<std::bad_function_call>();
  }

  ReturnType operator()(Args... args) noexcept {
    auto& fn = *static_cast<Function<NonConstSignature>*>(this);
    return fn.call_(static_cast<Args&&>(args)..., fn.data_);
  }

  using SharedProxy =
      FunctionTraitsSharedProxy<NonConstSignature, ReturnType, Args...>;
};

template <typename ReturnType, typename... Args>
struct FunctionTraits<ReturnType(Args...) const noexcept> {
  using Call = ReturnType (*)(CallArg<Args>..., Data&) noexcept;
  using ConstSignature = ReturnType(Args...) const noexcept;
  using NonConstSignature = ReturnType(Args...) noexcept;
  using OtherSignature = NonConstSignature;

  template <typename F, typename R = CallableResult<const F&, Args...>>
  using IfSafeResult = IfSafeResultImpl<R, ReturnType>;

  template <typename Fun>
  static ReturnType callSmall(CallArg<Args>... args, Data& p) noexcept {
    auto& fn = *static_cast<const Fun*>(static_cast<void*>(&p.tiny));
#if __cpp_if_constexpr >= 201606L
    if constexpr (std::is_void<ReturnType>::value) {
      fn(static_cast<Args&&>(args)...);
    } else {
      return fn(static_cast<Args&&>(args)...);
    }
#else
    return static_cast<ReturnType>(fn(static_cast<Args&&>(args)...));
#endif
  }

  template <typename Fun>
  static ReturnType callBig(CallArg<Args>... args, Data& p) noexcept {
    auto& fn = *static_cast<const Fun*>(p.big);
#if __cpp_if_constexpr >= 201606L
    if constexpr (std::is_void<ReturnType>::value) {
      fn(static_cast<Args&&>(args)...);
    } else {
      return fn(static_cast<Args&&>(args)...);
    }
#else
    return static_cast<ReturnType>(fn(static_cast<Args&&>(args)...));
#endif
  }

  static ReturnType uninitCall(CallArg<Args>..., Data&) noexcept {
    terminate_with<std::bad_function_call>();
  }

  ReturnType operator()(Args... args) const noexcept {
    auto& fn = *static_cast<const Function<ConstSignature>*>(this);
    return fn.call_(static_cast<Args&&>(args)..., fn.data_);
  }

  using SharedProxy =
      FunctionTraitsSharedProxy<ConstSignature, ReturnType, Args...>;
};
#endif

// These are control functions. They type-erase the operations of move-
// construction, destruction, and conversion to bool.
//
// The interface operations are noexcept, so the implementations are as well.
// Having the implementations be noexcept in the type permits callers to omit
// exception-handling machinery.

// Portably aliasing the type pointer-to-noexcept-function is tricky. Compilers
// sometimes reject the straightforward approach with an error. Even with this
// technique, some compilers accept the program while discarding the exception
// specification. This is best-effort.
std::size_t exec_(Op, Data*, Data*) noexcept;
using Exec = decltype(&exec_);
#if __cpp_noexcept_function_type >= 201510L || FOLLY_CPLUSPLUS >= 201702
static_assert(noexcept(Exec(nullptr)(Op{}, nullptr, nullptr)), "");
#endif

// This is intentionally instantiated per size rather than per function in order
// to minimize the number of instantiations. It would be safe to minimize
// instantiations even more by simply having a single non-template function that
// copies sizeof(Data) bytes rather than only copying sizeof(Fun) bytes, but
// then for small function types it would be likely to cross cache lines without
// need. But it is only necessary to handle those sizes which are multiples of
// the alignof(Data), and to round up other sizes.
struct DispatchSmallTrivial {
  template <typename Fun, typename Base>
  static constexpr auto call = Base::template callSmall<Fun>;

  template <std::size_t Size>
  static std::size_t exec_(Op o, Data* src, Data* dst) noexcept {
    switch (o) {
      case Op::MOVE:
        std::memcpy(static_cast<void*>(dst), static_cast<void*>(src), Size);
        break;
      case Op::NUKE:
        break;
      case Op::HEAP:
        break;
    }
    return 0U;
  }
  template <std::size_t size, std::size_t adjust = alignof(Data) - 1>
  static constexpr std::size_t size_ = (size + adjust) & ~adjust;
  template <typename Fun>
  static constexpr auto exec = exec_<size_<sizeof(Fun)>>;
};

struct DispatchSmall {
  template <typename Fun, typename Base>
  static constexpr auto call = Base::template callSmall<Fun>;

  template <typename Fun>
  static std::size_t exec(Op o, Data* src, Data* dst) noexcept {
    switch (o) {
      case Op::MOVE:
        ::new (static_cast<void*>(&dst->tiny)) Fun(static_cast<Fun&&>(
            *static_cast<Fun*>(static_cast<void*>(&src->tiny))));
        FOLLY_FALLTHROUGH;
      case Op::NUKE:
        static_cast<Fun*>(static_cast<void*>(&src->tiny))->~Fun();
        break;
      case Op::HEAP:
        break;
    }
    return 0U;
  }
};

struct DispatchBig {
  template <typename Fun, typename Base>
  static constexpr auto call = Base::template callBig<Fun>;

  template <typename Fun>
  static std::size_t exec(Op o, Data* src, Data* dst) noexcept {
    switch (o) {
      case Op::MOVE:
        dst->big = src->big;
        src->big = nullptr;
        break;
      case Op::NUKE:
        delete static_cast<Fun*>(src->big);
        break;
      case Op::HEAP:
        break;
    }
    return sizeof(Fun);
  }
};

} // namespace function
} // namespace detail

template <typename FunctionType>
class Function final : private detail::function::FunctionTraits<FunctionType> {
  // These utility types are defined outside of the template to reduce
  // the number of instantiations, and then imported in the class
  // namespace for convenience.
  using Data = detail::function::Data;
  using Op = detail::function::Op;
  using CoerceTag = detail::function::CoerceTag;

  using Traits = detail::function::FunctionTraits<FunctionType>;
  using Call = typename Traits::Call;
  using Exec = detail::function::Exec;

  // The `data_` member is mutable to allow `constCastFunction` to work without
  // invoking undefined behavior. Const-correctness is only violated when
  // `FunctionType` is a const function type (e.g., `int() const`) and `*this`
  // is the result of calling `constCastFunction`.
  mutable Data data_{};
  Call call_{&Traits::uninitCall};
  Exec exec_{nullptr};

  std::size_t exec(Op o, Data* src, Data* dst) const {
    if (!exec_) {
      return 0U;
    }
    return exec_(o, src, dst);
  }

  friend Traits;
  friend Function<typename Traits::ConstSignature> folly::constCastFunction<>(
      Function<typename Traits::NonConstSignature>&&) noexcept;
  friend class Function<typename Traits::OtherSignature>;

  template <typename Signature>
  Function(Function<Signature>&& fun, CoerceTag) {
    using Fun = Function<Signature>;
    if (fun) {
      data_.big = new Fun(static_cast<Fun&&>(fun));
      call_ = Traits::template callBig<Fun>;
      exec_ = Exec(detail::function::DispatchBig::exec<Fun>);
    }
  }

  Function(Function<typename Traits::OtherSignature>&& that, CoerceTag) noexcept
      : call_(that.call_), exec_(that.exec_) {
    that.call_ = &Traits::uninitCall;
    that.exec_ = nullptr;
    exec(Op::MOVE, &that.data_, &data_);
  }

 public:
  /**
   * Default constructor. Constructs an empty Function.
   */
  Function() = default;

  // not copyable
  Function(const Function&) = delete;

#ifdef __OBJC__
  // Make sure Objective C blocks are copied
  template <class ReturnType, class... Args>
  /*implicit*/ Function(ReturnType (^objCBlock)(Args... args))
      : Function([blockCopy = (ReturnType(^)(Args...))[objCBlock copy]](
                     Args... args) { return blockCopy(args...); }){};
#endif

  /**
   * Move constructor
   */
  Function(Function&& that) noexcept : call_(that.call_), exec_(that.exec_) {
    // that must be uninitialized before exec() call in the case of self move
    that.call_ = &Traits::uninitCall;
    that.exec_ = nullptr;
    exec(Op::MOVE, &that.data_, &data_);
  }

  /**
   * Constructs an empty `Function`.
   */
  /* implicit */ Function(std::nullptr_t) noexcept {}

  /**
   * Constructs a new `Function` from any callable object that is _not_ a
   * `folly::Function`. This handles function pointers, pointers to static
   * member functions, `std::reference_wrapper` objects, `std::function`
   * objects, and arbitrary objects that implement `operator()` if the parameter
   * signature matches (i.e. it returns an object convertible to `R` when called
   * with `Args...`).
   *
   * \note `typename Traits::template IfSafeResult<Fun>` prevents this overload
   * from being selected by overload resolution when `fun` is not a compatible
   * function.
   *
   * \note The noexcept requires some explanation. `IsSmall` is true when the
   * decayed type fits within the internal buffer and is noexcept-movable. But
   * this ctor might copy, not move. What we need here, if this ctor does a
   * copy, is that this ctor be noexcept when the copy is noexcept. That is not
   * checked in `IsSmall`, and shouldn't be, because once the `Function` is
   * constructed, the contained object is never copied. This check is for this
   * ctor only, in the case that this ctor does a copy.
   */
  template <
      typename Fun,
      typename =
          std::enable_if_t<!detail::is_similar_instantiation_v<Function, Fun>>,
      typename = typename Traits::template IfSafeResult<Fun>,
      bool IsSmall = sizeof(Fun) <=
          sizeof(Data::tiny) && noexcept(Fun(FOLLY_DECLVAL(Fun)))>
  /* implicit */ Function(Fun fun) noexcept(
      IsSmall&& noexcept(Fun(static_cast<Fun&&>(fun)))) {
    using Dispatch = conditional_t<
        IsSmall && is_trivially_copyable_v<Fun>,
        detail::function::DispatchSmallTrivial,
        conditional_t<
            IsSmall,
            detail::function::DispatchSmall,
            detail::function::DispatchBig>>;
    if (detail::function::isEmptyFunction(fun)) {
      return;
    }
    if FOLLY_CXX17_CONSTEXPR (IsSmall) {
      ::new (&data_.tiny) Fun(static_cast<Fun&&>(fun));
    } else {
      data_.big = new Fun(static_cast<Fun&&>(fun));
    }
    call_ = Dispatch::template call<Fun, Traits>;
    exec_ = Exec(Dispatch::template exec<Fun>);
  }

  /**
   * For move-constructing from a `folly::Function<X(Ys...) [const?]>`.
   * For a `Function` with a `const` function type, the object must be
   * callable from a `const`-reference, i.e. implement `operator() const`.
   * For a `Function` with a non-`const` function type, the object will
   * be called from a non-const reference, which means that it will execute
   * a non-const `operator()` if it is defined, and falls back to
   * `operator() const` otherwise.
   */
  template <
      typename Signature,
      typename Fun = Function<Signature>,
      // prevent gcc from making this a better match than move-ctor
      typename = std::enable_if_t<!std::is_same<Function, Fun>::value>,
      typename = typename Traits::template IfSafeResult<Fun>>
  Function(Function<Signature>&& that) noexcept(
      noexcept(Function(std::move(that), CoerceTag{})))
      : Function(std::move(that), CoerceTag{}) {}

  /**
   * If `ptr` is null, constructs an empty `Function`. Otherwise,
   * this constructor is equivalent to `Function(std::mem_fn(ptr))`.
   */
  template <
      typename Member,
      typename Class,
      // Prevent this overload from being selected when `ptr` is not a
      // compatible member function pointer.
      typename = decltype(Function(std::mem_fn((Member Class::*)0)))>
  /* implicit */ Function(Member Class::*ptr) noexcept {
    if (ptr) {
      *this = std::mem_fn(ptr);
    }
  }

  ~Function() { exec(Op::NUKE, &data_, nullptr); }

  Function& operator=(const Function&) = delete;

#ifdef __OBJC__
  // Make sure Objective C blocks are copied
  template <class ReturnType, class... Args>
  /* implicit */ Function& operator=(ReturnType (^objCBlock)(Args... args)) {
    (*this) = [blockCopy = (ReturnType(^)(Args...))[objCBlock copy]](
                  Args... args) { return blockCopy(args...); };
    return *this;
  }
#endif

  /**
   * Move assignment operator
   *
   * \note Leaves `that` in a valid but unspecified state. If `&that == this`
   * then `*this` is left in a valid but unspecified state.
   */
  Function& operator=(Function&& that) noexcept {
    exec(Op::NUKE, &data_, nullptr);
    that.exec(Op::MOVE, &that.data_, &data_);
    exec_ = that.exec_;
    call_ = that.call_;
    that.exec_ = nullptr;
    that.call_ = &Traits::uninitCall;
    return *this;
  }

  /**
   * Assigns a callable object to this `Function`. If the operation fails,
   * `*this` is left unmodified.
   *
   * \note `typename = decltype(Function(FOLLY_DECLVAL(Fun&&)))` prevents this
   * overload from being selected by overload resolution when `fun` is not a
   * compatible function.
   */
  template <
      typename Fun,
      typename...,
      bool Nx = noexcept(Function(FOLLY_DECLVAL(Fun&&)))>
  Function& operator=(Fun fun) noexcept(Nx) {
    // Doing this in place is more efficient when we can do so safely.
    if (Nx) {
      // Q: Why is is safe to destroy and reconstruct this object in place?
      // A: See the explanation in the move assignment operator.
      this->~Function();
      ::new (this) Function(static_cast<Fun&&>(fun));
    } else {
      // Construct a temporary and (nothrow) swap.
      Function(static_cast<Fun&&>(fun)).swap(*this);
    }
    return *this;
  }

  /**
   * For assigning from a `Function<X(Ys..) [const?]>`.
   */
  template <
      typename Signature,
      typename...,
      typename = typename Traits::template IfSafeResult<Function<Signature>>>
  Function& operator=(Function<Signature>&& that) noexcept(
      noexcept(Function(std::move(that)))) {
    return (*this = Function(std::move(that)));
  }

  /**
   * Clears this `Function`.
   */
  Function& operator=(std::nullptr_t) noexcept { return (*this = Function()); }

  /**
   * If `ptr` is null, clears this `Function`. Otherwise, this assignment
   * operator is equivalent to `*this = std::mem_fn(ptr)`.
   */
  template <typename Member, typename Class>
  auto operator=(Member Class::*ptr) noexcept
      // Prevent this overload from being selected when `ptr` is not a
      // compatible member function pointer.
      -> decltype(operator=(std::mem_fn(ptr))) {
    return ptr ? (*this = std::mem_fn(ptr)) : (*this = Function());
  }

  /**
   * Call the wrapped callable object with the specified arguments.
   */
  using Traits::operator();

  /**
   * Exchanges the callable objects of `*this` and `that`.
   */
  void swap(Function& that) noexcept { std::swap(*this, that); }

  /**
   * Returns `true` if this `Function` contains a callable, i.e. is
   * non-empty.
   */
  explicit operator bool() const noexcept { return exec_ != nullptr; }

  /**
   * Returns the size of the allocation made to store the callable on the
   * heap. If `0` is returned, there has been no additional memory
   * allocation because the callable is stored within the `Function` object.
   */
  std::size_t heapAllocatedMemory() const noexcept {
    return exec(Op::HEAP, nullptr, nullptr);
  }

  using typename Traits::SharedProxy;

  /**
   * Move this `Function` into a copyable callable object, of which all copies
   * share the state.
   */
  SharedProxy asSharedProxy() && { return SharedProxy{std::move(*this)}; }

  /**
   * Construct a `std::function` by moving in the contents of this `Function`.
   * Note that the returned `std::function` will share its state (i.e. captured
   * data) across all copies you make of it, so be very careful when copying.
   */
  std::function<typename Traits::NonConstSignature> asStdFunction() && {
    return std::move(*this).asSharedProxy();
  }
};

template <typename FunctionType>
void swap(Function<FunctionType>& lhs, Function<FunctionType>& rhs) noexcept {
  lhs.swap(rhs);
}

template <typename FunctionType>
bool operator==(const Function<FunctionType>& fn, std::nullptr_t) {
  return !fn;
}

template <typename FunctionType>
bool operator==(std::nullptr_t, const Function<FunctionType>& fn) {
  return !fn;
}

template <typename FunctionType>
bool operator!=(const Function<FunctionType>& fn, std::nullptr_t) {
  return !(fn == nullptr);
}

template <typename FunctionType>
bool operator!=(std::nullptr_t, const Function<FunctionType>& fn) {
  return !(nullptr == fn);
}

/**
 * NOTE: See detailed note about `constCastFunction` at the top of the file.
 * This is potentially dangerous and requires the equivalent of a `const_cast`.
 */
template <typename ReturnType, typename... Args>
Function<ReturnType(Args...) const> constCastFunction(
    Function<ReturnType(Args...)>&& that) noexcept {
  return Function<ReturnType(Args...) const>{
      std::move(that), detail::function::CoerceTag{}};
}

template <typename ReturnType, typename... Args>
Function<ReturnType(Args...) const> constCastFunction(
    Function<ReturnType(Args...) const>&& that) noexcept {
  return std::move(that);
}

#if FOLLY_HAVE_NOEXCEPT_FUNCTION_TYPE
template <typename ReturnType, typename... Args>
Function<ReturnType(Args...) const noexcept> constCastFunction(
    Function<ReturnType(Args...) noexcept>&& that) noexcept {
  return Function<ReturnType(Args...) const noexcept>{
      std::move(that), detail::function::CoerceTag{}};
}

template <typename ReturnType, typename... Args>
Function<ReturnType(Args...) const noexcept> constCastFunction(
    Function<ReturnType(Args...) const noexcept>&& that) noexcept {
  return std::move(that);
}
#endif

/**
 * @class FunctionRef
 *
 * @brief A reference wrapper for callable objects
 *
 * FunctionRef is similar to std::reference_wrapper, but the template parameter
 * is the function signature type rather than the type of the referenced object.
 * A folly::FunctionRef is cheap to construct as it contains only a pointer to
 * the referenced callable and a pointer to a function which invokes the
 * callable.
 *
 * The user of FunctionRef must be aware of the reference semantics: storing a
 * copy of a FunctionRef is potentially dangerous and should be avoided unless
 * the referenced object definitely outlives the FunctionRef object. Thus any
 * function that accepts a FunctionRef parameter should only use it to invoke
 * the referenced function and not store a copy of it. Knowing that FunctionRef
 * itself has reference semantics, it is generally okay to use it to reference
 * lambdas that capture by reference.
 */

template <typename FunctionType>
class FunctionRef;

template <typename ReturnType, typename... Args>
class FunctionRef<ReturnType(Args...)> final {
  template <typename Arg>
  using CallArg = detail::function::CallArg<Arg>;

  using Call = ReturnType (*)(CallArg<Args>..., void*);

  static ReturnType uninitCall(CallArg<Args>..., void*) {
    throw_exception<std::bad_function_call>();
  }

  template <
      typename Fun,
      std::enable_if_t<!std::is_pointer<Fun>::value, int> = 0>
  static ReturnType call(CallArg<Args>... args, void* object) {
    using Pointer = std::add_pointer_t<Fun>;
    return static_cast<ReturnType>(invoke(
        static_cast<Fun&&>(*static_cast<Pointer>(object)),
        static_cast<Args&&>(args)...));
  }
  template <
      typename Fun,
      std::enable_if_t<std::is_pointer<Fun>::value, int> = 0>
  static ReturnType call(CallArg<Args>... args, void* object) {
    return static_cast<ReturnType>(
        invoke(reinterpret_cast<Fun>(object), static_cast<Args&&>(args)...));
  }

  void* object_{nullptr};
  Call call_{&FunctionRef::uninitCall};

 public:
  /**
   * Default constructor. Constructs an empty FunctionRef.
   *
   * Invoking it will throw std::bad_function_call.
   */
  constexpr FunctionRef() = default;

  /**
   * Like default constructor. Constructs an empty FunctionRef.
   *
   * Invoking it will throw std::bad_function_call.
   */
  constexpr explicit FunctionRef(std::nullptr_t) noexcept {}

  /**
   * Construct a FunctionRef from a reference to a callable object. If the
   * callable is considered to be an empty callable, the FunctionRef will be
   * empty.
   */
  template <
      typename Fun,
      std::enable_if_t<
          Conjunction<
              Negation<std::is_same<FunctionRef, std::decay_t<Fun>>>,
              is_invocable_r<ReturnType, Fun&&, Args&&...>>::value,
          int> = 0>
  constexpr /* implicit */ FunctionRef(Fun&& fun) noexcept {
    // `Fun` may be a const type, in which case we have to do a const_cast
    // to store the address in a `void*`. This is safe because the `void*`
    // will be cast back to `Fun*` (which is a const pointer whenever `Fun`
    // is a const type) inside `FunctionRef::call`
    auto& ref = fun; // work around forwarding lint advice
    if (!detail::function::isEmptyFunction(ref)) {
      auto ptr = std::addressof(ref);
      object_ = const_cast<void*>(static_cast<void const*>(ptr));
      call_ = &FunctionRef::template call<Fun>;
    }
  }

  /**
   * Constructs a FunctionRef from a pointer to a function. If the
   * pointer is nullptr, the FunctionRef will be empty.
   */
  template <
      typename Fun,
      std::enable_if_t<std::is_function<Fun>::value, int> = 0,
      std::enable_if_t<is_invocable_r_v<ReturnType, Fun&, Args&&...>, int> = 0>
  constexpr /* implicit */ FunctionRef(Fun* fun) noexcept {
    if (fun) {
      object_ = const_cast<void*>(reinterpret_cast<void const*>(fun));
      call_ = &FunctionRef::template call<Fun*>;
    }
  }

  ReturnType operator()(Args... args) const {
    return call_(static_cast<Args&&>(args)..., object_);
  }

  constexpr explicit operator bool() const noexcept { return object_; }

  constexpr friend bool operator==(
      FunctionRef<ReturnType(Args...)> ref, std::nullptr_t) noexcept {
    return ref.object_ == nullptr;
  }
  constexpr friend bool operator!=(
      FunctionRef<ReturnType(Args...)> ref, std::nullptr_t) noexcept {
    return ref.object_ != nullptr;
  }

  constexpr friend bool operator==(
      std::nullptr_t, FunctionRef<ReturnType(Args...)> ref) noexcept {
    return ref.object_ == nullptr;
  }
  constexpr friend bool operator!=(
      std::nullptr_t, FunctionRef<ReturnType(Args...)> ref) noexcept {
    return ref.object_ != nullptr;
  }
};

} // namespace folly
