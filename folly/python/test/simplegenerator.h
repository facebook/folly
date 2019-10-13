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

#pragma once
#include <folly/experimental/coro/AsyncGenerator.h>

namespace folly {
namespace python {
namespace test {

namespace {
struct SomeError : std::exception {};
} // namespace

coro::AsyncGenerator<int> make_generator_empty() {
  coro::AsyncGenerator<int> g;
  return g;
}

coro::AsyncGenerator<int> make_generator() {
  co_yield 1;
  co_yield 2;
  co_yield 3;
  co_yield 4;
  co_yield 5;
  co_return;
}

coro::AsyncGenerator<int> make_generator_error() {
  co_yield 42;
  throw SomeError{};
}

} // namespace test
} // namespace python
} // namespace folly
