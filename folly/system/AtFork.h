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

#include <list>
#include <map>
#include <mutex>

#include <folly/Function.h>

namespace folly {

//  AtForkList
//
//  The list data structure used internally in AtFork's internal singleton.
//
//  Useful for AtFork, but may not be useful more broadly.
class AtForkList {
 public:
  //  prepare
  //
  //  Acquires the mutex. Performs trial passes in a loop until a trial pass
  //  succeeds. A trial pass invokes each prepare handler in reverse order of
  //  insertion, failing the pass and cleaning up if any handler returns false.
  //  Cleanup entails invoking parent handlers in reverse order, up to but not
  //  including the parent handler corresponding to the failed prepare handler.
  void prepare() noexcept;

  //  parent
  //
  //  Invokes parent handlers in order of insertion. Releases the mutex.
  void parent() noexcept;

  //  child
  //
  //  Invokes child handlers in order of insertion. Releases the mutex.
  void child() noexcept;

  //  append
  //
  //  While holding the mutex, inserts a set of handlers to the end of the list.
  //
  //  If handle is not nullptr, the set of handlers is indexed by handle and may
  //  be found by members remove() and contain().
  void append( //
      void const* handle,
      Function<bool()> prepare,
      Function<void()> parent,
      Function<void()> child);

  //  remove
  //
  //  While holding the mutex, removes a set of handlers found by handle, if not
  //  null.
  void remove( //
      void const* handle);

  //  contains
  //
  //  While holding the mutex, finds a set of handlers found by handle, if not
  //  null, returning true if found and false otherwise.
  bool contains( //
      void const* handle);

 private:
  struct Task {
    void const* handle;
    Function<bool()> prepare;
    Function<void()> parent;
    Function<void()> child;
  };

  std::mutex mutex;
  std::list<Task> tasks;
  std::map<void const*, std::list<Task>::iterator> index;
};

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
