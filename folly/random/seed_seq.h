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

#include <cstring>
#include <random>
#include <type_traits>

#include <folly/Traits.h>
#include <folly/container/span.h>
#include <folly/functional/Invoke.h>

namespace folly {

/// seed_seq_generate
///
/// Deterministically fills a fixed-size state buffer, represented as a span of
/// words, with uniformly-distributed data generated from a seed-sequence.
///
/// Handles mismatches between the seed-sequence result-type and the word type
/// of the buffer being filled.
struct seed_seq_generate_fn {
 private:
  FOLLY_CREATE_MEMBER_INVOKER(invoke_generate, generate);
  using traits = invoke_traits<invoke_generate>;
  template <typename T, typename R = typename T::result_type>
  static constexpr bool seq_inv_v = traits::is_invocable_v<T&, R*, R*>;
  template <typename T, typename R = typename T::result_type>
  static constexpr bool seq_nx_inv_v =
      traits::is_nothrow_invocable_v<T&, R*, R*>;

 public:
  template <
      typename SeedSeq,
      typename Word,
      std::size_t Size,
      std::enable_if_t<std::is_trivially_copyable_v<Word>, int> = 0,
      std::enable_if_t<Size != dynamic_extent, int> = 0,
      std::enable_if_t<seq_inv_v<SeedSeq>, int> = 0>
  void operator()(span<Word, Size> dst, SeedSeq& seq) const //
      noexcept(seq_nx_inv_v<SeedSeq>) {
    using out_t = typename SeedSeq::result_type;
    constexpr auto dst_bytes = Size * sizeof(Word);
    constexpr auto out_sz = (dst_bytes + sizeof(out_t) - 1) / sizeof(out_t);
    out_t out[out_sz];
    seq.generate(out, out + out_sz);
    std::memcpy(dst.data(), out, dst_bytes);
  }
};
constexpr inline seed_seq_generate_fn seed_seq_generate{};

/// make_std_seed_seq
///
/// Constructs a std::seed_seq seed-sequence with a given seed value.
///
/// Handles mismatches between the std::seed_seq result-type and the input value
/// type.
struct make_std_seed_seq_fn {
  template <
      typename Word,
      std::enable_if_t<is_non_bool_integral_v<Word>, int> = 0>
  std::seed_seq operator()(Word word) const {
    using val_t = std::seed_seq::result_type;
    constexpr auto vals_sz = (sizeof(Word) + sizeof(val_t) - 1) / sizeof(val_t);
    val_t vals[vals_sz] = {};
    std::memcpy(vals, &word, sizeof(Word));
    return std::seed_seq(vals, vals + vals_sz);
  }
};
constexpr inline make_std_seed_seq_fn make_std_seed_seq{};

} // namespace folly
