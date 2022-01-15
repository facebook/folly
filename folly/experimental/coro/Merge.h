/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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
// The 'executor' parameter specifies the execution context to
// be used for awaiting each value from the sources.
// The 'sources' parameter represents an async-stream of async-streams.
// The resulting generator merges the results from each of the streams
// produced by 'sources', interleaving them in the order that the values
// are produced.
//
// The resulting stream will terminate when the end of the 'sources' stream has
// been reached and the ends of all of the input streams it produced have been
// reached.
//
// On exception or cancellation, cancels remaining input streams and 'sources',
// discards any remaining values, and produces an exception (if an input stream
// produced an exception) or end-of-stream (if next() call was cancelled).
//
// Structured concurrency: if the output stream produced an empty value
// (end-of-stream) or an exception, it's guaranteed that 'sources' and all input
// generators have been destroyed.
// If the output stream is destroyed early (before reaching end-of-stream or
// exception), the remaining input generators are cancelled and detached; beware
// of use-after-free.
//
// Normally cancelling output stream's next() call cancels the stream, discards
// any remaining values, and returns an end-of-stream. But there are caveats:
//  * If there's an item ready to be delivered, next() call returns it without
//    checking for cancellation. So if input streams are fast, and next() is
//    called infrequently, cancellation may go unprocessed indefinitely unless
//    you also check for cancellation on your side (which you should probably do
//    anyway unless you're calling next() in a tight loop).
//  * It's possible that the cancelled next() registers the cancellation but
//    returns a value anyway (if it was produced at just the right moment). Then
//    a later next() call would return end-of-stream even if it was called with
//    a different, non-cancelled cancellation token.
template <typename Reference, typename Value>
AsyncGenerator<Reference, Value> merge(
    folly::Executor::KeepAlive<> executor,
    AsyncGenerator<AsyncGenerator<Reference, Value>> sources);

} // namespace coro
} // namespace folly

#endif // FOLLY_HAS_COROUTINES

#include <folly/experimental/coro/Merge-inl.h>
