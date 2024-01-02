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

#pragma once

#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/container/Iterator.h>
#include <folly/lang/Hint.h>
#include <folly/memory/UninitializedMemoryHacks.h>

#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if FOLLY_HAS_STRING_VIEW
#include <string_view>
#endif

#if FOLLY_HAS_RANGES
#include <ranges>
#endif

namespace folly {
namespace detail {

template <template <typename...> class Templ, typename T>
struct instance_of : std::false_type {};

template <template <typename...> class Templ, typename... Args>
struct instance_of<Templ, Templ<Args...>> : std::true_type {};

template <template <typename...> class Templ, typename T>
constexpr bool instance_of_v = instance_of<Templ, T>::value;

#if FOLLY_HAS_RANGES
template <typename R>
constexpr bool guaranteed_contigious_range_cpp20_v =
    std::ranges::contiguous_range<R>;
#endif

template <typename R>
constexpr bool guaranteed_contigious_range_cpp14_v =
    instance_of_v<std::vector, R> || instance_of_v<std::basic_string, R> ||
    std::is_pointer_v<typename R::iterator>
#if FOLLY_HAS_STRING_VIEW
    || instance_of_v<std::basic_string_view, R>
#endif
    ;

#if FOLLLY_HAS_RANGES

template <typename R>
constexpr bool guaranteed_contigious_range =
    guaranteed_contigious_range_cpp20_v<R>;

#else

template <typename R>
constexpr bool guaranteed_contigious_range =
    guaranteed_contigious_range_cpp14_v<R>;

#endif

template <typename Container, bool = guaranteed_contigious_range<Container>>
struct tape_reference_traits {
  using iterator = typename Container::const_iterator;
  using reference = Range<iterator>;

  static constexpr reference make(iterator f, iterator l) {
    return reference{f, l};
  }
};

template <typename Container>
struct tape_reference_traits<Container, true> {
  using iterator = typename Container::const_iterator;
  using value_type = typename std::iterator_traits<iterator>::value_type;
  using reference = Range<const value_type*>;

  static constexpr auto* get_address(iterator it) {
    // std::to_address is only available since C++20
    if constexpr (std::is_pointer_v<iterator>) {
      return it;
    } else {
      return it.operator->();
    }
  }

  static constexpr reference make(iterator f, iterator l) {
    return reference{get_address(f), get_address(l)};
  }
};

template <typename R>
using get_range_const_iterator_t =
    decltype(std::cbegin(std::declval<const R&>()));

struct fake_type {};

template <typename R>
using maybe_range_const_iterator_t =
    detected_or_t<fake_type*, get_range_const_iterator_t, R>;

template <typename R>
using maybe_range_value_t =
    iterator_value_type_t<maybe_range_const_iterator_t<R>>;

// This is a big function to inline but it's used insie a big function too
template <typename I, typename S>
auto compute_total_tape_len_if_possible(I f, S l) {
  using success = std::pair<std::size_t, std::size_t>;
  using failure = fake_type;
  if constexpr (!iterator_category_matches_v<I, std::forward_iterator_tag>) {
    return failure{};
  }
  // We have to special case StringPiece to special case `const char*` and
  // `char[]`
  else if constexpr (std::is_convertible_v<
                         iterator_value_type_t<I>,
                         folly::StringPiece>) {
    std::size_t records_size = 0U;
    std::size_t flat_size = 0U;

    for (I i = f; i != l; ++i) {
      ++records_size;
      flat_size += folly::StringPiece(*i).size();
    }
    return success{records_size, flat_size};
  } else if constexpr (!range_has_known_distance_v<iterator_value_type_t<I>>) {
    return failure{};
  } else {
    std::size_t records_size = 0U;
    std::size_t flat_size = 0U;

    for (I i = f; i != l; ++i) {
      ++records_size;
      flat_size +=
          static_cast<std::size_t>(std::distance(std::begin(*i), std::end(*i)));
    }
    return success{records_size, flat_size};
  }
}

template <typename Container, typename I, typename S>
void append_range_unsafe(Container& c, I f, S l) {
  if constexpr (
      !iterator_category_matches_v<I, std::random_access_iterator_tag> ||
      !std::is_trivially_copy_constructible_v<iterator_value_type_t<I>> ||
      !(instance_of_v<std::vector, Container> ||
        instance_of_v<std::basic_string, Container>)) {
    c.insert(c.end(), f, l);
  } else {
    folly::compiler_may_unsafely_assume(l >= f);
    auto old_size = c.size();
    detail::unsafeVectorSetLargerSize(c, c.size() + (l - f));
    std::copy(f, l, c.begin() + old_size);
  }
}

} // namespace detail
} // namespace folly
