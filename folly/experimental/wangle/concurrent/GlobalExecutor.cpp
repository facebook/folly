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

#include <folly/experimental/Singleton.h>
#include <folly/experimental/wangle/concurrent/IOExecutor.h>
#include <folly/experimental/wangle/concurrent/IOThreadPoolExecutor.h>

using namespace folly;
using namespace folly::wangle;

namespace {

Singleton<IOThreadPoolExecutor> globalIOThreadPoolSingleton(
    "GlobalIOThreadPool",
    [](){
      return new IOThreadPoolExecutor(
          sysconf(_SC_NPROCESSORS_ONLN),
          std::make_shared<NamedThreadFactory>("GlobalIOThreadPool"));
    });

}

namespace folly { namespace wangle {

IOExecutor* getIOExecutor() {
  auto singleton = IOExecutor::getSingleton();
  auto executor = singleton->load();
  while (!executor) {
    IOExecutor* nullIOExecutor = nullptr;
    singleton->compare_exchange_strong(
        nullIOExecutor,
        Singleton<IOThreadPoolExecutor>::get("GlobalIOThreadPool"));
    executor = singleton->load();
  }
  return executor;
}

void setIOExecutor(IOExecutor* executor) {
  IOExecutor::getSingleton()->store(executor);
}

}} // folly::wangle
