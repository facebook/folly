/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include <folly/io/async/EventUtil.h>
#include <folly/net/NetOps.h>
#include <folly/portability/Event.h>
#include <folly/portability/IOVec.h>

namespace folly {
class EventBase;

class EventReadCallback {
 public:
  struct IoVec {
    virtual ~IoVec() = default;
    using FreeFunc = void (*)(IoVec*);
    using CallbackFunc = void (*)(IoVec*, int);
    void* arg_{nullptr};
    struct iovec data_;
    FreeFunc freeFunc_{nullptr};
    CallbackFunc cbFunc_{nullptr};
  };

  EventReadCallback() = default;
  virtual ~EventReadCallback() = default;

  virtual IoVec* allocateData() = 0;
};

class EventRecvmsgCallback {
 public:
  struct MsgHdr {
    virtual ~MsgHdr() = default;
    using FreeFunc = void (*)(MsgHdr*);
    using CallbackFunc = void (*)(MsgHdr*, int);
    void* arg_{nullptr};
    struct msghdr data_;
    FreeFunc freeFunc_{nullptr};
    CallbackFunc cbFunc_{nullptr};
  };

  EventRecvmsgCallback() = default;
  virtual ~EventRecvmsgCallback() = default;

  virtual MsgHdr* allocateData() = 0;
};

struct EventCallback {
  enum class Type { TYPE_NONE = 0, TYPE_READ = 1, TYPE_RECVMSG = 2 };
  Type type_{Type::TYPE_NONE};
  union {
    EventReadCallback* readCb_;
    EventRecvmsgCallback* recvmsgCb_;
  };

  void set(EventReadCallback* cb) {
    type_ = Type::TYPE_READ;
    readCb_ = cb;
  }

  void set(EventRecvmsgCallback* cb) {
    type_ = Type::TYPE_RECVMSG;
    recvmsgCb_ = cb;
  }

  void reset() { type_ = Type::TYPE_NONE; }
};

class EventBaseEvent {
 public:
  EventBaseEvent() = default;
  ~EventBaseEvent() {
    if (userData_ && freeFn_) {
      freeFn_(userData_);
    }
  }

  EventBaseEvent(const EventBaseEvent&) = delete;
  EventBaseEvent& operator=(const EventBaseEvent&) = delete;

  typedef void (*FreeFunction)(void* userData);

  const struct event* getEvent() const { return &event_; }

  struct event* getEvent() {
    return &event_;
  }

  bool isEventRegistered() const {
    return EventUtil::isEventRegistered(&event_);
  }

  libevent_fd_t eb_ev_fd() const { return event_.ev_fd; }

  short eb_ev_events() const { return event_.ev_events; }

  int eb_ev_res() const { return event_.ev_res; }

  void* getUserData() { return userData_; }

  void setUserData(void* userData) { userData_ = userData; }

  void setUserData(void* userData, FreeFunction freeFn) {
    userData_ = userData;
    freeFn_ = freeFn;
  }

  void setCallback(EventReadCallback* cb) { cb_.set(cb); }

  void setCallback(EventRecvmsgCallback* cb) { cb_.set(cb); }

  void resetCallback() { cb_.reset(); }

  const EventCallback& getCallback() const { return cb_; }

  void eb_event_set(
      libevent_fd_t fd,
      short events,
      void (*callback)(libevent_fd_t, short, void*),
      void* arg) {
    event_set(&event_, fd, events, callback, arg);
  }

  void eb_signal_set(
      int signum, void (*callback)(libevent_fd_t, short, void*), void* arg) {
    event_set(&event_, signum, EV_SIGNAL | EV_PERSIST, callback, arg);
  }

  void eb_timer_set(void (*callback)(libevent_fd_t, short, void*), void* arg) {
    event_set(&event_, -1, 0, callback, arg);
  }

  void eb_ev_base(EventBase* evb);
  EventBase* eb_ev_base() const { return evb_; }

  int eb_event_base_set(EventBase* evb);

  int eb_event_add(const struct timeval* timeout);

  int eb_event_del();

  bool eb_event_active(int res);

  bool setEdgeTriggered();

 protected:
  struct event event_;
  EventBase* evb_{nullptr};
  void* userData_{nullptr};
  FreeFunction freeFn_{nullptr};
  EventCallback cb_;
};

class EventBaseBackendBase {
 public:
  using Event = EventBaseEvent;
  using FactoryFunc =
      std::function<std::unique_ptr<folly::EventBaseBackendBase>()>;

  EventBaseBackendBase() = default;
  virtual ~EventBaseBackendBase() = default;

  EventBaseBackendBase(const EventBaseBackendBase&) = delete;
  EventBaseBackendBase& operator=(const EventBaseBackendBase&) = delete;

  virtual event_base* getEventBase() = 0;
  virtual int eb_event_base_loop(int flags) = 0;
  virtual int eb_event_base_loopbreak() = 0;

  virtual int eb_event_add(Event& event, const struct timeval* timeout) = 0;
  virtual int eb_event_del(Event& event) = 0;

  virtual bool eb_event_active(Event& event, int res) = 0;
};

} // namespace folly
