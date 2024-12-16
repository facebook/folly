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

#include <folly/CancellationToken.h>

#include <folly/Benchmark.h>

using namespace folly;

//
// Merge benchmarks
//
// NOTE: The "RVal" `copy(token)` benchmarks  appear ~10ns per token more
// expensive than "LVal" `token)` ones, for reasons unclear -- moving
// ownership of a token SHOULD be nearly free.  Make sure this gap does not
// grow, since the major point of comparing "lval" and "rval" is to show we
// DO NOT introduce an extra copy when a move would do.
//

struct BenchMergeContext {
  CancellationSource s1, s2, s3;
  CancellationToken t1, t2, t3;
  BenchMergeContext()
      : t1(s1.getToken()), t2(s2.getToken()), t3(s3.getToken()) {}
  template <typename... Ts>
  static inline auto merge(Ts&&... ts) { // abbreviation
    return CancellationToken::merge(std::forward<Ts>(ts)...);
  }
};

template <typename MergeFn>
void benchMerge(size_t iters, MergeFn mergeFn) {
  folly::BenchmarkSuspender susp;
  BenchMergeContext ctx;
  susp.dismissing([&]() {
    for (size_t i = 0; i < iters; i++) {
      folly::doNotOptimizeAway(mergeFn(ctx));
    }
  });
}

BENCHMARK(merge1RVal, iters) {
  benchMerge(iters, [](auto& c) { return c.merge(copy(c.t1)); });
}
BENCHMARK(merge1LVal, iters) {
  benchMerge(iters, [](auto& c) { return c.merge(c.t1); });
}
BENCHMARK_DRAW_LINE();

BENCHMARK(merge2_Empty_Empty, iters) {
  benchMerge(iters, [](auto& c) {
    return c.merge(CancellationToken(), CancellationToken());
  });
}
BENCHMARK_DRAW_LINE();

BENCHMARK(merge2_RValA_Empty, iters) {
  benchMerge(iters, [](auto& c) {
    return c.merge(copy(c.t1), CancellationToken());
  });
}
BENCHMARK(merge2_LValA_Empty, iters) {
  benchMerge(iters, [](auto& c) { return c.merge(c.t1, CancellationToken()); });
}
BENCHMARK_DRAW_LINE();

BENCHMARK(merge2_RValA_RValA, iters) {
  benchMerge(iters, [](auto& c) { return c.merge(copy(c.t1), copy(c.t1)); });
}
BENCHMARK(merge2_LValA_RValA, iters) {
  benchMerge(iters, [](auto& c) { return c.merge(c.t1, copy(c.t1)); });
}
BENCHMARK(merge2_LValA_LValA, iters) {
  benchMerge(iters, [](auto& c) { return c.merge(c.t1, c.t1); });
}
BENCHMARK_DRAW_LINE();

BENCHMARK(merge2_RValA_RValB, iters) {
  benchMerge(iters, [](auto& c) { return c.merge(copy(c.t1), copy(c.t2)); });
}
BENCHMARK(merge2_LValA_RValB, iters) {
  benchMerge(iters, [](auto& c) { return c.merge(c.t1, copy(c.t2)); });
}
BENCHMARK(merge2_LValA_LValB, iters) {
  benchMerge(iters, [](auto& c) { return c.merge(c.t1, c.t2); });
}
BENCHMARK_DRAW_LINE();

BENCHMARK(merge3_Empty_RValA_Empty, iters) {
  benchMerge(iters, [](auto& c) {
    return c.merge(CancellationToken(), copy(c.t1), CancellationToken());
  });
}
BENCHMARK(merge3_Empty_LValA_Empty, iters) {
  benchMerge(iters, [](auto& c) {
    return c.merge(CancellationToken(), c.t1, CancellationToken());
  });
}
BENCHMARK_DRAW_LINE();

