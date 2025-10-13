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

#include <folly/debugging/exception_tracer/SmartExceptionTracer.h>

#include <glog/logging.h>
#include <folly/MapUtil.h>
#include <folly/ScopeGuard.h>
#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>
#include <folly/debugging/exception_tracer/ExceptionTracerLib.h>
#include <folly/debugging/exception_tracer/SmartExceptionTracerSingleton.h>
#include <folly/debugging/exception_tracer/StackTrace.h>
#include <folly/lang/Exception.h>

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

#if FOLLY_HAS_EXCEPTION_TRACER

namespace folly {
namespace exception_tracer {
namespace {

std::atomic_bool loggedMessage{false};

// ExceptionMetaFunc takes a `const ExceptionMeta&` and
// returns a pair of iterators to represent a range of addresses for
// the stack frames to use.
template <typename ExceptionMetaFunc>
ExceptionInfo getTraceWithFunc(
    const std::exception& ex, ExceptionMetaFunc func) {
  if (!detail::isSmartExceptionTracerHookEnabled() &&
      !loggedMessage.load(std::memory_order_relaxed)) {
    LOG(WARNING)
        << "Smart exception tracer library not linked, stack traces not available";
    loggedMessage = true;
  }

  ExceptionInfo info;
  info.type = &typeid(ex);

  if (auto meta = get_default(*detail::getMetaMap().rlock(), &ex)) {
    auto [traceBeginIt, traceEndIt] = func(*meta);
    info.frames.assign(traceBeginIt, traceEndIt);
  }

  return info;
}

template <typename ExceptionMetaFunc>
ExceptionInfo getTraceWithFunc(
    const std::exception_ptr& ptr, ExceptionMetaFunc func) {
  if (auto* ex = folly::exception_ptr_get_object<std::exception>(ptr)) {
    return getTraceWithFunc(*ex, std::move(func));
  }
  return ExceptionInfo();
}

template <typename ExceptionMetaFunc>
ExceptionInfo getTraceWithFunc(
    const exception_wrapper& ew, ExceptionMetaFunc func) {
  return getTraceWithFunc(ew.exception_ptr(), std::move(func));
}

auto getAsyncStackTraceItPair(const detail::ExceptionMeta& meta) {
  auto addr = meta.traceAsync.addresses;
  return std::pair(addr, addr + meta.traceAsync.frameCount);
}

auto getNormalStackTraceItPair(const detail::ExceptionMeta& meta) {
  auto addr = meta.trace.addresses;
  return std::pair(addr, addr + meta.trace.frameCount);
}

} // namespace

ExceptionInfo getTrace(const std::exception_ptr& ptr) {
  return getTraceWithFunc(ptr, getNormalStackTraceItPair);
}

ExceptionInfo getTrace(const exception_wrapper& ew) {
  return getTraceWithFunc(ew, getNormalStackTraceItPair);
}

ExceptionInfo getTrace(const std::exception& ex) {
  return getTraceWithFunc(ex, getNormalStackTraceItPair);
}

ExceptionInfo getAsyncTrace(const std::exception_ptr& ptr) {
  return getTraceWithFunc(ptr, getAsyncStackTraceItPair);
}

ExceptionInfo getAsyncTrace(const exception_wrapper& ew) {
  return getTraceWithFunc(ew, getAsyncStackTraceItPair);
}

ExceptionInfo getAsyncTrace(const std::exception& ex) {
  return getTraceWithFunc(ex, getAsyncStackTraceItPair);
}

} // namespace exception_tracer
} // namespace folly

#endif //  FOLLY_HAS_EXCEPTION_TRACER

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
