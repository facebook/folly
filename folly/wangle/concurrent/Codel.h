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

#pragma once

#include <atomic>
#include <chrono>

namespace folly { namespace wangle {

/* Codel algorithm implementation:
 * http://en.wikipedia.org/wiki/CoDel
 *
 * Algorithm modified slightly:  Instead of changing the interval time
 * based on the average min delay, instead we use an alternate timeout
 * for each task if the min delay during the interval period is too
 * high.
 *
 * This was found to have better latency metrics than changing the
 * window size, since we can communicate with the sender via thrift
 * instead of only via the tcp window size congestion control, as in TCP.
 */
class Codel {

 public:
  Codel();

  // Given a delay, returns wether the codel algorithm would
  // reject a queued request with this delay.
  //
  // Internally, it also keeps track of the interval
  bool overloaded(std::chrono::microseconds delay);

  // Get the queue load, as seen by the codel algorithm
  // Gives a rough guess at how bad the queue delay is.
  //
  // Return:  0 = no delay, 100 = At the queueing limit
  int getLoad();

  int getMinDelay();

 private:
  std::chrono::microseconds codelMinDelay_;
  std::chrono::time_point<std::chrono::steady_clock> codelIntervalTime_;

  // flag to make overloaded() thread-safe, since we only want
  // to reset the delay once per time period
  std::atomic<bool> codelResetDelay_;

  bool overloaded_;
};

}} // Namespace
