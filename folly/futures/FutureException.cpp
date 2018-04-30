/*
 * Copyright 2017-present Facebook, Inc.
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

#include <folly/futures/FutureException.h>

namespace folly {

[[noreturn]] void throwNoState() {
  throw NoState();
}

[[noreturn]] void throwPromiseAlreadySatisfied() {
  throw PromiseAlreadySatisfied();
}

[[noreturn]] void throwFutureNotReady() {
  throw FutureNotReady();
}

[[noreturn]] void throwFutureAlreadyRetrieved() {
  throw FutureAlreadyRetrieved();
}

[[noreturn]] void throwTimedOut() {
  throw TimedOut();
}

[[noreturn]] void throwPredicateDoesNotObtain() {
  throw PredicateDoesNotObtain();
}

[[noreturn]] void throwNoFutureInSplitter() { throw NoFutureInSplitter(); }

    [[noreturn]] void throwNoExecutor() {
  throw NoExecutor();
}
} // namespace folly
