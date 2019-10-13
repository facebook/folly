/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/lang/Aligned.h>

#include <folly/lang/Align.h>
#include <folly/portability/GTest.h>

template <typename T>
using A = folly::aligned<T, folly::max_align_v>;

class AlignedTest : public testing::Test {};

TEST_F(AlignedTest, constructors) {
  {
    struct T {
      T() = delete;
    };
    EXPECT_FALSE((std::is_constructible<A<T>>::value));
    EXPECT_FALSE((std::is_nothrow_constructible<A<T>>::value));
  }

  {
    struct T {
      T() noexcept(false) {}
    };
    EXPECT_TRUE((std::is_constructible<A<T>>::value));
    EXPECT_FALSE((std::is_nothrow_constructible<A<T>>::value));
  }

  {
    struct T {
      T() noexcept(true) {}
    };
    EXPECT_TRUE((std::is_constructible<A<T>>::value));
    EXPECT_TRUE((std::is_nothrow_constructible<A<T>>::value));
  }

  {
    struct T {
      T(T const&) = delete;
    };
    EXPECT_FALSE((std::is_copy_constructible<A<T>>::value));
    EXPECT_FALSE((std::is_nothrow_copy_constructible<A<T>>::value));
    EXPECT_FALSE((std::is_constructible<A<T>, T const&>::value));
    EXPECT_FALSE((std::is_nothrow_constructible<A<T>, T const&>::value));
  }

  {
    struct T {
      T(T const&) noexcept(false) {}
    };
    EXPECT_TRUE((std::is_copy_constructible<A<T>>::value));
    EXPECT_FALSE((std::is_nothrow_copy_constructible<A<T>>::value));
    EXPECT_TRUE((std::is_constructible<A<T>, T const&>::value));
    EXPECT_FALSE((std::is_nothrow_constructible<A<T>, T const&>::value));
  }

  {
    struct T {
      T(T const&) noexcept(true) {}
    };
    EXPECT_TRUE((std::is_copy_constructible<A<T>>::value));
    EXPECT_TRUE((std::is_nothrow_copy_constructible<A<T>>::value));
    EXPECT_TRUE((std::is_constructible<A<T>, T const&>::value));
    EXPECT_TRUE((std::is_nothrow_constructible<A<T>, T const&>::value));
  }

  {
    struct T {
      T(T&&) = delete;
    };
    EXPECT_FALSE((std::is_move_constructible<A<T>>::value));
    EXPECT_FALSE((std::is_nothrow_move_constructible<A<T>>::value));
    EXPECT_FALSE((std::is_constructible<A<T>, T&&>::value));
    EXPECT_FALSE((std::is_nothrow_constructible<A<T>, T&&>::value));
  }

  {
    struct T {
      T(T&&) noexcept(false) {}
    };
    EXPECT_TRUE((std::is_move_constructible<A<T>>::value));
    EXPECT_FALSE((std::is_nothrow_move_constructible<A<T>>::value));
    EXPECT_TRUE((std::is_constructible<A<T>, T&&>::value));
    EXPECT_FALSE((std::is_nothrow_constructible<A<T>, T&&>::value));
  }

  {
    struct T {
      T(T&&) noexcept(true) {}
    };
    EXPECT_TRUE((std::is_move_constructible<A<T>>::value));
    EXPECT_TRUE((std::is_nothrow_move_constructible<A<T>>::value));
    EXPECT_TRUE((std::is_constructible<A<T>, T&&>::value));
    EXPECT_TRUE((std::is_nothrow_constructible<A<T>, T&&>::value));
  }

  {
    using P = folly::in_place_t;
    struct L {};
    struct M {};
    struct N {};
    struct T {
      explicit T(M) noexcept(false) {}
      explicit T(N) noexcept(true) {}
    };
    EXPECT_FALSE((std::is_constructible<A<T>, P, L>::value));
    EXPECT_FALSE((std::is_nothrow_constructible<A<T>, P, L>::value));
    EXPECT_TRUE((std::is_constructible<A<T>, P, M>::value));
    EXPECT_FALSE((std::is_nothrow_constructible<A<T>, P, M>::value));
    EXPECT_TRUE((std::is_constructible<A<T>, P, N>::value));
    EXPECT_TRUE((std::is_nothrow_constructible<A<T>, P, N>::value));
  }
}

// TODO ...
