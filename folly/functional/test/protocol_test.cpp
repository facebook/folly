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

#include <folly/functional/protocol.h>

#include <folly/Traits.h>
#include <folly/portability/GTest.h>

struct ProtocolTest : testing::Test {
  template <typename F>
  static constexpr bool empty_function(F const& f) {
    return folly::match_empty_function_protocol(f);
  }

  template <typename F>
  static constexpr bool static_lambda_v =
      folly::match_static_lambda_protocol_v<F>;

  template <typename F, typename S>
  static constexpr bool safely_invocable_v =
      folly::match_safely_invocable_as_protocol_v<F, S>;
};

TEST_F(ProtocolTest, match_empty_function_protocol) {
  constexpr auto lambda = [] {};
  struct nada {
    void go() {}
  };

  using sig = void();
  using mfnp = void (nada::*)();

  EXPECT_FALSE((folly::bool_constant<empty_function(lambda)>{}));

  EXPECT_TRUE(empty_function(static_cast<sig*>(nullptr)));
  EXPECT_FALSE(empty_function(static_cast<sig*>(lambda)));

  EXPECT_TRUE(empty_function(static_cast<mfnp>(nullptr)));
  EXPECT_FALSE(empty_function(static_cast<mfnp>(&nada::go)));

  EXPECT_TRUE(empty_function(std::function<sig>{}));
  EXPECT_TRUE(empty_function(std::function<sig>{nullptr}));
  EXPECT_FALSE(empty_function(std::function<sig>{lambda}));
}

TEST_F(ProtocolTest, match_static_lambda_protocol) {
  struct not_empty {
    int dummy;
  };
  EXPECT_FALSE(static_lambda_v<not_empty>);

  struct not_trivially_copyable {
    not_trivially_copyable(not_trivially_copyable const&);
    not_trivially_copyable& operator=(not_trivially_copyable const&);
  };
  EXPECT_FALSE(static_lambda_v<not_trivially_copyable>);

  struct empty_trivially_copyable {
    empty_trivially_copyable() = delete;
  };
  EXPECT_TRUE(static_lambda_v<empty_trivially_copyable>);

  auto lambda = [] {};
  EXPECT_TRUE(static_lambda_v<decltype(lambda)>);

  EXPECT_TRUE(static_lambda_v<std::default_delete<int>>);
  EXPECT_TRUE(static_lambda_v<std::less<int>>);
  EXPECT_TRUE(static_lambda_v<std::greater<int>>);
  EXPECT_TRUE(static_lambda_v<std::equal_to<int>>);
  EXPECT_TRUE(static_lambda_v<std::hash<int>>);
}

TEST_F(ProtocolTest, match_safely_invocable_as_protocol) {
  // non-const non-noexcept invocation
  {
    struct fun {
      [[maybe_unused]] void operator()();
    };
    EXPECT_TRUE((safely_invocable_v<fun, void()>));
    EXPECT_FALSE((safely_invocable_v<fun, void() const>));
    EXPECT_FALSE((safely_invocable_v<fun, void() noexcept>));
  }

  // non-const noexcept-invocation
  {
    struct fun {
      [[maybe_unused]] void operator()() noexcept;
    };
    EXPECT_TRUE((safely_invocable_v<fun, void()>));
    EXPECT_FALSE((safely_invocable_v<fun, void() const>));
    EXPECT_TRUE((safely_invocable_v<fun, void() noexcept>));
  }

  // const non-noexcept invocation
  {
    struct fun {
      [[maybe_unused]] void operator()() const;
    };
    EXPECT_TRUE((safely_invocable_v<fun, void()>));
    EXPECT_TRUE((safely_invocable_v<fun, void() const>));
    EXPECT_FALSE((safely_invocable_v<fun, void() noexcept>));
  }

  // return-type conversions
  {
    struct foo {};
    struct bar : foo {};
    struct wiz_x {
      /* implicit */ [[maybe_unused]] wiz_x(bar);
    };
    struct wiz_nx {
      /* implicit */ [[maybe_unused]] wiz_nx(bar) noexcept;
    };
    struct fun {
      [[maybe_unused]] bar operator()() noexcept;
    };
    EXPECT_TRUE((safely_invocable_v<fun, bar()>));
    EXPECT_FALSE((safely_invocable_v<fun, foo()>)); // slicing
    EXPECT_TRUE((safely_invocable_v<fun, void()>));
    EXPECT_FALSE((safely_invocable_v<fun, bar const&()>)); // dangling-ref
    EXPECT_FALSE((safely_invocable_v<fun, foo const&()>)); // dangling-ref
    EXPECT_TRUE((safely_invocable_v<fun, wiz_x()>));
    EXPECT_TRUE((safely_invocable_v<fun, wiz_nx()>));
    EXPECT_FALSE((safely_invocable_v<fun, wiz_x() noexcept>));
    EXPECT_TRUE((safely_invocable_v<fun, wiz_nx() noexcept>));
  }

  // return-type ref conversions
  {
    struct foo {};
    struct bar : foo {};
    struct fun {
      [[maybe_unused]] bar&& operator()() noexcept;
    };
    using bar_rr = bar&&; // workaround for clang-format fail case
    using foo_rr = foo&&; // workaround for clang-format fail case
    EXPECT_TRUE((safely_invocable_v<fun, bar()>));
    EXPECT_TRUE((safely_invocable_v<fun, bar_rr()>));
    EXPECT_TRUE((safely_invocable_v<fun, bar const&()>));
    EXPECT_FALSE((safely_invocable_v<fun, foo()>)); // slicing
    EXPECT_TRUE((safely_invocable_v<fun, foo_rr()>)); // maybe not slicing
    EXPECT_TRUE((safely_invocable_v<fun, foo const&()>)); // maybe not slicing
    EXPECT_TRUE((safely_invocable_v<fun, void()>));
  }

  // cvref compatibility
  {
    struct fun {
      [[maybe_unused]] void operator()() const&&;
    };
    EXPECT_FALSE((safely_invocable_v<fun, void()>));
    EXPECT_FALSE((safely_invocable_v<fun, void() const>));
    EXPECT_FALSE((safely_invocable_v<fun, void() volatile>));
    EXPECT_FALSE((safely_invocable_v<fun, void() const volatile>));
    EXPECT_FALSE((safely_invocable_v<fun, void()&>));
    EXPECT_FALSE((safely_invocable_v<fun, void() const&>));
    EXPECT_FALSE((safely_invocable_v<fun, void() volatile&>));
    EXPECT_FALSE((safely_invocable_v<fun, void() const volatile&>));
    EXPECT_TRUE((safely_invocable_v<fun, void() &&>));
    EXPECT_TRUE((safely_invocable_v<fun, void() const&&>));
    EXPECT_FALSE((safely_invocable_v<fun, void() volatile&&>));
    EXPECT_FALSE((safely_invocable_v<fun, void() const volatile&&>));
  }
}
