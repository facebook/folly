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
#include <iterator>
#include <memory>
#include <stdexcept>

#include <folly/Format.h>
#include <folly/Function.h>
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
  using CallbackArray =
      std::array<typename This::NewConstructorCallback, MaxCallbacks>;

  explicit ConstructorCallback(T* t) {
    // This code depends on the C++ standard where values that are
    // initialized to zero ("Zero Initiation") are initialized before any more
    // complex static pre-main() dynamic initialization - see
    // https://en.cppreference.com/w/cpp/language/initialization) for
    // more details.
    //
    // This assumption prevents a subtle initialization race condition
    // where something could call this code pre-main() before
    // nConstructorCallbacks_ was set to zero, and thus prevents issuing
    // callbacks on garbage data.

    // fire callbacks to inform listeners about the new constructor
    auto nCBs = This::nConstructorCallbacks_.load(std::memory_order_acquire);
    /****
     * We don't need the full lock here, just the atomic int to tell us
     * how far into the array to go/how many callbacks are registered
     *
     * NOTE that nCBs > 0 will always imply that callbacks_ is non-nullptr
     */
    for (int i = 0; i < nCBs; i++) {
      (*This::callbacks_)[i](t);
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
    // Ensure that a single callback is added at a time
    std::lock_guard<SharedMutex> g(This::getMutex());
    auto idx = nConstructorCallbacks_.load(std::memory_order_acquire);
    if (callbacks_ == nullptr) {
      // Get a pointer to the callback array if we've not already done
      // so.
      //
      // NOTE: this only happens once per class (e.g., when the first
      // callback is registered) and we're already locked on getMutex()
      // so this should be very cheap.
      //
      // NOTE: store a raw/dumb pointer to avoid a race condition
      // and -Wglobal-error issues on shutdown
      callbacks_ = This::getCallbackArray();
    }
    if (idx >= (*callbacks_).size()) {
      throw std::length_error(
          folly::sformat("Too many callbacks - max {}", MaxCallbacks));
    }
    (*callbacks_)[idx] = std::move(cb);
    // Only increment nConstructorCallbacks_ after fully initializing the array
    // entry. This step makes the new array entry visible to other threads.
    nConstructorCallbacks_.store(idx + 1, std::memory_order_release);
  }

 private:
  // allocate an array internal to function to avoid init() races
  static folly::SharedMutex& getMutex();
  static This::CallbackArray* getCallbackArray();

  static This::CallbackArray* callbacks_;
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
typename ConstructorCallback<T, MaxCallbacks>::CallbackArray*
    ConstructorCallback<T, MaxCallbacks>::callbacks_;

template <class T, std::size_t MaxCallbacks>
typename ConstructorCallback<T, MaxCallbacks>::CallbackArray*
ConstructorCallback<T, MaxCallbacks>::getCallbackArray() {
  // NOTE the compiler implicitly generates a lock here
  // to avoid double (static) allocation of this object
  // we avoid paying the cost of the lock by only calling this
  // function once per class (while we're already locked on getMutex())
  // and recording the pointer info
  static ConstructorCallback<T, MaxCallbacks>::CallbackArray a{};
  return &a;
}

} // namespace folly
