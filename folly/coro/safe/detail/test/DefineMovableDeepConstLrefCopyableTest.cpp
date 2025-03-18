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

#include <folly/portability/GTest.h>

#include <folly/coro/safe/detail/DefineMovableDeepConstLrefCopyable.h>

template <typename T>
struct Foo {
  FOLLY_MOVABLE_AND_DEEP_CONST_LREF_COPYABLE(Foo, T);
};
// Don't leak this macro from your header!
#undef FOLLY_MOVABLE_AND_DEEP_CONST_LREF_COPYABLE

// This is here so that test "runs" show up in CI history
TEST(DefineMovableDeepConstLrefCopyableTest, all_tests_run_at_build_time) {}

// Fully movable if `T` is a value
static_assert(std::is_move_constructible_v<Foo<int>>);
static_assert(std::is_constructible_v<Foo<int>, Foo<int>&&>);
static_assert(std::is_move_assignable_v<Foo<int>>);
static_assert(std::is_assignable_v<Foo<int>, Foo<int>&&>);

// Fully movable if `T` is an rval ref
static_assert(std::is_move_constructible_v<Foo<int&&>>);
static_assert(std::is_constructible_v<Foo<int&&>, Foo<int&&>&&>);
static_assert(std::is_move_assignable_v<Foo<int&&>>);
static_assert(std::is_assignable_v<Foo<int&&>, Foo<int&&>&&>);

// Fully movable if `T` is an lval ref
static_assert(std::is_move_constructible_v<Foo<int&>>);
static_assert(std::is_constructible_v<Foo<int&>, Foo<int&>&&>);
static_assert(std::is_move_assignable_v<Foo<int&>>);
static_assert(std::is_assignable_v<Foo<int&>, Foo<int&>&&>);

// Not copyable if `T` is a value
static_assert(!std::is_copy_constructible_v<Foo<int>>);
static_assert(!std::is_constructible_v<Foo<int>, Foo<int>&>);
static_assert(!std::is_constructible_v<Foo<int>, const Foo<int>&>);
static_assert(!std::is_constructible_v<Foo<int>, const Foo<int>&&>);
static_assert(!std::is_copy_assignable_v<Foo<int>>);
static_assert(!std::is_assignable_v<Foo<int>, Foo<int>&>);
static_assert(!std::is_assignable_v<Foo<int>, const Foo<int>&>);
static_assert(!std::is_assignable_v<Foo<int>, const Foo<int>&&>);

// Not copyable if `T` is an rval ref
static_assert(!std::is_copy_constructible_v<Foo<int&&>>);
static_assert(!std::is_constructible_v<Foo<int&&>, Foo<int&&>&>);
static_assert(!std::is_constructible_v<Foo<int&&>, const Foo<int&&>&>);
static_assert(!std::is_constructible_v<Foo<int&&>, const Foo<int&&>&&>);
static_assert(!std::is_copy_assignable_v<Foo<int&&>>);
static_assert(!std::is_assignable_v<Foo<int&&>, Foo<int&&>&>);
static_assert(!std::is_assignable_v<Foo<int&&>, const Foo<int&&>&>);
static_assert(!std::is_assignable_v<Foo<int&&>, const Foo<int&&>&&>);

// Folly copyable if `T` is an l-val ref to `const`
static_assert(std::is_copy_constructible_v<Foo<const int&>>);
static_assert(std::is_constructible_v<Foo<const int&>, Foo<const int&>&>);
static_assert(std::is_constructible_v<Foo<const int&>, const Foo<const int&>&>);
static_assert(
    std::is_constructible_v<Foo<const int&>, const Foo<const int&>&&>);
static_assert(std::is_copy_assignable_v<Foo<const int&>>);
static_assert(std::is_assignable_v<Foo<const int&>, Foo<const int&>&>);
static_assert(std::is_assignable_v<Foo<const int&>, const Foo<const int&>&>);
static_assert(std::is_assignable_v<Foo<const int&>, const Foo<const int&>&&>);

// Only copyable from mutable refs if `T` is an lval ref to non-`const`
static_assert(std::is_copy_constructible_v<Foo<const int&>>); // Has caveat:
static_assert(std::is_constructible_v<Foo<int&>, Foo<int&>&>);
static_assert(!std::is_constructible_v<Foo<int&>, const Foo<int&>&>);
static_assert(!std::is_constructible_v<Foo<int&>, const Foo<int&>&&>);
static_assert(std::is_copy_assignable_v<Foo<const int&>>); // Has caveat:
static_assert(std::is_assignable_v<Foo<int&>, Foo<int&>&>);
static_assert(!std::is_assignable_v<Foo<int&>, const Foo<int&>&>);
static_assert(!std::is_assignable_v<Foo<int&>, const Foo<int&>&&>);
