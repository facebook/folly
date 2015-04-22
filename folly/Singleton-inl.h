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

namespace folly {

namespace detail {

template <typename T>
template <typename Tag, typename VaultTag>
SingletonHolder<T>& SingletonHolder<T>::singleton() {
  static auto entry = new SingletonHolder<T>(
    {typeid(T), typeid(Tag)},
    *SingletonVault::singleton<VaultTag>());
  return *entry;
}

template <typename T>
void SingletonHolder<T>::registerSingleton(CreateFunc c, TeardownFunc t) {
  std::lock_guard<std::mutex> entry_lock(mutex_);

  if (state_ != SingletonHolderState::NotRegistered) {
    throw std::logic_error("Double registration");
  }

  create_ = std::move(c);
  teardown_ = std::move(t);

  state_ = SingletonHolderState::Dead;
}

template <typename T>
void SingletonHolder<T>::registerSingletonMock(CreateFunc c, TeardownFunc t) {
  if (state_ == SingletonHolderState::NotRegistered) {
    throw std::logic_error("Registering mock before singleton was registered");
  }
  destroyInstance();

  std::lock_guard<std::mutex> entry_lock(mutex_);

  create_ = std::move(c);
  teardown_ = std::move(t);
}

template <typename T>
T* SingletonHolder<T>::get() {
  if (LIKELY(state_ == SingletonHolderState::Living)) {
    return instance_ptr_;
  }
  createInstance();

  if (instance_weak_.expired()) {
    throw std::runtime_error(
      "Raw pointer to a singleton requested after its destruction.");
  }

  return instance_ptr_;
}

template <typename T>
std::weak_ptr<T> SingletonHolder<T>::get_weak() {
  if (UNLIKELY(state_ != SingletonHolderState::Living)) {
    createInstance();
  }

  return instance_weak_;
}

template <typename T>
TypeDescriptor SingletonHolder<T>::type() {
  return type_;
}

template <typename T>
bool SingletonHolder<T>::hasLiveInstance() {
  return !instance_weak_.expired();
}

template <typename T>
void SingletonHolder<T>::destroyInstance() {
  state_ = SingletonHolderState::Dead;
  instance_.reset();
  if (destroy_baton_) {
    auto wait_result = destroy_baton_->timed_wait(
      std::chrono::steady_clock::now() + kDestroyWaitTime);
    if (!wait_result) {
      print_destructor_stack_trace_->store(true);
      LOG(ERROR) << "Singleton of type " << type_.name() << " has a "
                 << "living reference at destroyInstances time; beware! Raw "
                 << "pointer is " << instance_ptr_ << ". It is very likely "
                 << "that some other singleton is holding a shared_ptr to it. "
                 << "Make sure dependencies between these singletons are "
                 << "properly defined.";
    }
  }
}

template <typename T>
SingletonHolder<T>::SingletonHolder(TypeDescriptor type__,
                                    SingletonVault& vault) :
    type_(type__), vault_(vault) {
}

template <typename T>
void SingletonHolder<T>::createInstance() {
  // There's no synchronization here, so we may not see the current value
  // for creating_thread if it was set by other thread, but we only care about
  // it if it was set by current thread anyways.
  if (creating_thread_ == std::this_thread::get_id()) {
    throw std::out_of_range(std::string("circular singleton dependency: ") +
                            type_.name());
  }

  std::lock_guard<std::mutex> entry_lock(mutex_);
  if (state_ == SingletonHolderState::Living) {
    return;
  }
  if (state_ == SingletonHolderState::NotRegistered) {
    throw std::out_of_range("Creating instance for unregistered singleton");
  }

  if (state_ == SingletonHolderState::Living) {
    return;
  }

  creating_thread_ = std::this_thread::get_id();

  RWSpinLock::ReadHolder rh(&vault_.stateMutex_);
  if (vault_.state_ == SingletonVault::SingletonVaultState::Quiescing) {
    creating_thread_ = std::thread::id();
    return;
  }

  auto destroy_baton = std::make_shared<folly::Baton<>>();
  auto print_destructor_stack_trace =
    std::make_shared<std::atomic<bool>>(false);
  auto teardown = teardown_;
  auto type_name = type_.name();

  // Can't use make_shared -- no support for a custom deleter, sadly.
  instance_ = std::shared_ptr<T>(
    create_(),
    [destroy_baton, print_destructor_stack_trace, teardown, type_name]
    (T* instance_ptr) mutable {
      teardown(instance_ptr);
      destroy_baton->post();
      if (print_destructor_stack_trace->load()) {
        std::string output = "Singleton " + type_name + " was destroyed.\n";

        auto stack_trace_getter = SingletonVault::stackTraceGetter().load();
        auto stack_trace = stack_trace_getter ? stack_trace_getter() : "";
        if (stack_trace.empty()) {
          output += "Failed to get destructor stack trace.";
        } else {
          output += "Destructor stack trace:\n";
          output += stack_trace;
        }

        LOG(ERROR) << output;
      }
    });

  // We should schedule destroyInstances() only after the singleton was
  // created. This will ensure it will be destroyed before singletons,
  // not managed by folly::Singleton, which were initialized in its
  // constructor
  SingletonVault::scheduleDestroyInstances();

  instance_weak_ = instance_;
  instance_ptr_ = instance_.get();
  creating_thread_ = std::thread::id();
  destroy_baton_ = std::move(destroy_baton);
  print_destructor_stack_trace_ = std::move(print_destructor_stack_trace);

  // This has to be the last step, because once state is Living other threads
  // may access instance and instance_weak w/o synchronization.
  state_.store(SingletonHolderState::Living);

  {
    RWSpinLock::WriteHolder wh(&vault_.mutex_);
    vault_.creation_order_.push_back(type_);
  }
}

}

}
