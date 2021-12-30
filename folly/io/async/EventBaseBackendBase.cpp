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

#include <folly/io/async/EventBaseBackendBase.h>

#include <folly/io/async/EventBase.h>

#if defined(__linux__) && !FOLLY_MOBILE
#define FOLLY_USE_EPOLLET

#include <sys/epoll.h>

struct event_base {
  void* evsel;
  void* evbase;
};

struct epollop {
  void* fds;
  int nfds;
  void* events;
  int nevents;
  int epfd;
};
#endif

namespace folly {
void EventBaseEvent::eb_ev_base(EventBase* evb) {
  evb_ = evb;
  event_.ev_base = evb ? evb->getLibeventBase() : nullptr;
}

int EventBaseEvent::eb_event_base_set(EventBase* evb) {
  evb_ = evb;
  auto* base = evb_ ? (evb_->getLibeventBase()) : nullptr;
  if (base) {
    return ::event_base_set(base, &event_);
  }

  return 0;
}

int EventBaseEvent::eb_event_add(const struct timeval* timeout) {
  auto* backend = evb_ ? (evb_->getBackend()) : nullptr;
  if (backend) {
    return backend->eb_event_add(*this, timeout);
  }

  return -1;
}

int EventBaseEvent::eb_event_del() {
  auto* backend = evb_ ? (evb_->getBackend()) : nullptr;
  if (backend) {
    return backend->eb_event_del(*this);
  }

  return -1;
}

bool EventBaseEvent::eb_event_active(int res) {
  auto* backend = evb_ ? (evb_->getBackend()) : nullptr;
  if (backend) {
    return backend->eb_event_active(*this, res);
  }

  return false;
}

bool EventBaseEvent::setEdgeTriggered() {
#ifdef FOLLY_USE_EPOLLET
  // Until v2 libevent doesn't expose API to set edge-triggered flag for events.
  // If epoll backend is used by libevent, we can enable it though epoll_ctl
  // directly.
  // Note that this code depends on internal event_base and epollop layout, so
  // we have to validate libevent version.
  static const bool supportedVersion =
      !strcmp(event_get_version(), "1.4.14b-stable");
  if (!supportedVersion) {
    return false;
  }
  auto* base = evb_ ? (evb_->getLibeventBase()) : nullptr;
  if (!base || strcmp(event_base_get_method(base), "epoll")) {
    return false;
  }

  auto epfd = static_cast<epollop*>(base->evbase)->epfd;
  epoll_event epev = {0, {0}};
  epev.data.fd = eb_ev_fd();
  epev.events = EPOLLET;
  if (eb_ev_events() & EV_READ) {
    epev.events |= EPOLLIN;
  }
  if (eb_ev_events() & EV_WRITE) {
    epev.events |= EPOLLOUT;
  }
  if (::epoll_ctl(epfd, EPOLL_CTL_MOD, eb_ev_fd(), &epev) == -1) {
    LOG(DFATAL) << "epoll_ctl failed: " << errno;
    return false;
  }
  return true;
#else
  return false;
#endif
}
} // namespace folly
