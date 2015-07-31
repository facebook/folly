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

#include <atomic>
#include <exception>
#include <folly/futures/Future.h>

#include <gtest/gtest.h>

using namespace std;
using namespace std::chrono;
using namespace folly;

TEST(RetryingTest, has_op_call) {
  using ew = exception_wrapper;
  auto policy_raw = [](size_t n, const ew&) { return n < 3; };
  auto policy_fut = [](size_t n, const ew&) { return makeFuture(n < 3); };
  using namespace futures::detail;
  EXPECT_TRUE(retrying_policy_traits<decltype(policy_raw)>::is_raw::value);
  EXPECT_TRUE(retrying_policy_traits<decltype(policy_fut)>::is_fut::value);
}

TEST(RetryingTest, basic) {
  auto r = futures::retrying(
      [](size_t n, const exception_wrapper&) { return n < 3; },
      [](size_t n) {
          return n < 2
            ? makeFuture<size_t>(runtime_error("ha"))
            : makeFuture(n);
      }
  ).wait();
  EXPECT_EQ(2, r.value());
}

TEST(RetryingTest, policy_future) {
  atomic<size_t> sleeps {0};
  auto r = futures::retrying(
      [&](size_t n, const exception_wrapper&) {
          return n < 3
            ? makeFuture(++sleeps).then([] { return true; })
            : makeFuture(false);
      },
      [](size_t n) {
          return n < 2
            ? makeFuture<size_t>(runtime_error("ha"))
            : makeFuture(n);
      }
  ).wait();
  EXPECT_EQ(2, r.value());
  EXPECT_EQ(2, sleeps);
}

TEST(RetryingTest, policy_basic) {
  auto r = futures::retrying(
      futures::retryingPolicyBasic(3),
      [](size_t n) {
          return n < 2
            ? makeFuture<size_t>(runtime_error("ha"))
            : makeFuture(n);
      }
  ).wait();
  EXPECT_EQ(2, r.value());
}

TEST(RetryingTest, policy_capped_jittered_exponential_backoff) {
  using ms = milliseconds;
  auto start = steady_clock::now();
  auto r = futures::retrying(
      futures::retryingPolicyCappedJitteredExponentialBackoff(
        3, ms(100), ms(1000), 0.1, mt19937_64(0),
        [](size_t, const exception_wrapper&) { return true; }),
      [](size_t n) {
          return n < 2
            ? makeFuture<size_t>(runtime_error("ha"))
            : makeFuture(n);
      }
  ).wait();
  auto finish = steady_clock::now();
  auto duration = duration_cast<milliseconds>(finish - start);
  EXPECT_EQ(2, r.value());
  EXPECT_NEAR(
      milliseconds(300).count(),
      duration.count(),
      milliseconds(25).count());
}

TEST(RetryingTest, policy_sleep_defaults) {
  //  To ensure that this compiles with default params.
  using ms = milliseconds;
  auto start = steady_clock::now();
  auto r = futures::retrying(
      futures::retryingPolicyCappedJitteredExponentialBackoff(
        3, ms(100), ms(1000), 0.1),
      [](size_t n) {
          return n < 2
            ? makeFuture<size_t>(runtime_error("ha"))
            : makeFuture(n);
      }
  ).wait();
  auto finish = steady_clock::now();
  auto duration = duration_cast<milliseconds>(finish - start);
  EXPECT_EQ(2, r.value());
  EXPECT_NEAR(
      milliseconds(300).count(),
      duration.count(),
      milliseconds(100).count());
}

/*
TEST(RetryingTest, policy_sleep_cancel) {
  mt19937_64 rng(0);
  using ms = milliseconds;
  auto start = steady_clock::now();
  auto r = futures::retrying(
      futures::retryingPolicyCappedJitteredExponentialBackoff(
        5, ms(100), ms(1000), 0.1, rng,
        [](size_t n, const exception_wrapper&) { return true; }),
      [](size_t n) {
          return n < 4
            ? makeFuture<size_t>(runtime_error("ha"))
            : makeFuture(n);
      }
  );
  r.cancel();
  r.wait();
  auto finish = steady_clock::now();
  auto duration = duration_cast<milliseconds>(finish - start);
  EXPECT_EQ(2, r.value());
  EXPECT_NEAR(
      milliseconds(0).count(),
      duration.count(),
      milliseconds(10).count());
}
*/
