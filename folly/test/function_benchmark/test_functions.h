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

#ifndef TEST_FUNCTIONS_H_
#define TEST_FUNCTIONS_H_

#include <exception>
#include <string>

void doNothing();

void throwException();
std::exception_ptr returnExceptionPtr();
void exceptionPtrReturnParam(std::exception_ptr* excReturn);
std::string returnString();
std::string returnStringNoExcept() noexcept;
int returnCode(int value);
int returnCodeNoExcept(int value) noexcept;

class TestClass {
 public:
  void doNothing();
};

class VirtualClass {
 public:
  virtual ~VirtualClass();
  virtual void doNothing();
};

#endif // TEST_FUNCTIONS_H_
