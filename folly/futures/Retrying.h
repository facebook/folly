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

#pragma once

#include <folly/Random.h>
#include <folly/functional/Invoke.h>
#include <folly/futures/Future.h>

namespace folly {
namespace futures {

/**
 *  retrying
 *
 *  Given a policy and a future-factory, creates futures according to the
 *  policy.
 *
 *  The policy must be moveable - retrying will move it a lot - and callable of
 *  any of the forms:
 *  - Future<bool>(size_t, exception_wrapper)
 *  - SemiFuture<bool>(size_t, exception_wrapper)
 *  - bool(size_t, exception_wrapper)
 *  Internally, the latter is transformed into the former in the obvious way.
 *  The first parameter is the attempt number of the next prospective attempt;
 *  the second parameter is the most recent exception. The policy returns a
 *  (Semi)Future<bool>  which, when completed with true, indicates that a retry
 *  is desired.
 *
 *  If the callable or policy returns a SemiFuture, then retrying returns a
 *  SemiFuture. Note that, consistent with other SemiFuture-returning functions
 *  the implication of this statement is that retrying should be assumed to be
 *  lazy: it may do nothing until .wait()/.get() is called on the result or
 *  an executor is attached with .via.
 *
 *  We provide a few generic policies:
 *  - Basic
 *  - CappedJitteredexponentialBackoff
 *
 *  Custom policies may use the most recent try number and exception to decide
 *  whether to retry and optionally to do something interesting like delay
 *  before the retry. Users may pass inline lambda expressions as policies, or
 *  may define their own data types meeting the above requirements. Users are
 *  responsible for managing the lifetimes of anything pointed to or referred to
 *  from inside the policy.
 *
 *  For example, one custom policy may try up to k times, but only if the most
 *  recent exception is one of a few types or has one of a few error codes
 *  indicating that the failure was transitory.
 *
 *  Cancellation is not supported.
 *
 *  If both FF and Policy inline executes, then it is possible to hit a stack
 *  overflow due to the recursive nature of the retry implementation
 */
template <class Policy, class FF>
auto retrying(Policy&& p, FF&& ff);

namespace detail {

struct retrying_policy_raw_tag {};
struct retrying_policy_fut_tag {};

template <class Policy>
struct retrying_policy_traits {
  using result = invoke_result_t<Policy, size_t, const exception_wrapper&>;
  using is_raw = std::is_same<result, bool>;
  using is_fut = std::is_same<result, Future<bool>>;
  using is_semi_fut = std::is_same<result, SemiFuture<bool>>;
  using tag = typename std::conditional<
      is_raw::value,
      retrying_policy_raw_tag,
      typename std::conditional<
          is_fut::value || is_semi_fut::value,
          retrying_policy_fut_tag,
          void>::type>::type;
};

template <class Policy, class FF, class Prom>
void retryingImpl(size_t k, Policy&& p, FF&& ff, Prom prom) {
  using F = invoke_result_t<FF, size_t>;
  using T = typename F::value_type;
  auto f = makeFutureWith([&] { return ff(k++); });
  std::move(f).thenTry([k,
                        prom = std::move(prom),
                        pm = static_cast<Policy&&>(p),
                        ffm = static_cast<FF&&>(ff)](Try<T>&& t) mutable {
    if (t.hasValue()) {
      prom.setValue(std::move(t).value());
      return;
    }
    auto& x = t.exception();
    auto q = makeFutureWith([&] { return pm(k, x); });
    std::move(q).thenTry([k,
                          prom = std::move(prom),
                          xm = std::move(x),
                          pm = std::move(pm),
                          ffm = std::move(ffm)](Try<bool> shouldRetry) mutable {
      if (shouldRetry.hasValue() && shouldRetry.value()) {
        retryingImpl(k, std::move(pm), std::move(ffm), std::move(prom));
      } else if (shouldRetry.hasValue()) {
        prom.setException(std::move(xm));
      } else {
        prom.setException(std::move(shouldRetry.exception()));
      }
    });
  });
}

template <class Policy, class FF>
typename std::enable_if<
    !(isSemiFuture<invoke_result_t<FF, size_t>>::value ||
      isSemiFuture<invoke_result_t<Policy, size_t, exception_wrapper>>::value),
    invoke_result_t<FF, size_t>>::type
retrying(size_t k, Policy&& p, FF&& ff) {
  using F = invoke_result_t<FF, size_t>;
  using T = typename F::value_type;
  auto prom = Promise<T>();
  auto f = prom.getFuture();
  retryingImpl(
      k, static_cast<Policy&&>(p), static_cast<FF&&>(ff), std::move(prom));
  return f;
}

template <class Policy, class FF>
typename std::enable_if<
    isSemiFuture<invoke_result_t<FF, size_t>>::value ||
        isSemiFuture<invoke_result_t<Policy, size_t, exception_wrapper>>::value,
    SemiFuture<typename isFutureOrSemiFuture<
        invoke_result_t<FF, size_t>>::Inner>>::type
retrying(size_t k, Policy&& p, FF&& ff) {
  auto sf = folly::makeSemiFuture().deferExValue(
      [k, p = static_cast<Policy&&>(p), ff = static_cast<FF&&>(ff)](
          Executor::KeepAlive<> ka, auto&&) mutable {
        auto futureP = [p = static_cast<Policy&&>(p), ka](
                           size_t kk, exception_wrapper e) {
          return p(kk, std::move(e)).via(ka);
        };
        auto futureFF = [ff = static_cast<FF&&>(ff), ka = std::move(ka)](
                            size_t v) { return ff(v).via(ka); };
        return retrying(k, std::move(futureP), std::move(futureFF));
      });
  return sf;
}

template <class Policy, class FF>
invoke_result_t<FF, size_t>
retrying(Policy&& p, FF&& ff, retrying_policy_raw_tag) {
  auto q = [pm = static_cast<Policy&&>(p)](size_t k, exception_wrapper x) {
    return makeFuture<bool>(pm(k, x));
  };
  return retrying(0, std::move(q), static_cast<FF&&>(ff));
}

template <class Policy, class FF>
auto retrying(Policy&& p, FF&& ff, retrying_policy_fut_tag) {
  return retrying(0, static_cast<Policy&&>(p), static_cast<FF&&>(ff));
}

//  jittered exponential backoff, clamped to [backoff_min, backoff_max]
template <class URNG>
Duration retryingJitteredExponentialBackoffDur(
    size_t n,
    Duration backoff_min,
    Duration backoff_max,
    double jitter_param,
    URNG& rng) {
  auto dist = std::normal_distribution<double>(0.0, jitter_param);
  auto jitter = std::exp(dist(rng));
  auto backoff_rep = jitter * backoff_min.count() * std::pow(2, n - 1);
  if (UNLIKELY(backoff_rep >= std::numeric_limits<Duration::rep>::max())) {
    return std::max(backoff_min, backoff_max);
  }
  auto backoff = Duration(Duration::rep(backoff_rep));
  return std::max(backoff_min, std::min(backoff_max, backoff));
}

template <class Policy, class URNG>
std::function<Future<bool>(size_t, const exception_wrapper&)>
retryingPolicyCappedJitteredExponentialBackoff(
    size_t max_tries,
    Duration backoff_min,
    Duration backoff_max,
    double jitter_param,
    URNG&& rng,
    Policy&& p) {
  return [pm = static_cast<Policy&&>(p),
          max_tries,
          backoff_min,
          backoff_max,
          jitter_param,
          rngp = static_cast<URNG&&>(rng)](
             size_t n, const exception_wrapper& ex) mutable {
    if (n == max_tries) {
      return makeFuture(false);
    }
    return pm(n, ex).thenValue(
        [n, backoff_min, backoff_max, jitter_param, rngp = std::move(rngp)](
            bool v) mutable {
          if (!v) {
            return makeFuture(false);
          }
          auto backoff = detail::retryingJitteredExponentialBackoffDur(
              n, backoff_min, backoff_max, jitter_param, rngp);
          return futures::sleep(backoff).toUnsafeFuture().thenValue(
              [](auto&&) { return true; });
        });
  };
}

template <class Policy, class URNG>
std::function<Future<bool>(size_t, const exception_wrapper&)>
retryingPolicyCappedJitteredExponentialBackoff(
    size_t max_tries,
    Duration backoff_min,
    Duration backoff_max,
    double jitter_param,
    URNG&& rng,
    Policy&& p,
    retrying_policy_raw_tag) {
  auto q = [pm = static_cast<Policy&&>(p)](
               size_t n, const exception_wrapper& e) {
    return makeFuture(pm(n, e));
  };
  return retryingPolicyCappedJitteredExponentialBackoff(
      max_tries,
      backoff_min,
      backoff_max,
      jitter_param,
      static_cast<URNG&&>(rng),
      std::move(q));
}

template <class Policy, class URNG>
std::function<Future<bool>(size_t, const exception_wrapper&)>
retryingPolicyCappedJitteredExponentialBackoff(
    size_t max_tries,
    Duration backoff_min,
    Duration backoff_max,
    double jitter_param,
    URNG&& rng,
    Policy&& p,
    retrying_policy_fut_tag) {
  return retryingPolicyCappedJitteredExponentialBackoff(
      max_tries,
      backoff_min,
      backoff_max,
      jitter_param,
      static_cast<URNG&&>(rng),
      static_cast<Policy&&>(p));
}

} // namespace detail

template <class Policy, class FF>
auto retrying(Policy&& p, FF&& ff) {
  using tag = typename detail::retrying_policy_traits<Policy>::tag;
  return detail::retrying(
      static_cast<Policy&&>(p), static_cast<FF&&>(ff), tag());
}

inline std::function<bool(size_t, const exception_wrapper&)>
retryingPolicyBasic(size_t max_tries) {
  return [=](size_t n, const exception_wrapper&) { return n < max_tries; };
}

template <class Policy, class URNG>
std::function<Future<bool>(size_t, const exception_wrapper&)>
retryingPolicyCappedJitteredExponentialBackoff(
    size_t max_tries,
    Duration backoff_min,
    Duration backoff_max,
    double jitter_param,
    URNG&& rng,
    Policy&& p) {
  using tag = typename detail::retrying_policy_traits<Policy>::tag;
  return detail::retryingPolicyCappedJitteredExponentialBackoff(
      max_tries,
      backoff_min,
      backoff_max,
      jitter_param,
      static_cast<URNG&&>(rng),
      static_cast<Policy&&>(p),
      tag());
}

inline std::function<Future<bool>(size_t, const exception_wrapper&)>
retryingPolicyCappedJitteredExponentialBackoff(
    size_t max_tries,
    Duration backoff_min,
    Duration backoff_max,
    double jitter_param) {
  auto p = [](size_t, const exception_wrapper&) { return true; };
  return retryingPolicyCappedJitteredExponentialBackoff(
      max_tries,
      backoff_min,
      backoff_max,
      jitter_param,
      ThreadLocalPRNG(),
      std::move(p));
}

} // namespace futures
} // namespace folly
