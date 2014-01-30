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

#include "folly/test/function_benchmark/test_functions.h"

/*
 * These functions are defined in a separate file so that
 * gcc won't be able to inline them.
 */


class Exception : public std::exception {
 public:
  explicit Exception(const std::string& value) : value_(value) {}
  virtual ~Exception(void) throw() {}

  virtual const char *what(void) const throw() {
    return value_.c_str();
  }

 private:
  std::string value_;
};

void doNothing() {
}

void throwException() {
  throw Exception("this is a test");
}

std::exception_ptr returnExceptionPtr() {
  Exception ex("this is a test");
  return std::make_exception_ptr(ex);
}

void exceptionPtrReturnParam(std::exception_ptr* excReturn) {
  if (excReturn) {
    Exception ex("this is a test");
    *excReturn = std::make_exception_ptr(ex);
  }
}

std::string returnString() {
  return "this is a test";
}

std::string returnStringNoExcept() noexcept {
  return "this is a test";
}

int returnCode(int value) {
  return value;
}

int returnCodeNoExcept(int value) noexcept {
  return value;
}

void TestClass::doNothing() {
}

VirtualClass::~VirtualClass() {
}

void VirtualClass::doNothing() {
};
