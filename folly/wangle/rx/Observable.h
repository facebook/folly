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

#pragma once

#include <folly/wangle/rx/types.h> // must come first
#include <folly/wangle/rx/Subject.h>
#include <folly/wangle/rx/Subscription.h>

#include <folly/RWSpinLock.h>
#include <folly/SmallLocks.h>
#include <folly/ThreadLocal.h>
#include <folly/small_vector.h>
#include <folly/Executor.h>
#include <folly/Memory.h>
#include <map>
#include <memory>

namespace folly { namespace wangle {

template <class T, size_t InlineObservers>
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

  // The next three methods subscribe the given Observer to this Observable.
  //
  // If these are called within an Observer callback, the new observer will not
  // get the current update but will get subsequent updates.
  //
  // subscribe() returns a Subscription object. The observer will continue to
  // get updates until the Subscription is destroyed.
  //
  // observe(ObserverPtr<T>) creates an indefinite subscription
  //
  // observe(Observer<T>*) also creates an indefinite subscription, but the
  // caller is responsible for ensuring that the given Observer outlives this
  // Observable. This might be useful in high performance environments where
  // allocations must be kept to a minimum. Template parameter InlineObservers
  // specifies how many observers can been subscribed inline without any
  // allocations (it's just the size of a folly::small_vector).
  virtual Subscription<T> subscribe(ObserverPtr<T> observer) {
    return subscribeImpl(observer, false);
  }

  virtual void observe(ObserverPtr<T> observer) {
    subscribeImpl(observer, true);
  }

  virtual void observe(Observer<T>* observer) {
    if (inCallback_ && *inCallback_) {
      if (!newObservers_) {
        newObservers_.reset(new ObserverList());
      }
      newObservers_->push_back(observer);
    } else {
      RWSpinLock::WriteHolder{&observersLock_};
      observers_.push_back(observer);
    }
  }

  // TODO unobserve(ObserverPtr<T>), unobserve(Observer<T>*)

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
      ViaSubject(SchedulerPtr sched,
                 Observable* obs)
        : scheduler_(sched), observable_(obs)
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
  // Safely execute an operation on each observer. F must take a single
  // Observer<T>* as its argument.
  template <class F>
  void forEachObserver(F f) {
    if (UNLIKELY(!inCallback_)) {
      inCallback_.reset(new bool{false});
    }
    CHECK(!(*inCallback_));
    *inCallback_ = true;

    {
      RWSpinLock::ReadHolder rh(observersLock_);
      for (auto o : observers_) {
        f(o);
      }

      for (auto& kv : subscribers_) {
        f(kv.second.get());
      }
    }

    if (UNLIKELY((newObservers_ && !newObservers_->empty()) ||
                 (newSubscribers_ && !newSubscribers_->empty()) ||
                 (oldSubscribers_ && !oldSubscribers_->empty()))) {
      {
        RWSpinLock::WriteHolder wh(observersLock_);
        if (newObservers_) {
          for (auto observer : *(newObservers_)) {
            observers_.push_back(observer);
          }
          newObservers_->clear();
        }
        if (newSubscribers_) {
          for (auto& kv : *(newSubscribers_)) {
            subscribers_.insert(std::move(kv));
          }
          newSubscribers_->clear();
        }
        if (oldSubscribers_) {
          for (auto id : *(oldSubscribers_)) {
            subscribers_.erase(id);
          }
          oldSubscribers_->clear();
        }
      }
    }
    *inCallback_ = false;
  }

 private:
  Subscription<T> subscribeImpl(ObserverPtr<T> observer, bool indefinite) {
    auto subscription = makeSubscription(indefinite);
    typename SubscriberMap::value_type kv{subscription.id_, std::move(observer)};
    if (inCallback_ && *inCallback_) {
      if (!newSubscribers_) {
        newSubscribers_.reset(new SubscriberMap());
      }
      newSubscribers_->insert(std::move(kv));
    } else {
      RWSpinLock::WriteHolder{&observersLock_};
      subscribers_.insert(std::move(kv));
    }
    return subscription;
  }

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
      if (!oldSubscribers_) {
        oldSubscribers_.reset(new std::vector<uint64_t>());
      }
      if (newSubscribers_) {
        auto it = newSubscribers_->find(id);
        if (it != newSubscribers_->end()) {
          newSubscribers_->erase(it);
          return;
        }
      }
      oldSubscribers_->push_back(id);
    } else {
      RWSpinLock::WriteHolder{&observersLock_};
      subscribers_.erase(id);
    }
  }

  Subscription<T> makeSubscription(bool indefinite) {
    if (indefinite) {
      return Subscription<T>(nullptr, nextSubscriptionId_++);
    } else {
      if (!unsubscriber_) {
        std::lock_guard<MicroSpinLock> guard(unsubscriberLock_);
        if (!unsubscriber_) {
          unsubscriber_ = std::make_shared<Unsubscriber>(this);
        }
      }
      return Subscription<T>(unsubscriber_, nextSubscriptionId_++);
    }
  }

  std::atomic<uint64_t> nextSubscriptionId_;
  RWSpinLock observersLock_;
  folly::ThreadLocalPtr<bool> inCallback_;

  typedef folly::small_vector<Observer<T>*, InlineObservers> ObserverList;
  ObserverList observers_;
  folly::ThreadLocalPtr<ObserverList> newObservers_;

  typedef std::map<uint64_t, ObserverPtr<T>> SubscriberMap;
  SubscriberMap subscribers_;
  folly::ThreadLocalPtr<SubscriberMap> newSubscribers_;
  folly::ThreadLocalPtr<std::vector<uint64_t>> oldSubscribers_;
};

}}
