/*
 * Copyright 2012-present Facebook, Inc.
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

//
// Author: andrei.alexandrescu@fb.com

#include <list>
#include <memory>

#include <boost/random.hpp>

#include <folly/FBVector.h>
#include <folly/Traits.h>
#include <folly/container/Foreach.h>
#include <folly/portability/GFlags.h>
#include <folly/test/FBVectorTestUtil.h>

using namespace std;
using namespace folly;
using namespace folly::test::detail;

typedef vector<int> IntVector;
typedef fbvector<int> IntFBVector;
typedef vector<folly::fbstring> FBStringVector;
typedef fbvector<folly::fbstring> FBStringFBVector;

#define VECTOR IntVector
#include <folly/test/FBVectorBenchmarks.cpp.h> // nolint
#undef VECTOR
#define VECTOR IntFBVector
#include <folly/test/FBVectorBenchmarks.cpp.h> // nolint
#undef VECTOR
#define VECTOR FBStringVector
#include <folly/test/FBVectorBenchmarks.cpp.h> // nolint
#undef VECTOR
#define VECTOR FBStringFBVector
#include <folly/test/FBVectorBenchmarks.cpp.h> // nolint
#undef VECTOR

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
