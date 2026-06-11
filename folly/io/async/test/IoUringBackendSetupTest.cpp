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

#include <folly/io/async/IoUringBackend.h>
#include <folly/portability/GTest.h>

namespace folly::test {

class IoUringBackendSetupTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!IoUringBackend::isAvailable()) {
      GTEST_SKIP() << "io_uring not available";
    }
  }
};

TEST_F(IoUringBackendSetupTest, SetupPollNoGroup) {
  IoUringBackend::Options options;
  options.setFlags(IoUringBackend::Options::Flags::POLL_SQ);

  IoUringBackend io(std::move(options));

  EXPECT_TRUE(io.params().flags & IORING_SETUP_CQSIZE);
  EXPECT_TRUE(io.params().flags & IORING_SETUP_SQPOLL);
  EXPECT_FALSE(io.params().flags & IORING_SETUP_SQ_AFF);
  EXPECT_FALSE(io.params().flags & IORING_SETUP_ATTACH_WQ);
}

TEST_F(IoUringBackendSetupTest, SetupPollWithGroup) {
  auto makeOptions = []() {
    IoUringBackend::Options options;
    options.setFlags(IoUringBackend::Options::Flags::POLL_SQ)
        .setSQGroupName("test group")
        .setSQGroupNumThreads(1);
    return options;
  };

  IoUringBackend io1(makeOptions());
  IoUringBackend io2(makeOptions());

  // We set up one thread for the group, so the first call should be normal...
  EXPECT_TRUE(io1.params().flags & IORING_SETUP_SQPOLL);
  EXPECT_FALSE(io1.params().flags & IORING_SETUP_SQ_AFF);
  EXPECT_FALSE(io1.params().flags & IORING_SETUP_ATTACH_WQ);

  // second call should have attached to existing fd 1.
  EXPECT_TRUE(io2.params().flags & IORING_SETUP_SQPOLL);
  EXPECT_FALSE(io2.params().flags & IORING_SETUP_SQ_AFF);
  EXPECT_TRUE(io2.params().flags & IORING_SETUP_ATTACH_WQ);
  EXPECT_EQ(io2.params().wq_fd, io1.ioRingPtr()->ring_fd);
}

TEST_F(IoUringBackendSetupTest, SetupPollWithGroupAndCpu) {
  auto makeOptions = []() {
    IoUringBackend::Options options;
    options.setFlags(IoUringBackend::Options::Flags::POLL_SQ)
        .setSQGroupName("test group")
        .setSQGroupNumThreads(2)
        .setSQCpu(1)
        .setSQCpu(0);
    return options;
  };

  IoUringBackend io1(makeOptions());
  IoUringBackend io2(makeOptions());
  IoUringBackend io3(makeOptions());

  // The first call should create a thread with CPU affinity set.
  EXPECT_TRUE(io1.params().flags & IORING_SETUP_SQPOLL);
  EXPECT_TRUE(io1.params().flags & IORING_SETUP_SQ_AFF);
  EXPECT_FALSE(io1.params().flags & IORING_SETUP_ATTACH_WQ);
  // We don't know which CPU code will choose, but it better be one or the
  // other.
  EXPECT_TRUE(
      io1.params().sq_thread_cpu == 1 || io1.params().sq_thread_cpu == 0);

  // We set two threads, so second call should create a thread with other CPU.
  EXPECT_TRUE(io2.params().flags & IORING_SETUP_CQSIZE);
  EXPECT_TRUE(io2.params().flags & IORING_SETUP_SQPOLL);
  EXPECT_TRUE(io2.params().flags & IORING_SETUP_SQ_AFF);
  EXPECT_FALSE(io2.params().flags & IORING_SETUP_ATTACH_WQ);
  // This one better choose the other CPU.
  EXPECT_TRUE(
      io2.params().sq_thread_cpu == 1 || io2.params().sq_thread_cpu == 0);
  EXPECT_NE(io1.params().sq_thread_cpu, io2.params().sq_thread_cpu);

  // And the third thread should have attached to an existing SQ and not
  // specified an affinity.
  EXPECT_TRUE(io3.params().flags & IORING_SETUP_SQPOLL);
  EXPECT_FALSE(io3.params().flags & IORING_SETUP_SQ_AFF);
  EXPECT_TRUE(io3.params().flags & IORING_SETUP_ATTACH_WQ);
}

TEST_F(IoUringBackendSetupTest, DisableIoWaitAppliesNoIoWait) {
  IoUringBackend::Options options;
  options.setDisableIoWait(true);
  IoUringBackend io(std::move(options));

  io.eb_event_base_loop(EVLOOP_NONBLOCK);

  io_uring* ring = io.ioRingPtr();
  if (!(ring->features & IORING_FEAT_NO_IOWAIT)) {
    GTEST_SKIP() << "kernel lacks IORING_FEAT_NO_IOWAIT";
  }
  EXPECT_TRUE(ring->int_flags & IORING_ENTER_NO_IOWAIT);
}

TEST_F(IoUringBackendSetupTest, DefaultDoesNotDisableIoWait) {
  IoUringBackend::Options options;
  IoUringBackend io(std::move(options));
  io.eb_event_base_loop(EVLOOP_NONBLOCK);

  EXPECT_FALSE(io.ioRingPtr()->int_flags & IORING_ENTER_NO_IOWAIT);
}

} // namespace folly::test
