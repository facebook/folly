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

#include <folly/test/TokenBucketTest.h>

#include <glog/logging.h>

#include <folly/String.h>
#include <folly/portability/GTest.h>

using namespace folly;

TEST(TokenBucket, ReverseTime) {
  const double rate = 1000;
  TokenBucket tokenBucket(rate, rate * 0.01 + 1e-6, 0);
  size_t count = 0;
  while (tokenBucket.consume(1, 0.1)) {
    count += 1;
  }
  EXPECT_EQ(10, count);
  // Going backwards in time has no affect on the toke count (this protects
  // against different threads providing out of order timestamps).
  double tokensBefore = tokenBucket.available();
  EXPECT_FALSE(tokenBucket.consume(1, 0.09999999));
  EXPECT_EQ(tokensBefore, tokenBucket.available());
}

TEST(TokenBucketTest, CtorAssign) {
  BasicDynamicTokenBucket bucketA(100.0);
  EXPECT_EQ(0, bucketA.available(10, 10, 90));

  BasicDynamicTokenBucket bucketB(bucketA);
  EXPECT_EQ(0, bucketB.available(10, 10, 90));

  bucketA.reset(0.0);
  EXPECT_EQ(10, bucketA.available(10, 10, 90));

  bucketB = bucketA;
  EXPECT_EQ(10, bucketB.available(10, 10, 90));
}

TEST_P(TokenBucketTest, sanity) {
  std::pair<double, double> params = GetParam();
  double rate = params.first;
  double consumeSize = params.second;

  const double tenMillisecondBurst = rate * 0.010;
  // Select a burst size of 10 milliseconds at the max rate or the consume size
  // if 10 ms at rate is too small.
  const double burstSize = std::max(consumeSize, tenMillisecondBurst);
  TokenBucket tokenBucket(rate, burstSize, 0);
  double tokenCounter = 0;
  double currentTime = 0;
  // Simulate time advancing 10 seconds
  for (; currentTime <= 10.0; currentTime += 0.001) {
    EXPECT_FALSE(tokenBucket.consume(burstSize + 1, currentTime));
    while (tokenBucket.consume(consumeSize, currentTime)) {
      tokenCounter += consumeSize;
    }
    // Tokens consumed should exceed some lower bound based on rate.
    // Note: The token bucket implementation is not precise, so the lower bound
    // is somewhat fudged. The upper bound is accurate however.
    EXPECT_LE(rate * currentTime * 0.9 - 1, tokenCounter);
    // Tokens consumed should not exceed some upper bound based on rate.
    EXPECT_GE(rate * currentTime + 1e-6, tokenCounter);
  }
}

static std::vector<std::pair<double, double>> rateToConsumeSize = {
    {100, 1},
    {1000, 1},
    {10000, 1},
    {10000, 5},
};

INSTANTIATE_TEST_SUITE_P(
    TokenBucket, TokenBucketTest, ::testing::ValuesIn(rateToConsumeSize));

TEST(TokenBucket, drainOnFail) {
  DynamicTokenBucket tokenBucket;

  // Almost empty the bucket
  EXPECT_TRUE(tokenBucket.consume(9, 10, 10, 1));

  // Request more tokens than available
  EXPECT_FALSE(tokenBucket.consume(5, 10, 10, 1));
  EXPECT_DOUBLE_EQ(1.0, tokenBucket.available(10, 10, 1));

  // Again request more tokens than available, but ask to drain
  EXPECT_DOUBLE_EQ(1.0, tokenBucket.consumeOrDrain(5, 10, 10, 1));
  EXPECT_DOUBLE_EQ(0.0, tokenBucket.consumeOrDrain(1, 10, 10, 1));
}

TEST(TokenBucket, returnTokensTest) {
  DynamicTokenBucket tokenBucket;

  // Empty the bucket.
  EXPECT_TRUE(tokenBucket.consume(10, 10, 10, 5));
  // consume should fail now.
  EXPECT_FALSE(tokenBucket.consume(1, 10, 10, 5));
  EXPECT_DOUBLE_EQ(0.0, tokenBucket.consumeOrDrain(1, 10, 10, 5));

  // Return tokens. Return 40 'excess' tokens but they wont be available to
  // later callers.
  tokenBucket.returnTokens(50, 10);
  // Should be able to allocate 10 tokens again but the extra 40 returned in
  // previous call are gone.
  EXPECT_TRUE(tokenBucket.consume(10, 10, 10, 5));
  EXPECT_FALSE(tokenBucket.consume(1, 10, 10, 5));
}

TEST(TokenBucket, consumeOrBorrowTest) {
  DynamicTokenBucket tokenBucket;

  // Empty the bucket.
  EXPECT_TRUE(tokenBucket.consume(10, 10, 10, 1));
  // consume should fail now.
  EXPECT_FALSE(tokenBucket.consume(1, 10, 10, 1));
  // Now borrow from future allocations. Each call is asking for 1s worth of
  // allocations so it should return (i+1)*1s in the ith iteration as the time
  // caller needs to wait.
  for (int i = 0; i < 10; ++i) {
    auto waitTime = tokenBucket.consumeWithBorrowNonBlocking(10, 10, 10, 1);
    EXPECT_TRUE(waitTime.has_value());
    EXPECT_DOUBLE_EQ((i + 1) * 1.0, *waitTime);
  }

  // No allocation will succeed until nowInSeconds goes higher than 11s.
  EXPECT_FALSE(tokenBucket.consume(1, 10, 10, 11));
}

