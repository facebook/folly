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

#include <folly/experimental/Singleton.h>

#include <string>

namespace folly {

SingletonVault::~SingletonVault() { destroyInstances(); }

void SingletonVault::destroyInstances() {
  RWSpinLock::WriteHolder state_wh(&stateMutex_);

  if (state_ == SingletonVaultState::Quiescing) {
    return;
  }
  state_ = SingletonVaultState::Quiescing;

  RWSpinLock::ReadHolder state_rh(std::move(state_wh));

  {
    RWSpinLock::ReadHolder rh(&mutex_);

    CHECK_GE(singletons_.size(), creation_order_.size());

    for (auto type_iter = creation_order_.rbegin();
         type_iter != creation_order_.rend();
         ++type_iter) {
      auto type = *type_iter;
      auto it = singletons_.find(type);
      CHECK(it != singletons_.end());
      auto& entry = it->second;
      std::lock_guard<std::mutex> entry_guard(entry->mutex);
      if (entry->instance.use_count() > 1) {
        LOG(ERROR) << "Singleton of type " << type.name() << " has a living "
                   << "reference at destroyInstances time; beware! Raw pointer "
                   << "is " << entry->instance.get() << " with use_count of "
                   << entry->instance.use_count();
      }
      entry->instance.reset();
      entry->state = SingletonEntryState::Dead;
      entry->state_condvar.notify_all();
    }
  }

  {
    RWSpinLock::WriteHolder wh(&mutex_);
    creation_order_.clear();
  }
}

void SingletonVault::reenableInstances() {
  RWSpinLock::WriteHolder state_wh(&stateMutex_);

  stateCheck(SingletonVaultState::Quiescing);

  state_ = SingletonVaultState::Running;
}

SingletonVault* SingletonVault::singleton() {
  static SingletonVault* vault = new SingletonVault();
  return vault;
}

void SingletonVault::scheduleDestroyInstances() {
  class SingletonVaultDestructor {
   public:
    ~SingletonVaultDestructor() {
      SingletonVault::singleton()->destroyInstances();
    }
  };

  // Here we intialize a singleton, which calls destroyInstances in its
  // destructor. Because of singleton destruction order - it will be destroyed
  // before all the singletons, which were initialized before it and after all
  // the singletons initialized after it.
  static SingletonVaultDestructor singletonVaultDestructor;
}

}
