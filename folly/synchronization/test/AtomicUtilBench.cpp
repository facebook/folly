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

#include <folly/synchronization/AtomicUtil.h>

#include <random>

#include <folly/Benchmark.h>
#include <folly/init/Init.h>
#include <folly/lang/Keep.h>

#define FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_VAR(op, width)                       \
  extern "C" FOLLY_KEEP void                                                 \
      check_folly_atomic_fetch_##op##_u##width##_var_drop_fallback(          \
          std::atomic<std::uint##width##_t>& atomic, std::size_t bit) {      \
    folly::detail::atomic_fetch_##op##_fallback(                             \
        atomic, bit, std::memory_order_relaxed);                             \
  }                                                                          \
  extern "C" FOLLY_KEEP void                                                 \
      check_folly_atomic_fetch_##op##_u##width##_var_drop_native(            \
          std::atomic<std::uint##width##_t>& atomic, std::size_t bit) {      \
    folly::atomic_fetch_##op(atomic, bit, std::memory_order_relaxed);        \
  }                                                                          \
  extern "C" FOLLY_KEEP bool                                                 \
      check_folly_atomic_fetch_##op##_u##width##_var_keep_fallback(          \
          std::atomic<std::uint##width##_t>& atomic, std::size_t bit) {      \
    return folly::detail::atomic_fetch_##op##_fallback(                      \
        atomic, bit, std::memory_order_relaxed);                             \
  }                                                                          \
  extern "C" FOLLY_KEEP bool                                                 \
      check_folly_atomic_fetch_##op##_u##width##_var_keep_native(            \
          std::atomic<std::uint##width##_t>& atomic, std::size_t bit) {      \
    return folly::atomic_fetch_##op(atomic, bit, std::memory_order_relaxed); \
  }                                                                          \
  extern "C" FOLLY_KEEP void                                                 \
      check_folly_atomic_fetch_##op##_u##width##_var_cond_fallback(          \
          std::atomic<std::uint##width##_t>& atomic, std::size_t bit) {      \
    if (folly::detail::atomic_fetch_##op##_fallback(                         \
            atomic, bit, std::memory_order_relaxed)) {                       \
      folly::detail::keep_sink_nx();                                         \
    }                                                                        \
  }                                                                          \
  extern "C" FOLLY_KEEP void                                                 \
      check_folly_atomic_fetch_##op##_u##width##_var_cond_native(            \
          std::atomic<std::uint##width##_t>& atomic, std::size_t bit) {      \
    if (folly::atomic_fetch_##op(atomic, bit, std::memory_order_relaxed)) {  \
      folly::detail::keep_sink_nx();                                         \
    }                                                                        \
  }

#define FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_FIX(op, width, bit)                  \
  extern "C" FOLLY_KEEP void                                                 \
      check_folly_atomic_fetch_##op##_u##width##_fix_##bit##_drop_fallback(  \
          std::atomic<std::uint##width##_t>& atomic) {                       \
    folly::detail::atomic_fetch_##op##_fallback(                             \
        atomic, bit, std::memory_order_relaxed);                             \
  }                                                                          \
  extern "C" FOLLY_KEEP void                                                 \
      check_folly_atomic_fetch_##op##_u##width##_fix_##bit##_drop_native(    \
          std::atomic<std::uint##width##_t>& atomic) {                       \
    folly::atomic_fetch_##op(atomic, bit, std::memory_order_relaxed);        \
  }                                                                          \
  extern "C" FOLLY_KEEP bool                                                 \
      check_folly_atomic_fetch_##op##_u##width##_fix_##bit##_keep_fallback(  \
          std::atomic<std::uint##width##_t>& atomic) {                       \
    return folly::detail::atomic_fetch_##op##_fallback(                      \
        atomic, bit, std::memory_order_relaxed);                             \
  }                                                                          \
  extern "C" FOLLY_KEEP bool                                                 \
      check_folly_atomic_fetch_##op##_u##width##_fix_##bit##_keep_native(    \
          std::atomic<std::uint##width##_t>& atomic) {                       \
    return folly::atomic_fetch_##op(atomic, bit, std::memory_order_relaxed); \
  }                                                                          \
  extern "C" FOLLY_KEEP void                                                 \
      check_folly_atomic_fetch_##op##_u##width##_fix_##bit##_cond_fallback(  \
          std::atomic<std::uint##width##_t>& atomic) {                       \
    if (folly::detail::atomic_fetch_##op##_fallback(                         \
            atomic, bit, std::memory_order_relaxed)) {                       \
      folly::detail::keep_sink_nx();                                         \
    }                                                                        \
  }                                                                          \
  extern "C" FOLLY_KEEP void                                                 \
      check_folly_atomic_fetch_##op##_u##width##_fix_##bit##_cond_native(    \
          std::atomic<std::uint##width##_t>& atomic) {                       \
    if (folly::atomic_fetch_##op(atomic, bit, std::memory_order_relaxed)) {  \
      folly::detail::keep_sink_nx();                                         \
    }                                                                        \
  }

FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_VAR(set, 8)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_FIX(set, 8, 3)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_VAR(set, 16)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_FIX(set, 16, 3)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_FIX(set, 16, 11)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_VAR(reset, 8)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_FIX(reset, 8, 3)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_VAR(reset, 16)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_FIX(reset, 16, 3)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_FIX(reset, 16, 11)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_VAR(flip, 8)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_FIX(flip, 8, 3)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_VAR(flip, 16)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_FIX(flip, 16, 3)
FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_FIX(flip, 16, 11)

#undef FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_FIX
#undef FOLLY_ATOMIC_FETCH_BIT_OP_CHECK_VAR

namespace {

enum class what { drop, keep, cond };
template <what what_>
using what_constant = std::integral_constant<what, what_>;

using mo = std::memory_order;

struct sp_atomic_fetch_set_fn {
  template <typename w>
  bool operator()(std::atomic<w>& a, size_t bit, mo o) const {
    auto m0 = w(w(1u) << bit);
    auto v = a.load(o);
    a.store(v | m0, o);
    return bool(v & m0);
  }
};
inline sp_atomic_fetch_set_fn sp_atomic_fetch_set{};

std::random_device csprng{};
std::mt19937 rng{csprng()};

template <typename word_type, size_t nrands, size_t nwords>
auto make_rands_var() {
  constexpr size_t offmod = 8 * sizeof(word_type);
  std::array<std::pair<uint8_t, uint8_t>, nrands> rands{};
  for (auto& w : rands) {
    uint8_t idx = folly::to_narrow(rng() % nwords);
    uint8_t off = folly::to_narrow(rng() % offmod);
    w = {idx, off};
  }
  return rands;
}

template <typename word_type, size_t nrands, size_t nwords>
auto make_rands_fix() {
  std::array<uint8_t, nrands> rands{};
  for (auto& w : rands) {
    w = folly::to_narrow(rng() % nwords);
  }
  return rands;
}

template <typename Op, typename what_>
void accumulate(folly::BenchmarkSuspender& braces, size_t iters, what_, Op op) {
  braces.dismissing([&] {
    size_t sum = 0;
    for (size_t i = 0; i < iters; ++i) {
      folly::makeUnpredictable(sum);
      auto res = op(i);
      switch (what_{}) {
        case what::drop:
          ++sum;
          break;
        case what::keep:
          sum += size_t(res);
          break;
        case what::cond:
          sum = 0;
          if (FOLLY_LIKELY(res)) {
            asm volatile("");
          }
          break;
      }
    }
    folly::doNotOptimizeAway(sum);
  });
}

template <typename word_type, typename op, typename what_>
FOLLY_NOINLINE void atomic_fetch_op_var_(
    word_type init, op op_, what_ wh, size_t iters) {
  constexpr size_t nrands = 256;
  constexpr size_t nwords = 16;

  folly::BenchmarkSuspender braces;

  auto rands = make_rands_var<word_type, nrands, nwords>();

  std::array<std::atomic<word_type>, nwords> words;
  std::fill(words.begin(), words.end(), init);
  folly::makeUnpredictable(words);

  accumulate(braces, iters, wh, [&](auto i) {
    auto [idx, off] = rands[i % nrands];
    return op_(words[idx], off, std::memory_order_relaxed);
  });
  folly::doNotOptimizeAway(words);
}

template <typename word_type, typename op, typename what_>
FOLLY_NOINLINE void atomic_fetch_op_fix_(
    word_type init, op op_, what_ wh, size_t iters) {
  constexpr size_t nrands = 256;
  constexpr size_t nwords = 16;

  folly::BenchmarkSuspender braces;

  auto rands = make_rands_fix<word_type, nrands, nwords>();

  std::array<std::atomic<word_type>, nwords> words;
  std::fill(words.begin(), words.end(), init);
  folly::makeUnpredictable(words);

  accumulate(braces, iters, wh, [&](auto i) {
    auto idx = rands[i % nrands];
    return op_(words[idx], 3, std::memory_order_relaxed);
  });
  folly::doNotOptimizeAway(words);
}

} // namespace

