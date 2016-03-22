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

#pragma once

// included by Function.h, do not include directly.

#include <memory>

namespace folly {

namespace detail {
namespace function {

struct SelectConstFunctionTag {
  template <typename T>
  using QualifiedPointer = T const*;
};
struct SelectNonConstFunctionTag {
  template <typename T>
  using QualifiedPointer = T*;
};

// Helper class to extract properties from a function type
template <typename T>
struct FunctionTypeTraits;

// FunctionTypeTraits default implementation - this only exists to suppress
// very long compiler errors when Function is tried to be instantiated
// with an unsuitable type
template <typename T>
struct FunctionTypeTraits {
  using SuitableForFunction = std::false_type;

  // The following definitions are here only to suppress long and misleading
  // compiler errors.
  using ResultType = void;
  using ArgsTuple = int;
  using ArgsRefTuple = int;
  using NonConstFunctionType = void;
  using ConstFunctionType = int;

  template <typename X>
  class InvokeOperator {};
  class ExecutorMixin {};
};

// FunctionTypeTraits for non-const function types
template <typename R, typename... Args>
struct FunctionTypeTraits<R(Args...)> {
  using SuitableForFunction = std::true_type;
  using ResultType = R;
  using ArgsTuple = std::tuple<Args...>;
  using ArgsRefTuple = std::tuple<Args&&...>;
  using NonConstFunctionType = R(Args...);
  using ConstFunctionType = R(Args...) const;
  using IsConst = std::false_type;
  using DefaultSelectFunctionTag = SelectNonConstFunctionTag;
  template <typename F>
  using IsCallable =
      std::is_convertible<typename std::result_of<F&(Args...)>::type, R>;
  template <typename T>
  using QualifiedPointer = T*;
  template <typename Obj>
  using InvokeFunctionPtr = R (*)(Obj*, Args&&...);

  // Function inherits from InvokeOperator<Function>. This is
  // where Function's operator() is defined.
  template <typename FunctionType>
  class InvokeOperator {
   public:
    /**
     * Invokes the stored callable via the invokePtr stored in the Executor.
     *
     * Throws std::bad_function_call if @c *this is empty.
     */
    ResultType operator()(Args... args) {
      auto executor =
          static_cast<FunctionType*>(this)
              ->template access<typename FunctionType::ExecutorIf>();
      return executor->invokePtr(executor, std::forward<Args>(args)...);
    }
  };

  class ExecutorMixin;
};

// FunctionTypeTraits for const function types
template <typename R, typename... Args>
struct FunctionTypeTraits<R(Args...) const> {
  using SuitableForFunction = std::true_type;
  using ResultType = R;
  using ArgsTuple = std::tuple<Args...>;
  using ArgsRefTuple = std::tuple<Args&&...>;
  using NonConstFunctionType = R(Args...);
  using ConstFunctionType = R(Args...) const;
  using IsConst = std::true_type;
  using DefaultSelectFunctionTag = SelectConstFunctionTag;
  template <typename F>
  using IsCallable =
      std::is_convertible<typename std::result_of<F const&(Args...)>::type, R>;
  template <typename T>
  using QualifiedPointer = T const*;
  template <typename Obj>
  using InvokeFunctionPtr = R (*)(Obj const*, Args&&...);

  // Function inherits from InvokeOperator<Function>. This is
  // where Function's operator() is defined.
  template <typename FunctionType>
  class InvokeOperator {
   public:
    /**
     * Invokes the stored callable via the invokePtr stored in the Executor.
     *
     * Throws std::bad_function_call if @c *this is empty.
     */
    ResultType operator()(Args... args) const {
      auto executor =
          static_cast<FunctionType const*>(this)
              ->template access<typename FunctionType::ExecutorIf>();
      return executor->invokePtr(executor, std::forward<Args>(args)...);
    }
  };

  class ExecutorMixin;
};

// Helper template for checking if a type is a Function
template <typename T>
struct IsFunction : public std::false_type {};

template <typename FunctionType, FunctionMoveCtor NTM, size_t EmbedFunctorSize>
struct IsFunction<::folly::Function<FunctionType, NTM, EmbedFunctorSize>>
    : public std::true_type {};

// Helper template to check if a functor can be called with arguments of type
// Args..., if it returns a type convertible to R, and also is not a
// Function.
// Function objects can constructed or assigned from types for which
// IsCallableHelper is true_type.
template <typename FunctionType>
struct IsCallableHelper {
  using Traits = FunctionTypeTraits<FunctionType>;

