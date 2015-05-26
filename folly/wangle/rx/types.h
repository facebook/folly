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

#include <folly/ExceptionWrapper.h>
#include <folly/Executor.h>

namespace folly { namespace wangle {
  typedef folly::exception_wrapper Error;
  // The Executor is basically an rx Scheduler (by design). So just
  // alias it.
  typedef std::shared_ptr<folly::Executor> SchedulerPtr;

  template <class T, size_t InlineObservers = 3> class Observable;
  template <class T> struct Observer;
  template <class T> struct Subject;

  template <class T> using ObservablePtr = std::shared_ptr<Observable<T>>;
  template <class T> using ObserverPtr = std::shared_ptr<Observer<T>>;
  template <class T> using SubjectPtr = std::shared_ptr<Subject<T>>;
}}
