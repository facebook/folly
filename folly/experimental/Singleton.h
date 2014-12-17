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
// Within same compilation unit you should directly access it by the variable
// defining the singleton via get_fast()/get_weak_fast(), and even treat that
// variable like a smart pointer (dereferencing it or using the -> operator):
//
// MyExpensiveService* instance = the_singleton.get_fast();
// or
// std::weak_ptr<MyExpensiveService> instance = the_singleton.get_weak_fast();
// or even
// the_singleton->doSomething();
//
// *_fast() accessors are faster than static accessors, and have performance
// similar to Meyers singletons/static objects.
//
// Please note, however, that all non-weak_ptr interfaces are
// inherently subject to races with destruction.  Use responsibly.
//
// The singleton will be created on demand.  If the constructor for
// MyExpensiveService actually makes use of *another* Singleton, then
// the right thing will happen -- that other singleton will complete
// construction before get() returns.  However, in the event of a
// circular dependency, a runtime error will occur.
//
// You can have multiple singletons of the same underlying type, but
// each must be given a unique tag. If no tag is specified - default tag is used
//
// namespace {
// struct Tag1 {};
// struct Tag2 {};
// folly::Singleton<MyExpensiveService> s_default();
// folly::Singleton<MyExpensiveService, Tag1> s1();
// folly::Singleton<MyExpensiveService, Tag2> s2();
// }
// ...
// MyExpensiveService* svc_default = s_default.get_fast();
// MyExpensiveService* svc1 = s1.get_fast();
// MyExpensiveService* svc2 = s2.get_fast();
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
// Singletons won't be recreated after destroyInstances call. If you
// want to re-enable singleton creation (say after fork was called) you
// should call reenableInstances.

#pragma once
#include <folly/Exception.h>
#include <folly/Hash.h>
#include <folly/Memory.h>
#include <folly/RWSpinLock.h>

#include <algorithm>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
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

namespace detail {

struct DefaultTag {};

// A TypeDescriptor is the unique handle for a given singleton.  It is
// a combinaiton of the type and of the optional name, and is used as
// a key in unordered_maps.
class TypeDescriptor {
 public:
  TypeDescriptor(const std::type_info& ti,
                 const std::type_info& tag_ti)
      : ti_(ti), tag_ti_(tag_ti) {
  }

  TypeDescriptor(const TypeDescriptor& other)
      : ti_(other.ti_), tag_ti_(other.tag_ti_) {
  }

  TypeDescriptor& operator=(const TypeDescriptor& other) {
    if (this != &other) {
      ti_ = other.ti_;
      tag_ti_ = other.tag_ti_;
    }

    return *this;
  }

  std::string name() const {
    std::string ret = ti_.name();
    if (tag_ti_ != std::type_index(typeid(DefaultTag))) {
      ret += "/";
      ret += tag_ti_.name();
    }
    return ret;
  }

  friend class TypeDescriptorHasher;

  bool operator==(const TypeDescriptor& other) const {
    return ti_ == other.ti_ && tag_ti_ == other.tag_ti_;
  }

 private:
  std::type_index ti_;
  std::type_index tag_ti_;
};

class TypeDescriptorHasher {
 public:
  size_t operator()(const TypeDescriptor& ti) const {
    return folly::hash::hash_combine(ti.ti_, ti.tag_ti_);
  }
};

enum class SingletonEntryState {
  Dead,
  Living,
};

// An actual instance of a singleton, tracking the instance itself,
// its state as described above, and the create and teardown
// functions.
struct SingletonEntry {
  typedef std::function<void(void*)> TeardownFunc;
  typedef std::function<void*(void)> CreateFunc;

  SingletonEntry(CreateFunc c, TeardownFunc t) :
      create(std::move(c)), teardown(std::move(t)) {}

  // mutex protects the entire entry during construction/destruction
  std::mutex mutex;

