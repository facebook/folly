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

#ifndef FOLLY_EXPERIMENTAL_FUNCTION_SCHEDULER_H_
#define FOLLY_EXPERIMENTAL_FUNCTION_SCHEDULER_H_

#include <folly/Range.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace folly {

/**
 * Schedules any number of functions to run at various intervals. E.g.,
 *
 *   FunctionScheduler fs;
 *
 *   fs.addFunction([&] { LOG(INFO) << "tick..."; }, seconds(1), "ticker");
 *   fs.addFunction(std::bind(&TestClass::doStuff, this), minutes(5), "stuff");
 *   fs.start();
 *   ........
 *   fs.cancelFunction("ticker");
 *   fs.addFunction([&] { LOG(INFO) << "tock..."; }, minutes(3), "tocker");
 *   ........
 *   fs.shutdown();
 *
 *
 * Note: the class uses only one thread - if you want to use more than one
 *       thread use multiple FunctionScheduler objects
 *
 * start() schedules the functions, while shutdown() terminates further
 * scheduling.
 */
class FunctionScheduler {
 public:
  FunctionScheduler();
  ~FunctionScheduler();

  /**
   * By default steady is false, meaning schedules may lag behind overtime.
   * This could be due to long running tasks or time drift because of randomness
   * in thread wakeup time.
   * By setting steady to true, FunctionScheduler will attempt to catch up.
   * i.e. more like a cronjob
   *
   * NOTE: it's only safe to set this before calling start()
   */
  void setSteady(bool steady) { steady_ = steady; }

  /*
   * Parameters to control the function interval.
   *
   * If isPoisson is true, then use std::poisson_distribution to pick the
   * interval between each invocation of the function.
   *
   * If isPoisson os false, then always use fixed the interval specified to
   * addFunction().
   */
  struct LatencyDistribution {
    bool isPoisson;
    double poissonMean;

    LatencyDistribution(bool poisson, double mean)
      : isPoisson(poisson),
        poissonMean(mean) {
    }
  };

  /**
   * Adds a new function to the FunctionScheduler.
   *
   * Functions will not be run until start() is called.  When start() is
   * called, each function will be run after its specified startDelay.
   * Functions may also be added after start() has been called, in which case
   * startDelay is still honored.
   *
   * Throws an exception on error.  In particular, each function must have a
   * unique name--two functions cannot be added with the same name.
   */
  void addFunction(const std::function<void()>& cb,
                   std::chrono::milliseconds interval,
                   StringPiece nameID = StringPiece(),
                   std::chrono::milliseconds startDelay =
                     std::chrono::milliseconds(0));

  /*
   * Add a new function to the FunctionScheduler with a specified
   * LatencyDistribution
   */
  void addFunction(
      const std::function<void()>& cb,
      std::chrono::milliseconds interval,
      const LatencyDistribution& latencyDistr,
      StringPiece nameID = StringPiece(),
      std::chrono::milliseconds startDelay = std::chrono::milliseconds(0));

  /**
    * Add a new function to the FunctionScheduler with the time
    * interval being distributed uniformly within the given interval
    * [minInterval, maxInterval].
    */
  void addFunctionUniformDistribution(const std::function<void()>& cb,
                                      std::chrono::milliseconds minInterval,
                                      std::chrono::milliseconds maxInterval,
                                      StringPiece nameID,
                                      std::chrono::milliseconds startDelay);

  /**
   * A type alias for function that is called to determine the time
   * interval for the next scheduled run.
   */
  using IntervalDistributionFunc = std::function<std::chrono::milliseconds()>;

  /**
   * Add a new function to the FunctionScheduler. The scheduling interval
   * is determined by the interval distribution functor, which is called
   * every time the next function execution is scheduled. This allows
   * for supporting custom interval distribution algorithms in addition
   * to built in constant interval; and Poisson and jitter distributions
   * (@see FunctionScheduler::addFunction and
   * @see FunctionScheduler::addFunctionJitterInterval).
   */
  void addFunctionGenericDistribution(
      const std::function<void()>& cb,
      const IntervalDistributionFunc& intervalFunc,
      const std::string& nameID,
      const std::string& intervalDescr,
      std::chrono::milliseconds startDelay);

  /**
   * Cancels the function with the specified name, so it will no longer be run.
   *
   * Returns false if no function exists with the specified name.
   */
  bool cancelFunction(StringPiece nameID);

  /**
   * All functions registered will be canceled.
   */
  void cancelAllFunctions();

  /**
   * Starts the scheduler.
   *
   * Returns false if the scheduler was already running.
   */
  bool start();

  /**
   * Stops the FunctionScheduler.
   *
   * It may be restarted later by calling start() again.
   */
  void shutdown();

  /**
   * Set the name of the worker thread.
   */
  void setThreadName(StringPiece threadName);

 private:
  struct RepeatFunc {
    std::function<void()> cb;
    IntervalDistributionFunc intervalFunc;
    std::chrono::steady_clock::time_point nextRunTime;
    std::string name;
    std::chrono::milliseconds startDelay;
    std::string intervalDescr;

    RepeatFunc(const std::function<void()>& cback,
               const IntervalDistributionFunc& intervalFn,
               const std::string& nameID,
               const std::string& intervalDistDescription,
               std::chrono::milliseconds delay)
        : cb(cback),
          intervalFunc(intervalFn),
          nextRunTime(),
          name(nameID),
          startDelay(delay),
          intervalDescr(intervalDistDescription) {}

    std::chrono::steady_clock::time_point getNextRunTime() const {
      return nextRunTime;
    }
    void setNextRunTimeStrict(std::chrono::steady_clock::time_point curTime) {
      nextRunTime = curTime + intervalFunc();
    }
    void setNextRunTimeSteady() { nextRunTime += intervalFunc(); }
    void resetNextRunTime(std::chrono::steady_clock::time_point curTime) {
      nextRunTime = curTime + startDelay;
    }
    void cancel() {
      // Simply reset cb to an empty function.
      cb = std::function<void()>();
    }
    bool isValid() const { return bool(cb); }
  };

  struct RunTimeOrder {
    bool operator()(const RepeatFunc& f1, const RepeatFunc& f2) const {
      return f1.getNextRunTime() > f2.getNextRunTime();
    }
  };

  typedef std::vector<RepeatFunc> FunctionHeap;

  void run();
  void runOneFunction(std::unique_lock<std::mutex>& lock,
                      std::chrono::steady_clock::time_point now);
  void cancelFunction(const std::unique_lock<std::mutex>& lock,
                      FunctionHeap::iterator it);

  std::thread thread_;

  // Mutex to protect our member variables.
  std::mutex mutex_;
  bool running_{false};

  // The functions to run.
  // This is a heap, ordered by next run time.
  FunctionHeap functions_;
  RunTimeOrder fnCmp_;

  // The function currently being invoked by the running thread.
  // This is null when the running thread is idle
  RepeatFunc* currentFunction_{nullptr};

  // Condition variable that is signalled whenever a new function is added
  // or when the FunctionScheduler is stopped.
  std::condition_variable runningCondvar_;

  std::string threadName_;
  bool steady_{false};
};

}

#endif
