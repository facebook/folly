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

#include "types.h"
#include "Subject.h"
#include "Subscription.h"

#include <folly/RWSpinLock.h>
#include <folly/ThreadLocal.h>
#include <folly/wangle/Executor.h>
#include <list>
#include <memory>

namespace folly { namespace wangle {

template <class T>
class Observable {
 public:
  Observable() = default;
  Observable(Observable&& other) noexcept {
    RWSpinLock::WriteHolder{&other.observersLock_};
    observers_ = std::move(other.observers_);
  }

  virtual ~Observable() = default;

  /// Subscribe the given Observer to this Observable.
  // Eventually this will return a Subscription object of some kind, in order
  // to support cancellation. This is kinda really important. Maybe I should
  // just do it now, using an dummy Subscription object.
  //
  // If this is called within an Observer callback, the new observer will not
  // get the current update but will get subsequent updates.
  virtual Subscription subscribe(ObserverPtr<T> o) {
    if (inCallback_ && *inCallback_) {
      if (!newObservers_) {
        newObservers_.reset(new std::list<ObserverPtr<T>>());
      }
      newObservers_->push_back(o);
    } else {
      RWSpinLock::WriteHolder{&observersLock_};
      observers_.push_back(o);
    }
    return Subscription();
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

      Subscription subscribe(ObserverPtr<T> o) override {
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

      Subscription subscribe(ObserverPtr<T> o) {
        scheduler_->add([=] {
          observable_->subscribe(o);
        });
        return Subscription();
      }

     protected:
      SchedulerPtr scheduler_;
      Observable* observable_;
    };

    return folly::make_unique<Subject_>(scheduler, this);
  }

 protected:
  const std::list<ObserverPtr<T>>& getObservers() {
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
      if (UNLIKELY(o_->newObservers_ && !o_->newObservers_->empty())) {
        {
          RWSpinLock::WriteHolder(o_->observersLock_);
          for (auto& o : *(o_->newObservers_)) {
            o_->observers_.push_back(o);
          }
        }
        o_->newObservers_->clear();
      }
      *o_->inCallback_ = false;
    }

   private:
    Observable* o_;
  };

 private:
  std::list<ObserverPtr<T>> observers_;
  RWSpinLock observersLock_;
  folly::ThreadLocalPtr<bool> inCallback_;
  folly::ThreadLocalPtr<std::list<ObserverPtr<T>>> newObservers_;
};

}}
