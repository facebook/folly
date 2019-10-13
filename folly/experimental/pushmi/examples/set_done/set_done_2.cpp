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

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include <folly/experimental/pushmi/o/empty.h>
#include <folly/experimental/pushmi/o/filter.h>
#include <folly/experimental/pushmi/o/just.h>
#include <folly/experimental/pushmi/o/tap.h>
#include <folly/experimental/pushmi/o/transform.h>

using namespace folly::pushmi::aliases;

const bool setting_exists = false;

auto get_setting() {
  return mi::make_single_sender([](auto out) {
    if (setting_exists) {
      op::just(42) | op::submit(out);
    } else {
      op::empty() | op::submit(out);
    }
  });
}

auto println = [](auto v) { std::cout << v << std::endl; };

// concat not yet implemented
template <class T, class E = std::exception_ptr>
auto concat() {
  return [](auto in) {
    return mi::make_single_sender([in](auto out) mutable {
      mi::submit(in, mi::make_receiver(out, [](auto out_, auto v) {
                   mi::submit(v, mi::any_receiver<E, T>(out_));
                 }));
    });
  };
}

int main() {
  get_setting() | op::transform([](int i) { return std::to_string(i); }) |
      op::submit(println);

  op::just(42) | op::filter([](int i) { return i < 42; }) |
      op::transform([](int i) { return std::to_string(i); }) |
      op::submit(println);

  op::just(42) | op::transform([](int i) {
    if (i < 42) {
      return mi::any_single_sender<std::exception_ptr, std::string>{
          op::empty()};
    }
    return mi::any_single_sender<std::exception_ptr, std::string>{
        op::just(std::to_string(i))};
  }) | concat<std::string>() |
      op::submit(println);

  std::cout << "OK" << std::endl;
}