  template <typename F>
  static std::integral_constant<bool, Traits::template IsCallable<F>::value>
  test(int);
  template <typename F>
  static std::false_type test(...);
};

template <typename F, typename FunctionType>
struct IsCallable : public std::integral_constant<
                        bool,
                        (!IsFunction<typename std::decay<F>::type>::value &&
                         decltype(IsCallableHelper<FunctionType>::template test<
                                  typename std::decay<F>::type>(0))::value)> {};

// MaybeUnaryOrBinaryFunction: helper template class for deriving
// Function from std::unary_function or std::binary_function
template <typename R, typename ArgsTuple>
struct MaybeUnaryOrBinaryFunctionImpl {
  using result_type = R;
};

template <typename R, typename Arg>
struct MaybeUnaryOrBinaryFunctionImpl<R, std::tuple<Arg>>
    : public std::unary_function<Arg, R> {};

template <typename R, typename Arg1, typename Arg2>
struct MaybeUnaryOrBinaryFunctionImpl<R, std::tuple<Arg1, Arg2>>
    : public std::binary_function<Arg1, Arg2, R> {};

template <typename FunctionType>
using MaybeUnaryOrBinaryFunction = MaybeUnaryOrBinaryFunctionImpl<
    typename FunctionTypeTraits<FunctionType>::ResultType,
    typename FunctionTypeTraits<FunctionType>::ArgsTuple>;

// Invoke helper
template <typename F, typename... Args>
inline auto invoke(F&& f, Args&&... args)
    -> decltype(std::forward<F>(f)(std::forward<Args>(args)...)) {
  return std::forward<F>(f)(std::forward<Args>(args)...);
}

template <typename M, typename C, typename... Args>
inline auto invoke(M(C::*d), Args&&... args)
    -> decltype(std::mem_fn(d)(std::forward<Args>(args)...)) {
  return std::mem_fn(d)(std::forward<Args>(args)...);
}

// Executors helper class
template <typename FunctionType>
struct Executors {
  class ExecutorIf;
  class EmptyExecutor;
  template <class F, class SelectFunctionTag>
  class FunctorPtrExecutor;
  template <class F, class SelectFunctionTag>
  class FunctorExecutor;

  using Traits = FunctionTypeTraits<FunctionType>;
  using NonConstFunctionExecutors =
      Executors<typename Traits::NonConstFunctionType>;
  using ConstFunctionExecutors = Executors<typename Traits::ConstFunctionType>;
  using InvokeFunctionPtr = typename Traits::template InvokeFunctionPtr<
      Executors<FunctionType>::ExecutorIf>;
};

template <typename R, typename... Args>
class FunctionTypeTraits<R(Args...)>::ExecutorMixin {
 public:
  using ExecutorIf = typename Executors<R(Args...)>::ExecutorIf;
  using InvokeFunctionPtr = typename Executors<R(Args...)>::InvokeFunctionPtr;

  ExecutorMixin(InvokeFunctionPtr invoke_ptr) : invokePtr(invoke_ptr) {}
  virtual ~ExecutorMixin() {}

  template <typename F>
  static F* selectFunctionHelper(F* f, SelectNonConstFunctionTag) {
    return f;
  }

  template <typename F>
  static F const* selectFunctionHelper(F* f, SelectConstFunctionTag) {
    return f;
  }

  static R invokeEmpty(ExecutorIf*, Args&&...) {
    throw std::bad_function_call();
  }

  template <typename Ex>
  static R invokeFunctor(ExecutorIf* executor, Args&&... args) {
    return folly::detail::function::invoke(
        *Ex::getFunctor(executor), std::forward<Args>(args)...);
  }

  // invokePtr is of type
  //   ReturnType (*)(ExecutorIf*, Args&&...)
  // and it will be set to the address of one of the static functions above
  // (invokeEmpty or invokeFunctor), which will invoke the stored callable
  InvokeFunctionPtr const invokePtr;
};

template <typename R, typename... Args>
class FunctionTypeTraits<R(Args...) const>::ExecutorMixin {
 public:
  using ExecutorIf = typename Executors<R(Args...) const>::ExecutorIf;
  using InvokeFunctionPtr =
      typename Executors<R(Args...) const>::InvokeFunctionPtr;

  ExecutorMixin(InvokeFunctionPtr invoke_ptr) : invokePtr(invoke_ptr) {}
  virtual ~ExecutorMixin() {}

  template <typename F>
  static F* selectFunctionHelper(F const* f, SelectNonConstFunctionTag) {
    return const_cast<F*>(f);
  }

  template <typename F>
  static F const* selectFunctionHelper(F const* f, SelectConstFunctionTag) {
    return f;
  }

  static R invokeEmpty(ExecutorIf const*, Args&&...) {
    throw std::bad_function_call();
  }

  template <typename Ex>
  static R invokeFunctor(ExecutorIf const* executor, Args&&... args) {
    return folly::detail::function::invoke(
        *Ex::getFunctor(executor), std::forward<Args>(args)...);
  }

  // invokePtr is of type
  //   ReturnType (*)(ExecutorIf*, Args&&...)
  // and it will be set to the address of one of the static functions above
  // (invokeEmpty or invokeFunctor), which will invoke the stored callable
  InvokeFunctionPtr const invokePtr;
};

} // namespace function
} // namespace detail
} // namespace folly
