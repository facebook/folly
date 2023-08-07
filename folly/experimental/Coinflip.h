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

// @author: Logan Evans <lpe@fb.com>

#pragma once

#include <cfloat>
#include <cmath>

#include <folly/CPortability.h>
#include <folly/ConstexprMath.h>
#include <folly/Likely.h>
#include <folly/Random.h>
#include <folly/Utility.h>

namespace folly {

class Coinflip {
 public:
  // Set *counter = 0 to have it be initialized by this function. It is correct
  // to reuse the same *counter after calling cointflip, even for unrelated
  // callsites and different wait values.
  template <class RNG = ThreadLocalPRNG>
  inline static bool coinflip(
      uint64_t* counter, uint32_t wait, RNG&& rng = ThreadLocalPRNG()) {
    if (wait <= kMaxWaitNaive) {
      // It's convention for coinflip(0) to return false and coinflip(1) to
      // return true.
      if (wait <= 1) {
        return wait == 1;
      }
      return coinflip_naive(wait, std::forward<RNG>(rng));
    }

    // The value *counter is sampled from an exponential distribution, so we can
    // use the memoryless property Pr(T > s + t | T > s) = Pr(T > t). In other
    // words, if we subtract any amount s from *counter and *counter is still
    // positive, then the expected wait time will be unchanged. If we resample
    // *counter from that exponential distribution, the expected wait time will
    // also be unchanged. What we are going to do is pick an amount to subtract
    // from the counter that is quick to calculate. Once we detect a possible
    // event using an over-approximation, we will perform a second calculation
    // to check if we would have likewise produced an event using the precise
    // step size.
    //
    // In order to facilitate a quick approximation, we will use
    //   X ~ Exp(2^k)
    // as our distribution. See kScaleBits for k.
    //
    // After every pass through this coinflip function, the value *counter is
    // still a valid sample from Exp(2^k), which means that for any value wait
    // passed into the function, we can produce a positive signal with the
    // correct probability. That means that if one code location calls
    // coinflip(1000) nine times, we can then use the same counter for a call
    // to coinflip(100) in an unrelated code location.
    //
    // Since value *counter was sampled from an Exp(2^k) distribution, it
    // represents the wait time until the next event. We can calculate the
    // precise wait time by using the CDF for an exponential distribution,
    //   CDF(step | scale=2^k) = 1 - e^(-step/(2^k)).
    // We set the CDF to the expected probability of 1/wait and solve for step:
    //   1/wait = 1 - e^(-step/(2^k))
    //   step = -ln(1 - 1/wait) * 2^k.
    //
    // However, this is expensive to calculate on the fast path, so instead we
    // find an approximation for the step size by using an approximation for
    // the ln function. The derivative of ln(x) is 1/x. We can imagine placing
    // a line tangent to the graph of ln(x) with a slope of 1/x, and using that
    // line for our approximation. In order to avoid divisions later on, we
    // will only use slopes where x is a power of 2. That gives us
    //   ln(x) ~= mx+b = (1/2^c)x + b
    // where c is some constant and b won't end up mattering.
    //
    // The value we are approximating is
    //   ln(1 - 1/wait) = ln((wait-1)/wait) = ln(wait-1) - ln(wait).
    //
    // Since we are approximating ln(x) using a line, the result is equal to the
    // inverse of the slope we are using for an estimate. That is,
    //   ln(wait-1) - ln(wait) ~= ((1/2^c)(wait-1) + b) - ((1/2^c)(wait) + b)
    //                          = -1/(2^c).
    //
    // This gives us
    //   step  = -ln(1 - 1/wait) * 2^k
    //   step ~= (2^k) / (2^c).
    //
    // As long as our step estimate is greater than the precise step, we will
    // generate false positives. If instead the step estimate were less than the
    // precise step, we would have false negatives, which we can't correct. To
    // make sure our step estimate is greater than the precise step, we need to
    // use the slope approximation for the value wait-1.
    //
    // That slope approximation is equal to the number of bits needed to encode
    // the value, or the floor(lg(x)) function (integer log base 2). We then
    // multiply by the scale of 2^k for the exponential distribution, which is
    // some constant that depends on our choice of k plus the number of leading
    // zeros needed to encode wait-1 as a 32 bit integer.
    const uint64_t step_approx = kStepApproxBase << __builtin_clz(wait - 1);
    if (FOLLY_UNLIKELY(*counter <= step_approx)) {
      return possible_event(counter, wait, std::forward<RNG>(rng));
    }
    *counter -= step_approx;
    return false;
  }

