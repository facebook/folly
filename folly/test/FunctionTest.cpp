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

#include <cstdarg>

#include <folly/Function.h>

#include <folly/Memory.h>
#include <gtest/gtest.h>

using folly::FunctionMoveCtor;
using folly::Function;

namespace {
int func_int_int_add_25(int x) {
  return x + 25;
}
int func_int_int_add_111(int x) {
  return x + 111;
}
int func_int_return_987() {
  return 987;
}
float floatMult(float a, float b) {
  return a * b;
}

template <class T, size_t S>
struct Functor {
  std::array<T, S> data = {{0}};

  // Two operator() with different argument types.
  // The InvokeReference tests use both
  T const& operator()(size_t index) const {
    return data[index];
  }
  T operator()(size_t index, T const& value) {
    T oldvalue = data[index];
    data[index] = value;
    return oldvalue;
  }
};

// TEST =====================================================================
// NoExceptMovable

struct MoveMayThrow {
  bool doThrow{false};

  MoveMayThrow() = default;
  MoveMayThrow(MoveMayThrow const&) = default;
  MoveMayThrow& operator=(MoveMayThrow const&) = default;
  MoveMayThrow(MoveMayThrow&&) noexcept(false) {
    if (doThrow) {
      throw std::runtime_error("MoveMayThrow(MoveMayThrow&&)");
    }
  }
  MoveMayThrow& operator=(MoveMayThrow&&) noexcept(false) {
    if (doThrow) {
      throw std::runtime_error("MoveMayThrow::operator=(MoveMayThrow&&)");
    }
    return *this;
  }
};
}

TEST(Function, NoExceptMovable) {
  // callable_noexcept is noexcept-movable
  auto callable_noexcept = [](int x) { return x + 1; };
  EXPECT_TRUE(
      std::is_nothrow_move_constructible<decltype(callable_noexcept)>::value);

  // callable_throw may throw when moved
  MoveMayThrow mmt;
  auto callable_throw = [mmt](int x) { return x + 10; };
  EXPECT_FALSE(
      std::is_nothrow_move_constructible<decltype(callable_throw)>::value);

  // callable_noexcept can be stored in the Function object
  Function<int(int), FunctionMoveCtor::NO_THROW> func(callable_noexcept);
  EXPECT_EQ(func(42), 43);
  EXPECT_FALSE(func.hasAllocatedMemory());
  EXPECT_TRUE(std::is_nothrow_move_constructible<decltype(func)>::value);

  // callable_throw cannot be stored in the Function object,
  // because Function guarantees noexcept-movability, but
  // callable_throw may throw when moved
  Function<int(int), FunctionMoveCtor::NO_THROW> func_safe_move(callable_throw);
  EXPECT_EQ(func_safe_move(42), 52);
  EXPECT_TRUE(func_safe_move.hasAllocatedMemory());
  EXPECT_TRUE(
      std::is_nothrow_move_constructible<decltype(func_safe_move)>::value);

  // callable_throw can be stored in the Function object when
  // the NoExceptMovable template parameter is set to NO
  Function<int(int), FunctionMoveCtor::MAY_THROW> func_movethrows(
      callable_throw);
  EXPECT_EQ(func_movethrows(42), 52);
  EXPECT_FALSE(func_movethrows.hasAllocatedMemory());
  EXPECT_FALSE(
      std::is_nothrow_move_constructible<decltype(func_movethrows)>::value);
}

// TEST =====================================================================
// InvokeFunctor & InvokeReference

template <FunctionMoveCtor NEM, size_t S>
void invoke_functor_test() {
  Functor<int, 100> func;
  func(5, 123);

  // Try Functions with differently sized storage areas
  // S=0: request storage for functors of size 0. The storage size
  // will be actually larger, because there is a lower limit which
  // still allows to store at least pointers to functors on the heap.
  // S=1: request minimum storage size of 0.5x the sizeof(func)
  // S=2: request sizeof(func)
  // S=3: request 1.5*sizeof(func)
  Function<int(size_t) const, NEM, sizeof(func)* S / 2> getter =
      std::move(func);

  // Function will allocate memory on the heap to store
  // the functor object if the internal storage area is smaller than
  // sizeof(func).
  EXPECT_EQ(getter.hasAllocatedMemory(), S < 2);

  EXPECT_EQ(getter(5), 123);
}
TEST(Function, InvokeFunctor_T0) {
  invoke_functor_test<FunctionMoveCtor::MAY_THROW, 0>();
}
TEST(Function, InvokeFunctor_N0) {
  invoke_functor_test<FunctionMoveCtor::NO_THROW, 0>();
}
TEST(Function, InvokeFunctor_T1) {
  invoke_functor_test<FunctionMoveCtor::MAY_THROW, 1>();
}
TEST(Function, InvokeFunctor_N1) {
  invoke_functor_test<FunctionMoveCtor::NO_THROW, 1>();
}
TEST(Function, InvokeFunctor_T2) {
  invoke_functor_test<FunctionMoveCtor::MAY_THROW, 2>();
}
TEST(Function, InvokeFunctor_N2) {
  invoke_functor_test<FunctionMoveCtor::NO_THROW, 2>();
}
TEST(Function, InvokeFunctor_T3) {
  invoke_functor_test<FunctionMoveCtor::MAY_THROW, 3>();
}
TEST(Function, InvokeFunctor_N3) {
  invoke_functor_test<FunctionMoveCtor::NO_THROW, 3>();
}