template <typename>
struct Wrapper;
template <>
struct Wrapper<TokenBucket> {
  explicit Wrapper(double genRate, double burstSize)
      : tb_(genRate, burstSize) {}
  double available(double /*rate*/, double /*burstSize*/, double nowInSeconds) {
    return tb_.available(nowInSeconds);
  }
  double balance(double /*rate*/, double /*burstSize*/, double nowInSeconds) {
    return tb_.balance(nowInSeconds);
  }
  Optional<double> consumeWithBorrowNonBlocking(
      double toConsume,
      double /*rate*/,
      double /*burstSize*/,
      double nowInSeconds) {
    return tb_.consumeWithBorrowNonBlocking(toConsume, nowInSeconds);
  }
  static double defaultClockNow() { return TokenBucket::defaultClockNow(); }

 private:
  TokenBucket tb_;
};
template <>
struct Wrapper<DynamicTokenBucket> {
  explicit Wrapper(double, double) {}

  double available(double rate, double burstSize, double nowInSeconds) {
    return dtb_.available(rate, burstSize, nowInSeconds);
  }
  double balance(double rate, double burstSize, double nowInSeconds) {
    return dtb_.balance(rate, burstSize, nowInSeconds);
  }
  Optional<double> consumeWithBorrowNonBlocking(
      double toConsume, double rate, double burstSize, double nowInSeconds) {
    return dtb_.consumeWithBorrowNonBlocking(
        toConsume, rate, burstSize, nowInSeconds);
  }
  static double defaultClockNow() {
    return DynamicTokenBucket::defaultClockNow();
  }

 private:
  DynamicTokenBucket dtb_;
};

class TokenBucketTypedTestConfig {
 public:
  using Types =
      ::testing::Types<Wrapper<TokenBucket>, Wrapper<DynamicTokenBucket>>;
  class TestNames {
   public:
    template <typename TypeParam>
    static std::string GetName(int) {
      StringPiece name = pretty_name<TypeParam>();
      name.removePrefix("folly::");
      return std::string(name);
    }
  };
};

/**
 * Helper class to enable typed tests.
 *
 * Enables a single test to test common functionality across the different
 * TokenBucket implementations (BasicTokenBucket, DynamicTokenBucket).
 */
template <typename TypeParam>
class TokenBucketTypedTest : public ::testing::Test {
 public:
  TokenBucketTypedTest() = default;

  /**
   * Initialize a TokenBucket for the test.
   *
   * - For a basic TokenBucket, we pass the rate and burst size.
   * - For a DynamicTokenBucket, those are not specified during construction,
   *   so they are instead kept around for calls to consume() operations.
   */
  void initTokenBucket(double genRate, double burstSize) {
    genRate_ = genRate;
    burstSize_ = burstSize;
    tb_ = std::make_unique<TypeParam>(genRate, burstSize);
  }

  /**
   * Invoke tb->available().
   */
  double available(double nowInSeconds) const noexcept {
    return CHECK_NOTNULL(tb_.get())->available(
        genRate_, burstSize_, nowInSeconds);
  }

  /**
   * Invoke tb->balance.
   */
  double balance(double nowInSeconds) const noexcept {
    return CHECK_NOTNULL(tb_.get())->balance(
        genRate_, burstSize_, nowInSeconds);
  }

  /**
   * Invoke tb->consumeWithBorrowNonBlocking.
   */
  Optional<double> consumeWithBorrowNonBlocking(
      double toConsume, double nowInSeconds) {
    return CHECK_NOTNULL(tb_.get())->consumeWithBorrowNonBlocking(
        toConsume, genRate_, burstSize_, nowInSeconds);
  }

  /**
   * Return current token bucket; initTokenBucket() must have been called.
   */
  TypeParam& getTokenBucket() { return *CHECK_NOTNULL(tb_.get()); }

 private:
  double genRate_{0};
  double burstSize_{0};
  std::unique_ptr<TypeParam> tb_;
};

TYPED_TEST_SUITE(
    TokenBucketTypedTest,
    TokenBucketTypedTestConfig::Types,
    TokenBucketTypedTestConfig::TestNames);

/**
 * Test behavior of available() and balance().
 *
 *  - available() should only return a value greater than or equal to zero; if
 *    the TokenBucket is in debt it should return zero.
 *  - balance() should return the actual number of tokens, which is negative
 *    if the TokenBucket is in debt.
 */
TYPED_TEST(TokenBucketTypedTest, AvailableAndBalance) {
  auto now = TypeParam::defaultClockNow();

  // initialize the TokenBucket
  TestFixture::initTokenBucket(100 /* genRate */, 100 /* burstSize */);

  // bucket should have 100 tokens
  EXPECT_EQ(100, TestFixture::available(now));
  EXPECT_EQ(100, TestFixture::balance(now));

  // deplete tokens to a negative balance over three intervals
  TestFixture::consumeWithBorrowNonBlocking(50 /* toConsume */, now);
  EXPECT_EQ(50, TestFixture::available(now));
  EXPECT_EQ(50, TestFixture::balance(now));

  TestFixture::consumeWithBorrowNonBlocking(50 /* toConsume */, now);
  EXPECT_EQ(0, TestFixture::available(now));
  EXPECT_EQ(0, TestFixture::balance(now));

  TestFixture::consumeWithBorrowNonBlocking(50 /* toConsume */, now);
  EXPECT_EQ(0, TestFixture::available(now));
  EXPECT_EQ(-50, TestFixture::balance(now));
}
