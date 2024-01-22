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

#include <folly/fibers/FiberManagerMap.h>
#include <folly/init/Init.h>

FOLLY_PUSH_WARNING
FOLLY_CLANG_DISABLE_WARNING("-Winfinite-recursion")

void f(int* p) {
  LOG(INFO) << "f()";
  // Make sure recursion is not optimized out
  int a[100];
  for (size_t i = 0; i < 100; ++i) {
    a[i] = i;
    ++(a[i]);
    if (p) {
      a[i] += p[i];
    }
  }
  f(a);
}

FOLLY_POP_WARNING

int main(int argc, char* argv[]) {
  folly::Init init(&argc, &argv);

  folly::EventBase evb;
  folly::fibers::getFiberManager(evb).addTask([&]() { f(nullptr); });
  evb.loop();
}