  // State of the singleton entry. If state is Living, instance_ptr and
  // instance_weak can be safely accessed w/o synchronization.
  std::atomic<SingletonEntryState> state{SingletonEntryState::Dead};

  // the thread creating the singleton (only valid while creating an object)
  std::thread::id creating_thread;

  // The singleton itself and related functions.

  // holds a shared_ptr to singleton instance, set when state is changed from
  // Dead to Living. Reset when state is changed from Living to Dead.
  std::shared_ptr<void> instance;
  // weak_ptr to the singleton instance, set when state is changed from Dead
  // to Living. We never write to this object after initialization, so it is
  // safe to read it from different threads w/o synchronization if we know
  // that state is set to Living
  std::weak_ptr<void> instance_weak;
  void* instance_ptr = nullptr;
  CreateFunc create = nullptr;
  TeardownFunc teardown = nullptr;

  SingletonEntry(const SingletonEntry&) = delete;
  SingletonEntry& operator=(const SingletonEntry&) = delete;
  SingletonEntry& operator=(SingletonEntry&&) = delete;
  SingletonEntry(SingletonEntry&&) = delete;
};

}

class SingletonVault {
 public:
  enum class Type { Strict, Relaxed };

  explicit SingletonVault(Type type = Type::Relaxed) : type_(type) {}

  // Destructor is only called by unit tests to check destroyInstances.
  ~SingletonVault();

  typedef std::function<void(void*)> TeardownFunc;
  typedef std::function<void*(void)> CreateFunc;

  // Ensure that Singleton has not been registered previously and that
  // registration is not complete. If validations succeeds,
  // register a singleton of a given type with the create and teardown
  // functions.
  detail::SingletonEntry& registerSingleton(detail::TypeDescriptor type,
                                            CreateFunc create,
                                            TeardownFunc teardown) {
    RWSpinLock::ReadHolder rh(&stateMutex_);

    stateCheck(SingletonVaultState::Running);

    if (UNLIKELY(registrationComplete_)) {
      throw std::logic_error(
        "Registering singleton after registrationComplete().");
    }

    RWSpinLock::ReadHolder rhMutex(&mutex_);
    CHECK_THROW(singletons_.find(type) == singletons_.end(), std::logic_error);

    return registerSingletonImpl(type, create, teardown);
  }

  // Register a singleton of a given type with the create and teardown
  // functions. Must hold reader locks on stateMutex_ and mutex_
  // when invoking this function.
  detail::SingletonEntry& registerSingletonImpl(detail::TypeDescriptor type,
                             CreateFunc create,
                             TeardownFunc teardown) {
    RWSpinLock::UpgradedHolder wh(&mutex_);

    singletons_[type] =
      folly::make_unique<detail::SingletonEntry>(std::move(create),
                                                 std::move(teardown));
    return *singletons_[type];
  }

  /* Register a mock singleton used for testing of singletons which
   * depend on other private singletons which cannot be otherwise injected.
   */
  void registerMockSingleton(detail::TypeDescriptor type,
                             CreateFunc create,
                             TeardownFunc teardown) {
    RWSpinLock::ReadHolder rh(&stateMutex_);
    RWSpinLock::ReadHolder rhMutex(&mutex_);

    auto entry_it = singletons_.find(type);
    // Mock singleton registration, we allow existing entry to be overridden.
    if (entry_it == singletons_.end()) {
      throw std::logic_error(
        "Registering mock before the singleton was registered");
    }

    {
      auto& entry = *(entry_it->second);
      // Destroy existing singleton.
      std::lock_guard<std::mutex> entry_lg(entry.mutex);

      destroyInstance(entry_it);
      entry.create = create;
      entry.teardown = teardown;
    }

    // Upgrade to write lock.
    RWSpinLock::UpgradedHolder whMutex(&mutex_);

    // Remove singleton from creation order and singletons_.
    // This happens only in test code and not frequently.
    // Performance is not a concern here.
    auto creation_order_it = std::find(
      creation_order_.begin(),
      creation_order_.end(),
      type);
    if (creation_order_it != creation_order_.end()) {
      creation_order_.erase(creation_order_it);
    }
  }

