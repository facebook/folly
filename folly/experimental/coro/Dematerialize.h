/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <folly/ExceptionWrapper.h>
#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/Coroutine.h>
#include <folly/experimental/coro/Materialize.h>

#include <variant>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

// Dematerialize the CallbackRecords from an input stream into a stream that
// replays all of the events materialized in the input stream.
//
// The input is a stream of CallbackRecord.
//
// The output is a stream of Value.
//
// Example:
//   AsyncGenerator<CallbackRecord<int>> stream();
//
//   Task<void> consumer() {
//     auto events = dematerialize(stream());
//     try {
//       while (auto item = co_await events.next()) {
//         // Value
//         auto&& value = *item;
//         std::cout << "value " << value << "\n";
//       }
//       // None
//       std::cout << "end\n";
//     } catch (const std::exception& error) {
//       // Exception
//       std::cout << "error " << error.what() << "\n";
//     }
//   }
template <typename Reference, typename Value>
AsyncGenerator<Reference, Value> dematerialize(
    AsyncGenerator<CallbackRecord<Reference>, CallbackRecord<Value>> source);

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/Dematerialize-inl.h>
