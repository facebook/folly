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

#include <exception>
#include <string>

namespace folly { namespace wangle {

class WangleException : public std::exception {

public:

  explicit WangleException(std::string message_arg)
    : message(message_arg) {}

  ~WangleException() throw(){}

  virtual const char *what() const throw() {
    return message.c_str();
  }

  bool operator==(const WangleException &other) const{
    return other.message == this->message;
  }

  bool operator!=(const WangleException &other) const{
    return !(*this == other);
  }

  protected:
    std::string message;
};

class BrokenPromise : public WangleException {
  public:
    explicit BrokenPromise() :
      WangleException("Broken promise") { }
};

class NoState : public WangleException {
  public:
    explicit NoState() : WangleException("No state") { }
};

class PromiseAlreadySatisfied : public WangleException {
  public:
    explicit PromiseAlreadySatisfied() :
      WangleException("Promise already satisfied") { }
};

class FutureAlreadyRetrieved : public WangleException {
  public:
    explicit FutureAlreadyRetrieved () :
      WangleException("Future already retrieved") { }
};

class UsingUninitializedTry : public WangleException {
  public:
    explicit UsingUninitializedTry() :
      WangleException("Using unitialized try") { }
};

}}