  // Mark registration is complete; no more singletons can be
  // registered at this point.
  void registrationComplete() {
    std::atexit([](){ SingletonVault::singleton()->destroyInstances(); });

    RWSpinLock::WriteHolder wh(&stateMutex_);

    stateCheck(SingletonVaultState::Running);

    if (type_ == Type::Strict) {
      for (const auto& id_singleton_entry: singletons_) {
        const auto& singleton_entry = *id_singleton_entry.second;
        if (singleton_entry.state != detail::SingletonEntryState::Dead) {
          throw std::runtime_error(
            "Singleton created before registration was complete.");
        }
      }
    }

    registrationComplete_ = true;
  }

  // Destroy all singletons; when complete, the vault can't create
  // singletons once again until reenableInstances() is called.
  void destroyInstances();

  // Enable re-creating singletons after destroyInstances() was called.
  void reenableInstances();

  // Retrieve a singleton from the vault, creating it if necessary.
  std::weak_ptr<void> get_weak(detail::TypeDescriptor type) {
    auto entry = get_entry_create(type);
    return entry->instance_weak;
  }

  // This function is inherently racy since we don't hold the
  // shared_ptr that contains the Singleton.  It is the caller's
  // responsibility to be sane with this, but it is preferable to use
  // the weak_ptr interface for true safety.
  void* get_ptr(detail::TypeDescriptor type) {
    auto entry = get_entry_create(type);
    if (UNLIKELY(entry->instance_weak.expired())) {
      throw std::runtime_error(
        "Raw pointer to a singleton requested after its destruction.");
    }
    return entry->instance_ptr;
  }

  // For testing; how many registered and living singletons we have.
  size_t registeredSingletonCount() const {
    RWSpinLock::ReadHolder rh(&mutex_);

    return singletons_.size();
  }

