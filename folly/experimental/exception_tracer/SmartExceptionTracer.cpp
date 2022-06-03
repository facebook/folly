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

#include <folly/experimental/exception_tracer/SmartExceptionTracer.h>

#include <glog/logging.h>
#include <folly/MapUtil.h>
#include <folly/ScopeGuard.h>
#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>
#include <folly/experimental/exception_tracer/ExceptionTracerLib.h>
#include <folly/experimental/exception_tracer/SmartExceptionTracerSingleton.h>
#include <folly/experimental/exception_tracer/StackTrace.h>
#include <folly/experimental/symbolizer/Symbolizer.h>
#include <folly/lang/Exception.h>

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

#if defined(__GLIBCXX__)

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
  auto rlockedMeta = detail::getMetaMap().withRLock(
      [&](const auto& locked) noexcept
      -> detail::SynchronizedExceptionMeta::RLockedPtr {
        auto* meta = get_ptr(locked, (void*)&ex);
        // If we can't find the exception, return an empty stack trace.
        if (!meta) {
          return {};
        }
        CHECK(*meta);
        // Acquire the meta rlock while holding the map's rlock, to block meta's
        // destruction.
        return (*meta)->rlock();
      });

  if (!rlockedMeta) {
    return info;
  }

  auto [traceBeginIt, traceEndIt] = func(*rlockedMeta);
  info.frames.assign(traceBeginIt, traceEndIt);
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
  if (auto* ex = ew.get_exception()) {
    return getTraceWithFunc(*ex, std::move(func));
  }
  return ExceptionInfo();
}

auto getAsyncStackTraceItPair(const detail::ExceptionMeta& meta) {
  return std::make_pair(
      meta.traceAsync.addresses,
      meta.traceAsync.addresses + meta.traceAsync.frameCount);
}

auto getNormalStackTraceItPair(const detail::ExceptionMeta& meta) {
  return std::make_pair(
      meta.trace.addresses, meta.trace.addresses + meta.trace.frameCount);
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

#endif // defined(__GLIBCXX__)

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