template <FunctionMoveCtor NEM>
void invoke_reference_test() {
  Functor<int, 10> func;
  func(5, 123);

  // Have Functions for getter and setter, both referencing the
  // same funtor
  Function<int(size_t) const, NEM, 0> getter = std::ref(func);
  Function<int(size_t, int), NEM, 0> setter = std::ref(func);

  EXPECT_EQ(getter(5), 123);
  EXPECT_EQ(setter(5, 456), 123);
  EXPECT_EQ(setter(5, 567), 456);
  EXPECT_EQ(getter(5), 567);
}
TEST(Function, InvokeReference_T) {
  invoke_reference_test<FunctionMoveCtor::MAY_THROW>();
}
TEST(Function, InvokeReference_N) {
  invoke_reference_test<FunctionMoveCtor::NO_THROW>();
}

// TEST =====================================================================
// Emptiness

template <FunctionMoveCtor NEM>
void emptiness_test() {
  Function<int(int), NEM> f;
  EXPECT_EQ(f, nullptr);
  EXPECT_EQ(nullptr, f);
  EXPECT_FALSE(f);
  EXPECT_THROW(f(98), std::bad_function_call);

  Function<int(int), NEM> g([](int x) { return x + 1; });
  EXPECT_NE(g, nullptr);
  EXPECT_NE(nullptr, g);
  EXPECT_TRUE(g);
  EXPECT_EQ(g(99), 100);

  Function<int(int), NEM> h(&func_int_int_add_25);
  EXPECT_NE(h, nullptr);
  EXPECT_NE(nullptr, h);
  EXPECT_TRUE(h);
  EXPECT_EQ(h(100), 125);

  h = {};
  EXPECT_EQ(h, nullptr);
  EXPECT_EQ(nullptr, h);
  EXPECT_FALSE(h);
  EXPECT_THROW(h(101), std::bad_function_call);
}

TEST(Function, Emptiness_T) {
  emptiness_test<FunctionMoveCtor::MAY_THROW>();
}
TEST(Function, Emptiness_N) {
  emptiness_test<FunctionMoveCtor::NO_THROW>();
}

// TEST =====================================================================
// Types

TEST(Function, Types) {
  EXPECT_TRUE((
      !std::is_base_of<std::unary_function<int, int>, Function<int()>>::value));
  EXPECT_TRUE(
      (!std::is_base_of<std::binary_function<int, int, int>, Function<int()>>::
           value));
  EXPECT_TRUE((std::is_same<Function<int()>::ResultType, int>::value));

  EXPECT_TRUE((
      std::is_base_of<std::unary_function<int, double>, Function<double(int)>>::
          value));
  EXPECT_TRUE((!std::is_base_of<
               std::binary_function<int, int, double>,
               Function<double(int)>>::value));
  EXPECT_TRUE((std::is_same<Function<double(int)>::ResultType, double>::value));
  EXPECT_TRUE(
      (std::is_same<Function<double(int)>::result_type, double>::value));
  EXPECT_TRUE((std::is_same<Function<double(int)>::argument_type, int>::value));

  EXPECT_TRUE((!std::is_base_of<
               std::unary_function<int, double>,
               Function<double(int, char)>>::value));
  EXPECT_TRUE((std::is_base_of<
               std::binary_function<int, char, double>,
               Function<double(int, char)>>::value));
  EXPECT_TRUE(
      (std::is_same<Function<double(int, char)>::ResultType, double>::value));
  EXPECT_TRUE(
      (std::is_same<Function<double(int, char)>::result_type, double>::value));
  EXPECT_TRUE(
      (std::is_same<Function<double(int, char)>::first_argument_type, int>::
           value));
  EXPECT_TRUE(
      (std::is_same<Function<double(int, char)>::second_argument_type, char>::
           value));
}

// TEST =====================================================================
// Swap

