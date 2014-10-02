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

#pragma once

#include <folly/experimental/wangle/rx/Subject.h>
#include <folly/experimental/wangle/rx/Subscription.h>
#include <folly/experimental/wangle/rx/types.h>

#include <folly/RWSpinLock.h>
#include <folly/SmallLocks.h>
#include <folly/ThreadLocal.h>
#include <folly/wangle/Executor.h>
#include <map>
#include <memory>

namespace folly { namespace wangle {

template <class T>
class Observable {
 public:
  Observable() : nextSubscriptionId_{1} {}

  // TODO perhaps we want to provide this #5283229
  Observable(Observable&& other) = delete;

  virtual ~Observable() {
    if (unsubscriber_) {
      unsubscriber_->disable();
    }
  }

  typedef typename std::map<uint64_t, ObserverPtr<T>> ObserverMap;

  // Subscribe the given Observer to this Observable.
  //
  // If this is called within an Observer callback, the new observer will not
  // get the current update but will get subsequent updates.
  virtual Subscription<T> subscribe(ObserverPtr<T> observer) {
    auto subscription = makeSubscription();
    typename ObserverMap::value_type kv{subscription.id_, std::move(observer)};
    if (inCallback_ && *inCallback_) {
      if (!newObservers_) {
        newObservers_.reset(new ObserverMap());
      }
      newObservers_->insert(std::move(kv));
    } else {
      RWSpinLock::WriteHolder{&observersLock_};
      observers_.insert(std::move(kv));
    }
    return subscription;
  }

  /// Returns a new Observable that will call back on the given Scheduler.
  /// The returned Observable must outlive the parent Observable.

  // This and subscribeOn should maybe just be a first-class feature of an
  // Observable, rather than making new ones whose lifetimes are tied to their
  // parents. In that case it'd return a reference to this object for
  // chaining.
  ObservablePtr<T> observeOn(SchedulerPtr scheduler) {
    // you're right Hannes, if we have Observable::create we don't need this
    // helper class.
    struct ViaSubject : public Observable<T>
    {
      ViaSubject(SchedulerPtr scheduler,
                 Observable* obs)
        : scheduler_(scheduler), observable_(obs)
      {}

      Subscription<T> subscribe(ObserverPtr<T> o) override {
        return observable_->subscribe(
          Observer<T>::create(
            [=](T val) { scheduler_->add([o, val] { o->onNext(val); }); },
            [=](Error e) { scheduler_->add([o, e] { o->onError(e); }); },
            [=]() { scheduler_->add([o] { o->onCompleted(); }); }));
      }

     protected:
      SchedulerPtr scheduler_;
      Observable* observable_;
    };

    return std::make_shared<ViaSubject>(scheduler, this);
  }

  /// Returns a new Observable that will subscribe to this parent Observable
  /// via the given Scheduler. This can be subtle and confusing at first, see
  /// http://www.introtorx.com/Content/v1.0.10621.0/15_SchedulingAndThreading.html#SubscribeOnObserveOn
  std::unique_ptr<Observable> subscribeOn(SchedulerPtr scheduler) {
    struct Subject_ : public Subject<T> {
     public:
      Subject_(SchedulerPtr s, Observable* o) : scheduler_(s), observable_(o) {
      }

      Subscription<T> subscribe(ObserverPtr<T> o) {
        scheduler_->add([=] {
          observable_->subscribe(o);
        });
        return Subscription<T>(nullptr, 0); // TODO
      }

     protected:
      SchedulerPtr scheduler_;
      Observable* observable_;
    };

    return folly::make_unique<Subject_>(scheduler, this);
  }

 protected:
  const ObserverMap& getObservers() {
    return observers_;
  }

  // This guard manages deferred modification of the observers list.
  // Subclasses should use this guard if they want to subscribe new observers
  // in the course of a callback. New observers won't be added until the guard
  // goes out of scope. See Subject for an example.
  class ObserversGuard {
   public:
    explicit ObserversGuard(Observable* o) : o_(o) {
      if (UNLIKELY(!o_->inCallback_)) {
        o_->inCallback_.reset(new bool{false});
      }
      CHECK(!(*o_->inCallback_));
      *o_->inCallback_ = true;
      o_->observersLock_.lock_shared();
    }

    ~ObserversGuard() {
      o_->observersLock_.unlock_shared();
      if (UNLIKELY((o_->newObservers_ && !o_->newObservers_->empty()) ||
                   (o_->oldObservers_ && !o_->oldObservers_->empty()))) {
        {
          RWSpinLock::WriteHolder(o_->observersLock_);
          if (o_->newObservers_) {
            for (auto& kv : *(o_->newObservers_)) {
              o_->observers_.insert(std::move(kv));
            }
            o_->newObservers_->clear();
          }
          if (o_->oldObservers_) {
            for (auto id : *(o_->oldObservers_)) {
              o_->observers_.erase(id);
            }
            o_->oldObservers_->clear();
          }
        }
      }
      *o_->inCallback_ = false;
    }

   private:
    Observable* o_;
  };

 private:
  class Unsubscriber {
   public:
    explicit Unsubscriber(Observable* observable) : observable_(observable) {
      CHECK(observable_);
    }

    void unsubscribe(uint64_t id) {
      CHECK(id > 0);
      RWSpinLock::ReadHolder guard(lock_);
      if (observable_) {
        observable_->unsubscribe(id);
      }
    }

    void disable() {
      RWSpinLock::WriteHolder guard(lock_);
      observable_ = nullptr;
    }

   private:
    RWSpinLock lock_;
    Observable* observable_;
  };

  std::shared_ptr<Unsubscriber> unsubscriber_{nullptr};
  MicroSpinLock unsubscriberLock_{0};

  friend class Subscription<T>;

  void unsubscribe(uint64_t id) {
    if (inCallback_ && *inCallback_) {
      if (!oldObservers_) {
        oldObservers_.reset(new std::vector<uint64_t>());
      }
      if (newObservers_) {
        auto it = newObservers_->find(id);
        if (it != newObservers_->end()) {
          newObservers_->erase(it);
          return;
        }
      }
      oldObservers_->push_back(id);
    } else {
      RWSpinLock::WriteHolder{&observersLock_};
      observers_.erase(id);
    }
  }

  Subscription<T> makeSubscription() {
    if (!unsubscriber_) {
      std::lock_guard<MicroSpinLock> guard(unsubscriberLock_);
      if (!unsubscriber_) {
        unsubscriber_ = std::make_shared<Unsubscriber>(this);
      }
    }
    return Subscription<T>(unsubscriber_, nextSubscriptionId_++);
  }

  std::atomic<uint64_t> nextSubscriptionId_;
  ObserverMap observers_;
  RWSpinLock observersLock_;
  folly::ThreadLocalPtr<bool> inCallback_;
  folly::ThreadLocalPtr<ObserverMap> newObservers_;
  folly::ThreadLocalPtr<std::vector<uint64_t>> oldObservers_;
};

}}
