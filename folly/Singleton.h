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
// You also can directly access it by the variable defining the
// singleton rather than via get(), and even treat that variable like
// a smart pointer (dereferencing it or using the -> operator).
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
// folly::Singleton<MyExpensiveService> s_default;
// folly::Singleton<MyExpensiveService, Tag1> s1;
// folly::Singleton<MyExpensiveService, Tag2> s2;
// }
// ...
// MyExpensiveService* svc_default = s_default.get();
// MyExpensiveService* svc1 = s1.get();
// MyExpensiveService* svc2 = s2.get();
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
// The above examples detail a situation where an expensive singleton is loaded
// on-demand (thus only if needed).  However if there is an expensive singleton
// that will likely be needed, and initialization takes a potentially long time,
// e.g. while initializing, parsing some files, talking to remote services,
// making uses of other singletons, and so on, the initialization of those can
// be scheduled up front, or "eagerly".
//
// In that case the singleton can be declared this way:
//
// namespace {
// auto the_singleton =
//     folly::Singleton<MyExpensiveService>(/* optional create, destroy args */)
//     .shouldEagerInit();
// }
//
// This way the singleton's instance is built at program initialization,
// if the program opted-in to that feature by calling "doEagerInit" or
// "doEagerInitVia" during its startup.
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
#include <folly/Baton.h>
#include <folly/Exception.h>
#include <folly/Hash.h>
#include <folly/Memory.h>
#include <folly/RWSpinLock.h>
#include <folly/Demangle.h>
#include <folly/Executor.h>
#include <folly/io/async/Request.h>
#include <folly/experimental/ReadMostlySharedPtr.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glog/logging.h>

// use this guard to handleSingleton breaking change in 3rd party code
#ifndef FOLLY_SINGLETON_TRY_GET
#define FOLLY_SINGLETON_TRY_GET
#endif

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
//   1. Registration phase; singletons can be registered:
//      a) Strict: no singleton can be created in this stage.
//      b) Relaxed: singleton can be created (the default vault is Relaxed).
//   2. registrationComplete() has been called; singletons can no
//      longer be registered, but they can be created.
//   3. A vault can return to stage 1 when destroyInstances is called.
//
// In general, you don't need to worry about any of the above; just
// ensure registrationComplete() is called near the top of your main()
// function, otherwise no singletons can be instantiated.

class SingletonVault;

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
    auto ret = demangle(ti_.name());
    if (tag_ti_ != std::type_index(typeid(DefaultTag))) {
      ret += "/";
      ret += demangle(tag_ti_.name());
    }
    return ret.toStdString();
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

// This interface is used by SingletonVault to interact with SingletonHolders.
// Having a non-template interface allows SingletonVault to keep a list of all
// SingletonHolders.
class SingletonHolderBase {
 public:
  virtual ~SingletonHolderBase() = default;

  virtual TypeDescriptor type() = 0;
  virtual bool hasLiveInstance() = 0;
  virtual void createInstance() = 0;
  virtual bool creationStarted() = 0;
  virtual void destroyInstance() = 0;

 protected:
  static constexpr std::chrono::seconds kDestroyWaitTime{5};
};

// An actual instance of a singleton, tracking the instance itself,
// its state as described above, and the create and teardown
// functions.
template <typename T>
struct SingletonHolder : public SingletonHolderBase {
 public:
  typedef std::function<void(T*)> TeardownFunc;
  typedef std::function<T*(void)> CreateFunc;

  template <typename Tag, typename VaultTag>
  inline static SingletonHolder<T>& singleton();

  inline T* get();
  inline std::weak_ptr<T> get_weak();
  inline std::shared_ptr<T> try_get();
  inline folly::ReadMostlySharedPtr<T> try_get_fast();

  void registerSingleton(CreateFunc c, TeardownFunc t);
  void registerSingletonMock(CreateFunc c, TeardownFunc t);
  virtual TypeDescriptor type() override;
  virtual bool hasLiveInstance() override;
  virtual void createInstance() override;
  virtual bool creationStarted() override;
  virtual void destroyInstance() override;

 private:
  SingletonHolder(TypeDescriptor type, SingletonVault& vault);

  enum class SingletonHolderState {
    NotRegistered,
    Dead,
    Living,
  };

  TypeDescriptor type_;
  SingletonVault& vault_;

  // mutex protects the entire entry during construction/destruction
  std::mutex mutex_;

  // State of the singleton entry. If state is Living, instance_ptr and
  // instance_weak can be safely accessed w/o synchronization.
  std::atomic<SingletonHolderState> state_{SingletonHolderState::NotRegistered};

  // the thread creating the singleton (only valid while creating an object)
  std::atomic<std::thread::id> creating_thread_;

  // The singleton itself and related functions.

