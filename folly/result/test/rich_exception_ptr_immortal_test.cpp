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

#include <folly/result/test/rich_exception_ptr_check_get.h>

#include <folly/portability/GTest.h>
#include <folly/result/immortal_rich_error.h>

// Tests for immortal `rich_exception_ptr`, covering exception queries &
// `throw_exception`. Complements `rich_exception_ptr_fundamental_test.cpp`.
// Look at `immortal_rich_error_test.cpp` for usage-oriented tests.

#if FOLLY_HAS_RESULT

namespace folly::detail {

// `get_outer_exception` analogs of `get_exception` & `get_mutable_exception_fn`
template <typename Ptr>
struct rich_ptr_stub { // quacks like `rich_ptr_to_underlying_error`
  Ptr ptr_;
  Ptr get() { return ptr_; }
};
template <typename Ex>
struct get_outer_exception_fn {
  auto operator()(const auto& rep) const {
    return rich_ptr_stub{rep.template get_outer_exception<Ex>()};
  }
};
template <typename Ex>
struct get_mutable_outer_exception_fn {
  auto operator()(auto& rep) const {
    static_assert(!std::is_const_v<std::remove_reference_t<decltype(rep)>>);
    return rich_ptr_stub{rep.template get_outer_exception<Ex>()};
  }
};

template <
    typename Ptr,
    template <typename> class Get,
    template <typename> class GetMut>
auto getQueries(auto& rep) {
  return std::make_tuple(
      std::pair{Ptr::Constexpr, Get<RichErr>{}(rep).get()},
      std::pair{Ptr::Constexpr, Get<rich_error_base>{}(rep).get()},

      std::pair{Ptr::Immutable, Get<rich_error<RichErr>>{}(rep).get()},
      std::pair{Ptr::Immutable, Get<std::exception>{}(rep).get()},

      std::pair{Ptr::Mutable, GetMut<RichErr>{}(rep).get()},
      std::pair{Ptr::Mutable, GetMut<rich_error_base>{}(rep).get()},
      std::pair{Ptr::Mutable, GetMut<rich_error<RichErr>>{}(rep).get()},
      std::pair{Ptr::Mutable, GetMut<std::exception>{}(rep).get()});
}
// Immortal REPs have 3 distinct exception object pointers:
//   - Immortal / static user base (RichErr, rich_error_base)
//   - Immutable singleton for wrapper (rich_error<RichErr>, std::exception)
//   - Mutable singleton (all mutable queries return this)
// This test exercises all of them, and checks that the pointers are/aren't
// equal, as expected.
void checkPointerEquivalence(auto& rep) {
  enum class Ptr { Constexpr, Immutable, Mutable };
  auto queries = std::tuple_cat(
      getQueries<Ptr, get_exception_fn, get_mutable_exception_fn>(rep),
      getQueries<Ptr, get_outer_exception_fn, get_mutable_outer_exception_fn>(
          rep));

  // When `A` and `B` are related, return `true` iff `a` and `b` are the same
  // object. Otherwise return `nullopt`.
  auto compareIfRelated = []<typename A, typename B>(const A* a, const B* b)
      -> std::optional<bool> {
    if constexpr (std::is_base_of_v<A, B>) {
      return a == static_cast<const A*>(b);
    } else if constexpr (std::is_base_of_v<B, A>) {
      return b == static_cast<const B*>(a);
    } else {
      return std::nullopt;
    }
  };

  // Compare all pairs where types are related
  size_t comparisonCount = 0;
  constexpr size_t numQueries = std::tuple_size_v<decltype(queries)>;
  [&]<size_t... Indices>(std::index_sequence<Indices...>) {
    (
        [&] {
          auto [aCategory, a] = std::get<Indices / numQueries>(queries);
          auto [bCategory, b] = std::get<Indices % numQueries>(queries);
          if (auto ptrsEqual = compareIfRelated(a, b)) {
            EXPECT_EQ((aCategory == bCategory), *ptrsEqual);
            ++comparisonCount;
          }
        }(),
        ...);
  }(std::make_index_sequence<numQueries * numQueries>{});

  // Check we did the expected number of comparisons. The above queries have 2
  // of each type (const & mutable), times 2 (get_exception & get_outer...):
  //   `std::exception`, `RichErr`, `rich_error_base`, `rich_error<RichError>`
  // So there are 16 = 2 * 2 * 4 in all, for 256 = 16 * 16 possible comparisons.
  //
  // But, `std::exception` (4 queries) is not comparable with `RichErr` (4) &
  // `rich_error_base` (4). Counting twice for both comparison orders, we must
  // exclude 64, leaving 192 = 256 - 2 * (4 * (4 + 4))).
  EXPECT_EQ(comparisonCount, 192);
}

template <typename REP>
void checkAccessExceptions() {
  REP rep = immortal_rich_error_t<REP, rich_error<RichErr>>{}.ptr();

  // Behavior matches "owned", except for the mutable/immutable singleton split
  checkGetExceptionForRichErr</*constAndMutPointersAreSame=*/false>(rep);
  checkGetExceptionForEpitaphRichErr<REP, /*PointersAreSame=*/false>(
      // We cannot use `REP` for the inner error, since `underlying_error()` is
      // always `rich_exception_ptr`.
      immortal_rich_error<rich_error<RichErr>>.ptr());

  // NB: Since immortals do not support `underlying_error()` today, there's no
  // test coverage for immortal epitaph wrappers.

  // Exercise (get, outer) x (const, mutable) queries for types served by
  // (constexpr pointer, immutable singleton, mutable singleton)
  checkPointerEquivalence(rep);
}

TEST(RichExceptionPtrImmortal, accessExceptions) {
  checkAccessExceptions<rich_exception_ptr>();
}
TEST(RichExceptionPtrImmortal, accessExceptionsSeparate) {
  checkAccessExceptions<rich_exception_ptr_separate>();
}
TEST(RichExceptionPtrImmortal, accessExceptionsPacked) {
  if constexpr (rich_exception_ptr_packed_storage::is_supported) {
    checkAccessExceptions<rich_exception_ptr_packed>();
  } else {
    GTEST_SKIP() << "Packed storage not supported on this platform";
  }
}

template <typename REP>
void checkThrowExceptionImmortal() {
  auto checkThrow = [&](auto... args) {
    REP rep = immortal_rich_error_t<REP, rich_error<RichErr>>{}.ptr();
    REP rep_copy{rep}; // NOLINT(performance-unnecessary-copy-initialization)
    // It is important that we catch `rich_error<RichErr>` and not merely
    // `RichErr` here -- that matches the "owned" behavior.
    EXPECT_THROW(rep_copy.throw_exception(args...), rich_error<RichErr>);
    EXPECT_EQ(rep, rep_copy); // Throwing is const
  };
  checkThrow(); // `result` flavor
  checkThrow(try_rich_exception_ptr_private_t{}); // `Try` flavor
}

TEST(RichExceptionPtrImmortal, throwException) {
  checkThrowExceptionImmortal<rich_exception_ptr>();
}
TEST(RichExceptionPtrImmortal, throwExceptionSeparate) {
  checkThrowExceptionImmortal<rich_exception_ptr_separate>();
}
TEST(RichExceptionPtrImmortal, throwExceptionPacked) {
  if constexpr (rich_exception_ptr_packed_storage::is_supported) {
    checkThrowExceptionImmortal<rich_exception_ptr_packed>();
  } else {
    GTEST_SKIP() << "Packed storage not supported on this platform";
  }
}

} // namespace folly::detail

#endif // FOLLY_HAS_RESULT
