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

#include <type_traits>
#include <utility>

#include <folly/Utility.h>
#include <folly/functional/Invoke.h>

namespace folly {

/// hash_counter_engine
///
/// Hashes an incrementing integer to produce a pseudo-random output.
///
/// Pros:
/// * Keeps minimal state.
///
/// Cons:
/// * Randomness quality depends on the hash function.
/// * Less performant than a dedicated random engine, even than one based on the
///   same hash function might be.
template <typename Hash, typename State>
class hash_counter_engine : private Hash {
 private:
  static auto call(Hash& hash, State state) {
    if constexpr (is_invocable_v<Hash&, State>) {
      return hash(state);
    } else if constexpr (std::is_invocable_v<Hash&, uint8_t const*, size_t>) {
      auto const ptr = reinterpret_cast<uint8_t const*>(std::addressof(state));
      return hash(ptr, sizeof(state));
    } else {
      static_assert(always_false<Hash, State>);
    }
  }

 public:
  using state_type = State;
  using result_type =
      decltype(call(std::declval<Hash&>(), std::declval<state_type>()));
  static_assert(std::is_integral_v<result_type>);
  static_assert(!std::is_signed_v<result_type>);

  static constexpr result_type min() { return result_type(); }
  static constexpr result_type max() { return ~result_type(); }

  explicit hash_counter_engine(state_type state) noexcept : state_{state} {}
  hash_counter_engine(Hash const& hash, state_type state) noexcept
      : Hash{hash}, state_{state} {}

  result_type operator()() { return call(*this, state_++); }

 private:
  state_type state_;
};

} // namespace folly
