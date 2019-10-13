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

#include <folly/synchronization/detail/Hardware.h>

#include <folly/portability/GTest.h>

#include <glog/logging.h>

using namespace folly;

class HardwareTest : public testing::Test {};

TEST_F(HardwareTest, RtmBasicUsage) {
  // Test the checkers, though whichever values they returns,
  // we are allowed to run the loop below and use the return value of rtmBegin
  // to indicate whether RTM is supported.
  if (kRtmSupportEnabled) {
    LOG(INFO) << "rtmEnabled: " << (rtmEnabled() ? 1 : 0);
  } else {
    EXPECT_FALSE(rtmEnabled());
  }

  unsigned status;
  static constexpr uint8_t kAbortCode = 1;
  unsigned successCounts = 0;
  unsigned abortCounts = 0;
  // 30 attempts to start transactions
  for (int i = 0; i < 30; ++i) {
    EXPECT_FALSE(rtmTest());
    status = rtmBegin();
    if (status == kRtmBeginStarted) {
      // we are inside a txn now
      successCounts++;
      auto t = rtmTest();
      if (!t) {
        EXPECT_TRUE(t);
      }

      if (i % 2 == 0) {
        rtmEnd();
        continue;
      }
      // abort explicitly
      rtmAbort(kAbortCode);
      EXPECT_TRUE(false);
    } else if (status == kRtmDisabled) {
      break;
    } else if (
        (status & kRtmAbortExplicit) != 0 &&
        rtmStatusToAbortCode(status) == kAbortCode) {
      abortCounts++;
    } else if ((status & kRtmAbortRetry) != 0) {
    }
  }
  // Expect that there is at least 1 successful transaction in an environment
  // where RTM is enabled.
  EXPECT_TRUE(
      (successCounts > 0 && abortCounts > 0) ||
      (status == kRtmDisabled && !rtmEnabled()));
}
