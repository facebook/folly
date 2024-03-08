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

#include <folly/concurrency/ConcurrentHashMap.h>
#include <folly/python/AsyncioExecutor.h>
#ifdef WIN32
#include <folly/portability/Windows.h>
#endif

namespace folly {
namespace python {

namespace detail {
class ProactorExecutorCallback {
 public:
  explicit ProactorExecutorCallback(folly::Func func) noexcept
      : func_(std::move(func)) {}

  uintptr_t address() const {
    return reinterpret_cast<uintptr_t>(&overlapped_);
  }

  void send([[maybe_unused]] uint64_t iocp) const {
#ifdef WIN32
    auto handle = reinterpret_cast<HANDLE>(iocp);
    auto success =
        PostQueuedCompletionStatus(handle, 0, 0, (LPOVERLAPPED)&overlapped_);
    if (!success) {
      auto what = fmt::format(
          "Failed to notify asyncio IOCP. Errror code %d", GetLastError());
      throw std::runtime_error(what);
    }
#endif
  }

  void invoke() {
    folly::python::AsyncioExecutor::invokeCatchingExns(
        "ProactorExecutor: task", std::exchange(func_, {}));
  }

 private:
  folly::Func func_;
#ifdef WIN32
  _OVERLAPPED overlapped_{};
#else
  FOLLY_ATTR_NO_UNIQUE_ADDRESS tag_t<> overlapped_{};
#endif
};
} // namespace detail

class ProactorExecutor : public DroppableAsyncioExecutor<ProactorExecutor> {
 private:
  uint64_t iocp_;
  folly::ConcurrentHashMap<
      uintptr_t,
      std::unique_ptr<detail::ProactorExecutorCallback>>
      cache_;

 public:
  using Func = folly::Func;

  explicit ProactorExecutor(uint64_t iocp);
  void add(Func func) noexcept override;
  void drive() noexcept override;
  bool execute(uintptr_t address);
};

} // namespace python
} // namespace folly
