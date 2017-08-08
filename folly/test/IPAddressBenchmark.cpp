/*
 * Copyright 2017 Facebook, Inc.
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

#include <folly/Format.h>
#include <folly/IPAddress.h>

#include <glog/logging.h>

#include <folly/Benchmark.h>

using namespace folly;
using std::string;

BENCHMARK(ipv4_to_string_inet_ntop, iters) {
  folly::IPAddressV4 ipv4Addr("127.0.0.1");
  in_addr ip = ipv4Addr.toAddr();
  char outputString[INET_ADDRSTRLEN] = {0};

  while (iters--) {
    const char* val =
        inet_ntop(AF_INET, &ip, outputString, sizeof(outputString));
  }
}

BENCHMARK_RELATIVE(ipv4_to_fully_qualified, iters) {
  IPAddressV4 ip("127.0.0.1");
  while (iters--) {
    string outputString = ip.toFullyQualified();
  }
}

BENCHMARK_DRAW_LINE()

BENCHMARK(ipv4_to_fully_qualified_port, iters) {
  IPAddressV4 ip("255.255.255.255");
  while (iters--) {
    string outputString = folly::sformat("{}:{}", ip.toFullyQualified(), 65535);
    folly::doNotOptimizeAway(outputString);
    folly::doNotOptimizeAway(outputString.data());
  }
}

BENCHMARK_RELATIVE(ipv4_append_to_fully_qualified_port, iters) {
  IPAddressV4 ip("255.255.255.255");
  while (iters--) {
    string outputString;
    outputString.reserve(IPAddressV4::kMaxToFullyQualifiedSize + 1 + 5);
    ip.toFullyQualifiedAppend(outputString);
    outputString += ':';
    folly::toAppend(65535, &outputString);
    folly::doNotOptimizeAway(outputString);
    folly::doNotOptimizeAway(outputString.data());
  }
}

BENCHMARK_DRAW_LINE()

BENCHMARK(ipv6_to_string_inet_ntop, iters) {
  IPAddressV6 ipv6Addr("F1E0:0ACE:FB94:7ADF:22E8:6DE6:9672:3725");
  in6_addr ip = ipv6Addr.toAddr();
  char outputString[INET6_ADDRSTRLEN] = {0};
  bool checkResult = (iters == 1);

  while (iters--) {
    const char* val =
        inet_ntop(AF_INET6, &ip, outputString, sizeof(outputString));
  }
}

BENCHMARK_RELATIVE(ipv6_to_fully_qualified, iters) {
  IPAddressV6 ip("F1E0:0ACE:FB94:7ADF:22E8:6DE6:9672:3725");
  string outputString;
  while (iters--) {
    outputString = ip.toFullyQualified();
  }
}

BENCHMARK_DRAW_LINE()

BENCHMARK(ipv6_to_fully_qualified_port, iters) {
  IPAddressV6 ip("F1E0:0ACE:FB94:7ADF:22E8:6DE6:9672:3725");
  while (iters--) {
    string outputString = folly::sformat("{}:{}", ip.toFullyQualified(), 65535);
    folly::doNotOptimizeAway(outputString);
    folly::doNotOptimizeAway(outputString.data());
  }
}

BENCHMARK_RELATIVE(ipv6_append_to_fully_qualified_port, iters) {
  IPAddressV6 ip("F1E0:0ACE:FB94:7ADF:22E8:6DE6:9672:3725");
  while (iters--) {
    string outputString;
    outputString.reserve(folly::IPAddressV6::kToFullyQualifiedSize + 1 + 5);
    ip.toFullyQualifiedAppend(outputString);
    outputString += ':';
    folly::toAppend(65535, &outputString);
    folly::doNotOptimizeAway(outputString);
    folly::doNotOptimizeAway(outputString.data());
  }
}

// Benchmark results on Intel Xeon CPU E5-2660 @ 2.20GHz
// ============================================================================
// folly/test/IPAddressBenchmark.cpp               relative  time/iter  iters/s
// ============================================================================
// ipv4_to_string_inet_ntop                                   227.13ns    4.40M
// ipv4_to_fully_qualified                         1418.95%    16.01ns   62.47M
// ----------------------------------------------------------------------------
// ipv4_to_fully_qualified_port                                77.51ns   12.90M
// ipv4_append_to_fully_qualified_port              133.72%    57.96ns   17.25M
// ----------------------------------------------------------------------------
// ipv6_to_string_inet_ntop                                   750.53ns    1.33M
// ipv6_to_fully_qualified                          608.68%   123.30ns    8.11M
// ----------------------------------------------------------------------------
// ipv6_to_fully_qualified_port                               150.76ns    6.63M
// ipv6_append_to_fully_qualified_port              178.73%    84.35ns   11.86M
// ============================================================================

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  runBenchmarks();
  return 0;
}
