/*
 * Copyright 2015-present Facebook, Inc.
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

#include <folly/Function.h>
#include <condition_variable>
#include <thread>
#include <vector>

namespace folly {

/**
 * For each function `fn` you add to this object, `fn` will be run in a loop
 * in its own thread, with the thread sleeping between invocations of `fn`
 * for the duration returned by `fn`'s previous run.
 *
 * To clean up these threads, invoke `stop()`, which will interrupt sleeping
 * threads.  `stop()` will wait for already-running functions to return.
 *
 * == Alternatives ==
 *
 * If you want to multiplex multiple functions on the same thread, you can
 * either use EventBase with AsyncTimeout objects, or FunctionScheduler for
 * a slightly simpler API.
 *
 * == Thread-safety ==
 *
 * This type follows the common rule that:
 *  (1) const member functions are safe to call concurrently with const
 *      member functions, but
 *  (2) non-const member functions are not safe to call concurrently with
 *      any member functions.
 *
 * == Pitfalls ==
 *
 * Threads and classes don't mix well in C++, so you have to be very careful
 * if you want to have ThreadedRepeatingFunctionRunner as a member of your
 * class.  A reasonable pattern looks like this:
 *
 * struct MyClass {
 *   // Note that threads are NOT added in the constructor, for two reasons:
 *   //
 *   //   (1) If you added some, and had any subsequent initialization (e.g.
 *   //       derived class constructors), 'this' would not be fully
 *   //       constructed when the worker threads came up, causing
 *   //       heisenbugs.
 *   //
 *   //   (2) Also, if your constructor threw after thread creation, the
 *   //       class destructor would not be invoked, potentially leaving the
 *   //       threads running too long.
 *   //
 *   // It's better to have explicit two-step initialization, or to lazily
 *   // add threads the first time they are needed.
 *   MyClass() : count_(0) {}
 *
 *   // You must stop the threads as early as possible in the destruction
 *   // process (or even before).  In the case of a class hierarchy, the
 *   // final class MUST always call stop() as the first thing in its
 *   // destructor -- otherwise, the worker threads may access already-
 *   // destroyed state.
 *   ~MyClass() {
 *     // if MyClass is abstract:
 *     threads_.stopAndWarn("MyClass");
 *     // Otherwise:
 *     threads_.stop();
 *   }
 *
 *   // See the constructor for why two-stage initialization is preferred.
 *   void init() {
 *     threads_.add(bind(&MyClass::incrementCount, this));
 *   }
 *
 *   std::chrono::milliseconds incrementCount() {
 *     ++count_;
 *     return 10;
 *   }
 *
 * private:
 *   std::atomic<int> count_;
 *   // Declared last because the threads' functions access other members.
 *   ThreadedRepeatingFunctionRunner threads_;
 * };
 */
class ThreadedRepeatingFunctionRunner final {
 public:
  // Returns how long to wait before the next repetition. Must not throw.
  using RepeatingFn = folly::Function<std::chrono::milliseconds() noexcept>;

  ThreadedRepeatingFunctionRunner();
  ~ThreadedRepeatingFunctionRunner();

  /**
   * Ideally, you will call this before initiating the destruction of the
   * host object.  Otherwise, this should be the first thing in the
   * destruction sequence.  If it comes any later, worker threads may access
   * class state that had already been destroyed.
   */
  void stop();

  /**
   * Must be called at the TOP of the destructor of any abstract class that
   * contains ThreadedRepeatingFunctionRunner (directly or through a
   * parent).  Any non-abstract class destructor must instead stop() at the
   * top.
   */
  void stopAndWarn(const std::string& class_of_destructor);

  /**
   * Run your noexcept function `f` in a background loop, sleeping between
   * calls for a duration returned by `f`.  Optionally waits for
   * `initialSleep` before calling `f` for the first time.
   *
   * DANGER: If a non-final class has a ThreadedRepeatingFunctionRunner
   * member (which, by the way, must be declared last in the class), then
   * you must not call add() in your constructor.  Otherwise, your thread
   * risks accessing uninitialized data belonging to a child class.  To
   * avoid this design bug, prefer to use two-stage initialization to start
   * your threads.
   */
  void add(
      RepeatingFn f,
      std::chrono::milliseconds initialSleep = std::chrono::milliseconds(0));

  size_t size() const { return threads_.size(); }

 private:
  // Returns true if this is the first stop().
  bool stopImpl();

  // Sleep for a duration, or until stop() is called.
  bool waitFor(std::chrono::milliseconds duration) noexcept;

  // Noexcept allows us to get a good backtrace on crashes -- otherwise,
  // std::terminate would get called **outside** of the thread function.
  void executeInLoop(
      RepeatingFn,
      std::chrono::milliseconds initialSleep) noexcept;

  std::mutex stopMutex_;
  bool stopping_{false}; // protected by stopMutex_
  std::condition_variable stopCv_;

  std::vector<std::thread> threads_;
};

} // namespace folly
