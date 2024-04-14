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

#include <folly/experimental/exception_tracer/ExceptionTracerLib.h>
#include <folly/experimental/exception_tracer/SmartExceptionTracerSingleton.h>
#include <folly/experimental/symbolizer/Symbolizer.h>

#if FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF

#if defined(__GLIBCXX__)

namespace folly::exception_tracer {

namespace {

void metaDeleter(void* ex) noexcept {
  auto syncMeta = detail::getMetaMap().withWLock([ex](auto& locked) noexcept {
    auto iter = locked.find(ex);
    auto ret = std::move(iter->second);
    locked.erase(iter);
    return ret;
  });

  // If the thrown object was allocated statically it may not have a deleter.
  auto meta = syncMeta->wlock();
  if (meta->deleter) {
    meta->deleter(ex);
  }
}

// This callback runs when an exception is thrown so we can grab the stack
// trace. To manage the lifetime of the stack trace we override the deleter with
// our own wrapper.
void throwCallback(
    void* ex, std::type_info*, void (**deleter)(void*)) noexcept {
  // Make this code reentrant safe in case we throw an exception while
  // handling an exception. Thread local variables are zero initialized.
  static thread_local bool handlingThrow;
  if (handlingThrow) {
    return;
  }
  SCOPE_EXIT {
    handlingThrow = false;
  };
  handlingThrow = true;

  // This can allocate memory potentially causing problems in an OOM
  // situation so we catch and short circuit.
  try {
    auto newMeta = std::make_unique<detail::SynchronizedExceptionMeta>();
    newMeta->withWLock([&deleter](auto& lockedMeta) {
      // Override the deleter with our custom one and capture the old one.
      lockedMeta.deleter = std::exchange(*deleter, metaDeleter);

      ssize_t n = folly::symbolizer::getStackTrace(
          lockedMeta.trace.addresses, kMaxFrames);
      if (n != -1) {
        lockedMeta.trace.frameCount = n;
      }
      ssize_t nAsync = folly::symbolizer::getAsyncStackTraceSafe(
          lockedMeta.traceAsync.addresses, kMaxFrames);
      if (nAsync != -1) {
        lockedMeta.traceAsync.frameCount = nAsync;
      }
    });

    auto oldMeta = detail::getMetaMap().withWLock([ex, &newMeta](auto& wlock) {
      return std::exchange(wlock[ex], std::move(newMeta));
    });
    DCHECK(oldMeta == nullptr);

  } catch (const std::bad_alloc&) {
  }
}

struct Initialize {
  Initialize() {
    registerCxaThrowCallback(throwCallback);
    detail::setSmartExceptionTracerHookEnabled(true);
  }
};

Initialize initialize;

} // namespace
} // namespace folly::exception_tracer

#endif // defined(__GLIBCXX__)

#endif // FOLLY_HAVE_ELF && FOLLY_HAVE_DWARF
