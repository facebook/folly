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

#include <folly/experimental/TimerFD.h>
#ifdef FOLLY_HAVE_TIMERFD
#include <folly/FileUtil.h>
#include <sys/timerfd.h>
#endif

namespace folly {
#ifdef FOLLY_HAVE_TIMERFD
// TimerFD
TimerFD::TimerFD(folly::EventBase* eventBase)
    : TimerFD(eventBase, createTimerFd()) {}

TimerFD::TimerFD(folly::EventBase* eventBase, int fd)
    : folly::EventHandler(eventBase, NetworkSocket::fromFd(fd)), fd_(fd) {
  setEventCallback(this);
  if (fd_ > 0) {
    registerHandler(folly::EventHandler::READ | folly::EventHandler::PERSIST);
  }
}

TimerFD::~TimerFD() {
  cancel();
  close();
}

void TimerFD::close() {
  unregisterHandler();

  if (fd_ > 0) {
    detachEventBase();
    changeHandlerFD(NetworkSocket());
    ::close(fd_);
    fd_ = -1;
  }
}

void TimerFD::schedule(std::chrono::microseconds timeout) {
  // schedule(0) will stop the timer otherwise
  setTimer(timeout.count() ? timeout : std::chrono::microseconds(1));
}

void TimerFD::cancel() {
  setTimer(std::chrono::microseconds(0));
}

bool TimerFD::setTimer(std::chrono::microseconds useconds) {
  if (fd_ <= 0) {
    return false;
  }

  struct itimerspec val;
  val.it_interval = {0, 0};
  val.it_value.tv_sec =
      std::chrono::duration_cast<std::chrono::seconds>(useconds).count();
  val.it_value.tv_nsec =
      std::chrono::duration_cast<std::chrono::nanoseconds>(useconds).count() %
      1000000000LL;

  return (0 == ::timerfd_settime(fd_, 0, &val, nullptr));
}

void TimerFD::handlerReady(uint16_t events) noexcept {
  DestructorGuard dg(this);

  auto relevantEvents = uint16_t(events & folly::EventHandler::READ_WRITE);
  if (relevantEvents == folly::EventHandler::READ ||
      relevantEvents == folly::EventHandler::READ_WRITE) {
    uint64_t data = 0;
    ssize_t num = folly::readNoInt(fd_, &data, sizeof(data));
    if (num == sizeof(data)) {
      onTimeout();
    }
  }
}

void TimerFD::eventReadCallback(IoVec* ioVec, int res) {
  // reset it
  ioVec->timerData_ = 0;
  // save it for future use
  ioVecPtr_.reset(ioVec);

  if (res == sizeof(ioVec->timerData_)) {
    DestructorGuard dg(this);
    onTimeout();
  }
}

int TimerFD::createTimerFd() {
  return ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
}
#else
TimerFD::TimerFD(folly::EventBase* eventBase) : timeout_(eventBase, this) {}

TimerFD::~TimerFD() {
  // cancel has to be called from the derived classes !!!
}

void TimerFD::schedule(std::chrono::microseconds timeout) {
  timeout_.scheduleTimeoutHighRes(timeout);
}

void TimerFD::cancel() {
  timeout_.cancelTimeout();
}

TimerFD::TimerFDAsyncTimeout::TimerFDAsyncTimeout(
    folly::EventBase* eventBase,
    TimerFD* timerFd)
    : folly::AsyncTimeout(eventBase), timerFd_(timerFd) {}

void TimerFD::TimerFDAsyncTimeout::timeoutExpired() noexcept {
  timerFd_->onTimeout();
}
#endif
} // namespace folly
