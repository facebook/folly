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

#include <folly/ThreadLocal.h>
#include <folly/synchronization/detail/ThreadCachedReaders.h>

#include <folly/portability/GTest.h>

using namespace folly;

namespace {
class ThreadCachedReadersWidget {
 public:
  ThreadCachedReadersWidget() {}

  ~ThreadCachedReadersWidget() {
    if (readers_) {
      readers_->increment(0);
    }
  }

  void set(detail::ThreadCachedReaders* readers) { readers_ = readers; }

 private:
  detail::ThreadCachedReaders* readers_{nullptr};
};
} // namespace

TEST(ThreadCachedReaders, ThreadLocalCreateOnThreadExit) {
  detail::ThreadCachedReaders readers;
  ThreadLocal<ThreadCachedReadersWidget> w;

  std::thread([&] {
    // Make sure the readers thread local object is created.
    readers.increment(1);
    // Now the widget.
    w->set(&readers);
  }).join();
}