template <FunctionMoveCtor NEM1, FunctionMoveCtor NEM2>
void swap_test() {
  Function<int(int), NEM1> mf1(func_int_int_add_25);
  Function<int(int), NEM2> mf2(func_int_int_add_111);

  EXPECT_EQ(mf1(100), 125);
  EXPECT_EQ(mf2(100), 211);

  mf1.swap(mf2);

  EXPECT_EQ(mf2(100), 125);
  EXPECT_EQ(mf1(100), 211);

  Function<int(int)> mf3(nullptr);
  EXPECT_EQ(mf3, nullptr);

  mf1.swap(mf3);

  EXPECT_EQ(mf3(100), 211);
  EXPECT_EQ(mf1, nullptr);

  Function<int(int)> mf4([](int x) { return x + 222; });
  EXPECT_EQ(mf4(100), 322);

  mf4.swap(mf3);
  EXPECT_EQ(mf4(100), 211);
  EXPECT_EQ(mf3(100), 322);

  mf3.swap(mf1);
  EXPECT_EQ(mf3, nullptr);
  EXPECT_EQ(mf1(100), 322);
}
TEST(Function, Swap_TT) {
  swap_test<FunctionMoveCtor::MAY_THROW, FunctionMoveCtor::MAY_THROW>();
}
TEST(Function, Swap_TN) {
  swap_test<FunctionMoveCtor::MAY_THROW, FunctionMoveCtor::NO_THROW>();
}
TEST(Function, Swap_NT) {
  swap_test<FunctionMoveCtor::NO_THROW, FunctionMoveCtor::MAY_THROW>();
}
TEST(Function, Swap_NN) {
  swap_test<FunctionMoveCtor::NO_THROW, FunctionMoveCtor::NO_THROW>();
}

// TEST =====================================================================
// Bind

template <FunctionMoveCtor NEM>
void bind_test() {
  Function<float(float, float), NEM> fnc = floatMult;
  auto task = std::bind(std::move(fnc), 2.f, 4.f);
  EXPECT_THROW(fnc(0, 0), std::bad_function_call);
  EXPECT_EQ(task(), 8);
  auto task2(std::move(task));
  EXPECT_THROW(task(), std::bad_function_call);
  EXPECT_EQ(task2(), 8);
}
TEST(Function, Bind_T) {
  bind_test<FunctionMoveCtor::MAY_THROW>();
}
TEST(Function, Bind_N) {
  bind_test<FunctionMoveCtor::NO_THROW>();
}

// TEST =====================================================================
// NonCopyableLambda

template <FunctionMoveCtor NEM, size_t S>
void non_copyable_lambda_test() {
  auto unique_ptr_int = folly::make_unique<int>(900);
  EXPECT_EQ(*unique_ptr_int, 900);

  char fooData[64] = {0};
  EXPECT_EQ(fooData[0], 0); // suppress gcc warning about fooData not being used

  auto functor = std::bind(
      [fooData](std::unique_ptr<int>& up) mutable { return ++*up; },
      std::move(unique_ptr_int));

  EXPECT_EQ(functor(), 901);

  Function<int(void), NEM, sizeof(functor)* S / 2> func = std::move(functor);
  EXPECT_EQ(
      func.hasAllocatedMemory(),
      S < 2 || (NEM == FunctionMoveCtor::NO_THROW &&
                !std::is_nothrow_move_constructible<decltype(functor)>::value));

  EXPECT_EQ(func(), 902);
}
TEST(Function, NonCopyableLambda_T0) {
  non_copyable_lambda_test<FunctionMoveCtor::MAY_THROW, 0>();
}
TEST(Function, NonCopyableLambda_N0) {
  non_copyable_lambda_test<FunctionMoveCtor::NO_THROW, 0>();
}
TEST(Function, NonCopyableLambda_T1) {
  non_copyable_lambda_test<FunctionMoveCtor::MAY_THROW, 1>();
}
TEST(Function, NonCopyableLambda_N1) {
  non_copyable_lambda_test<FunctionMoveCtor::NO_THROW, 1>();
}
TEST(Function, NonCopyableLambda_T2) {
  non_copyable_lambda_test<FunctionMoveCtor::MAY_THROW, 2>();
}
TEST(Function, NonCopyableLambda_N2) {
  non_copyable_lambda_test<FunctionMoveCtor::NO_THROW, 2>();
}
TEST(Function, NonCopyableLambda_T3) {
  non_copyable_lambda_test<FunctionMoveCtor::MAY_THROW, 3>();
}
TEST(Function, NonCopyableLambda_N3) {
  non_copyable_lambda_test<FunctionMoveCtor::NO_THROW, 3>();
}

// TEST =====================================================================
// Downsize

