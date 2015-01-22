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
      destroyInstance(singletons_.find(*type_iter));
    }
  }

  {
    RWSpinLock::WriteHolder wh(&mutex_);
    creation_order_.clear();
  }
}

/* Destroy and clean-up one singleton. Must be invoked while holding
 * a read lock on mutex_.
 * @param typeDescriptor - the type key for the removed singleton.
 */
void SingletonVault::destroyInstance(SingletonMap::iterator entry_it) {
  const auto& type = entry_it->first;
  auto& entry = *(entry_it->second);
  if (entry.instance.use_count() > 1) {
    LOG(ERROR) << "Singleton of type " << type.name() << " has a living "
               << "reference at destroyInstances time; beware! Raw pointer "
               << "is " << entry.instance.get() << " with use_count of "
               << entry.instance.use_count();
  }
  entry.state = detail::SingletonEntryState::Dead;
  entry.instance.reset();
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
  RequestContext::getStaticContext();

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