  template <class RNG = ThreadLocalPRNG>
  explicit Coinflip(RNG&& rng = ThreadLocalPRNG())
      : counter_(sample_new_counter(std::forward<RNG>(rng))) {}

  template <class RNG = ThreadLocalPRNG>
  bool coinflip(uint32_t wait, RNG&& rng = ThreadLocalPRNG()) {
    return Coinflip::coinflip(&counter_, wait, std::forward<RNG>(rng));
  }

 private:
  // Benchmarks show that the overhead of dealing with false positives is
  // more expensive than the naive algorithm up through a wait of 8.
  static constexpr int kMaxWaitNaive = 8;

  // kScaleBits value needs to leave enough space in a 64-bit value for
  // -log(DBL_EPSILON) * kScale to avoid an overflow while sampling a new
  // counter. We use the inequality chain
  //   abs(log(DBL_EPSILON)) < abs(log2(DBL_EPSILON)) <= DBL_MAX_EXP
  // and then count the bits needed to encode DBL_MAX_EXP to decide on the
  // number of bits.
  static constexpr int kScaleBits =
      64 - folly::constexpr_log2_ceil(DBL_MAX_EXP); // 54 on Skylake.

  static constexpr uint64_t kScale = 1ull << kScaleBits;
  static constexpr uint64_t kStepApproxBase = 1ull << (kScaleBits - 31);

  // Since counter_ is an exponential random variable, it is memoryless. That
  // means that if we subtract any value from counter_ and either confirm that
  // counter_ is greater than 0 or else generate a new value for counter_, then
  // the expected value of counter_ is the same at the beginning and at the end
  // of that process. This allows us to use counter_ to support multiple wait
  // times.
  uint64_t counter_;

  template <class RNG = ThreadLocalPRNG>
  static uint64_t sample_new_counter(RNG&& rng = ThreadLocalPRNG()) {
    // This uses the principle that the CDF of an exponential distribution is
    //   f(x) = 1 - e^(-x/scale)
    //   x = -scale * ln(1 - f(x)).
    // When
    //   f(x) ~ Uniform(0, 1)
    // then
    //   x ~ Exp(scale).
    //
    // The + 1 at the end of the expression is used to make sure that the new
    // counter cannot be 0. This allows the code to detect that counter == 0
    // is a user-initialized state and that this shouldn't automatically
    // generate an event.
    double U = Random::randDouble01(std::forward<RNG>(rng));
    return to_integral(-log(1 - U) * kScale + 1);
  }

  template <class RNG = ThreadLocalPRNG>
  FOLLY_NOINLINE static bool coinflip_naive(uint32_t wait, RNG&& rng) {
    return Random::rand32(wait, std::forward<RNG>(rng)) == 0;
  }

  template <class RNG = ThreadLocalPRNG>
  FOLLY_NOINLINE static bool possible_event(
      uint64_t* counter, uint32_t wait, RNG&& rng) {
    const uint64_t old_counter = *counter;
    *counter = sample_new_counter(std::forward<RNG>(rng));
    if (old_counter == 0) {
      // Since sample_new_counter will not produce a value of 0, this branch
      // will only happen when *counter is manually initialized to 0.
      return coinflip_naive(wait, std::forward<RNG>(rng));
    }
    const uint64_t step_precise = to_integral(-log(1 - 1.0 / wait) * kScale);
    // The false positive rate is close to 50% when wait is a power of 2 and
    // is close to 0% when wait-1 is a power of 2.
    return old_counter <= step_precise;
  }
};

} // namespace folly
