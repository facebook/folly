/*
 * Copyright 2014 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "QueuedImmediateExecutor.h"
#include "folly/ThreadLocal.h"
#include <queue>

namespace folly { namespace wangle {

void QueuedImmediateExecutor::add(Action&& callback)
{
  thread_local std::queue<Action> q;

  if (q.empty()) {
    q.push(std::move(callback));
    while (!q.empty()) {
      q.front()();
      q.pop();
    }
  } else {
    q.push(callback);
  }
}

}} // namespace
