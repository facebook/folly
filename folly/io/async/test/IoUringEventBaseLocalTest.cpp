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

#include <folly/experimental/io/IoUringBackend.h>
#include <folly/experimental/io/IoUringEventBaseLocal.h>
#include <folly/futures/Future.h>
#include <folly/futures/Promise.h>
#include <folly/portability/GTest.h>

namespace folly {

struct NopSqe : IoSqeBase {
  void processSubmit(struct io_uring_sqe* sqe) noexcept override {
    io_uring_prep_nop(sqe);
  }
  void callback(const io_uring_cqe* cqe) noexcept override {
    prom.setValue(cqe->res);
  }
  void callbackCancelled(const io_uring_cqe*) noexcept override {
    prom.setException(FutureCancellation{});
  }

  Promise<int> prom;
};

TEST(IoUringEventBaseLocalTest, Basic) {
  folly::EventBase eb;
  IoUringEventBaseLocal::attach(&eb, {});
  IoUringBackend* backend = IoUringEventBaseLocal::try_get(&eb);
  ASSERT_NE(nullptr, backend);
  NopSqe nop;
  backend->submit(nop);
  EXPECT_EQ(0, nop.prom.getSemiFuture().via(&eb).getVia(&eb));
}

} // namespace folly
