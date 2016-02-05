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

#pragma once

#include <glog/logging.h>
#include <folly/Portability.h>

namespace folly {

template <typename T, bool Debug>
class ConditionallyExistent;

template <typename T>
class ConditionallyExistent<T, false> {
 public:
  static constexpr bool present() { return false; }
  explicit ConditionallyExistent(const T&) {}
  explicit ConditionallyExistent(T&&) {}
  ConditionallyExistent(const ConditionallyExistent&) = delete;
  ConditionallyExistent(ConditionallyExistent&&) = delete;
  ConditionallyExistent& operator=(const ConditionallyExistent&) = delete;
  ConditionallyExistent& operator=(ConditionallyExistent&&) = delete;
  template <typename F>
  void with(const F&&) {}
};

template <typename T>
class ConditionallyExistent<T, true> {
 public:
  static constexpr bool present() { return true; }
  explicit ConditionallyExistent(const T& v) : value_(v) {}
  explicit ConditionallyExistent(T&& v) : value_(std::move(v)) {}
  ConditionallyExistent(const ConditionallyExistent&) = delete;
  ConditionallyExistent(ConditionallyExistent&&) = delete;
  ConditionallyExistent& operator=(const ConditionallyExistent&) = delete;
  ConditionallyExistent& operator=(ConditionallyExistent&&) = delete;
  template <typename F>
  void with(F&& f) {
    f(value_);
  }

 private:
  T value_;
};

template <typename T>
using ExistentIfDebug = ConditionallyExistent<T, kIsDebug>;
}
