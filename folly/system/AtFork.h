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

#include <sys/types.h>

#include <folly/Function.h>

namespace folly {

//  AtFork
//
//  Wraps pthread_atfork on platforms with pthread_atfork, but with additional
//  facilities.
struct AtFork {
  static void init();
  static void registerHandler(
      void const* handle,
      Function<bool()> prepare,
      Function<void()> parent,
      Function<void()> child);
  static void unregisterHandler(void const* handle);

  using fork_t = pid_t();
  static pid_t forkInstrumented(fork_t forkFn);
};

} // namespace folly
