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

#include <tuple>
#include <variant>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

template <typename Id, typename Value>
using Enumerated = std::tuple<Id, Value>;

// Multiplex the results of multiple streams into a single stream that
// contains all of the events of the input stream.
//
// The output is a stream of std::tuple<Id, Event> where the first tuple
// element is the result of a call to selectId(innerStream). The default
// selectId returns a size_t set to the index of the stream that the event
// came from and where Event is the CallbackRecord result of
// materialize(innerStream).
//
// Example:
//   AsyncGenerator<AsyncGenerator<int>&&> streams();
//
//   Task<void> consumer() {
//     auto events = multiplex(streams());
//     while (auto item != co_await events.next()) {
//       auto&& [index, event] = *item;
//       if (event.index() == 0) {
//          // Value
//          int value = std::get<0>(event);
//          std::cout << index << " value " << value << "\n";
//       } else if (event.index() == 1) {
//         // End Of Stream
//         std::cout << index << " end\n";
//       } else {
//         // Exception
//         folly::exception_wrapper error = std::get<2>(event);
//         std::cout << index << " error " << error.what() << "\n";
//       }
//     }
//   }
template <
    typename SelectIdFn,
    typename Reference,
    typename Value,
    typename KeyType = std::decay_t<
        invoke_result_t<SelectIdFn&, const AsyncGenerator<Reference, Value>&>>>
AsyncGenerator<
    Enumerated<KeyType, CallbackRecord<Reference>>,
    Enumerated<KeyType, CallbackRecord<Value>>>
multiplex(
    folly::Executor::KeepAlive<> exec,
    AsyncGenerator<AsyncGenerator<Reference, Value>>&& sources,
    SelectIdFn&& selectId);

template <typename Reference, typename Value>
AsyncGenerator<
    Enumerated<size_t, CallbackRecord<Reference>>,
    Enumerated<size_t, CallbackRecord<Value>>>
multiplex(
    folly::Executor::KeepAlive<> exec,
    AsyncGenerator<AsyncGenerator<Reference, Value>>&& sources);

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/Multiplex-inl.h>