BENCHMARK(merge3_RValA_Empty_RValA, iters) {
  benchMerge(iters, [](auto& c) {
    return c.merge(copy(c.t1), CancellationToken(), copy(c.t1));
  });
}
BENCHMARK(merge3_LValA_Empty_RValA, iters) {
  benchMerge(iters, [](auto& c) {
    return c.merge(c.t1, CancellationToken(), copy(c.t1));
  });
}
BENCHMARK(merge3_LValA_Empty_LValA, iters) {
  benchMerge(iters, [](auto& c) {
    return c.merge(c.t1, CancellationToken(), c.t1);
  });
}
BENCHMARK_DRAW_LINE();

BENCHMARK(merge3_RValA_RValB_RValB, iters) {
  benchMerge(iters, [](auto& c) {
    return c.merge(copy(c.t1), copy(c.t2), copy(c.t2));
  });
}
BENCHMARK(merge3_LValA_RValB_LValB, iters) {
  benchMerge(iters, [](auto& c) { return c.merge(c.t1, copy(c.t2), c.t2); });
}
BENCHMARK(merge3_LValA_LValB_LValB, iters) {
  benchMerge(iters, [](auto& c) { return c.merge(c.t1, c.t2, c.t2); });
}
BENCHMARK_DRAW_LINE();

BENCHMARK(merge3_RValA_RValB_RValC, iters) {
  benchMerge(iters, [](auto& c) {
    return c.merge(copy(c.t1), copy(c.t2), copy(c.t3));
  });
}
BENCHMARK(merge3_LValA_RValB_LValC, iters) {
  benchMerge(iters, [](auto& c) { return c.merge(c.t1, copy(c.t2), c.t3); });
}
BENCHMARK(merge3_LValA_LValB_LValC, iters) {
  benchMerge(iters, [](auto& c) { return c.merge(c.t1, c.t2, c.t3); });
}
BENCHMARK_DRAW_LINE();

template <size_t NTokens, bool RVal>
void benchMergeNDistinct(size_t iters) {
  folly::BenchmarkSuspender susp;
  std::array<CancellationSource, NTokens> srcs{};
  std::array<CancellationToken, NTokens> toks{};
  for (size_t i = 0; i < NTokens; ++i) {
    toks[i] = srcs[i].getToken();
  }
  susp.dismissing([&]() {
    for (size_t i = 0; i < iters; i++) {
      folly::doNotOptimizeAway(std::apply(
          [&](const auto&... ts) {
            if constexpr (RVal) {
              return CancellationToken::merge(copy(ts)...);
            } else {
              return CancellationToken::merge(ts...);
            }
          },
          toks));
    }
  });
}

BENCHMARK(merge3DistinctRVals, iters) {
  benchMergeNDistinct<3, true>(iters);
}
BENCHMARK(merge3DistinctLVals, iters) {
  benchMergeNDistinct<3, false>(iters);
}
BENCHMARK(merge4DistinctRVals, iters) {
  benchMergeNDistinct<4, true>(iters);
}
BENCHMARK(merge4DistinctLVals, iters) {
  benchMergeNDistinct<4, false>(iters);
}
BENCHMARK(merge5DistinctRVals, iters) {
  benchMergeNDistinct<5, true>(iters);
}
BENCHMARK(merge5DistinctLVals, iters) {
  benchMergeNDistinct<5, false>(iters);
}
BENCHMARK(merge7DistinctRVals, iters) {
  benchMergeNDistinct<7, true>(iters);
}
BENCHMARK(merge7DistinctLVals, iters) {
  benchMergeNDistinct<7, false>(iters);
}
BENCHMARK(merge10DistinctRVals, iters) {
  benchMergeNDistinct<10, true>(iters);
}
BENCHMARK(merge10DistinctLVals, iters) {
  benchMergeNDistinct<10, false>(iters);
}
BENCHMARK(merge15DistinctRVals, iters) {
  benchMergeNDistinct<15, true>(iters);
}
BENCHMARK(merge15DistinctLVals, iters) {
  benchMergeNDistinct<15, false>(iters);
}

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
