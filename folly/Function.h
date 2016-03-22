/*
 * Copyright 2016 Facebook, Inc.
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
 *     int operator(int);
 *     int operator(int) const;
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
 * The first (and usually only specified) template parameter of
 * `folly::Function`, the `FunctionType`, can be const-qualified. Be aware
 * that the const is part of the function signature. It does not mean that
 * the function type is a const type.
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
 * not happen implicitly. The function `folly::constCastfolly::Function` can
 * be used to perform the cast.
 *
 *     // Mutable lambda: can only be stored in a non-const folly::Function:
 *     int number = 0;
 *     folly::Function<void()> print_number =
 *       [number] () mutable { std::cout << ++number << std::endl; };
 *
 *     // const-cast to a const folly::Function:
 *     folly::Function<void() const> print_number_const =
 *       constCastfolly::Function(std::move(print_number));
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
 * `std::function` can wrap object with non-const invokation behaviour but
 * exposes them as const. The equivalent behaviour can be achieved with
 * `folly::Function` like so:
 *
 *     std::function<void(void)> stdfunc = someCallable;
 *
 *     folly::Function<void(void) const> uniqfunc = constCastfolly::Function(
 *       folly::Function<void(void)>(someCallable)
 *     );
 *
 * You need to wrap the callable first in a non-const `folly::Function` to
 * select a non-const invoke operator (or the const one if no non-const one is
 * present), and then move it into a const `folly::Function` using
 * `constCastfolly::Function`.
 * The name of `constCastfolly::Function` should warn you that something
 * potentially dangerous is happening. As a matter of fact, using
 * `std::function` always involves this potentially dangerous aspect, which
 * is why it is not considered fully const-safe or even const-correct.
 * However, in most of the cases you will not need the dangerous aspect at all.
 * Either you do not require invokation of the function from a const context,
 * in which case you do not need to use `constCastfolly::Function` and just
 * use the inner `folly::Function` in the example above, i.e. just use a
 * non-const `folly::Function`. Or, you may need invokation from const, but
 * the callable you are wrapping does not mutate its state (e.g. it is a class
 * object and implements `operator() const`, or it is a normal,
 * non-mutable lambda), in which case you can wrap the callable in a const
 * `folly::Function` directly, without using `constCastfolly::Function`.
 * Only if you require invokation from a const context of a callable that
 * may mutate itself when invoked you have to go through the above procedure.
 * However, in that case what you do is potentially dangerous and requires
 * the equivalent of a `const_cast`, hence you need to call
 * `constCastfolly::Function`.
 *
 * `folly::Function` also has two additional template paremeters:
 *   * `NTM`: if set to `folly::FunctionMoveCtor::NO_THROW`, the
 *     `folly::Function` object is guaranteed to be nothrow move constructible.
 *     The downside is that any function object that itself is
 *     not nothrow move constructible cannot be stored in-place in the
 *     `folly::Function` object and will be stored on the heap instead.
 *   * `EmbedFunctorSize`: a number of bytes that will be reserved in the
 *     `folly::Function` object to store callable objects in-place. If you
 *     wrap a callable object bigger than this in a `folly::Function` object,
 *     it will be stored on the heap and the `folly::Function` object will keep
 *     a `std::unique_ptr` to it.
 */

#pragma once

#include <functional>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include <folly/ScopeGuard.h>
#include <folly/portability/Constexpr.h>

namespace folly {

enum class FunctionMoveCtor { NO_THROW, MAY_THROW };

template <
    typename FunctionType,
    FunctionMoveCtor NTM = FunctionMoveCtor::NO_THROW,
    size_t EmbedFunctorSize = (NTM == FunctionMoveCtor::NO_THROW)
        ? sizeof(void (*)(void))
        : sizeof(std::function<void(void)>)>
class Function;

} // folly

// boring predeclarations and details
#include "Function-pre.h"

