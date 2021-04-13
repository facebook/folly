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
#include <array>
#include <atomic>
#include <stdexcept>

#include <folly/Format.h>
#include <folly/Function.h>
#include <folly/Optional.h>
#include <folly/SharedMutex.h>

namespace folly {

// A mixin to register and issue callbacks every time a class constructor is
// invoked
//
// For example:
// #include <folly/ConstructorCallback>
//
// class Foo {
//    ...
//   private:
//    ...
//    // add this member last to minimize partially constructed errors
//    ConstructorCallback<Foo> constructorCB_{this};
// }
//
// int main() {
//   auto cb = [](Foo * f) {
//    std::cout << "New Foo" << f << std::endl;
//   };
//   ConstructorCallback<Foo>::addNewConstructorCallback(cb);
//   Foo f{}; // will call callback, print to stdout
// }
//
// This code is designed to be light weight so as to mixin to many
// places with low overhead.
//
// NOTE: The callback is triggered with a *partially* constructed object.
// This implies that that callback code can only access members that are
// constructed *before* the ConstructorCallback object.  Also, at the time
// of the callback, none of the Foo() constructor code will have run.
// Per the example above,
// the best practice is to place the constructorCallback declaration last
// in the parent class.  This will minimize the amount of uninitialized
// data in the Foo instance, but will not eliminate it unless it has a trivial
// constructor.
//
// Implementation/Overhead Notes:
//
// By design, adding ConstructorCallback() to an object shoud be very light
// weight.  From a memory context, this adds 1 byte of memory to the parent
// class. From a CPU/performance perspective, the constructor does a load of an
// atomic int and the cost of the actual callbacks themselves.  So if this
// is put in place and only used infrequently, e.g., during debugging,
// this cost should be quite small.
//
// A compile-time static array is used intentionally over a dynamic one for
// two reasons: (1) a dynamic array seems to require a proper lock in
// the constructor which would exceed our perf target, and (2) having a
// finite array provides some sanity checking on the number of callbacks
// that can be registered.

template <class T, std::size_t MaxCallbacks = 4>
class ConstructorCallback {
 public:
  static constexpr std::size_t kMaxCallbacks = MaxCallbacks;

  using NewConstructorCallback = folly::Function<void(T*)>;
  using This = ConstructorCallback<T, MaxCallbacks>;

  explicit ConstructorCallback(T* t) {
    // fire callbacks to inform listeners about the new constructor
    auto nCBs = This::nConstructorCallbacks_.load(std::memory_order_acquire);
    /****
     * We don't need the full lock here, just the atomic int to tell us
     * how far into the array to go/how many callbacks are registered
     *
     * NOTE that nCBs > 0 will always imply that callbacks_ is non-nullopt
     */
    for (int i = 0; i < nCBs; i++) {
      This::callbacks_.value()[i](t);
    }
  }

  /**
   * Add a callback to the static class that will fire every time
   * someone creates a new one.
   *
   * Implement this as a static array of callbacks rather than a dynamic
   * vector to avoid nasty race conditions on resize, startup and shutdown.
   *
   * Implement this with functions rather than an observer pattern classes
   * to avoid race conditions on shutdown
   *
   * Intentionally don't implement removeConstructorCallback to simplify
   * implementation (e.g., just the counter is atomic rather than the whole
   * array) and thus reduce computational cost.
   *
   * @throw std::length_error() if this callback would exceed our max
   */
  static void addNewConstructorCallback(NewConstructorCallback cb) {
    std::lock_guard<SharedMutex> g(This::getMutex());
    auto idx = nConstructorCallbacks_.load(std::memory_order_acquire);
    if (!callbacks_) {
      // initialize the array if unallocated
      callbacks_.emplace(
          std::array<This::NewConstructorCallback, MaxCallbacks>());
    }
    if (idx >= callbacks_.value().size()) {
      throw std::length_error(
          folly::sformat("Too many callbacks - max {}", MaxCallbacks));
    }
    callbacks_.value()[idx] = std::move(cb);
    // Only increment nConstructorCallbacks_ after fully initializing the array
    // entry. This step makes the new array entry visible to other threads.
    nConstructorCallbacks_.store(idx + 1, std::memory_order_release);
  }

 private:
  // allocate an array internal to function to avoid init() races
  static folly::Optional<std::array<NewConstructorCallback, MaxCallbacks>>
      callbacks_;
  static folly::SharedMutex& getMutex();
  static std::atomic<int> nConstructorCallbacks_;
};

template <class T, std::size_t MaxCallbacks>
std::atomic<int> ConstructorCallback<T, MaxCallbacks>::nConstructorCallbacks_{
    0};

template <class T, std::size_t MaxCallbacks>
folly::SharedMutex& ConstructorCallback<T, MaxCallbacks>::getMutex() {
  static folly::SharedMutex mutex;
  return mutex;
}

template <class T, std::size_t MaxCallbacks>
folly::Optional<std::array<
    typename ConstructorCallback<T, MaxCallbacks>::NewConstructorCallback,
    MaxCallbacks>>
    ConstructorCallback<T, MaxCallbacks>::callbacks_{folly::none};

} // namespace folly
