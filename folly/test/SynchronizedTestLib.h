/*
 * Copyright 2012 Facebook, Inc.
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

#ifndef FOLLY_TEST_SYNCHRONIZEDTESTLIB_H
#define FOLLY_TEST_SYNCHRONIZEDTESTLIB_H

// We have mutex types outside of folly that we want to test with Synchronized.
// Make it easy for mutex implementators to test their classes with
// Synchronized by just having a test like:
//
// class MyMutex { ... };
//
// TEST(Synchronized, Basic) {
//   testBasic<MyMutex>();
// }
//
// ... similar for testConcurrency, testDualLocking, etc.


template <class Mutex> void testBasic();

template <class Mutex> void testConcurrency();

template <class Mutex> void testDualLocking();

template <class Mutex> void testDualLockingWithConst();

template <class Mutex> void testTimedSynchronized();

template <class Mutex> void testConstCopy();

#include "folly/test/SynchronizedTestLib-inl.h"

#endif /* FOLLY_TEST_SYNCHRONIZEDTESTLIB_H */
