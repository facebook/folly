/*
 * Copyright 2011-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * This file is supposed to be included from within
 * FBVectorBenchmark. Do not use otherwise.
 */

BENCHMARK(BENCHFUN(zzInitRNG)) {
  // LOG(INFO) << "\nTesting with type " << typeid(VECTOR).name() << "\n";
  srand(seed);
}

BENCHMARK(BENCHFUN(defaultCtor), iters) {
  FOR_EACH_RANGE (i, 0, iters) {
    VECTOR v[4096];
    doNotOptimizeAway(&v);
  }
}

void BENCHFUN(sizeCtor)(int iters, int size) {
  FOR_EACH_RANGE (i, 0, iters) {
    VECTOR v(size);
    doNotOptimizeAway(&v);
  }
}
BENCHMARK_PARAM(BENCHFUN(sizeCtor), 128);
BENCHMARK_PARAM(BENCHFUN(sizeCtor), 1024);
BENCHMARK_PARAM(BENCHFUN(sizeCtor), 1048576);

void BENCHFUN(fillCtor)(int iters, int size) {
  FOR_EACH_RANGE (i, 0, iters) {
    VECTOR v(size_t(size), randomObject<VECTOR::value_type>());
    doNotOptimizeAway(&v);
  }
}
BENCHMARK_PARAM(BENCHFUN(fillCtor), 128);
BENCHMARK_PARAM(BENCHFUN(fillCtor), 1024);
BENCHMARK_PARAM(BENCHFUN(fillCtor), 10240);

void BENCHFUN(pushBack)(int iters, int size) {
  auto const obj = randomObject<VECTOR::value_type>();
  FOR_EACH_RANGE (i, 0, iters) {
    VECTOR v;
    FOR_EACH_RANGE (j, 0, size) { v.push_back(obj); }
  }
}
BENCHMARK_PARAM(BENCHFUN(pushBack), 128);
BENCHMARK_PARAM(BENCHFUN(pushBack), 1024);
BENCHMARK_PARAM(BENCHFUN(pushBack), 10240);
BENCHMARK_PARAM(BENCHFUN(pushBack), 102400);
BENCHMARK_PARAM(BENCHFUN(pushBack), 512000);

void BENCHFUN(reserve)(int iters, int /* size */) {
  auto const obj = randomObject<VECTOR::value_type>();
  VECTOR v(random(0U, 10000U), obj);
  FOR_EACH_RANGE (i, 0, iters) { v.reserve(random(0U, 100000U)); }
}
BENCHMARK_PARAM(BENCHFUN(reserve), 128);
BENCHMARK_PARAM(BENCHFUN(reserve), 1024);
BENCHMARK_PARAM(BENCHFUN(reserve), 10240);

void BENCHFUN(insert)(int iters, int /* size */) {
  auto const obj1 = randomObject<VECTOR::value_type>();
  auto const obj2 = randomObject<VECTOR::value_type>();
  VECTOR v(random(0U, 1U), obj1);
  FOR_EACH_RANGE (i, 0, iters / 100) { v.insert(v.begin(), obj2); }
}
BENCHMARK_PARAM(BENCHFUN(insert), 100);

void BENCHFUN(erase)(int iters, int /* size */) {
  auto const obj1 = randomObject<VECTOR::value_type>();
  VECTOR v(random(0U, 100U), obj1);
  FOR_EACH_RANGE (i, 0, iters) {
    if (v.empty()) {
      continue;
    }
    v.erase(v.begin());
  }
}
BENCHMARK_PARAM(BENCHFUN(erase), 1024);
