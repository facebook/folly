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

#include <folly/Singleton.h>
#include <folly/experimental/io/IoUringEvent.h>
#include <folly/experimental/io/IoUringEventBaseLocal.h>
#include <folly/io/async/EventBaseLocal.h>

#if FOLLY_HAS_LIBURING

namespace folly {

namespace {
struct Tag {};
Singleton<EventBaseLocal<std::unique_ptr<IoUringEvent>>, Tag> singleton;

void detach(EventBase* evb) {
  auto local = singleton.try_get();
  if (!local) {
    return;
  }
  local->erase(*evb);
}

} // namespace

IoUringBackend* IoUringEventBaseLocal::try_get(EventBase* evb) {
  auto local = singleton.try_get();
  if (!local) {
    throw std::runtime_error("EventBaseLocal is already destructed");
  }
  std::unique_ptr<IoUringEvent>* ptr = local->get(*evb);
  if (!ptr) {
    return nullptr;
  }
  return &ptr->get()->backend();
}

void IoUringEventBaseLocal::attach(
    EventBase* evb, IoUringBackend::Options const& options, bool use_eventfd) {
  evb->dcheckIsInEventBaseThread();

  // We tie into event base callbacks which need to be cleaned up earlier than
  // locals are destroyed, so do this manually
  evb->runOnDestruction([evb]() { detach(evb); });

  auto local = singleton.try_get();
  if (!local) {
    throw std::runtime_error("EventBaseLocal is already destructed");
  }
  if (try_get(evb)) {
    throw std::runtime_error(
        "this event base already has a local io_uring attached");
  }
  local->emplace(
      *evb, std::make_unique<IoUringEvent>(evb, options, use_eventfd));
}

} // namespace folly

#endif
