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

#include <folly/experimental/coro/AsyncGenerator.h>
#include <folly/experimental/coro/Coroutine.h>

#if FOLLY_HAS_COROUTINES

namespace folly {
namespace coro {

// Merge the results of a number of input streams.
//
// The 'executor' parameter represents specifies the execution context to
// be used for awaiting each value from the sources.
// The 'sources' parameter represents an async-stream of async-streams.
// The resulting generator merges the results from each of the streams
// produced by 'sources', interleaving them in the order that the values
// are produced.
//
// If any of the input streams completes with an error then the error
// is produced from the output stream and the remainder of the input streams
// are truncated, discarding any remaining values.
//
// The resulting stream will terminate only when the end of the 'sources'
// stream has been reached and the ends of all of the input streams it
// produced have been reached.
template <typename Reference, typename Value>
AsyncGenerator<Reference, Value> merge(
    folly::Executor::KeepAlive<> executor,
    AsyncGenerator<AsyncGenerator<Reference, Value>> sources);

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/Merge-inl.h>
