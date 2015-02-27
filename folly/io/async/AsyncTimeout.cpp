/*
 * Copyright 2015 Facebook, Inc.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventUtil.h>
#include <folly/io/async/Request.h>

#include <assert.h>
#include <glog/logging.h>

namespace folly {

AsyncTimeout::AsyncTimeout(TimeoutManager* timeoutManager)
    : timeoutManager_(timeoutManager) {

  event_set(&event_, -1, EV_TIMEOUT, &AsyncTimeout::libeventCallback, this);
  event_.ev_base = nullptr;
  timeoutManager_->attachTimeoutManager(
      this,
      TimeoutManager::InternalEnum::NORMAL);
  RequestContext::getStaticContext();
}

AsyncTimeout::AsyncTimeout(EventBase* eventBase)
    : timeoutManager_(eventBase) {

  event_set(&event_, -1, EV_TIMEOUT, &AsyncTimeout::libeventCallback, this);
  event_.ev_base = nullptr;
  if (eventBase) {
    timeoutManager_->attachTimeoutManager(
      this,
      TimeoutManager::InternalEnum::NORMAL);
  }
  RequestContext::getStaticContext();
}

AsyncTimeout::AsyncTimeout(TimeoutManager* timeoutManager,
                             InternalEnum internal)
    : timeoutManager_(timeoutManager) {

  event_set(&event_, -1, EV_TIMEOUT, &AsyncTimeout::libeventCallback, this);
  event_.ev_base = nullptr;
  timeoutManager_->attachTimeoutManager(this, internal);
  RequestContext::getStaticContext();
}

AsyncTimeout::AsyncTimeout(EventBase* eventBase, InternalEnum internal)
    : timeoutManager_(eventBase) {

  event_set(&event_, -1, EV_TIMEOUT, &AsyncTimeout::libeventCallback, this);
  event_.ev_base = nullptr;
  timeoutManager_->attachTimeoutManager(this, internal);
  RequestContext::getStaticContext();
}

AsyncTimeout::AsyncTimeout(): timeoutManager_(nullptr) {
  event_set(&event_, -1, EV_TIMEOUT, &AsyncTimeout::libeventCallback, this);
  event_.ev_base = nullptr;
  RequestContext::getStaticContext();
}

AsyncTimeout::~AsyncTimeout() {
  cancelTimeout();
}

bool AsyncTimeout::scheduleTimeout(std::chrono::milliseconds timeout) {
  assert(timeoutManager_ != nullptr);
  context_ = RequestContext::saveContext();
  return timeoutManager_->scheduleTimeout(this, timeout);
}

bool AsyncTimeout::scheduleTimeout(uint32_t milliseconds) {
  return scheduleTimeout(std::chrono::milliseconds(milliseconds));
}

void AsyncTimeout::cancelTimeout() {
  if (isScheduled()) {
    timeoutManager_->cancelTimeout(this);
  }
}

bool AsyncTimeout::isScheduled() const {
  return EventUtil::isEventRegistered(&event_);
}

void AsyncTimeout::attachTimeoutManager(
    TimeoutManager* timeoutManager,
    InternalEnum internal) {
  // This also implies no timeout is scheduled.
  assert(timeoutManager_ == nullptr);
  assert(timeoutManager->isInTimeoutManagerThread());
  timeoutManager_ = timeoutManager;

  timeoutManager_->attachTimeoutManager(this, internal);
}

void AsyncTimeout::attachEventBase(
    EventBase* eventBase,
    InternalEnum internal) {
  attachTimeoutManager(eventBase, internal);
}

void AsyncTimeout::detachTimeoutManager() {
  // Only allow the event base to be changed if the timeout is not
  // currently installed.
  if (isScheduled()) {
    // Programmer bug.  Abort the program.
    LOG(ERROR) << "detachEventBase() called on scheduled timeout; aborting";
    abort();
    return;
  }

  if (timeoutManager_) {
    timeoutManager_->detachTimeoutManager(this);
    timeoutManager_ = nullptr;
  }
}

void AsyncTimeout::detachEventBase() {
  detachTimeoutManager();
}

void AsyncTimeout::libeventCallback(int fd, short events, void* arg) {
  AsyncTimeout* timeout = reinterpret_cast<AsyncTimeout*>(arg);
  assert(fd == -1);
  assert(events == EV_TIMEOUT);

  // double check that ev_flags gets reset when the timeout is not running
  assert((timeout->event_.ev_flags & ~EVLIST_INTERNAL) == EVLIST_INIT);

  // this can't possibly fire if timeout->eventBase_ is nullptr
  (void) timeout->timeoutManager_->bumpHandlingTime();

  auto old_ctx =
    RequestContext::setContext(timeout->context_);

  timeout->timeoutExpired();

  RequestContext::setContext(old_ctx);
}

} // folly
