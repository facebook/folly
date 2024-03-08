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

// We don't bother with "real" checking of any sort of difference in generated
// code (which would be hard to test, can vary depending on build mode, etc.).
// It's still valuable to have some syntax checking (in the sense of making sure
// various calls compile as expected without warning) in one reduced place.

#include <folly/lang/Hint.h>

#include <folly/portability/GTest.h>

using namespace folly;

// is_unsafe_for_async_usage_v -----------------

struct UnsafeForAsyncUsageYes {
  using folly_is_unsafe_for_async_usage = std::true_type;
};

struct UnsafeForAsyncUsageNo0 {};

struct UnsafeForAsyncUsageNo1 {
  using folly_is_unsafe_for_async_usage = std::false_type;
};

static_assert(detail::is_unsafe_for_async_usage_v<UnsafeForAsyncUsageYes>, "");
static_assert(!detail::is_unsafe_for_async_usage_v<UnsafeForAsyncUsageNo0>, "");
static_assert(!detail::is_unsafe_for_async_usage_v<UnsafeForAsyncUsageNo1>, "");

// is_coro_aware_mutex ----------------------------

struct CoroAwareMutexYes {
  using folly_coro_aware_mutex = std::true_type;
};

struct CoroAwareMutexNo {};

static_assert(is_coro_aware_mutex_v<CoroAwareMutexYes>, "");
static_assert(!is_coro_aware_mutex_v<CoroAwareMutexNo>, "");

//-----------------

TEST(Hint, CompilerMayUnsafelyAssume) {
  compiler_may_unsafely_assume(true);
  bool falseValue = false;
  compiler_must_not_predict(falseValue);
  if (falseValue) {
    compiler_may_unsafely_assume(false);
  }
}

TEST(Hint, CompilerMayUnsafelyAssumeUnreachable) {
  bool falseValue = false;
  compiler_must_not_predict(falseValue);
  if (falseValue) {
    compiler_may_unsafely_assume_unreachable();
  }
}

TEST(Hint, CompilerMayUnsafelyAssumeSeparateStorage) {
  int a = 123;
  int b = 456;
  compiler_may_unsafely_assume_separate_storage(&a, &b);
}

struct NonTriviallyCopyable {
  NonTriviallyCopyable() {}
  NonTriviallyCopyable(const NonTriviallyCopyable&) {}

  NonTriviallyCopyable& operator=(const NonTriviallyCopyable&) { return *this; }
  int data = 123;
};

TEST(Hint, CompilerMustNotElide) {
  int x = 123;
  compiler_must_not_elide(x);
  NonTriviallyCopyable ntc;
  compiler_must_not_elide(ntc);
}

TEST(Hint, CompilerMustNotPredict) {
  int x = 123;
  compiler_must_not_predict(x);
  NonTriviallyCopyable ntc;
  compiler_must_not_predict(ntc);
}
