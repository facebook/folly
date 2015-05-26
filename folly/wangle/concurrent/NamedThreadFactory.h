/*
 * Copyright 2015 Facebook, Inc.
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

#include <atomic>
#include <string>
#include <thread>

#include <folly/wangle/concurrent/ThreadFactory.h>
#include <folly/Conv.h>
#include <folly/Range.h>
#include <folly/ThreadName.h>

namespace folly { namespace wangle {

class NamedThreadFactory : public ThreadFactory {
 public:
  explicit NamedThreadFactory(folly::StringPiece prefix)
    : prefix_(prefix.str()), suffix_(0) {}

  std::thread newThread(Func&& func) override {
    auto thread = std::thread(std::move(func));
    folly::setThreadName(
        thread.native_handle(),
        folly::to<std::string>(prefix_, suffix_++));
    return thread;
  }

  void setNamePrefix(folly::StringPiece prefix) {
    prefix_ = prefix.str();
  }

  std::string getNamePrefix() {
    return prefix_;
  }

 private:
  std::string prefix_;
  std::atomic<uint64_t> suffix_;
};

}} // folly::wangle
