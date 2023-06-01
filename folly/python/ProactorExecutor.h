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

#include <folly/python/AsyncioExecutor.h>

namespace folly {
namespace python {

class ProactorExecutor : public DroppableAsyncioExecutor<ProactorExecutor> {
 private:
  int iocp_;
  std::set<uint64_t> notification_cache_;
  std::queue<Func> queue_;
  std::mutex mutex_;

 public:
  using Func = folly::Func;

  explicit ProactorExecutor(int iocp);
  void add(Func func) noexcept override;
  void drive() noexcept override;
  void notify();
  bool pop(uint64_t address);
};

} // namespace python
} // namespace folly