  size_t livingSingletonCount() const {
    RWSpinLock::ReadHolder rh(&mutex_);

    size_t ret = 0;
    for (const auto& p : singletons_) {
      if (p.second->state == detail::SingletonEntryState::Living) {
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
    Running,
    Quiescing,
  };

  // Each singleton in the vault can be in two states: dead
  // (registered but never created), living (CreateFunc returned an instance).

  void stateCheck(SingletonVaultState expected,
                  const char* msg="Unexpected singleton state change") {
    if (expected != state_) {
        throw std::logic_error(msg);
    }
  }

  // This method only matters if registrationComplete() is never called.
  // Otherwise destroyInstances is scheduled to be executed atexit.
  //
  // Initializes static object, which calls destroyInstances on destruction.
  // Used to have better deletion ordering with singleton not managed by
  // folly::Singleton. The desruction will happen in the following order:
  // 1. Singletons, not managed by folly::Singleton, which were created after
  //    any of the singletons managed by folly::Singleton was requested.
  // 2. All singletons managed by folly::Singleton
  // 3. Singletons, not managed by folly::Singleton, which were created before
  //    any of the singletons managed by folly::Singleton was requested.
  static void scheduleDestroyInstances();

  detail::SingletonEntry* get_entry(detail::TypeDescriptor type) {
    RWSpinLock::ReadHolder rh(&mutex_);

    auto it = singletons_.find(type);
    if (it == singletons_.end()) {
      throw std::out_of_range(std::string("non-existent singleton: ") +
                              type.name());
    }

    return it->second.get();
  }

  // Get a pointer to the living SingletonEntry for the specified
  // type.  The singleton is created as part of this function, if
  // necessary.
  detail::SingletonEntry* get_entry_create(detail::TypeDescriptor type) {
    auto entry = get_entry(type);

    if (LIKELY(entry->state == detail::SingletonEntryState::Living)) {
      return entry;
    }

    // There's no synchronization here, so we may not see the current value
    // for creating_thread if it was set by other thread, but we only care about
    // it if it was set by current thread anyways.
    if (entry->creating_thread == std::this_thread::get_id()) {
      throw std::out_of_range(std::string("circular singleton dependency: ") +
                              type.name());
    }

    std::lock_guard<std::mutex> entry_lock(entry->mutex);

    if (entry->state == detail::SingletonEntryState::Living) {
      return entry;
    }

    entry->creating_thread = std::this_thread::get_id();

    RWSpinLock::ReadHolder rh(&stateMutex_);
    if (state_ == SingletonVaultState::Quiescing) {
      entry->creating_thread = std::thread::id();
      return entry;
    }

    // Can't use make_shared -- no support for a custom deleter, sadly.
    auto instance = std::shared_ptr<void>(entry->create(), entry->teardown);

    // We should schedule destroyInstances() only after the singleton was
    // created. This will ensure it will be destroyed before singletons,
    // not managed by folly::Singleton, which were initialized in its
    // constructor
    scheduleDestroyInstances();

    entry->instance = instance;
    entry->instance_weak = instance;
    entry->instance_ptr = instance.get();
    entry->creating_thread = std::thread::id();

    // This has to be the last step, because once state is Living other threads
    // may access instance and instance_weak w/o synchronization.
    entry->state.store(detail::SingletonEntryState::Living);

    {
      RWSpinLock::WriteHolder wh(&mutex_);
      creation_order_.push_back(type);
    }
    return entry;
  }

  typedef std::unique_ptr<detail::SingletonEntry> SingletonEntryPtr;
  typedef std::unordered_map<detail::TypeDescriptor,
                             SingletonEntryPtr,
                             detail::TypeDescriptorHasher> SingletonMap;

  /* Destroy and clean-up one singleton. Must be invoked while holding
   * a read lock on mutex_.
   * @param typeDescriptor - the type key for the removed singleton.
   */
  void destroyInstance(SingletonMap::iterator entry_it);

  mutable folly::RWSpinLock mutex_;
  SingletonMap singletons_;
  std::vector<detail::TypeDescriptor> creation_order_;
  SingletonVaultState state_{SingletonVaultState::Running};
  bool registrationComplete_{false};
  folly::RWSpinLock stateMutex_;
  Type type_{Type::Relaxed};
};

// This is the wrapper class that most users actually interact with.
// It allows for simple access to registering and instantiating
// singletons.  Create instances of this class in the global scope of
// type Singleton<T> to register your singleton for later access via
// Singleton<T>::get().
template <typename T, typename Tag = detail::DefaultTag>
class Singleton {
 public:
  typedef std::function<T*(void)> CreateFunc;
  typedef std::function<void(T*)> TeardownFunc;

  // Generally your program life cycle should be fine with calling
  // get() repeatedly rather than saving the reference, and then not
  // call get() during process shutdown.
  static T* get(SingletonVault* vault = nullptr /* for testing */) {
    return static_cast<T*>(
      (vault ?: SingletonVault::singleton())->get_ptr(typeDescriptor()));
  }

  // Same as get, but should be preffered to it in the same compilation
  // unit, where Singleton is registered.
  T* get_fast() {
    if (LIKELY(entry_->state == detail::SingletonEntryState::Living)) {
      return reinterpret_cast<T*>(entry_->instance_ptr);
    } else {
      return get(vault_);
    }
  }

  // If, however, you do need to hold a reference to the specific
  // singleton, you can try to do so with a weak_ptr.  Avoid this when
  // possible but the inability to lock the weak pointer can be a
  // signal that the vault has been destroyed.
  static std::weak_ptr<T> get_weak(
      SingletonVault* vault = nullptr /* for testing */) {
    auto weak_void_ptr =
      (vault ?: SingletonVault::singleton())->get_weak(typeDescriptor());

    // This is ugly and inefficient, but there's no other way to do it, because
    // there's no static_pointer_cast for weak_ptr.
    auto shared_void_ptr = weak_void_ptr.lock();
    if (!shared_void_ptr) {
      return std::weak_ptr<T>();
    }
    return std::static_pointer_cast<T>(shared_void_ptr);
  }

  // Same as get_weak, but should be preffered to it in the same compilation
  // unit, where Singleton is registered.
  std::weak_ptr<T> get_weak_fast() {
    if (LIKELY(entry_->state == detail::SingletonEntryState::Living)) {
      // This is ugly and inefficient, but there's no other way to do it,
      // because there's no static_pointer_cast for weak_ptr.
      auto shared_void_ptr = entry_->instance_weak.lock();
      if (!shared_void_ptr) {
        return std::weak_ptr<T>();
      }
      return std::static_pointer_cast<T>(shared_void_ptr);
    } else {
      return get_weak(vault_);
    }
  }

  // Allow the Singleton<t> instance to also retrieve the underlying
  // singleton, if desired.
  T* ptr() { return get_fast(); }
  T& operator*() { return *ptr(); }
  T* operator->() { return ptr(); }

  explicit Singleton(std::nullptr_t _ = nullptr,
                     Singleton::TeardownFunc t = nullptr,
                     SingletonVault* vault = nullptr) :
      Singleton ([]() { return new T; },
                 std::move(t),
                 vault) {
  }

  explicit Singleton(Singleton::CreateFunc c,
                     Singleton::TeardownFunc t = nullptr,
                     SingletonVault* vault = nullptr) {
    if (c == nullptr) {
      throw std::logic_error(
        "nullptr_t should be passed if you want T to be default constructed");
    }

    if (vault == nullptr) {
      vault = SingletonVault::singleton();
    }

    vault_ = vault;
    entry_ =
      &(vault->registerSingleton(typeDescriptor(), c, getTeardownFunc(t)));
  }

  /**
  * Construct and inject a mock singleton which should be used only from tests.
  * Unlike regular singletons which are initialized once per process lifetime,
  * mock singletons live for the duration of a test. This means that one process
  * running multiple tests can initialize and register the same singleton
  * multiple times. This functionality should be used only from tests
  * since it relaxes validation and performance in order to be able to perform
  * the injection. The returned mock singleton is functionality identical to
  * regular singletons.
  */
  static void make_mock(std::nullptr_t c = nullptr,
                        typename Singleton<T>::TeardownFunc t = nullptr,
                        SingletonVault* vault = nullptr /* for testing */ ) {
    make_mock([]() { return new T; }, t, vault);
  }

  static void make_mock(CreateFunc c,
                        typename Singleton<T>::TeardownFunc t = nullptr,
                        SingletonVault* vault = nullptr /* for testing */ ) {
    if (c == nullptr) {
      throw std::logic_error(
        "nullptr_t should be passed if you want T to be default constructed");
    }

    if (vault == nullptr) {
      vault = SingletonVault::singleton();
    }

    vault->registerMockSingleton(
      typeDescriptor(),
      c,
      getTeardownFunc(t));
  }

 private:
  static detail::TypeDescriptor typeDescriptor() {
    return {typeid(T), typeid(Tag)};
  }

  // Construct SingletonVault::TeardownFunc.
  static SingletonVault::TeardownFunc getTeardownFunc(
      TeardownFunc t) {
    SingletonVault::TeardownFunc teardown;
    if (t == nullptr) {
      teardown = [](void* v) { delete static_cast<T*>(v); };
    } else {
      teardown = [t](void* v) { t(static_cast<T*>(v)); };
    }

    return teardown;
  }

  // This is pointing to SingletonEntry paired with this singleton object. This
  // is never reset, so each SingletonEntry should never be destroyed.
  // We rely on the fact that Singleton destructor won't reset this pointer, so
  // it can be "safely" used even after static Singleton object is destroyed.
  detail::SingletonEntry* entry_;
  SingletonVault* vault_;
};

}
