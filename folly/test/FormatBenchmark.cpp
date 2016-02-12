/*
 * Copyright 2016 Facebook, Inc.
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

#include <glog/logging.h>

#include <folly/FBVector.h>
#include <folly/Benchmark.h>
#include <folly/dynamic.h>
#include <folly/json.h>

using namespace folly;

namespace {

char bigBuf[300];

}  // namespace

BENCHMARK(octal_sprintf, iters) {
  while (iters--) {
    sprintf(bigBuf, "%o", static_cast<unsigned int>(iters));
  }
}

BENCHMARK_RELATIVE(octal_uintToOctal, iters) {
  while (iters--) {
    detail::uintToOctal(bigBuf, detail::kMaxOctalLength,
                        static_cast<unsigned int>(iters));
  }
}

BENCHMARK_DRAW_LINE()

BENCHMARK(hex_sprintf, iters) {
  while (iters--) {
    sprintf(bigBuf, "%x", static_cast<unsigned int>(iters));
  }
}

BENCHMARK_RELATIVE(hex_uintToHex, iters) {
  while (iters--) {
    detail::uintToHexLower(bigBuf, detail::kMaxHexLength,
                           static_cast<unsigned int>(iters));
  }
}

BENCHMARK_DRAW_LINE()

BENCHMARK(intAppend_sprintf) {
  fbstring out;
  for (int i = -1000; i < 1000; i++) {
    sprintf(bigBuf, "%d", i);
    out.append(bigBuf);
  }
}

BENCHMARK_RELATIVE(intAppend_to) {
  fbstring out;
  for (int i = -1000; i < 1000; i++) {
    toAppend(i, &out);
  }
}

BENCHMARK_RELATIVE(intAppend_format) {
  fbstring out;
  for (int i = -1000; i < 1000; i++) {
    format(&out, "{}", i);
  }
}

BENCHMARK_DRAW_LINE()

BENCHMARK(bigFormat_sprintf, iters) {
  while (iters--) {
    for (int i = -100; i < 100; i++) {
      sprintf(bigBuf,
              "%d %d %d %d %d"
              "%d %d %d %d %d"
              "%d %d %d %d %d"
              "%d %d %d %d %d",
              i, i+1, i+2, i+3, i+4,
              i+5, i+6, i+7, i+8, i+9,
              i+10, i+11, i+12, i+13, i+14,
              i+15, i+16, i+17, i+18, i+19);
    }
  }
}

BENCHMARK_RELATIVE(bigFormat_format, iters) {
  char* p;
  auto writeToBuf = [&p] (StringPiece sp) mutable {
    memcpy(p, sp.data(), sp.size());
    p += sp.size();
  };

  while (iters--) {
    for (int i = -100; i < 100; i++) {
      p = bigBuf;
      format("{} {} {} {} {}"
             "{} {} {} {} {}"
             "{} {} {} {} {}"
             "{} {} {} {} {}",
              i, i+1, i+2, i+3, i+4,
              i+5, i+6, i+7, i+8, i+9,
              i+10, i+11, i+12, i+13, i+14,
              i+15, i+16, i+17, i+18, i+19)(writeToBuf);
    }
  }
}

BENCHMARK_DRAW_LINE()

BENCHMARK(format_nested_strings, iters) {
  while (iters--) {
    fbstring out;
    for (int i = 0; i < 1000; ++i) {
      out.clear();
      format(&out, "{} {}",
             format("{} {}", i, i + 1).str(),
             format("{} {}", -i, -i - 1).str());
    }
  }
}

BENCHMARK_RELATIVE(format_nested_fbstrings, iters) {
  while (iters--) {
    fbstring out;
    for (int i = 0; i < 1000; ++i) {
      out.clear();
      format(&out, "{} {}",
             format("{} {}", i, i + 1).fbstr(),
             format("{} {}", -i, -i - 1).fbstr());
    }
  }
}

BENCHMARK_RELATIVE(format_nested_direct, iters) {
  while (iters--) {
    fbstring out;
    for (int i = 0; i < 1000; ++i) {
      out.clear();
      format(&out, "{} {}",
             format("{} {}", i, i + 1),
             format("{} {}", -i, -i - 1));
    }
  }
}

// Benchmark results on my dev server (dual-CPU Xeon L5520 @ 2.7GHz)
//
// ============================================================================
// folly/test/FormatTest.cpp                         relative  ns/iter  iters/s
// ============================================================================
// octal_sprintf                                               100.57     9.94M
// octal_uintToOctal                                 2599.47%    3.87   258.46M
// ----------------------------------------------------------------------------
// hex_sprintf                                                 100.13     9.99M
// hex_uintToHex                                     3331.75%    3.01   332.73M
// ----------------------------------------------------------------------------
// intAppend_sprintf                                           406.07K    2.46K
// intAppend_to                                       166.03%  244.58K    4.09K
// intAppend_format                                   147.57%  275.17K    3.63K
// ----------------------------------------------------------------------------
// bigFormat_sprintf                                           255.40K    3.92K
// bigFormat_format                                   102.18%  249.94K    4.00K
// ============================================================================

int main(int argc, char *argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  runBenchmarks();
  return 0;
}
