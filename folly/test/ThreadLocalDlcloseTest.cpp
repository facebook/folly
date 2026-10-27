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

#include <folly/Subprocess.h>
#include <folly/portability/GTest.h>
#include <folly/system/EnvUtil.h>
#include <folly/testing/TestUtil.h>

// The host runs in its own process because this test binary links folly, and a
// library loaded into it would bind to that folly rather than its own copy.
TEST(ThreadLocal, ThreadExitAfterDlclose) {
  auto const host =
      folly::test::find_resource("folly/test/thread_local_dlclose_test_host");
  auto const lib =
      folly::test::find_resource("folly/test/thread_local_dlclose_test_lib.so");
  // Unloading folly loses the references to its leaked singletons and to the
  // values of threads still running, which LeakSanitizer would report.
  auto env = folly::experimental::EnvironmentState::fromCurrentEnvironment();
  (*env)["ASAN_OPTIONS"] += ":detect_leaks=0";
  auto const envVector = env.toVector();

  auto const ret =
      folly::Subprocess({host.string(), lib.string()}, {}, nullptr, &envVector)
          .wait();

  ASSERT_TRUE(ret.exited()) << ret.str();
  EXPECT_EQ(0, ret.exitStatus()) << ret.str();
}
