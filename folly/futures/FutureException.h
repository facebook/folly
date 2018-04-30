/*
 * Copyright 2014-present Facebook, Inc.
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

#include <stdexcept>
#include <string>

#include <folly/CPortability.h>

namespace folly {

class FOLLY_EXPORT FutureException : public std::logic_error {
 public:
  using std::logic_error::logic_error;
};

class FOLLY_EXPORT BrokenPromise : public FutureException {
 public:
  explicit BrokenPromise(const std::string& type)
      : FutureException("Broken promise for type name `" + type + '`') {}

  explicit BrokenPromise(const char* type) : BrokenPromise(std::string(type)) {}
};

class FOLLY_EXPORT NoState : public FutureException {
 public:
  NoState() : FutureException("No state") {}
};

[[noreturn]] void throwNoState();

class FOLLY_EXPORT PromiseAlreadySatisfied : public FutureException {
 public:
  PromiseAlreadySatisfied() : FutureException("Promise already satisfied") {}
};

[[noreturn]] void throwPromiseAlreadySatisfied();

class FOLLY_EXPORT FutureNotReady : public FutureException {
 public:
  FutureNotReady() : FutureException("Future not ready") {}
};

[[noreturn]] void throwFutureNotReady();

class FOLLY_EXPORT FutureAlreadyRetrieved : public FutureException {
 public:
  FutureAlreadyRetrieved() : FutureException("Future already retrieved") {}
};

[[noreturn]] void throwFutureAlreadyRetrieved();

class FOLLY_EXPORT FutureCancellation : public FutureException {
 public:
  FutureCancellation() : FutureException("Future was cancelled") {}
};

class FOLLY_EXPORT TimedOut : public FutureException {
 public:
  TimedOut() : FutureException("Timed out") {}
};

[[noreturn]] void throwTimedOut();

class FOLLY_EXPORT PredicateDoesNotObtain : public FutureException {
 public:
  PredicateDoesNotObtain() : FutureException("Predicate does not obtain") {}
};

[[noreturn]] void throwPredicateDoesNotObtain();

class FOLLY_EXPORT NoFutureInSplitter : public FutureException {
 public:
  NoFutureInSplitter() : FutureException("No Future in this FutureSplitter") {}
};

[[noreturn]] void throwNoFutureInSplitter();

class FOLLY_EXPORT NoTimekeeper : public FutureException {
 public:
  NoTimekeeper() : FutureException("No timekeeper available") {}
};

[[noreturn]] void throwNoExecutor();

class FOLLY_EXPORT NoExecutor : public FutureException {
 public:
  NoExecutor() : FutureException("No executor provided to via") {}
};
} // namespace folly
