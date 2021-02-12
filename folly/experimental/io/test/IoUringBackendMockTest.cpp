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

#include <folly/experimental/io/IoUringBackend.h>
#include <folly/portability/GTest.h>

namespace folly::test {

class IoUringBackendMockTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

using CapturedParams = std::vector<struct io_uring_params>;
static CapturedParams gCapturedParams;
static int gNextFakeFd = 1;

extern "C" {
// Crappy implementation of io_uring_queue_init_params which just captures
// params.
int io_uring_queue_init_params(
    unsigned /*entries*/, struct io_uring* ring, struct io_uring_params* p) {
  gCapturedParams.push_back(*p);
  ring->ring_fd = gNextFakeFd++;

  return 0;
}

// Need this so destructor does not blow up.
void io_uring_queue_exit(struct io_uring* ring) {
  ring->ring_fd = -1;
}
}

TEST(IoUringBackendMockTest, SetupPollNoGroup) {
  IoUringBackend::Options options;
  options.setFlags(IoUringBackend::Options::Flags::POLL_SQ);

  IoUringBackend io(options);

  EXPECT_EQ(gCapturedParams.size(), 1);
  EXPECT_TRUE(gCapturedParams[0].flags & IORING_SETUP_CQSIZE);
  EXPECT_TRUE(gCapturedParams[0].flags & IORING_SETUP_SQPOLL);
  EXPECT_FALSE(gCapturedParams[0].flags & IORING_SETUP_SQ_AFF);
  EXPECT_FALSE(gCapturedParams[0].flags & IORING_SETUP_ATTACH_WQ);
}

TEST(IoUringBackendMockTest, SetupPollWithGroup) {
  IoUringBackend::Options options;
  options.setFlags(IoUringBackend::Options::Flags::POLL_SQ)
      .setSQGroupName("test group")
      .setSQGroupNumThreads(1);

  IoUringBackend io1(options);
  IoUringBackend io2(options);

  EXPECT_EQ(gCapturedParams.size(), 2);

  // We set up one thread for the group, so the first call should be normal...
  EXPECT_TRUE(gCapturedParams[0].flags & IORING_SETUP_SQPOLL);
  EXPECT_FALSE(gCapturedParams[0].flags & IORING_SETUP_SQ_AFF);
  EXPECT_FALSE(gCapturedParams[0].flags & IORING_SETUP_ATTACH_WQ);

  // second call should have attached to existing fd 1.
  EXPECT_TRUE(gCapturedParams[1].flags & IORING_SETUP_SQPOLL);
  EXPECT_FALSE(gCapturedParams[1].flags & IORING_SETUP_SQ_AFF);
  EXPECT_TRUE(gCapturedParams[1].flags & IORING_SETUP_ATTACH_WQ);
  EXPECT_EQ(gCapturedParams[1].wq_fd, 1);
}

TEST(IoUringBackendMockTest, SetupPollWithGroupAndCpu) {
  IoUringBackend::Options options;
  options.setFlags(IoUringBackend::Options::Flags::POLL_SQ)
      .setSQGroupName("test group")
      .setSQGroupNumThreads(2)
      .setSQCpu(666)
      .setSQCpu(42);

  IoUringBackend io1(options);
  IoUringBackend io2(options);
  IoUringBackend io3(options);

  EXPECT_EQ(gCapturedParams.size(), 3);

  // The first call should create a thread with CPU affinity set.
  EXPECT_TRUE(gCapturedParams[0].flags & IORING_SETUP_SQPOLL);
  EXPECT_TRUE(gCapturedParams[0].flags & IORING_SETUP_SQ_AFF);
  EXPECT_FALSE(gCapturedParams[0].flags & IORING_SETUP_ATTACH_WQ);
  // We don't know which CPU code will choose, but it better be one or the
  // other.
  EXPECT_TRUE(
      gCapturedParams[0].sq_thread_cpu == 666 ||
      gCapturedParams[0].sq_thread_cpu == 42);

  // We set two threads, so second call should create a thread with other CPU.
  EXPECT_TRUE(gCapturedParams[1].flags & IORING_SETUP_CQSIZE);
  EXPECT_TRUE(gCapturedParams[1].flags & IORING_SETUP_SQPOLL);
  EXPECT_TRUE(gCapturedParams[1].flags & IORING_SETUP_SQ_AFF);
  EXPECT_FALSE(gCapturedParams[1].flags & IORING_SETUP_ATTACH_WQ);
  // This one better choose the other CPU.
  EXPECT_TRUE(
      gCapturedParams[1].sq_thread_cpu == 666 ||
      gCapturedParams[1].sq_thread_cpu == 42);
  EXPECT_NE(gCapturedParams[0].sq_thread_cpu, gCapturedParams[1].sq_thread_cpu);

  // And the third thread should have attached to an existing SQ and not
  // specified an affinity.
  EXPECT_TRUE(gCapturedParams[2].flags & IORING_SETUP_SQPOLL);
  EXPECT_FALSE(gCapturedParams[2].flags & IORING_SETUP_SQ_AFF);
  EXPECT_TRUE(gCapturedParams[2].flags & IORING_SETUP_ATTACH_WQ);
}

} // namespace folly::test
