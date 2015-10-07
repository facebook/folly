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

#include <folly/Singleton.h>

#include <string>

namespace folly {

namespace detail {

constexpr std::chrono::seconds SingletonHolderBase::kDestroyWaitTime;

}

namespace {

struct FatalHelper {
  ~FatalHelper() {
    if (!leakedSingletons_.empty()) {
      std::string leakedTypes;
      for (const auto& singleton : leakedSingletons_) {
        leakedTypes += "\t" + singleton.name() + "\n";
      }
      LOG(DFATAL) << "Singletons of the following types had living references "
                  << "after destroyInstances was finished:\n" << leakedTypes
                  << "beware! It is very likely that those singleton instances "
                  << "are leaked.";
    }
  }

  std::vector<detail::TypeDescriptor> leakedSingletons_;
};

#if defined(__APPLE__) || defined(_MSC_VER)
// OS X doesn't support constructor priorities.
FatalHelper fatalHelper;
#else
FatalHelper __attribute__ ((__init_priority__ (101))) fatalHelper;
#endif

}

SingletonVault::~SingletonVault() { destroyInstances(); }

void SingletonVault::registerSingleton(detail::SingletonHolderBase* entry) {
  RWSpinLock::ReadHolder rh(&stateMutex_);

  stateCheck(SingletonVaultState::Running);

  if (UNLIKELY(registrationComplete_)) {
    throw std::logic_error(
      "Registering singleton after registrationComplete().");
  }

  RWSpinLock::ReadHolder rhMutex(&mutex_);
  CHECK_THROW(singletons_.find(entry->type()) == singletons_.end(),
              std::logic_error);

  RWSpinLock::UpgradedHolder wh(&mutex_);
  singletons_[entry->type()] = entry;
}

void SingletonVault::addEagerInitSingleton(detail::SingletonHolderBase* entry) {
  RWSpinLock::ReadHolder rh(&stateMutex_);

  stateCheck(SingletonVaultState::Running);

  if (UNLIKELY(registrationComplete_)) {
    throw std::logic_error(
        "Registering for eager-load after registrationComplete().");
  }

  RWSpinLock::ReadHolder rhMutex(&mutex_);
  CHECK_THROW(singletons_.find(entry->type()) != singletons_.end(),
              std::logic_error);

  RWSpinLock::UpgradedHolder wh(&mutex_);
  eagerInitSingletons_.insert(entry);
}

void SingletonVault::registrationComplete() {
  RequestContext::saveContext();
  std::atexit([](){ SingletonVault::singleton()->destroyInstances(); });

  RWSpinLock::WriteHolder wh(&stateMutex_);

  stateCheck(SingletonVaultState::Running);

  if (type_ == Type::Strict) {
    for (const auto& p : singletons_) {
      if (p.second->hasLiveInstance()) {
        throw std::runtime_error(
            "Singleton created before registration was complete.");
      }
    }
  }

  registrationComplete_ = true;
}

void SingletonVault::doEagerInit() {
  std::unordered_set<detail::SingletonHolderBase*> singletonSet;
  {
    RWSpinLock::ReadHolder rh(&stateMutex_);
    stateCheck(SingletonVaultState::Running);
    if (UNLIKELY(!registrationComplete_)) {
      throw std::logic_error("registrationComplete() not yet called");
    }
    singletonSet = eagerInitSingletons_; // copy set of pointers
  }

  for (auto *single : singletonSet) {
    single->createInstance();
  }
}

Future<Unit> SingletonVault::doEagerInitVia(Executor* exe) {
  std::unordered_set<detail::SingletonHolderBase*> singletonSet;
  {
    RWSpinLock::ReadHolder rh(&stateMutex_);
    stateCheck(SingletonVaultState::Running);
    if (UNLIKELY(!registrationComplete_)) {
      throw std::logic_error("registrationComplete() not yet called");
    }
    singletonSet = eagerInitSingletons_; // copy set of pointers
  }

  std::vector<Future<Unit>> resultFutures;
  for (auto* single : singletonSet) {
    resultFutures.emplace_back(via(exe).then([single] {
      if (!single->creationStarted()) {
        single->createInstance();
      }
    }));
  }

  return collectAll(resultFutures).via(exe).then();
}

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
      singletons_[*type_iter]->destroyInstance();
    }

    for (auto& singleton_type: creation_order_) {
      auto singleton = singletons_[singleton_type];
      if (!singleton->hasLiveInstance()) {
        continue;
      }

      fatalHelper.leakedSingletons_.push_back(singleton->type());
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

void SingletonVault::scheduleDestroyInstances() {
  RequestContext::saveContext();

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
