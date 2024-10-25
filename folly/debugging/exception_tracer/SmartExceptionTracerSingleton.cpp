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

#include <folly/debugging/exception_tracer/SmartExceptionTracerSingleton.h>

extern "C" {

folly::exception_tracer::detail::ExceptionMetaMap*
    __folly_smart_exception_store{nullptr};
}

namespace folly::exception_tracer::detail {

Synchronized<ExceptionMetaMap>& getMetaMap() {
  static Indestructible<std::unique_ptr<Synchronized<ExceptionMetaMap>>> meta(
      folly::factory_constructor, []() {
        auto ret = std::make_unique<folly::Synchronized<ExceptionMetaMap>>();
        __folly_smart_exception_store = &ret->unsafeGetUnlocked();
        return ret;
      });
  return **meta;
}

static std::atomic_bool hookEnabled{false};

bool isSmartExceptionTracerHookEnabled() {
  return hookEnabled.load(std::memory_order_relaxed);
}
void setSmartExceptionTracerHookEnabled(bool enabled) {
  hookEnabled = enabled;
}

} // namespace folly::exception_tracer::detail