template <FunctionMoveCtor NEM>
void downsize_test() {
  Functor<int, 10> functor;

  // set element 3
  functor(3, 123);
  EXPECT_EQ(functor(3), 123);

  // Function with large callable storage area (twice the size of
  // the functor)
  Function<int(size_t, int), NEM, sizeof(functor)* 2> func2x =
      std::move(functor);
  EXPECT_FALSE(func2x.hasAllocatedMemory());
  EXPECT_EQ(func2x(3, 200), 123);
  EXPECT_EQ(func2x(3, 201), 200);

  // Function with sufficient callable storage area (equal to
  // size of the functor)
  Function<int(size_t, int), NEM, sizeof(functor)> func1x = std::move(func2x);
  EXPECT_THROW(func2x(0, 0), std::bad_function_call);
  EXPECT_FALSE(func2x);
  EXPECT_FALSE(func1x.hasAllocatedMemory());
  EXPECT_EQ(func1x(3, 202), 201);
  EXPECT_EQ(func1x(3, 203), 202);

  // Function with minimal callable storage area (functor does
  // not fit and will be moved to memory on the heap)
  Function<int(size_t, int), NEM, 0> func0x = std::move(func1x);
  EXPECT_THROW(func1x(0, 0), std::bad_function_call);
  EXPECT_FALSE(func1x);
  EXPECT_TRUE(func0x.hasAllocatedMemory());
  EXPECT_EQ(func0x(3, 204), 203);
  EXPECT_EQ(func0x(3, 205), 204);

  // bonus test: move to Function with opposite NoExceptMovable
  // setting
  Function<
      int(size_t, int),
      NEM == FunctionMoveCtor::NO_THROW ? FunctionMoveCtor::MAY_THROW
                                        : FunctionMoveCtor::NO_THROW,
      0>
      funcnot = std::move(func0x);
  EXPECT_THROW(func0x(0, 0), std::bad_function_call);
  EXPECT_FALSE(func0x);
  EXPECT_TRUE(funcnot.hasAllocatedMemory());
  EXPECT_EQ(funcnot(3, 206), 205);
  EXPECT_EQ(funcnot(3, 207), 206);
}
TEST(Function, Downsize_T) {
  downsize_test<FunctionMoveCtor::MAY_THROW>();
}
TEST(Function, Downsize_N) {
  downsize_test<FunctionMoveCtor::NO_THROW>();
}

// TEST =====================================================================
// Refcount

template <FunctionMoveCtor NEM>
void refcount_test() {
  Functor<int, 100> functor;
  functor(3, 999);
  auto shared_int = std::make_shared<int>(100);

  EXPECT_EQ(*shared_int, 100);
  EXPECT_EQ(shared_int.use_count(), 1);

  Function<int(void), NEM> func1 = [shared_int]() { return ++*shared_int; };
  EXPECT_EQ(shared_int.use_count(), 2);
  EXPECT_EQ(func1(), 101);
  EXPECT_EQ(*shared_int, 101);

  // func2: made to not fit functor.
  Function<int(void), NEM, sizeof(functor) / 2> func2 = std::move(func1);
  EXPECT_THROW(func1(), std::bad_function_call);
  EXPECT_EQ(shared_int.use_count(), 2);
  EXPECT_FALSE(func1);
  EXPECT_EQ(func2(), 102);
  EXPECT_EQ(*shared_int, 102);

  func2 = [shared_int]() { return ++*shared_int; };
  EXPECT_EQ(shared_int.use_count(), 2);
  EXPECT_EQ(func2(), 103);
  EXPECT_EQ(*shared_int, 103);

  // We set func2 to a lambda that captures 'functor', which forces it on
  // the heap
  func2 = [functor]() { return functor(3); };
  EXPECT_TRUE(func2.hasAllocatedMemory());
  EXPECT_EQ(func2(), 999);
  EXPECT_EQ(shared_int.use_count(), 1);
  EXPECT_EQ(*shared_int, 103);

  func2 = [shared_int]() { return ++*shared_int; };
  EXPECT_EQ(shared_int.use_count(), 2);
  EXPECT_EQ(func2(), 104);
  EXPECT_EQ(*shared_int, 104);

  // We set func2 to function pointer, which always fits into the
  // Function object and is no-except-movable
  func2 = &func_int_return_987;
  EXPECT_FALSE(func2.hasAllocatedMemory());
  EXPECT_EQ(func2(), 987);
  EXPECT_EQ(shared_int.use_count(), 1);
  EXPECT_EQ(*shared_int, 104);
}
TEST(Function, Refcount_T) {
  refcount_test<FunctionMoveCtor::MAY_THROW>();
}
TEST(Function, Refcount_N) {
  refcount_test<FunctionMoveCtor::NO_THROW>();
}

// TEST =====================================================================
// Target

template <FunctionMoveCtor NEM>
void target_test() {
  std::function<int(int)> func = [](int x) { return x + 25; };
  EXPECT_EQ(func(100), 125);

  Function<int(int), NEM> ufunc = std::move(func);
  EXPECT_THROW(func(0), std::bad_function_call);
  EXPECT_EQ(ufunc(200), 225);

  EXPECT_EQ(ufunc.target_type(), typeid(std::function<int(int)>));

  EXPECT_FALSE(ufunc.template target<int>());
  EXPECT_FALSE(ufunc.template target<std::function<void(void)>>());

  std::function<int(int)>& ufunc_target =
      *ufunc.template target<std::function<int(int)>>();

  EXPECT_EQ(ufunc_target(300), 325);
}

TEST(Function, Target_T) {
  target_test<FunctionMoveCtor::MAY_THROW>();
}
TEST(Function, Target_N) {
  target_test<FunctionMoveCtor::NO_THROW>();
}

// TEST =====================================================================
// OverloadedFunctor

