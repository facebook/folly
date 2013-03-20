/*
 * Copyright 2013 Facebook, Inc.
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

#include "folly/ThreadCachedArena.h"

namespace folly {

ThreadCachedArena::ThreadCachedArena(size_t minBlockSize)
  : minBlockSize_(minBlockSize) {
}

SysArena* ThreadCachedArena::allocateThreadLocalArena() {
  SysArena* arena = new SysArena(minBlockSize_);
  auto disposer = [this] (SysArena* t, TLPDestructionMode mode) {
    std::unique_ptr<SysArena> tp(t);  // ensure it gets deleted
    if (mode == TLPDestructionMode::THIS_THREAD) {
      zombify(std::move(*t));
    }
  };
  arena_.reset(arena, disposer);
  return arena;
}

void ThreadCachedArena::zombify(SysArena&& arena) {
  std::lock_guard<std::mutex> lock(zombiesMutex_);
  zombies_.merge(std::move(arena));
}

}  // namespace folly