namespace folly {

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
class Function final
    : public detail::function::FunctionTypeTraits<FunctionType>::
          template InvokeOperator<
              Function<FunctionType, NTM, EmbedFunctorSize>>,
      public detail::function::MaybeUnaryOrBinaryFunction<FunctionType> {
 private:
  using Traits = detail::function::FunctionTypeTraits<FunctionType>;
  static_assert(
      Traits::SuitableForFunction::value,
      "Function<FunctionType>: FunctionType must be of the "
      "form 'R(Args...)' or 'R(Args...) const'");

  using ThisType = Function<FunctionType, NTM, EmbedFunctorSize>;
  using InvokeOperator = typename Traits::template InvokeOperator<ThisType>;

  static constexpr bool hasNoExceptMoveCtor() noexcept {
    return NTM == FunctionMoveCtor::NO_THROW;
  };

 public:
  // not copyable
  Function(Function const&) = delete;
  Function& operator=(Function const&) = delete;

  /**
   * Default constructor. Constructs an empty Function.
   */
  Function() noexcept {
    initializeEmptyExecutor();
  }

  ~Function() {
    destroyExecutor();

    static_assert(
        kStorageSize == sizeof(*this),
        "There is something wrong with the size of Function");
  }

  // construct/assign from Function
  /**
   * Move constructor
   */
  Function(Function&& other) noexcept(hasNoExceptMoveCtor());
  /**
   * Move assignment operator
   */
  Function& operator=(Function&& rhs) noexcept(hasNoExceptMoveCtor());

  /**
   * Constructs a `Function` by moving from one with different template
   * parameters with regards to const-ness, no-except-movability and internal
   * storage size.
   */
  template <
      typename OtherFunctionType,
      FunctionMoveCtor OtherNTM,
      size_t OtherEmbedFunctorSize>
  Function(Function<OtherFunctionType, OtherNTM, OtherEmbedFunctorSize>&& other)
  noexcept(
      OtherNTM == FunctionMoveCtor::NO_THROW &&
      EmbedFunctorSize >= OtherEmbedFunctorSize);

  /**
   * Moves a `Function` with different template parameters with regards
   * to const-ness, no-except-movability and internal storage size into this
   * one.
   */
  template <
      typename RhsFunctionType,
      FunctionMoveCtor RhsNTM,
      size_t RhsEmbedFunctorSize>
  Function& operator=(Function<RhsFunctionType, RhsNTM, RhsEmbedFunctorSize>&&
                          rhs) noexcept(RhsNTM == FunctionMoveCtor::NO_THROW);

  /**
   * Constructs an empty `Function`.
   */
  /* implicit */ Function(std::nullptr_t) noexcept : Function() {}

  /**
   * Clears this `Function`.
   */
  Function& operator=(std::nullptr_t) noexcept {
    destroyExecutor();
    initializeEmptyExecutor();
    return *this;
  }

  /**
   * Constructs a new `Function` from any callable object. This
   * handles function pointers, pointers to static member functions,
   * `std::reference_wrapper` objects, `std::function` objects, and arbitrary
   * objects that implement `operator()` if the parameter signature
   * matches (i.e. it returns R when called with Args...).
   * For a `Function` with a const function type, the object must be
   * callable from a const-reference, i.e. implement `operator() const`.
   * For a `Function` with a non-const function type, the object will
   * be called from a non-const reference, which means that it will execute
   * a non-const `operator()` if it is defined, and falls back to
   * `operator() const` otherwise
   */
  template <typename F>
  /* implicit */ Function(
      F&& f,
      typename std::enable_if<
          detail::function::IsCallable<F, FunctionType>::value>::type* =
          0) noexcept(noexcept(typename std::decay<F>::
                                   type(std::forward<F>(f)))) {
    createExecutor(std::forward<F>(f));
  }

  /**
   * Assigns a callable object to this `Function`.
   */
  template <typename F>
  typename std::enable_if<
      detail::function::IsCallable<F, FunctionType>::value,
      Function&>::type
  operator=(F&& f) noexcept(
      noexcept(typename std::decay<F>::type(std::forward<F>(f)))) {
    destroyExecutor();
    SCOPE_FAIL {
      initializeEmptyExecutor();
    };
    createExecutor(std::forward<F>(f));
    return *this;
  }

  /**
   * Exchanges the callable objects of `*this` and `other`. `other` can be
   * a Function with different settings with regard to
   * no-except-movability and internal storage size, but must match
   * `*this` with regards to return type and argument types.
   */
  template <FunctionMoveCtor OtherNTM, size_t OtherEmbedFunctorSize>
  void
  swap(Function<FunctionType, OtherNTM, OtherEmbedFunctorSize>& o) noexcept(
      hasNoExceptMoveCtor() && OtherNTM == FunctionMoveCtor::NO_THROW);

  /**
   * Returns `true` if this `Function` contains a callable, i.e. is
   * non-empty.
   */
  explicit operator bool() const noexcept;

  /**
   * Returns `true` if this `Function` stores the callable on the
   * heap. If `false` is returned, there has been no additional memory
   * allocation and the callable is stored inside the `Function`
   * object itself.
   */
  bool hasAllocatedMemory() const noexcept;

  /**
   * Returns the `type_info` (as returned by `typeid`) of the callable stored
   * in this `Function`. Returns `typeid(void)` if empty.
   */
  std::type_info const& target_type() const noexcept;

  /**
   * Returns a pointer to the stored callable if its type matches `T`, and
   * `nullptr` otherwise.
   */
  template <typename T>
  T* target() noexcept;

  /**
   * Returns a const-pointer to the stored callable if its type matches `T`,
   * and `nullptr` otherwise.
   */
  template <typename T>
  const T* target() const noexcept;

  /**
   * Move out this `Function` into one with a const function type.
   *
   * This is a potentially dangerous operation, equivalent to a `const_cast`.
   * This converts a `Function` with a non-const function type, i.e.
   * one that can only be called when in the form of a non-const reference,
   * into one that can be called in a const context. Use at your own risk!
   */
  Function<typename Traits::ConstFunctionType, NTM, EmbedFunctorSize>
      castToConstFunction() && noexcept(hasNoExceptMoveCtor());

  using SignatureType = FunctionType;
  using ResultType = typename Traits::ResultType;
  using ArgsTuple = typename Traits::ArgsTuple;

 private:
  template <class, FunctionMoveCtor, size_t>
  friend class Function;

  friend struct detail::function::FunctionTypeTraits<FunctionType>;

  using ExecutorIf =
      typename detail::function::Executors<FunctionType>::ExecutorIf;
  using EmptyExecutor =
      typename detail::function::Executors<FunctionType>::EmptyExecutor;
  template <typename F, typename SelectFunctionTag>
  using FunctorPtrExecutor = typename detail::function::Executors<
      FunctionType>::template FunctorPtrExecutor<F, SelectFunctionTag>;
  template <typename F, typename SelectFunctionTag>
  using FunctorExecutor = typename detail::function::Executors<
      FunctionType>::template FunctorExecutor<F, SelectFunctionTag>;

  template <typename T>
  T const* access() const;

  template <typename T>
  T* access();

  void initializeEmptyExecutor() noexcept;

  template <typename F>
  void createExecutor(F&& f) noexcept(
      noexcept(typename std::decay<F>::type(std::forward<F>(f))));

  void destroyExecutor() noexcept;

  struct MinStorageSize;

  typename std::aligned_storage<MinStorageSize::value>::type data_;
  static constexpr size_t kStorageSize = sizeof(data_);
};

// operator==
template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
inline bool operator==(
    Function<FunctionType, NTM, EmbedFunctorSize> const& f,
    std::nullptr_t) noexcept {
  return !f;
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
inline bool operator==(
    std::nullptr_t,
    Function<FunctionType, NTM, EmbedFunctorSize> const& f) noexcept {
  return !f;
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
inline bool operator!=(
    Function<FunctionType, NTM, EmbedFunctorSize> const& f,
    std::nullptr_t) noexcept {
  return !!f;
}

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
inline bool operator!=(
    std::nullptr_t,
    Function<FunctionType, NTM, EmbedFunctorSize> const& f) noexcept {
  return !!f;
}

/**
 * Cast a `Function` into one with a const function type.
 *
 * This is a potentially dangerous operation, equivalent to a `const_cast`.
 * This converts a `Function` with a non-const function type, i.e.
 * one that can only be called when in the form of a non-const reference,
 * into one that can be called in a const context. Use at your own risk!
 */
template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
Function<
    typename detail::function::FunctionTypeTraits<
        FunctionType>::ConstFunctionType,
    NTM,
    EmbedFunctorSize>
constCastFunction(Function<FunctionType, NTM, EmbedFunctorSize>&&
                      from) noexcept(NTM == FunctionMoveCtor::NO_THROW) {
  return std::move(from).castToConstFunction();
}

} // folly

namespace std {
template <typename FunctionType, bool NOM1, bool NOM2, size_t S1, size_t S2>
void swap(
    ::folly::Function<FunctionType, NOM1, S1>& lhs,
    ::folly::Function<FunctionType, NOM2, S2>& rhs) {
  lhs.swap(rhs);
}
} // std

#include "Function-inl.h"
