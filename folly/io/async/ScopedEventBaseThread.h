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

#include <memory>
#include <thread>

#include <folly/io/async/EventBase.h>
#include <folly/synchronization/Baton.h>

namespace folly {

class EventBaseManager;
template <class Iter>
class Range;
typedef Range<const char*> StringPiece;

/**
 * ScopedEventBaseThread is a helper class that starts a new std::thread running
 * an EventBase loop.
 *
 * The new thread will be started by the ScopedEventBaseThread constructor.
 * When the ScopedEventBaseThread object is destroyed, the thread will be
 * stopped.
 *
 * ScopedEventBaseThread is not CopyConstructible nor CopyAssignable nor
 * MoveConstructible nor MoveAssignable.
 *
 * @refcode folly/docs/examples/folly/ScopedEventBaseThread.cpp
 * @class folly::ScopedEventBaseThread
 */
class ScopedEventBaseThread : public IOExecutor, public SequencedExecutor {
 public:
  /**
   * Default constructor, initializes with current EventBaseManager and empty
   * thread name.
   * @refcode folly/docs/examples/folly/ScopedEventBaseThread2.cpp
   */
  ScopedEventBaseThread();
  /**
   * Initializes with current EventBaseManager and passed-in thread name.
   */
  explicit ScopedEventBaseThread(StringPiece name);
  /**
   * Initializes with passed-in EventBaseManager and empty thread name.
   */
  explicit ScopedEventBaseThread(EventBaseManager* ebm);
  /**
   * Initializes with passed-in EventBaseManager and thread name.
   */
  explicit ScopedEventBaseThread(EventBaseManager* ebm, StringPiece name);
  /**
   * Initializes with passed-in EventBaseOptions, EventBaseManager and thread
   * name.
   */
  ScopedEventBaseThread(
      EventBase::Options eventBaseOptions,
      EventBaseManager* ebm,
      StringPiece name);
  ~ScopedEventBaseThread() override;

  /**
   * Returns the event base of the thread.
   * @methodset Observers
   */
  EventBase* getEventBase() const { return &eb_; }

  EventBase* getEventBase() override { return &eb_; }

  /**
   * Returns the ID of the thread.
   * @methodset Observers
   */
  std::thread::id getThreadId() const { return th_.get_id(); }

  /**
   * Returns the underlying implementation-defined thread handle.
   * @methodset Observers
   */
  std::thread::native_handle_type getNativeHandle() {
    return th_.native_handle();
  }

  /**
   * Runs the passed-in function on the event base of the thread.
   *
   * @param func Function to be run on event base of the thread.
   * @methodset Operations
   */
  void add(Func func) override { getEventBase()->add(std::move(func)); }

 protected:
  bool keepAliveAcquire() noexcept override {
    return getEventBase()->keepAliveAcquire();
  }

  void keepAliveRelease() noexcept override {
    getEventBase()->keepAliveRelease();
  }

 private:
  ScopedEventBaseThread(ScopedEventBaseThread&& other) = delete;
  ScopedEventBaseThread& operator=(ScopedEventBaseThread&& other) = delete;

  ScopedEventBaseThread(const ScopedEventBaseThread& other) = delete;
  ScopedEventBaseThread& operator=(const ScopedEventBaseThread& other) = delete;

  EventBaseManager* ebm_;
  union {
    mutable EventBase eb_;
  };
  std::thread th_;
  folly::Baton<> stop_;
};

} // namespace folly
