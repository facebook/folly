/*
 * Copyright 2016 Facebook, Inc.
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

#include <exception>
#include <string>

namespace folly {

class FutureException : public std::exception {

public:

  explicit FutureException(std::string message_arg)
    : message(message_arg) {}

  ~FutureException() throw(){}

  virtual const char *what() const throw() {
    return message.c_str();
  }

  bool operator==(const FutureException &other) const{
    return other.message == this->message;
  }

  bool operator!=(const FutureException &other) const{
    return !(*this == other);
  }

  protected:
    std::string message;
};

class BrokenPromise : public FutureException {
  public:
    explicit BrokenPromise(std::string type) :
      FutureException(
          (std::string("Broken promise for type name `") + type) + '`') { }
};

class NoState : public FutureException {
  public:
    explicit NoState() : FutureException("No state") { }
};

class PromiseAlreadySatisfied : public FutureException {
  public:
    explicit PromiseAlreadySatisfied() :
      FutureException("Promise already satisfied") { }
};

class FutureNotReady : public FutureException {
  public:
    explicit FutureNotReady() :
      FutureException("Future not ready") { }
};

class FutureAlreadyRetrieved : public FutureException {
  public:
    explicit FutureAlreadyRetrieved () :
      FutureException("Future already retrieved") { }
};

class FutureCancellation : public FutureException {
 public:
  FutureCancellation() : FutureException("Future was cancelled") {}
};

class TimedOut : public FutureException {
 public:
  TimedOut() : FutureException("Timed out") {}
};

class PredicateDoesNotObtain : public FutureException {
 public:
  PredicateDoesNotObtain() : FutureException("Predicate does not obtain") {}
};

}