  // holds a ReadMostlyMainPtr to singleton instance, set when state is changed
  // from Dead to Living. Reset when state is changed from Living to Dead.
  folly::ReadMostlyMainPtr<T> instance_;
  // weak_ptr to the singleton instance, set when state is changed from Dead
  // to Living. We never write to this object after initialization, so it is
  // safe to read it from different threads w/o synchronization if we know
  // that state is set to Living
  std::weak_ptr<T> instance_weak_;
  // Fast equivalent of instance_weak_
  folly::ReadMostlyWeakPtr<T> instance_weak_fast_;
  // Time we wait on destroy_baton after releasing Singleton shared_ptr.
  std::shared_ptr<folly::Baton<>> destroy_baton_;
  T* instance_ptr_ = nullptr;
  CreateFunc create_ = nullptr;
  TeardownFunc teardown_ = nullptr;

  std::shared_ptr<std::atomic<bool>> print_destructor_stack_trace_;

  SingletonHolder(const SingletonHolder&) = delete;
  SingletonHolder& operator=(const SingletonHolder&) = delete;
  SingletonHolder& operator=(SingletonHolder&&) = delete;
  SingletonHolder(SingletonHolder&&) = delete;
};

}

class SingletonVault {
 public:
  enum class Type {
    Strict, // Singletons can't be created before registrationComplete()
    Relaxed, // Singletons can be created before registrationComplete()
  };

  /**
   * Clears all singletons in the given vault at ctor and dtor times.
   * Useful for unit-tests that need to clear the world.
   *
   * This need can arise when a unit-test needs to swap out an object used by a
   * singleton for a test-double, but the singleton needing its dependency to be
   * swapped has a type or a tag local to some other translation unit and
   * unavailable in the current translation unit.
   *
   * Other, better approaches to this need are "plz 2 refactor" ....
   */
  struct ScopedExpunger {
    SingletonVault* vault;
    explicit ScopedExpunger(SingletonVault* v) : vault(v) { expunge(); }
    ~ScopedExpunger() { expunge(); }
    void expunge() {
      vault->destroyInstances();
      vault->reenableInstances();
    }
  };

  explicit SingletonVault(Type type = Type::Relaxed) : type_(type) {}

  // Destructor is only called by unit tests to check destroyInstances.
  ~SingletonVault();

  typedef std::function<void(void*)> TeardownFunc;
  typedef std::function<void*(void)> CreateFunc;

  // Ensure that Singleton has not been registered previously and that
  // registration is not complete. If validations succeeds,
  // register a singleton of a given type with the create and teardown
  // functions.
  void registerSingleton(detail::SingletonHolderBase* entry);

  /**
   * Called by `Singleton<T>.shouldEagerInit()` to ensure the instance
   * is built when `doEagerInit[Via]` is called; see those methods
   * for more info.
   */
  void addEagerInitSingleton(detail::SingletonHolderBase* entry);

  // Mark registration is complete; no more singletons can be
  // registered at this point.
  void registrationComplete();

  /**
   * Initialize all singletons which were marked as eager-initialized
   * (using `shouldEagerInit()`).  No return value.  Propagates exceptions
   * from constructors / create functions, as is the usual case when calling
   * for example `Singleton<Foo>::get_weak()`.
   */
  void doEagerInit();

  /**
   * Schedule eager singletons' initializations through the given executor.
   * If baton ptr is not null, its `post` method is called after all
   * early initialization has completed.
   *
   * If exceptions are thrown during initialization, this method will still
   * `post` the baton to indicate completion.  The exception will not propagate
   * and future attempts to `try_get` or `get_weak` the failed singleton will
   * retry initialization.
   *
   * Sample usage:
   *
   *   wangle::IOThreadPoolExecutor executor(max_concurrency_level);
   *   folly::Baton<> done;
   *   doEagerInitVia(executor, &done);
   *   done.wait();  // or 'timed_wait', or spin with 'try_wait'
   *
   */
  void doEagerInitVia(Executor& exe, folly::Baton<>* done = nullptr);

  // Destroy all singletons; when complete, the vault can't create
  // singletons once again until reenableInstances() is called.
  void destroyInstances();

  // Enable re-creating singletons after destroyInstances() was called.
  void reenableInstances();

  // For testing; how many registered and living singletons we have.
  size_t registeredSingletonCount() const {
    RWSpinLock::ReadHolder rh(&mutex_);

    return singletons_.size();
  }

  /**
   * Flips to true if eager initialization was used, and has completed.
   * Never set to true if "doEagerInit()" or "doEagerInitVia" never called.
   */
  bool eagerInitComplete() const;

  size_t livingSingletonCount() const {
    RWSpinLock::ReadHolder rh(&mutex_);

    size_t ret = 0;
    for (const auto& p : singletons_) {
      if (p.second->hasLiveInstance()) {
        ++ret;
      }
    }

    return ret;
  }

  // A well-known vault; you can actually have others, but this is the
  // default.
  static SingletonVault* singleton() {
    return singleton<>();
  }