BENCHMARK(atomic_fetch_set_u8_var_drop_monopoly, iters) {
  auto op = sp_atomic_fetch_set;
  atomic_fetch_op_var_(uint8_t(0), op, what_constant<what::drop>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_var_drop_fallback, iters) {
  auto op = folly::detail::atomic_fetch_set_fallback;
  atomic_fetch_op_var_(uint8_t(0), op, what_constant<what::drop>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_var_drop_native, iters) {
  auto op = folly::atomic_fetch_set;
  atomic_fetch_op_var_(uint8_t(0), op, what_constant<what::drop>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_var_keep_monopoly, iters) {
  auto op = sp_atomic_fetch_set;
  atomic_fetch_op_var_(uint8_t(0), op, what_constant<what::keep>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_var_keep_fallback, iters) {
  auto op = folly::detail::atomic_fetch_set_fallback;
  atomic_fetch_op_var_(uint8_t(0), op, what_constant<what::keep>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_var_keep_native, iters) {
  auto op = folly::atomic_fetch_set;
  atomic_fetch_op_var_(uint8_t(0), op, what_constant<what::keep>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_var_cond_monopoly, iters) {
  auto op = sp_atomic_fetch_set;
  atomic_fetch_op_var_(uint8_t(0), op, what_constant<what::cond>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_var_cond_fallback, iters) {
  auto op = folly::detail::atomic_fetch_set_fallback;
  atomic_fetch_op_var_(uint8_t(0), op, what_constant<what::cond>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_var_cond_native, iters) {
  auto op = folly::atomic_fetch_set;
  atomic_fetch_op_var_(uint8_t(0), op, what_constant<what::cond>{}, iters);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(atomic_fetch_set_u8_fix_drop_monopoly, iters) {
  auto op = sp_atomic_fetch_set;
  atomic_fetch_op_fix_(uint8_t(0), op, what_constant<what::drop>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_fix_drop_fallback, iters) {
  auto op = folly::detail::atomic_fetch_set_fallback;
  atomic_fetch_op_fix_(uint8_t(0), op, what_constant<what::drop>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_fix_drop_native, iters) {
  auto op = folly::atomic_fetch_set;
  atomic_fetch_op_fix_(uint8_t(0), op, what_constant<what::drop>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_fix_keep_monopoly, iters) {
  auto op = sp_atomic_fetch_set;
  atomic_fetch_op_fix_(uint8_t(0), op, what_constant<what::keep>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_fix_keep_fallback, iters) {
  auto op = folly::detail::atomic_fetch_set_fallback;
  atomic_fetch_op_fix_(uint8_t(0), op, what_constant<what::keep>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_fix_keep_native, iters) {
  auto op = folly::atomic_fetch_set;
  atomic_fetch_op_fix_(uint8_t(0), op, what_constant<what::keep>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_fix_cond_monopoly, iters) {
  auto op = sp_atomic_fetch_set;
  atomic_fetch_op_fix_(uint8_t(0), op, what_constant<what::cond>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_fix_cond_fallback, iters) {
  auto op = folly::detail::atomic_fetch_set_fallback;
  atomic_fetch_op_fix_(uint8_t(0), op, what_constant<what::cond>{}, iters);
}

BENCHMARK(atomic_fetch_set_u8_fix_cond_native, iters) {
  auto op = folly::atomic_fetch_set;
  atomic_fetch_op_fix_(uint8_t(0), op, what_constant<what::cond>{}, iters);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(atomic_fetch_set_u16_var_drop_monopoly, iters) {
  auto op = sp_atomic_fetch_set;
  atomic_fetch_op_var_(uint16_t(0), op, what_constant<what::drop>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_var_drop_fallback, iters) {
  auto op = folly::detail::atomic_fetch_set_fallback;
  atomic_fetch_op_var_(uint16_t(0), op, what_constant<what::drop>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_var_drop_native, iters) {
  auto op = folly::atomic_fetch_set;
  atomic_fetch_op_var_(uint16_t(0), op, what_constant<what::drop>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_var_keep_monopoly, iters) {
  auto op = sp_atomic_fetch_set;
  atomic_fetch_op_var_(uint16_t(0), op, what_constant<what::keep>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_var_keep_fallback, iters) {
  auto op = folly::detail::atomic_fetch_set_fallback;
  atomic_fetch_op_var_(uint16_t(0), op, what_constant<what::keep>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_var_keep_native, iters) {
  auto op = folly::atomic_fetch_set;
  atomic_fetch_op_var_(uint16_t(0), op, what_constant<what::keep>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_var_cond_monopoly, iters) {
  auto op = sp_atomic_fetch_set;
  atomic_fetch_op_var_(uint16_t(0), op, what_constant<what::cond>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_var_cond_fallback, iters) {
  auto op = folly::detail::atomic_fetch_set_fallback;
  atomic_fetch_op_var_(uint16_t(0), op, what_constant<what::cond>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_var_cond_native, iters) {
  auto op = folly::atomic_fetch_set;
  atomic_fetch_op_var_(uint16_t(0), op, what_constant<what::cond>{}, iters);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(atomic_fetch_set_u16_fix_drop_monopoly, iters) {
  auto op = sp_atomic_fetch_set;
  atomic_fetch_op_fix_(uint16_t(0), op, what_constant<what::drop>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_fix_drop_fallback, iters) {
  auto op = folly::detail::atomic_fetch_set_fallback;
  atomic_fetch_op_fix_(uint16_t(0), op, what_constant<what::drop>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_fix_drop_native, iters) {
  auto op = folly::atomic_fetch_set;
  atomic_fetch_op_fix_(uint16_t(0), op, what_constant<what::drop>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_fix_keep_monopoly, iters) {
  auto op = sp_atomic_fetch_set;
  atomic_fetch_op_fix_(uint16_t(0), op, what_constant<what::keep>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_fix_keep_fallback, iters) {
  auto op = folly::detail::atomic_fetch_set_fallback;
  atomic_fetch_op_fix_(uint16_t(0), op, what_constant<what::keep>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_fix_keep_native, iters) {
  auto op = folly::atomic_fetch_set;
  atomic_fetch_op_fix_(uint16_t(0), op, what_constant<what::keep>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_fix_cond_monopoly, iters) {
  auto op = sp_atomic_fetch_set;
  atomic_fetch_op_fix_(uint16_t(0), op, what_constant<what::cond>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_fix_cond_fallback, iters) {
  auto op = folly::detail::atomic_fetch_set_fallback;
  atomic_fetch_op_fix_(uint16_t(0), op, what_constant<what::cond>{}, iters);
}

BENCHMARK(atomic_fetch_set_u16_fix_cond_native, iters) {
  auto op = folly::atomic_fetch_set;
  atomic_fetch_op_fix_(uint16_t(0), op, what_constant<what::cond>{}, iters);
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
