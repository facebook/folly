/*
 * Copyright 2012 Facebook, Inc.
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

#ifndef FOLLY_THREADCACHEDARENA_H_
#define FOLLY_THREADCACHEDARENA_H_

#include <utility>
#include <mutex>
#include <limits>
#include <boost/intrusive/slist.hpp>

#include "folly/Likely.h"
#include "folly/Arena.h"
#include "folly/ThreadLocal.h"

namespace folly {

/**
 * Thread-caching arena: allocate memory which gets freed when the arena gets
 * destroyed.
 *
 * The arena itself allocates memory using malloc() in blocks of
 * at least minBlockSize bytes.
 *
 * For speed, each thread gets its own Arena (see Arena.h); when threads
 * exit, the Arena gets merged into a "zombie" Arena, which will be deallocated
 * when the ThreadCachedArena object is destroyed.
 */
class ThreadCachedArena {
 public:
  explicit ThreadCachedArena(
      size_t minBlockSize = SysArena::kDefaultMinBlockSize);

  void* allocate(size_t size) {
    SysArena* arena = arena_.get();
    if (UNLIKELY(!arena)) {
      arena = allocateThreadLocalArena();
    }

    return arena->allocate(size);
  }

  void deallocate(void* p) {
    // Deallocate? Never!
  }

 private:
  ThreadCachedArena(const ThreadCachedArena&) = delete;
  ThreadCachedArena(ThreadCachedArena&&) = delete;
  ThreadCachedArena& operator=(const ThreadCachedArena&) = delete;
  ThreadCachedArena& operator=(ThreadCachedArena&&) = delete;

  SysArena* allocateThreadLocalArena();

  // Zombify the blocks in arena, saving them for deallocation until
  // the ThreadCachedArena is destroyed.
  void zombify(SysArena&& arena);

  size_t minBlockSize_;
  SysArena zombies_;  // allocated from threads that are now dead
  std::mutex zombiesMutex_;
  ThreadLocalPtr<SysArena> arena_;  // per-thread arena
};

}  // namespace folly

#endif /* FOLLY_THREADCACHEDARENA_H_ */

