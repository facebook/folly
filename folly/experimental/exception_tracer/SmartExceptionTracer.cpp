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

#include <folly/experimental/exception_tracer/SmartExceptionTracer.h>

#include <folly/MapUtil.h>
#include <folly/ScopeGuard.h>
#include <folly/Synchronized.h>
#include <folly/experimental/exception_tracer/ExceptionTracerLib.h>
#include <folly/experimental/exception_tracer/StackTrace.h>
#include <folly/experimental/symbolizer/Symbolizer.h>

namespace folly {
namespace exception_tracer {
namespace {

struct ExceptionMeta {
  void (*deleter)(void*);
  StackTrace trace;
};

Synchronized<std::unordered_map<void*, ExceptionMeta>>& getMeta() {
  // Leaky Meyers Singleton
  static Indestructible<Synchronized<std::unordered_map<void*, ExceptionMeta>>>
      meta;
  return *meta;
}

void metaDeleter(void* ex) noexcept {
  auto deleter = getMeta().withWLock([&](auto& locked) noexcept {
    auto iter = locked.find(ex);
    auto* innerDeleter = iter->second.deleter;
    locked.erase(iter);
    return innerDeleter;
  });

  // If the thrown object was allocated statically it may not have a deleter.
  if (deleter) {
    deleter(ex);
  }
}

// This callback runs when an exception is thrown so we can grab the stack
// trace. To manage the lifetime of the stack trace we override the deleter with
// our own wrapper.
void throwCallback(
    void* ex,
    std::type_info*,
    void (**deleter)(void*)) noexcept {
  // Make this code reentrant safe in case we throw an exception while
  // handling an exception. Thread local variables are zero initialized.
  static FOLLY_TLS bool handlingThrow;
  if (handlingThrow) {
    return;
  }
  SCOPE_EXIT {
    handlingThrow = false;
  };
  handlingThrow = true;

  getMeta().withWLockPtr([&](auto wlock) noexcept {
    // This can allocate memory potentially causing problems in an OOM
    // situation so we catch and short circuit.
    try {
      auto& meta = (*wlock)[ex];
      auto rlock = wlock.moveFromWriteToRead();
      // Override the deleter with our custom one and capture the old one.
      meta.deleter = std::exchange(*deleter, metaDeleter);

      ssize_t n = symbolizer::getStackTrace(meta.trace.addresses, kMaxFrames);
      if (n != -1) {
        meta.trace.frameCount = n;
      }
    } catch (const std::bad_alloc&) {
    }
  });
}

struct Initialize {
  Initialize() {
    registerCxaThrowCallback(throwCallback);
  }
};

Initialize initialize;
} // namespace

ExceptionInfo getTrace(const std::exception_ptr& ptr) {
  try {
    // To get a pointer to the actual exception we need to rethrow the
    // exception_ptr and catch it.
    std::rethrow_exception(ptr);
  } catch (std::exception& ex) {
    return getTrace(ex);
  } catch (...) {
    return ExceptionInfo();
  }
}

ExceptionInfo getTrace(const exception_wrapper& ew) {
  if (auto* ex = ew.get_exception()) {
    return getTrace(*ex);
  }
  return ExceptionInfo();
}

ExceptionInfo getTrace(const std::exception& ex) {
  ExceptionInfo info;
  info.type = &typeid(ex);
  getMeta().withRLock([&](auto& locked) noexcept {
    auto* meta = get_ptr(locked, (void*)&ex);
    // If we can't find the exception, return an empty stack trace.
    if (!meta) {
      return;
    }
    info.frames.assign(
        meta->trace.addresses, meta->trace.addresses + meta->trace.frameCount);
  });
  return info;
}

} // namespace exception_tracer
} // namespace folly