TEST(Function, OverloadedFunctor) {
  struct OverloadedFunctor {
    // variant 1
    int operator()(int x) {
      return 100 + 1 * x;
    }

    // variant 2 (const-overload of v1)
    int operator()(int x) const {
      return 100 + 2 * x;
    }

    // variant 3
    int operator()(int x, int) {
      return 100 + 3 * x;
    }

    // variant 4 (const-overload of v3)
    int operator()(int x, int) const {
      return 100 + 4 * x;
    }

    // variant 5 (non-const, has no const-overload)
    int operator()(int x, char const*) {
      return 100 + 5 * x;
    }

    // variant 6 (const only)
    int operator()(int x, std::vector<int> const&) const {
      return 100 + 6 * x;
    }
  };
  OverloadedFunctor of;

  Function<int(int)> variant1 = of;
  EXPECT_EQ(variant1(15), 100 + 1 * 15);

  Function<int(int) const> variant2 = of;
  EXPECT_EQ(variant2(16), 100 + 2 * 16);

  Function<int(int, int)> variant3 = of;
  EXPECT_EQ(variant3(17, 0), 100 + 3 * 17);

  Function<int(int, int) const> variant4 = of;
  EXPECT_EQ(variant4(18, 0), 100 + 4 * 18);

  Function<int(int, char const*)> variant5 = of;
  EXPECT_EQ(variant5(19, "foo"), 100 + 5 * 19);

  Function<int(int, std::vector<int> const&)> variant6 = of;
  EXPECT_EQ(variant6(20, {}), 100 + 6 * 20);
  EXPECT_EQ(variant6(20, {1, 2, 3}), 100 + 6 * 20);

  Function<int(int, std::vector<int> const&) const> variant6const = of;
  EXPECT_EQ(variant6const(21, {}), 100 + 6 * 21);

  // Cast const-functions to non-const and the other way around: if the functor
  // has both const and non-const operator()s for a given parameter signature,
  // constructing a Function must select one of them, depending on
  // whether the function type template parameter is const-qualified or not.
  // When the const-ness is later changed (by moving the
  // Function<R(Args...)const> into a Function<R(Args...)> or by
  // calling the folly::constCastFunction which moves it into a
  // Function<R(Args...)const>), the Function must still execute
  // the initially selected function.

  auto variant1_const = folly::constCastFunction(std::move(variant1));
  EXPECT_THROW(variant1(0), std::bad_function_call);
  EXPECT_EQ(variant1_const(22), 100 + 1 * 22);

  Function<int(int)> variant2_nonconst = std::move(variant2);
  EXPECT_THROW(variant2(0), std::bad_function_call);
  EXPECT_EQ(variant2_nonconst(23), 100 + 2 * 23);

  auto variant3_const = folly::constCastFunction(std::move(variant3));
  EXPECT_THROW(variant3(0, 0), std::bad_function_call);
  EXPECT_EQ(variant3_const(24, 0), 100 + 3 * 24);

  Function<int(int, int)> variant4_nonconst = std::move(variant4);
  EXPECT_THROW(variant4(0, 0), std::bad_function_call);
  EXPECT_EQ(variant4_nonconst(25, 0), 100 + 4 * 25);

  auto variant5_const = folly::constCastFunction(std::move(variant5));
  EXPECT_THROW(variant5(0, ""), std::bad_function_call);
  EXPECT_EQ(variant5_const(26, "foo"), 100 + 5 * 26);

  auto variant6_const = folly::constCastFunction(std::move(variant6));
  EXPECT_THROW(variant6(0, {}), std::bad_function_call);
  EXPECT_EQ(variant6_const(27, {}), 100 + 6 * 27);

  Function<int(int, std::vector<int> const&)> variant6const_nonconst =
      std::move(variant6const);
  EXPECT_THROW(variant6const(0, {}), std::bad_function_call);
  EXPECT_EQ(variant6const_nonconst(28, {}), 100 + 6 * 28);
}

// TEST =====================================================================
// Lambda

TEST(Function, Lambda) {
  // Non-mutable lambdas: can be stored in a non-const...
  Function<int(int)> func = [](int x) { return 1000 + x; };
  EXPECT_EQ(func(1), 1001);

  // ...as well as in a const Function
  Function<int(int) const> func_const = [](int x) { return 2000 + x; };
  EXPECT_EQ(func_const(1), 2001);

  // Mutable lambda: can only be stored in a const Function:
  int number = 3000;
  Function<int()> func_mutable = [number]() mutable { return ++number; };
  EXPECT_EQ(func_mutable(), 3001);
  EXPECT_EQ(func_mutable(), 3002);

  // test after const-casting

  Function<int(int) const> func_made_const =
      folly::constCastFunction(std::move(func));
  EXPECT_EQ(func_made_const(2), 1002);
  EXPECT_THROW(func(0), std::bad_function_call);

  Function<int(int)> func_const_made_nonconst = std::move(func_const);
  EXPECT_EQ(func_const_made_nonconst(2), 2002);
  EXPECT_THROW(func_const(0), std::bad_function_call);

  Function<int() const> func_mutable_made_const =
      folly::constCastFunction(std::move(func_mutable));
  EXPECT_EQ(func_mutable_made_const(), 3003);
  EXPECT_EQ(func_mutable_made_const(), 3004);
  EXPECT_THROW(func_mutable(), std::bad_function_call);
}

// TEST =====================================================================
// DataMember & MemberFunction

