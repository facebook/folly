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

#include <folly/container/F14Set.h>

#include <functional>
#include <type_traits>

#include <folly/portability/GTest.h>

namespace folly {

struct NormalHash {
  std::size_t operator()(int i) const { return std::hash<int>()(i); }
};

struct Force32BitHash {
  using folly_assume_32bit_hash = std::true_type;

  std::size_t operator()(int i) const { return std::hash<int>()(i); }
};

struct F14ValueSetTester {
  static void test() {
    static_assert(
        !F14FastSet<int, NormalHash>::Policy::shouldAssume32BitHash(), "");
    static_assert(
        F14FastSet<int, Force32BitHash>::Policy::shouldAssume32BitHash(), "");
  }
};

TEST(F14Policy, assume32BitTag) {
  F14ValueSetTester::test();
}

} // namespace folly
