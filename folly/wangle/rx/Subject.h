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
#include <folly/wangle/rx/Observable.h>
#include <folly/wangle/rx/Observer.h>

namespace folly { namespace wangle {

/// Subject interface. A Subject is both an Observable and an Observer. There
/// is a default implementation of the Observer methods that just forwards the
/// observed events to the Subject's observers.
template <class T>
struct Subject : public Observable<T>, public Observer<T> {
  void onNext(const T& val) override {
    this->forEachObserver([&](Observer<T>* o){
      o->onNext(val);
    });
  }
  void onError(Error e) override {
    this->forEachObserver([&](Observer<T>* o){
      o->onError(e);
    });
  }
  void onCompleted() override {
    this->forEachObserver([](Observer<T>* o){
      o->onCompleted();
    });
  }
};

}}