struct MemberFunc {
  int x;
  int getX() const {
    return x;
  }
  void setX(int xx) {
    x = xx;
  }
};

TEST(Function, DataMember) {
  MemberFunc mf;
  MemberFunc const& cmf = mf;
  mf.x = 123;

  Function<int(MemberFunc const*)> data_getter1 = &MemberFunc::x;
  EXPECT_EQ(data_getter1(&cmf), 123);
  Function<int(MemberFunc*)> data_getter2 = &MemberFunc::x;
  EXPECT_EQ(data_getter2(&mf), 123);
  Function<int(MemberFunc const&)> data_getter3 = &MemberFunc::x;
  EXPECT_EQ(data_getter3(cmf), 123);
  Function<int(MemberFunc&)> data_getter4 = &MemberFunc::x;
  EXPECT_EQ(data_getter4(mf), 123);
}

TEST(Function, MemberFunction) {
  MemberFunc mf;
  MemberFunc const& cmf = mf;
  mf.x = 123;

  Function<int(MemberFunc const*)> getter1 = &MemberFunc::getX;
  EXPECT_EQ(getter1(&cmf), 123);
  Function<int(MemberFunc*)> getter2 = &MemberFunc::getX;
  EXPECT_EQ(getter2(&mf), 123);
  Function<int(MemberFunc const&)> getter3 = &MemberFunc::getX;
  EXPECT_EQ(getter3(cmf), 123);
  Function<int(MemberFunc&)> getter4 = &MemberFunc::getX;
  EXPECT_EQ(getter4(mf), 123);

  Function<void(MemberFunc*, int)> setter1 = &MemberFunc::setX;
  setter1(&mf, 234);
  EXPECT_EQ(mf.x, 234);

  Function<void(MemberFunc&, int)> setter2 = &MemberFunc::setX;
  setter2(mf, 345);
  EXPECT_EQ(mf.x, 345);
}

// TEST =====================================================================
// CaptureCopyMoveCount & ParameterCopyMoveCount

class CopyMoveTracker {
 public:
  struct ConstructorTag {};

  CopyMoveTracker() = delete;
  explicit CopyMoveTracker(ConstructorTag)
      : data_(std::make_shared<std::pair<size_t, size_t>>(0, 0)) {}

  CopyMoveTracker(CopyMoveTracker const& o) noexcept : data_(o.data_) {
    ++data_->first;
  }
  CopyMoveTracker& operator=(CopyMoveTracker const& o) noexcept {
    data_ = o.data_;
    ++data_->first;
    return *this;
  }

  CopyMoveTracker(CopyMoveTracker&& o) noexcept : data_(o.data_) {
    ++data_->second;
  }
  CopyMoveTracker& operator=(CopyMoveTracker&& o) noexcept {
    data_ = o.data_;
    ++data_->second;
    return *this;
  }

  size_t copyCount() const {
    return data_->first;
  }
  size_t moveCount() const {
    return data_->second;
  }
  size_t refCount() const {
    return data_.use_count();
  }
  void resetCounters() {
    data_->first = data_->second = 0;
  }

 private:
  // copy, move
  std::shared_ptr<std::pair<size_t, size_t>> data_;
};

TEST(Function, CaptureCopyMoveCount) {
  // This test checks that no unnecessary copies/moves are made.

  CopyMoveTracker cmt(CopyMoveTracker::ConstructorTag{});
  EXPECT_EQ(cmt.copyCount(), 0);
  EXPECT_EQ(cmt.moveCount(), 0);
  EXPECT_EQ(cmt.refCount(), 1);

  // Move into lambda, move lambda into Function
  auto lambda1 = [cmt = std::move(cmt)]() {
    return cmt.moveCount();
  };
  Function<size_t(void)> uf1 = std::move(lambda1);

  // Max copies: 0. Max copy+moves: 2.
  EXPECT_LE(cmt.moveCount() + cmt.copyCount(), 2);
  EXPECT_LE(cmt.copyCount(), 0);

  cmt.resetCounters();

  // Move into lambda, copy lambda into Function
  auto lambda2 = [cmt = std::move(cmt)]() {
    return cmt.moveCount();
  };
  Function<size_t(void)> uf2 = lambda2;

  // Max copies: 1. Max copy+moves: 2.
  EXPECT_LE(cmt.moveCount() + cmt.copyCount(), 2);
  EXPECT_LE(cmt.copyCount(), 1);

  // Invoking Function must not make copies/moves of the callable
  cmt.resetCounters();
  uf1();
  uf2();
  EXPECT_EQ(cmt.copyCount(), 0);
  EXPECT_EQ(cmt.moveCount(), 0);
}

