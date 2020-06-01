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

#include <folly/experimental/io/IoUring.h>
#include <folly/experimental/io/test/AsyncBaseTestLib.h>
#include <folly/init/Init.h>

using folly::IoUring;

namespace folly {
namespace test {
namespace async_base_test_lib_detail {
INSTANTIATE_TYPED_TEST_CASE_P(AsyncTest, AsyncTest, IoUring);
} // namespace async_base_test_lib_detail
} // namespace test
} // namespace folly

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  folly::init(&argc, &argv);

  bool avail = IoUring::isAvailable();
  if (!avail) {
    LOG(INFO)
        << "Not running tests since this kernel version does not support io_uring";
    return 0;
  }

  return RUN_ALL_TESTS();
}
