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

#include <folly/experimental/io/IoUringEvent.h>

#if FOLLY_HAS_LIBURING

#include <sys/eventfd.h>

namespace folly {

bool IoUringEvent::hasWork() {
  return backend_.isWaitingToSubmit() ||
      io_uring_cq_ready(backend_.ioRingPtr());
}

IoUringEvent::IoUringEvent(
    folly::EventBase* eventBase,
    IoUringBackend::Options const& o,
    bool use_event_fd)
    : EventHandler(eventBase), eventBase_(eventBase), backend_(o) {
  if (use_event_fd) {
    eventFd_ = folly::File{eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC), true};
    int ret = io_uring_register_eventfd(backend_.ioRingPtr(), eventFd_->fd());
    PLOG_IF(ERROR, ret) << "cannot register eventfd";
    if (ret) {
      throw std::runtime_error("cannot register eventfd");
    }
    changeHandlerFD(NetworkSocket{eventFd_->fd()});
  } else {
    changeHandlerFD(NetworkSocket{backend_.ioRingPtr()->ring_fd});
  }

  registerHandler(EventHandler::PERSIST | EventHandler::READ);
  edgeTriggered_ = use_event_fd && setEdgeTriggered();

  eventBase->runBeforeLoop(this);
}

IoUringEvent::~IoUringEvent() {
  // flush cq:
  while (hasWork() &&
         !backend_.eb_event_base_loop(EVLOOP_NONBLOCK | EVLOOP_ONCE)) {
    DVLOG(9) << "IoUringEvent::cleanup  done: isWaitingToSubmit="
             << backend_.isWaitingToSubmit()
             << " cqes=" << io_uring_cq_ready(backend_.ioRingPtr());
  }
}

void IoUringEvent::handlerReady(uint16_t events) noexcept {
  DVLOG(4) << "IoUringEvent::handlerReady(" << events << ")";

  if (!(events & EventHandler::READ)) {
    return;
  }

  size_t ready = io_uring_cq_ready(backend_.ioRingPtr());

  if (ready >= backend_.options().maxGet) {
    // don't even clear the eventfd
    backend_.processCompleted();
    lastWasResignalled_ = true;
    if (edgeTriggered_) {
      uint64_t val = 1;
      int ret = ::write(eventFd_->fd(), &val, sizeof(val));
      DCHECK(ret == 8);
    }
  } else if (eventFd_) {
    if (!edgeTriggered_) {
      uint64_t val;
      ::read(eventFd_->fd(), &val, sizeof(val));
    }
    backend_.loopPoll();

    // if we still have completions we really need to process them next loop
    lastWasResignalled_ = io_uring_cq_ready(backend_.ioRingPtr()) > 0;
    if (lastWasResignalled_) {
      uint64_t val = 1;
      int ret = ::write(eventFd_->fd(), &val, sizeof(val));
      DCHECK(ret == 8);
    }
  } else {
    backend_.loopPoll();
  }
}

void IoUringEvent::runLoopCallback() noexcept {
  DVLOG(9) << "IoUringEvent::runLoopCallback";

  eventBase_->runBeforeLoop(this);

  // we have to run this code before we wait in case there are some submissions
  // that have not been flushed

  if (lastWasResignalled_ || io_uring_cq_ready(backend_.ioRingPtr())) {
    // definitely will get into the main callback
    return;
  }

  if (backend_.isWaitingToSubmit()) {
    backend_.submitOutstanding();
  } else {
    DVLOG(9) << "IoUringEvent::runLoopCallback nothing to run";
  }
}

} // namespace folly
#endif
