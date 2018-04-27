/*
 * Copyright 2012-present Facebook, Inc.
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

#pragma once

#include <folly/SharedMutex.h>
#include <folly/stats/detail/DigestBuilder.h>
#include <folly/stats/detail/SlidingWindow.h>

namespace folly {
namespace detail {

/*
 * BufferedStat keeps a clock and every time period, will merge data from a
 * DigestBuilder into a DigestT. Updates are made by the first appender after
 * the expiry, or can be made at read time by calling update().
 */
template <typename DigestT, typename ClockT>
class BufferedStat {
 public:
  BufferedStat() = delete;

  BufferedStat(
      typename ClockT::duration bufferDuration,
      size_t bufferSize,
      size_t digestSize);

  virtual ~BufferedStat() {}

  void append(double value);

 protected:
  // https://www.mail-archive.com/llvm-bugs@lists.llvm.org/msg18280.html
  // Wrap the time point in something with a noexcept constructor.
  struct TimePointHolder {
   public:
    TimePointHolder() noexcept {}

    TimePointHolder(typename ClockT::time_point t) : tp(t) {}

    typename ClockT::time_point tp;
  };

  const typename ClockT::duration bufferDuration_;
  std::atomic<TimePointHolder> expiry_;
  SharedMutex mutex_;

  virtual void onNewDigest(
      DigestT digest,
      typename ClockT::time_point newExpiry,
      typename ClockT::time_point oldExpiry,
      const std::unique_lock<SharedMutex>& g) = 0;

  std::unique_lock<SharedMutex> updateIfExpired();

 private:
  DigestBuilder<DigestT> digestBuilder_;

  void doUpdate(
      typename ClockT::time_point now,
      const std::unique_lock<SharedMutex>& g);

  typename ClockT::time_point roundUp(typename ClockT::time_point t);
};

/*
 * BufferedDigest is a BufferedStat that holds data in a single digest.
 */
template <typename DigestT, typename ClockT>
class BufferedDigest : public BufferedStat<DigestT, ClockT> {
 public:
  BufferedDigest(
      typename ClockT::duration bufferDuration,
      size_t bufferSize,
      size_t digestSize);

  DigestT get();

  void onNewDigest(
      DigestT digest,
      typename ClockT::time_point newExpiry,
      typename ClockT::time_point oldExpiry,
      const std::unique_lock<SharedMutex>& g) final;

 private:
  DigestT digest_;
};

/*
 * BufferedSlidingWindow is a BufferedStat that holds data in a SlidingWindow.
 * onBufferSwap will slide the SlidingWindow and return the front of the list.
 */
template <typename DigestT, typename ClockT>
class BufferedSlidingWindow : public BufferedStat<DigestT, ClockT> {
 public:
  BufferedSlidingWindow(
      size_t nBuckets,
      typename ClockT::duration bufferDuration,
      size_t bufferSize,
      size_t digestSize);

  std::vector<DigestT> get();

  void onNewDigest(
      DigestT digest,
      typename ClockT::time_point newExpiry,
      typename ClockT::time_point oldExpiry,
      const std::unique_lock<SharedMutex>& g) final;

 private:
  SlidingWindow<DigestT> slidingWindow_;
};

} // namespace detail
} // namespace folly
