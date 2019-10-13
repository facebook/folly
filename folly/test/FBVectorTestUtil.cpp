/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/test/FBVectorTestUtil.h>

#include <list>
#include <string>

#include <folly/FBString.h>
#include <folly/Random.h>

namespace folly {
namespace test {
namespace detail {

RandomT rng(seed);

std::list<char> RandomList(unsigned int maxSize) {
  std::list<char> lst(random(0u, maxSize));
  std::list<char>::iterator i = lst.begin();
  for (; i != lst.end(); ++i) {
    *i = random('a', 'z');
  }
  return lst;
}

template <>
int randomObject<int>() {
  return random(0, 1024);
}

template <>
std::string randomObject<std::string>() {
  std::string result;
  randomString(&result);
  return result;
}

template <>
folly::fbstring randomObject<folly::fbstring>() {
  folly::fbstring result;
  randomString(&result);
  return result;
}
} // namespace detail
} // namespace test
} // namespace folly
