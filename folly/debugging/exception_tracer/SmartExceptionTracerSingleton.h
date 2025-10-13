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

#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>
#include <folly/debugging/exception_tracer/StackTrace.h>

namespace folly::exception_tracer::detail {

struct ExceptionMeta {
  void (*deleter)(void*);
  // normal stack trace
  StackTrace trace;
  // async stack trace
  StackTrace traceAsync;
};

// using a vector-map so that all the values are laid out contiguously in an
// array, specifically to make debugging easier - no need to learn the structure
// of the f14-chunk-array when debugging the exception hooks
using ExceptionMetaMap =
    folly::F14VectorMap<void const*, std::shared_ptr<ExceptionMeta const>>;

Synchronized<ExceptionMetaMap>& getMetaMap();

bool isSmartExceptionTracerHookEnabled();
void setSmartExceptionTracerHookEnabled(bool enabled);

} // namespace folly::exception_tracer::detail