TEST(Function, ParameterCopyMoveCount) {
  // This test checks that no unnecessary copies/moves are made.

  CopyMoveTracker cmt(CopyMoveTracker::ConstructorTag{});
  EXPECT_EQ(cmt.copyCount(), 0);
  EXPECT_EQ(cmt.moveCount(), 0);
  EXPECT_EQ(cmt.refCount(), 1);

  // pass by value
  Function<size_t(CopyMoveTracker)> uf1 = [](CopyMoveTracker c) {
    return c.moveCount();
  };

  cmt.resetCounters();
  uf1(cmt);
  // Max copies: 1. Max copy+moves: 2.
  EXPECT_LE(cmt.moveCount() + cmt.copyCount(), 2);
  EXPECT_LE(cmt.copyCount(), 1);

  cmt.resetCounters();
  uf1(std::move(cmt));
  // Max copies: 1. Max copy+moves: 2.
  EXPECT_LE(cmt.moveCount() + cmt.copyCount(), 2);
  EXPECT_LE(cmt.copyCount(), 0);

  // pass by reference
  Function<size_t(CopyMoveTracker&)> uf2 = [](CopyMoveTracker& c) {
    return c.moveCount();
  };

  cmt.resetCounters();
  uf2(cmt);
  // Max copies: 0. Max copy+moves: 0.
  EXPECT_LE(cmt.moveCount() + cmt.copyCount(), 0);
  EXPECT_LE(cmt.copyCount(), 0);

  // pass by const reference
  Function<size_t(CopyMoveTracker const&)> uf3 = [](CopyMoveTracker const& c) {
    return c.moveCount();
  };

  cmt.resetCounters();
  uf3(cmt);
  // Max copies: 0. Max copy+moves: 0.
  EXPECT_LE(cmt.moveCount() + cmt.copyCount(), 0);
  EXPECT_LE(cmt.copyCount(), 0);

  // pass by rvalue reference
  Function<size_t(CopyMoveTracker &&)> uf4 = [](CopyMoveTracker&& c) {
    return c.moveCount();
  };

  cmt.resetCounters();
  uf4(std::move(cmt));
  // Max copies: 0. Max copy+moves: 0.
  EXPECT_LE(cmt.moveCount() + cmt.copyCount(), 0);
  EXPECT_LE(cmt.copyCount(), 0);
}

// TEST =====================================================================
// CopyMoveThrows

enum ExceptionType { COPY, MOVE };

template <ExceptionType ET>
class CopyMoveException : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

template <bool CopyThrows, bool MoveThrows>
struct CopyMoveThrowsCallable {
  int allowCopyOperations{0};
  int allowMoveOperations{0};

  CopyMoveThrowsCallable() = default;
  CopyMoveThrowsCallable(CopyMoveThrowsCallable const& o) noexcept(
      !CopyThrows) {
    *this = o;
  }
  CopyMoveThrowsCallable& operator=(CopyMoveThrowsCallable const& o) noexcept(
      !CopyThrows) {
    allowCopyOperations = o.allowCopyOperations;
    allowMoveOperations = o.allowMoveOperations;

    if (allowCopyOperations > 0) {
      --allowCopyOperations;
    } else if (CopyThrows) {
      throw CopyMoveException<COPY>("CopyMoveThrowsCallable copy");
    }
    return *this;
  }
  CopyMoveThrowsCallable(CopyMoveThrowsCallable&& o) noexcept(!MoveThrows) {
    *this = std::move(o);
  }
  CopyMoveThrowsCallable& operator=(CopyMoveThrowsCallable&& o) noexcept(
      !MoveThrows) {
    allowCopyOperations = o.allowCopyOperations;
    allowMoveOperations = o.allowMoveOperations;

    if (o.allowMoveOperations > 0) {
      --allowMoveOperations;
    } else if (MoveThrows) {
      throw CopyMoveException<MOVE>("CopyMoveThrowsCallable move");
    }
    return *this;
  }

  void operator()() const {}
};

TEST(Function, CopyMoveThrowsCallable) {
  EXPECT_TRUE((std::is_nothrow_move_constructible<
               CopyMoveThrowsCallable<false, false>>::value));
  EXPECT_TRUE((std::is_nothrow_move_constructible<
               CopyMoveThrowsCallable<true, false>>::value));
  EXPECT_FALSE((std::is_nothrow_move_constructible<
                CopyMoveThrowsCallable<false, true>>::value));
  EXPECT_FALSE((std::is_nothrow_move_constructible<
                CopyMoveThrowsCallable<true, true>>::value));

  EXPECT_TRUE((std::is_nothrow_copy_constructible<
               CopyMoveThrowsCallable<false, false>>::value));
  EXPECT_FALSE((std::is_nothrow_copy_constructible<
                CopyMoveThrowsCallable<true, false>>::value));
  EXPECT_TRUE((std::is_nothrow_copy_constructible<
               CopyMoveThrowsCallable<false, true>>::value));
  EXPECT_FALSE((std::is_nothrow_copy_constructible<
                CopyMoveThrowsCallable<true, true>>::value));
}

