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

#include <folly/Benchmark.h>

#include <string>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <uuid/uuid.h>

#include <folly/codec/Uuid.h>

static std::string generateRandomGuid() {
  char guid[40];
  uuid_t uuid_raw;
  uuid_generate(uuid_raw);
  uuid_unparse_lower(uuid_raw, guid);

  return guid;
}

template <auto folly_uuid_parse_func>
inline void folly_uuid_parse_benchmark(size_t n) {
  std::vector<std::string> sVec;
  BENCHMARK_SUSPEND {
    sVec.resize(n);
    for (auto& s : sVec) {
      s = generateRandomGuid();
    }
  }

  std::string out;
  for (size_t i = 0; i < n; ++i) {
    folly::compiler_must_not_elide(folly_uuid_parse_func(out, sVec[i]));
    folly::compiler_must_not_elide(out);
  }
}

#if (FOLLY_X64 && defined(__AVX2__))
BENCHMARK(uuid_parse_folly_avx2, n) {
  folly_uuid_parse_benchmark<folly::detail::uuid_parse_avx2>(n);
}
#endif

#if (FOLLY_X64 && defined(__SSSE3__))
BENCHMARK(uuid_parse_folly_ssse3, n) {
  folly_uuid_parse_benchmark<folly::detail::uuid_parse_ssse3>(n);
}
#endif

BENCHMARK(uuid_parse_folly_scalar, n) {
  folly_uuid_parse_benchmark<folly::detail::uuid_parse_scalar>(n);
}

BENCHMARK(uuid_parse_folly, n) {
  using StrParseFunc = folly::UuidParseCode (*)(std::string&, std::string_view);
  folly_uuid_parse_benchmark<static_cast<StrParseFunc>(folly::uuid_parse)>(n);
}

BENCHMARK(uuid_parse_glibc, n) {
  std::vector<std::string> sVec;
  BENCHMARK_SUSPEND {
    sVec.resize(n);
    for (auto& s : sVec) {
      s = generateRandomGuid();
    }
  }
  uuid_t uuid;
  for (size_t i = 0; i < n; ++i) {
    folly::compiler_must_not_elide(uuid_parse(sVec[i].c_str(), uuid));
    folly::compiler_must_not_elide(uuid);
  }
}

BENCHMARK(uuid_parse_boost, n) {
  std::vector<std::string> sVec;
  BENCHMARK_SUSPEND {
    sVec.resize(n);
    for (auto& s : sVec) {
      s = generateRandomGuid();
    }
  }
  boost::uuids::uuid uuid;
  for (size_t i = 0; i < n; ++i) {
    folly::compiler_must_not_elide(
        uuid = boost::lexical_cast<boost::uuids::uuid>(sVec[i]));
    folly::compiler_must_not_elide(uuid);
  }
}

int main(int argc, char** argv) {
  folly::gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
