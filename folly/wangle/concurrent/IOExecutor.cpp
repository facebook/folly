/*
 * Copyright 2014 Facebook, Inc.
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
#include <folly/wangle/concurrent/IOExecutor.h>

#include <folly/experimental/Singleton.h>
#include <folly/wangle/concurrent/GlobalExecutor.h>

using folly::Singleton;
using folly::wangle::IOExecutor;

namespace {

Singleton<std::atomic<IOExecutor*>> globalIOExecutorSingleton(
    "GlobalIOExecutor",
    [](){
      return new std::atomic<IOExecutor*>(nullptr);
    });

}

namespace folly { namespace wangle {

IOExecutor::~IOExecutor() {
  auto thisCopy = this;
  try {
    getSingleton()->compare_exchange_strong(thisCopy, nullptr);
  } catch (const std::runtime_error& e) {
    // The global IOExecutor singleton was already destructed so doesn't need to
    // be restored. Ignore.
  }
}

std::atomic<IOExecutor*>* IOExecutor::getSingleton() {
  return Singleton<std::atomic<IOExecutor*>>::get("GlobalIOExecutor");
}

}} // folly::wangle
