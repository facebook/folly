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

#include <folly/io/async/TerminateCancellationToken.h>

#include <csignal>

#include <folly/Singleton.h>
#include <folly/io/async/AsyncSignalHandler.h>
#include <folly/io/async/ScopedEventBaseThread.h>

namespace folly {
namespace {
/**
 * A helper class that starts a new thread to listen for signals. It can issue
 * CancellationToken that can be used to schedule callback to execute on
 * receiving the signals.
 */
class ScopedTerminateSignalHandler : private AsyncSignalHandler {
 public:
  ScopedTerminateSignalHandler() : AsyncSignalHandler(nullptr) {
    attachEventBase(scopedEventBase_.getEventBase());
    // To prevent data race with unregisterSignalHandler
    scopedEventBase_.getEventBase()->runInEventBaseThreadAndWait([this]() {
      registerSignalHandler(SIGTERM);
      registerSignalHandler(SIGINT);
    });
  }

  CancellationToken getCancellationToken() {
    return cancellationSource_.getToken();
  }

 private:
  void signalReceived(int) noexcept override {
    // Unregister signal handler as we want to handle the signal only once
    unregisterSignalHandler(SIGTERM);
    unregisterSignalHandler(SIGINT);
    cancellationSource_.requestCancellation();
  }

  ScopedEventBaseThread scopedEventBase_;
  CancellationSource cancellationSource_;
};

folly::Singleton<ScopedTerminateSignalHandler> singletonSignalHandler;

} // namespace

CancellationToken getTerminateCancellationToken() {
  auto signalHandler = singletonSignalHandler.try_get();
  if (signalHandler == nullptr) {
    CancellationSource cs;
    cs.requestCancellation();
    return cs.getToken();
  }
  return signalHandler->getCancellationToken();
}

} // namespace folly