template <FunctionMoveCtor NEM, bool CopyThrows, bool MoveThrows>
void copy_and_move_throws_test() {
  CopyMoveThrowsCallable<CopyThrows, MoveThrows> c;
  Function<void(void), NEM> uf;

  if (CopyThrows) {
    EXPECT_THROW((uf = c), CopyMoveException<COPY>);
  } else {
    EXPECT_NO_THROW((uf = c));
  }

  if (MoveThrows) {
    EXPECT_THROW((uf = std::move(c)), CopyMoveException<MOVE>);
  } else {
    EXPECT_NO_THROW((uf = std::move(c)));
  }

  c.allowMoveOperations = 1;
  uf = std::move(c);
  if (NEM == FunctionMoveCtor::MAY_THROW && MoveThrows) {
    Function<void(void), NEM> uf2;
    EXPECT_THROW((uf2 = std::move(uf)), CopyMoveException<MOVE>);
  } else {
    Function<void(void), NEM> uf2;
    EXPECT_NO_THROW((uf2 = std::move(uf)));
  }

  c.allowMoveOperations = 0;
  c.allowCopyOperations = 1;
  uf = c;
  if (NEM == FunctionMoveCtor::MAY_THROW && MoveThrows) {
    Function<void(void), NEM> uf2;
    EXPECT_THROW((uf2 = std::move(uf)), CopyMoveException<MOVE>);
  } else {
    Function<void(void), NEM> uf2;
    EXPECT_NO_THROW((uf2 = std::move(uf)));
  }
}

TEST(Function, CopyAndMoveThrows_TNN) {
  copy_and_move_throws_test<FunctionMoveCtor::MAY_THROW, false, false>();
}

TEST(Function, CopyAndMoveThrows_NNN) {
  copy_and_move_throws_test<FunctionMoveCtor::NO_THROW, false, false>();
}

TEST(Function, CopyAndMoveThrows_TTN) {
  copy_and_move_throws_test<FunctionMoveCtor::MAY_THROW, true, false>();
}

TEST(Function, CopyAndMoveThrows_NTN) {
  copy_and_move_throws_test<FunctionMoveCtor::NO_THROW, true, false>();
}

TEST(Function, CopyAndMoveThrows_TNT) {
  copy_and_move_throws_test<FunctionMoveCtor::MAY_THROW, false, true>();
}

TEST(Function, CopyAndMoveThrows_NNT) {
  copy_and_move_throws_test<FunctionMoveCtor::NO_THROW, false, true>();
}

TEST(Function, CopyAndMoveThrows_TTT) {
  copy_and_move_throws_test<FunctionMoveCtor::MAY_THROW, true, true>();
}

TEST(Function, CopyAndMoveThrows_NTT) {
  copy_and_move_throws_test<FunctionMoveCtor::NO_THROW, true, true>();
}

// TEST =====================================================================
// VariadicTemplate & VariadicArguments

struct VariadicTemplateSum {
  int operator()() const {
    return 0;
  }
  template <class... Args>
  int operator()(int x, Args... args) const {
    return x + (*this)(args...);
  }
};

TEST(Function, VariadicTemplate) {
  Function<int(int)> uf1 = VariadicTemplateSum();
  Function<int(int, int)> uf2 = VariadicTemplateSum();
  Function<int(int, int, int)> uf3 = VariadicTemplateSum();

  EXPECT_EQ(uf1(66), 66);
  EXPECT_EQ(uf2(55, 44), 99);
  EXPECT_EQ(uf3(33, 22, 11), 66);
}

struct VariadicArgumentsSum {
  int operator()(int count, ...) const {
    int result = 0;
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; ++i) {
      result += va_arg(args, int);
    }
    va_end(args);
    return result;
  }
};

TEST(Function, VariadicArguments) {
  Function<int(int)> uf1 = VariadicArgumentsSum();
  Function<int(int, int)> uf2 = VariadicArgumentsSum();
  Function<int(int, int, int)> uf3 = VariadicArgumentsSum();

  EXPECT_EQ(uf1(0), 0);
  EXPECT_EQ(uf2(1, 66), 66);
  EXPECT_EQ(uf3(2, 55, 44), 99);
}

// TEST =====================================================================
// SafeCaptureByReference

// A function can use Function const& as a parameter to signal that it
// is safe to pass a lambda that captures local variables by reference.
// It is safe because we know the function called can only invoke the
// Function until it returns. It can't store a copy of the Function
// (because it's not copyable), and it can't move the Function somewhere
// else (because it gets only a const&).

template <typename T>
void for_each(
    T const& range,
    Function<void(typename T::value_type const&) const> const& func) {
  for (auto const& elem : range) {
    func(elem);
  }
}

TEST(Function, SafeCaptureByReference) {
  std::vector<int> const vec = {20, 30, 40, 2, 3, 4, 200, 300, 400};

  int sum = 0;

  // for_each's second parameter is of type Function<...> const&.
  // Hence we know we can safely pass it a lambda that references local
  // variables. There is no way the reference to x will be stored anywhere.
  for_each<std::vector<int>>(vec, [&sum](int x) { sum += x; });

  // gcc versions before 4.9 cannot deduce the type T in the above call
  // to for_each. Modern compiler versions can compile the following line:
  //   for_each(vec, [&sum](int x) { sum += x; });

  EXPECT_EQ(sum, 999);
}
