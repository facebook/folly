/*
 * Copyright 2014 Facebook, Inc.
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

// SingletonVault - a library to manage the creation and destruction
// of interdependent singletons.
//
// Basic usage of this class is very simple; suppose you have a class
// called MyExpensiveService, and you only want to construct one (ie,
// it's a singleton), but you only want to construct it if it is used.
//
// In your .h file:
// class MyExpensiveService { ... };
//
// In your .cpp file:
// namespace { folly::Singleton<MyExpensiveService> the_singleton; }
//
// Code can access it via:
//
// MyExpensiveService* instance = Singleton<MyExpensiveService>::get();
// or
// std::weak_ptr<MyExpensiveService> instance =
//     Singleton<MyExpensiveService>::get_weak();
//
// The singleton will be created on demand.  If the constructor for
// MyExpensiveService actually makes use of *another* Singleton, then
// the right thing will happen -- that other singleton will complete
// construction before get() returns.  However, in the event of a
// circular dependency, a runtime error will occur.
//
// By default, the singleton instance is constructed via new and
// deleted via delete, but this is configurable:
//
// namespace { folly::Singleton<MyExpensiveService> the_singleton(create,
//                                                                destroy); }
//
// Where create and destroy are functions, Singleton<T>::CreateFunc
// Singleton<T>::TeardownFunc.
//
// What if you need to destroy all of your singletons?  Say, some of
// your singletons manage threads, but you need to fork?  Or your unit
// test wants to clean up all global state?  Then you can call
// SingletonVault::singleton()->destroyInstances(), which invokes the
// TeardownFunc for each singleton, in the reverse order they were
// created.  It is your responsibility to ensure your singletons can
// handle cases where the singletons they depend on go away, however.

#pragma once
#include <folly/Exception.h>

#include <vector>
#include <mutex>
#include <string>
#include <unordered_map>
#include <functional>
#include <typeinfo>
#include <typeindex>

#include <glog/logging.h>

namespace folly {

// For actual usage, please see the Singleton<T> class at the bottom
// of this file; that is what you will actually interact with.

// SingletonVault is the class that manages singleton instances.  It
// is unaware of the underlying types of singletons, and simply
// manages lifecycles and invokes CreateFunc and TeardownFunc when
// appropriate.  In general, you won't need to interact with the
// SingletonVault itself.
//
// A vault goes through a few stages of life:
//
//   1. Registration phase; singletons can be registered, but no
//      singleton can be created.
//   2. registrationComplete() has been called; singletons can no
//      longer be registered, but they can be created.
//   3. A vault can return to stage 1 when destroyInstances is called.
//
// In general, you don't need to worry about any of the above; just
// ensure registrationComplete() is called near the top of your main()
// function, otherwise no singletons can be instantiated.
class SingletonVault {
 public:
  SingletonVault() {};
  ~SingletonVault();

  typedef std::function<void(void*)> TeardownFunc;
  typedef std::function<void*(void)> CreateFunc;

  // Register a singleton of a given type with the create and teardown
  // functions.
  void registerSingleton(const std::type_info& type,
                         CreateFunc create,
                         TeardownFunc teardown) {
    std::lock_guard<std::mutex> guard(mutex_);

    CHECK_THROW(state_ == SingletonVaultState::Registering, std::logic_error);
    CHECK_THROW(singletons_.find(type) == singletons_.end(), std::logic_error);
    auto& entry = singletons_[type];
    if (!entry) {
      entry.reset(new SingletonEntry);
    }

    std::lock_guard<std::mutex> entry_guard(entry->mutex_);
    CHECK(entry->instance == nullptr);
    CHECK(create);
    CHECK(teardown);
    entry->create = create;
    entry->teardown = teardown;
    entry->state = SingletonEntryState::Dead;
  }

  // Mark registration is complete; no more singletons can be
  // registered at this point.
  void registrationComplete() {
    std::lock_guard<std::mutex> guard(mutex_);
    CHECK_THROW(state_ == SingletonVaultState::Registering, std::logic_error);
    state_ = SingletonVaultState::Running;
  }

  // Destroy all singletons; when complete, the vault can create
  // singletons once again, or remain dormant.
  void destroyInstances();

  // Retrieve a singleton from the vault, creating it if necessary.
  std::shared_ptr<void> get_shared(const std::type_info& type) {
    std::unique_lock<std::mutex> lock(mutex_);
    CHECK_THROW(state_ == SingletonVaultState::Running, std::logic_error);

    auto it = singletons_.find(type);
    if (it == singletons_.end()) {
      throw std::out_of_range(std::string("non-existent singleton: ") +
                              type.name());
    }

    auto& entry = it->second;
    std::unique_lock<std::mutex> entry_lock(entry->mutex_);

    if (entry->state == SingletonEntryState::BeingBorn) {
      throw std::out_of_range(std::string("circular singleton dependency: ") +
                              type.name());
    }

    if (entry->instance == nullptr) {
      CHECK(entry->state == SingletonEntryState::Dead);
      entry->state = SingletonEntryState::BeingBorn;

      entry_lock.unlock();
      lock.unlock();
      // Can't use make_shared -- no support for a custom deleter, sadly.
      auto instance = std::shared_ptr<void>(entry->create(), entry->teardown);
      lock.lock();
      entry_lock.lock();

      CHECK(entry->state == SingletonEntryState::BeingBorn);
      entry->instance = instance;
      entry->state = SingletonEntryState::Living;

      creation_order_.push_back(type);
    }
    CHECK(entry->state == SingletonEntryState::Living);

    return entry->instance;
  }

  // For testing; how many registered and living singletons we have.
  size_t registeredSingletonCount() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return singletons_.size();
  }

  size_t livingSingletonCount() const {
    std::lock_guard<std::mutex> guard(mutex_);
    size_t ret = 0;
    for (const auto& p : singletons_) {
      if (p.second->instance) {
        ++ret;
      }
    }

    return ret;
  }

  // A well-known vault; you can actually have others, but this is the
  // default.
  static SingletonVault* singleton();

 private:
  // The two stages of life for a vault, as mentioned in the class comment.
  enum class SingletonVaultState {
    Registering,
    Running,
  };

  // Each singleton in the vault can be in three states: dead
  // (registered but never created), being born (running the
  // CreateFunc), and living (CreateFunc returned an instance).
  enum class SingletonEntryState {
    Dead,
    BeingBorn,
    Living,
  };

  // An actual instance of a singleton, tracking the instance itself,
  // its state as described above, and the create and teardown
  // functions.
  struct SingletonEntry {
    std::mutex mutex_;
    std::shared_ptr<void> instance;
    CreateFunc create = nullptr;
    TeardownFunc teardown = nullptr;
    SingletonEntryState state = SingletonEntryState::Dead;

    SingletonEntry() = default;
    SingletonEntry(const SingletonEntry&) = delete;
    SingletonEntry& operator=(const SingletonEntry&) = delete;
    SingletonEntry& operator=(SingletonEntry&&) = delete;
    SingletonEntry(SingletonEntry&&) = delete;
  };

  mutable std::mutex mutex_;
  typedef std::unique_ptr<SingletonEntry> SingletonEntryPtr;
  std::unordered_map<std::type_index, SingletonEntryPtr> singletons_;
  std::vector<std::type_index> creation_order_;
  SingletonVaultState state_ = SingletonVaultState::Registering;
};

// This is the wrapper class that most users actually interact with.
// It allows for simple access to registering and instantiating
// singletons.  Create instances of this class in the global scope of
// type Singleton<T> to register your singleton for later access via
// Singleton<T>::get().
template <typename T>
class Singleton {
 public:
  typedef std::function<T*(void)> CreateFunc;
  typedef std::function<void(T*)> TeardownFunc;

  // Generally your program life cycle should be fine with calling
  // get() repeatedly rather than saving the reference, and then not
  // call get() during process shutdown.
  static T* get(SingletonVault* vault = nullptr /* for testing */) {
    return get_shared(vault).get();
  }

  // If, however, you do need to hold a reference to the specific
  // singleton, you can try to do so with a weak_ptr.  Avoid this when
  // possible but the inability to lock the weak pointer can be a
  // signal that the vault has been destroyed.
  static std::weak_ptr<T> get_weak(SingletonVault* vault =
                                       nullptr /* for testing */) {
    return std::weak_ptr<T>(get_shared(vault));
  }

  Singleton(Singleton::CreateFunc c = nullptr,
            Singleton::TeardownFunc t = nullptr,
            SingletonVault* vault = nullptr /* for testing */) {
    if (c == nullptr) {
      c = []() { return new T; };
    }
    SingletonVault::TeardownFunc teardown;
    if (t == nullptr) {
      teardown = [](void* v) { delete static_cast<T*>(v); };
    } else {
      teardown = [t](void* v) { t(static_cast<T*>(v)); };
    }

    if (vault == nullptr) {
      vault = SingletonVault::singleton();
    }

    vault->registerSingleton(typeid(T), c, teardown);
  }

 private:
  // Don't use this function, it's private for a reason!  Using it
  // would defeat the *entire purpose* of the vault in that we lose
  // the ability to guarantee that, after a destroyInstances is
  // called, all instances are, in fact, destroyed.  You should use
  // weak_ptr if you need to hold a reference to the singleton and
  // guarantee briefly that it exists.
  //
  // Yes, you can just get the weak pointer and lock it, but hopefully
  // if you have taken the time to read this far, you see why that
  // would be bad.
  static std::shared_ptr<T> get_shared(SingletonVault* vault =
                                           nullptr /* for testing */) {
    return std::static_pointer_cast<T>(
        (vault ?: SingletonVault::singleton())->get_shared(typeid(T)));
  }
};
}
