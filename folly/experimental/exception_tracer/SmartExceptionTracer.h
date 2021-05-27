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
#include <folly/experimental/exception_tracer/ExceptionTracer.h>

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

#if defined(__GLIBCXX__)

namespace folly {
namespace exception_tracer {

ExceptionInfo getTrace(const std::exception_ptr& ex);

ExceptionInfo getTrace(const std::exception& ex);

ExceptionInfo getTrace(const exception_wrapper& ew);

ExceptionInfo getAsyncTrace(const std::exception_ptr& ex);

ExceptionInfo getAsyncTrace(const std::exception& ex);

ExceptionInfo getAsyncTrace(const exception_wrapper& ew);

} // namespace exception_tracer
} // namespace folly

#endif // defined(__GLIBCXX__)

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
