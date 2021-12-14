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

namespace folly {

namespace detail {

template <typename T>
template <typename Tag, typename VaultTag>
struct SingletonHolder<T>::Impl : SingletonHolder<T> {
  Impl()
      : SingletonHolder<T>(
            {typeid(T), typeid(Tag)}, *SingletonVault::singleton<VaultTag>()) {}
};

template <typename T>
template <typename Tag, typename VaultTag>
inline SingletonHolder<T>& SingletonHolder<T>::singleton() {
  return detail::createGlobal<Impl<Tag, VaultTag>, void>();
}

[[noreturn]] void singletonWarnDoubleRegistrationAndAbort(
    const TypeDescriptor& type);

template <typename T>
void SingletonHolder<T>::registerSingleton(CreateFunc c, TeardownFunc t) {
  std::lock_guard<std::mutex> entry_lock(mutex_);

  if (state_ != SingletonHolderState::NotRegistered) {
    /* Possible causes:
     *
     * You have two instances of the same
     * folly::Singleton<Class>. Probably because you define the
     * singleton in a header included in multiple places? In general,
     * folly::Singleton shouldn't be in the header, only off in some
     * anonymous namespace in a cpp file. Code needing the singleton
     * will find it when that code references folly::Singleton<Class>.
     *
     * Alternatively, you could have 2 singletons with the same type
     * defined with a different name in a .cpp (source) file. For
     * example:
     *
     * Singleton<int> a([] { return new int(3); });
     * Singleton<int> b([] { return new int(4); });
     *
     * Adding tags should fix this (see documentation in the header).
     *
     */
    singletonWarnDoubleRegistrationAndAbort(type());
  }

  create_ = std::move(c);
  teardown_ = std::move(t);

  state_ = SingletonHolderState::Dead;
}

template <typename T>
void SingletonHolder<T>::registerSingletonMock(CreateFunc c, TeardownFunc t) {
  if (state_ == SingletonHolderState::NotRegistered) {
    detail::singletonWarnRegisterMockEarlyAndAbort(type());
  }
  if (state_ == SingletonHolderState::Living ||
      state_ == SingletonHolderState::LivingInChildAfterFork) {
    destroyInstance();
  }

  {
    auto creationOrder = vault_.creationOrder_.wlock();

    auto it = std::find(creationOrder->begin(), creationOrder->end(), type());
    if (it != creationOrder->end()) {
      creationOrder->erase(it);
    }
  }

  std::lock_guard<std::mutex> entry_lock(mutex_);

  create_ = std::move(c);
  teardown_ = std::move(t);
}

template <typename T>
T* SingletonHolder<T>::get() {
  if (LIKELY(
          state_.load(std::memory_order_acquire) ==
          SingletonHolderState::Living)) {
    return instance_ptr_;
  }
  createInstance();

  if (instance_weak_.expired()) {
    detail::singletonThrowGetInvokedAfterDestruction(type());
  }

  return instance_ptr_;
}

template <typename T>
std::weak_ptr<T> SingletonHolder<T>::get_weak() {
  if (UNLIKELY(
          state_.load(std::memory_order_acquire) !=
          SingletonHolderState::Living)) {
    createInstance();
  }

  return instance_weak_core_cached_.get();
}

template <typename T>
std::shared_ptr<T> SingletonHolder<T>::try_get() {
  if (UNLIKELY(
          state_.load(std::memory_order_acquire) !=
          SingletonHolderState::Living)) {
    createInstance();
  }

  return instance_weak_core_cached_.lock();
}

template <typename T>
folly::ReadMostlySharedPtr<T> SingletonHolder<T>::try_get_fast() {
  if (UNLIKELY(
          state_.load(std::memory_order_acquire) !=
          SingletonHolderState::Living)) {
    createInstance();
  }

  return instance_weak_fast_.lock();
}

template <typename T>
template <typename Func>
invoke_result_t<Func, T*> detail::SingletonHolder<T>::apply(Func f) {
  return f(try_get().get());
}

template <typename T>
void SingletonHolder<T>::vivify() {
  if (UNLIKELY(
          state_.load(std::memory_order_relaxed) !=
          SingletonHolderState::Living)) {
    createInstance();
  }
}

template <typename T>
bool SingletonHolder<T>::hasLiveInstance() {
  return !instance_weak_.expired();
}

template <typename T>
void SingletonHolder<T>::preDestroyInstance(
    ReadMostlyMainPtrDeleter<>& deleter) {
  instance_copy_ = instance_;
  deleter.add(std::move(instance_));
}

template <typename T>
void SingletonHolder<T>::destroyInstance() {
  if (state_.load(std::memory_order_relaxed) ==
      SingletonHolderState::LivingInChildAfterFork) {
    if (vault_.failOnUseAfterFork_) {
      LOG(DFATAL) << "Attempting to destroy singleton " << type().name()
                  << " in child process after fork";
    } else {
      LOG(ERROR) << "Attempting to destroy singleton " << type().name()
                 << " in child process after fork";
    }
  }
  state_ = SingletonHolderState::Dead;
  instance_core_cached_.reset();
  instance_.reset();
  instance_copy_.reset();
  if (destroy_baton_) {
    constexpr std::chrono::seconds kDestroyWaitTime{5};
    auto const wait_options =
        destroy_baton_->wait_options().logging_enabled(false);
    auto last_reference_released =
        destroy_baton_->try_wait_for(kDestroyWaitTime, wait_options);
    if (last_reference_released) {
      vault_.addToShutdownLog("Destroying " + type().name());
      teardown_(instance_ptr_);
      vault_.addToShutdownLog(type().name() + " destroyed.");
    } else {
      print_destructor_stack_trace_->store(true);
      detail::singletonWarnDestroyInstanceLeak(type(), instance_ptr_);
    }
  }
}

template <typename T>
void SingletonHolder<T>::inChildAfterFork() {
  auto expected = SingletonHolderState::Living;
  state_.compare_exchange_strong(
      expected,
      SingletonHolderState::LivingInChildAfterFork,
      std::memory_order_relaxed,
      std::memory_order_relaxed);
}

template <typename T>
SingletonHolder<T>::SingletonHolder(
    TypeDescriptor typeDesc, SingletonVault& vault) noexcept
    : SingletonHolderBase(typeDesc), vault_(vault) {}

template <typename T>
bool SingletonHolder<T>::creationStarted() {
  // If alive, then creation was of course started.
  // This is flipped after creating_thread_ was set, and before it was reset.
  if (state_.load(std::memory_order_acquire) == SingletonHolderState::Living) {
    return true;
  }

  // Not yet built.  Is it currently in progress?
  if (creating_thread_.load(std::memory_order_acquire) != std::thread::id()) {
    return true;
  }

  return false;
}

template <typename T>
void SingletonHolder<T>::createInstance() {
  if (creating_thread_.load(std::memory_order_acquire) ==
      std::this_thread::get_id()) {
    detail::singletonWarnCreateCircularDependencyAndAbort(type());
  }

  std::lock_guard<std::mutex> entry_lock(mutex_);
  if (state_.load(std::memory_order_acquire) == SingletonHolderState::Living) {
    return;
  }
  if (state_.load(std::memory_order_relaxed) ==
      SingletonHolderState::LivingInChildAfterFork) {
    if (vault_.failOnUseAfterFork_) {
      LOG(DFATAL) << "Attempting to use singleton " << type().name()
                  << " in child process after fork";
    } else {
      LOG(ERROR) << "Attempting to use singleton " << type().name()
                 << " in child process after fork";
    }
    auto expected = SingletonHolderState::LivingInChildAfterFork;
    state_.compare_exchange_strong(
        expected,
        SingletonHolderState::Living,
        std::memory_order_relaxed,
        std::memory_order_relaxed);
    return;
  }
  if (state_.load(std::memory_order_acquire) ==
      SingletonHolderState::NotRegistered) {
    detail::singletonWarnCreateUnregisteredAndAbort(type());
  }

  if (state_.load(std::memory_order_acquire) == SingletonHolderState::Living) {
    return;
  }

  SCOPE_EXIT {
    // Clean up creator thread when complete, and also, in case of errors here,
    // so that subsequent attempts don't think this is still in the process of
    // being built.
    creating_thread_.store(std::thread::id(), std::memory_order_release);
  };

  creating_thread_.store(std::this_thread::get_id(), std::memory_order_release);

  auto state = vault_.state_.rlock();
  if (vault_.type_.load(std::memory_order_relaxed) !=
          SingletonVault::Type::Relaxed &&
      !state->registrationComplete) {
    detail::singletonWarnCreateBeforeRegistrationCompleteAndAbort(type());
  }
  if (state->state == detail::SingletonVaultState::Type::Quiescing) {
    return;
  }

  auto destroy_baton = std::make_shared<folly::Baton<>>();
  auto print_destructor_stack_trace =
      std::make_shared<std::atomic<bool>>(false);

  // Can't use make_shared -- no support for a custom deleter, sadly.
  std::shared_ptr<T> instance(
      create_(),
      [destroy_baton, print_destructor_stack_trace, type = type()](T*) mutable {
        destroy_baton->post();
        if (print_destructor_stack_trace->load()) {
          detail::singletonPrintDestructionStackTrace(type);
        }
      });

  // We should schedule destroyInstances() only after the singleton was
  // created. This will ensure it will be destroyed before singletons,
  // not managed by folly::Singleton, which were initialized in its
  // constructor
  SingletonVault::scheduleDestroyInstances();

  instance_weak_ = instance;
  instance_ptr_ = instance.get();
  instance_core_cached_.reset(instance);
  instance_.reset(std::move(instance));
  instance_weak_fast_ = instance_;
  instance_weak_core_cached_.reset(instance_core_cached_);

  destroy_baton_ = std::move(destroy_baton);
  print_destructor_stack_trace_ = std::move(print_destructor_stack_trace);

  // This has to be the last step, because once state is Living other threads
  // may access instance and instance_weak w/o synchronization.
  state_.store(SingletonHolderState::Living, std::memory_order_release);

  vault_.creationOrder_.wlock()->push_back(type());
}

} // namespace detail

} // namespace folly