  // Gets singleton vault for any Tag. Non-default tag should be used in unit
  // tests only.
  template <typename VaultTag = detail::DefaultTag>
  static SingletonVault* singleton() {
    static SingletonVault* vault = new SingletonVault();
    return vault;
  }

  typedef std::string(*StackTraceGetterPtr)();

  static std::atomic<StackTraceGetterPtr>& stackTraceGetter() {
    static std::atomic<StackTraceGetterPtr> stackTraceGetterPtr;
    return stackTraceGetterPtr;
  }

 private:
  template <typename T>
  friend struct detail::SingletonHolder;

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

  typedef std::unordered_map<detail::TypeDescriptor,
                             detail::SingletonHolderBase*,
                             detail::TypeDescriptorHasher> SingletonMap;

  mutable folly::RWSpinLock mutex_;
  SingletonMap singletons_;
  std::unordered_set<detail::SingletonHolderBase*> eagerInitSingletons_;
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
// Singleton<T>::try_get().
template <typename T,
          typename Tag = detail::DefaultTag,
          typename VaultTag = detail::DefaultTag /* for testing */>
class Singleton {
 public:
  typedef std::function<T*(void)> CreateFunc;
  typedef std::function<void(T*)> TeardownFunc;

  // Generally your program life cycle should be fine with calling
  // get() repeatedly rather than saving the reference, and then not
  // call get() during process shutdown.
  FOLLY_DEPRECATED("Replaced by try_get")
  static T* get() {
    return getEntry().get();
  }

  // If, however, you do need to hold a reference to the specific
  // singleton, you can try to do so with a weak_ptr.  Avoid this when
  // possible but the inability to lock the weak pointer can be a
  // signal that the vault has been destroyed.
  static std::weak_ptr<T>
  get_weak() __attribute__ ((__deprecated__("Replaced by try_get")))  {
    return getEntry().get_weak();
  }

  // Preferred alternative to get_weak, it returns shared_ptr that can be
  // stored; a singleton won't be destroyed unless shared_ptr is destroyed.
  // Avoid holding these shared_ptrs beyond the scope of a function;
  // don't put them in member variables, always use try_get() instead
  //
  // try_get() can return nullptr if the singleton was destroyed, caller is
  // responsible for handling nullptr return
  static std::shared_ptr<T> try_get() {
    return getEntry().try_get();
  }

  static folly::ReadMostlySharedPtr<T> try_get_fast() {
    return getEntry().try_get_fast();
  }

  explicit Singleton(std::nullptr_t _ = nullptr,
                     typename Singleton::TeardownFunc t = nullptr) :
      Singleton ([]() { return new T; }, std::move(t)) {
  }

  explicit Singleton(typename Singleton::CreateFunc c,
                     typename Singleton::TeardownFunc t = nullptr) {
    if (c == nullptr) {
      throw std::logic_error(
        "nullptr_t should be passed if you want T to be default constructed");
    }

    auto vault = SingletonVault::singleton<VaultTag>();
    getEntry().registerSingleton(std::move(c), getTeardownFunc(std::move(t)));
    vault->registerSingleton(&getEntry());
  }

  /**
   * Should be instantiated as soon as "doEagerInit[Via]" is called.
   * Singletons are usually lazy-loaded (built on-demand) but for those which
   * are known to be needed, to avoid the potential lag for objects that take
   * long to construct during runtime, there is an option to make sure these
   * are built up-front.
   *
   * Use like:
   *   Singleton<Foo> gFooInstance = Singleton<Foo>(...).shouldEagerInit();
   *
   * Or alternately, define the singleton as usual, and say
   *   gFooInstance.shouldEagerInit();
   *
   * at some point prior to calling registrationComplete().
   * Then doEagerInit() or doEagerInitVia(Executor*) can be called.
   */
  Singleton& shouldEagerInit() {
    auto vault = SingletonVault::singleton<VaultTag>();
    vault->addEagerInitSingleton(&getEntry());
    return *this;
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
                        typename Singleton<T>::TeardownFunc t = nullptr) {
    make_mock([]() { return new T; }, t);
  }

  static void make_mock(CreateFunc c,
                        typename Singleton<T>::TeardownFunc t = nullptr) {
    if (c == nullptr) {
      throw std::logic_error(
        "nullptr_t should be passed if you want T to be default constructed");
    }

    auto& entry = getEntry();

    entry.registerSingletonMock(c, getTeardownFunc(t));
  }

 private:
  inline static detail::SingletonHolder<T>& getEntry() {
    return detail::SingletonHolder<T>::template singleton<Tag, VaultTag>();
  }

  // Construct TeardownFunc.
  static typename detail::SingletonHolder<T>::TeardownFunc getTeardownFunc(
      TeardownFunc t)  {
    if (t == nullptr) {
      return  [](T* v) { delete v; };
    } else {
      return t;
    }
  }
};

}

#include <folly/Singleton-inl.h>
