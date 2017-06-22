/*
 * Copyright 2017 Facebook, Inc.
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

#include <algorithm>
#include <atomic>
#include <vector>

#include <folly/futures/Future.h>
#include <folly/portability/GTest.h>
#include <folly/portability/SysResource.h>
#include "TestExecutor.h"

using namespace std;
using namespace std::chrono;
using namespace folly;

// Runs func num_times in parallel, expects that all of them will take
// at least min_duration and at least 1 execution will take less than
// max_duration.
template <typename D, typename F>
void multiAttemptExpectDurationWithin(size_t num_tries,
                                      D min_duration,
                                      D max_duration,
                                      const F& func) {
  vector<thread> threads(num_tries);
  vector<D> durations(num_tries, D::min());
  for (size_t i = 0; i < num_tries; ++i) {
    threads[i] = thread([&,i]{
      auto start = steady_clock::now();
      func();
      durations[i] = duration_cast<D>(steady_clock::now() - start);
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  sort(durations.begin(), durations.end());
  for (auto d : durations) {
    EXPECT_GE(d, min_duration);
  }
  EXPECT_LE(durations[0], max_duration);
}

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

TEST(RetryingTest, future_factory_throws) {
  struct ReturnedException : exception {};
  struct ThrownException : exception {};
  auto result = futures::retrying(
                    [](size_t n, const exception_wrapper&) { return n < 2; },
                    [](size_t n) {
                      switch (n) {
                        case 0:
                          return makeFuture<size_t>(
                              make_exception_wrapper<ReturnedException>());
                        case 1:
                          throw ThrownException();
                        default:
                          return makeFuture(n);
                      }
                    })
                    .wait()
                    .getTry();
  EXPECT_THROW(result.throwIfFailed(), ThrownException);
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
  multiAttemptExpectDurationWithin(5, milliseconds(200), milliseconds(400), []{
    using ms = milliseconds;
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
    EXPECT_EQ(2, r.value());
  });
}

TEST(RetryingTest, policy_sleep_defaults) {
  multiAttemptExpectDurationWithin(5, milliseconds(200), milliseconds(400), []{
    //  To ensure that this compiles with default params.
    using ms = milliseconds;
    auto r = futures::retrying(
        futures::retryingPolicyCappedJitteredExponentialBackoff(
          3, ms(100), ms(1000), 0.1),
        [](size_t n) {
            return n < 2
              ? makeFuture<size_t>(runtime_error("ha"))
              : makeFuture(n);
        }
    ).wait();
    EXPECT_EQ(2, r.value());
  });
}

TEST(RetryingTest, large_retries) {
  rlimit oldMemLimit;
  PCHECK(getrlimit(RLIMIT_AS, &oldMemLimit) == 0);

  rlimit newMemLimit;
  newMemLimit.rlim_cur = std::min(1UL << 30, oldMemLimit.rlim_max);
  newMemLimit.rlim_max = oldMemLimit.rlim_max;
  if (!folly::kIsSanitizeAddress) { // ASAN reserves outside of the rlimit
    PCHECK(setrlimit(RLIMIT_AS, &newMemLimit) == 0);
  }
  SCOPE_EXIT {
    PCHECK(setrlimit(RLIMIT_AS, &oldMemLimit) == 0);
  };

  TestExecutor executor(4);
  // size of implicit promise is at least the size of the return.
  using LargeReturn = array<uint64_t, 16000>;
  auto func = [&executor](size_t retryNum) -> Future<LargeReturn> {
    return via(&executor).then([retryNum] {
      return retryNum < 10000
          ? makeFuture<LargeReturn>(
                make_exception_wrapper<std::runtime_error>("keep trying"))
          : makeFuture<LargeReturn>(LargeReturn());
    });
  };

  vector<Future<LargeReturn>> futures;
  for (auto idx = 0; idx < 40; ++idx) {
    futures.emplace_back(futures::retrying(
        [&executor](size_t, const exception_wrapper&) {
          return via(&executor).then([] { return true; });
        },
        func));
  }

  // 40 * 10,000 * 16,000B > 1GB; we should avoid OOM

  for (auto& f : futures) {
    f.wait();
    EXPECT_TRUE(f.hasValue());
  }
}

/*
TEST(RetryingTest, policy_sleep_cancel) {
  multiAttemptExpectDurationWithin(5, milliseconds(0), milliseconds(10), []{
    mt19937_64 rng(0);
    using ms = milliseconds;
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
    EXPECT_EQ(2, r.value());
  });
}
*/
