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

#include <folly/experimental/pushmi/executor/executor.h>
#include <folly/experimental/pushmi/sender/single_sender.h>
#include <folly/experimental/pushmi/sender/properties.h>
#include <folly/experimental/pushmi/executor/properties.h>

#include <queue>

namespace folly {
namespace pushmi {

template <class E, class Exec>
class strand_executor;

template <class E, class Exec>
struct strand_queue_receiver;

template <class E>
class strand_item {
 public:
  strand_item(any_receiver<E, any_executor_ref<E>> out)
      : what(std::move(out)) {}

  any_receiver<E, any_executor_ref<E>> what;
};
template <class E, class TP>
bool operator<(const strand_item<E>& l, const strand_item<E>& r) {
  return l.when < r.when;
}
template <class E, class TP>
bool operator>(const strand_item<E>& l, const strand_item<E>& r) {
  return l.when > r.when;
}
template <class E, class TP>
bool operator==(const strand_item<E>& l, const strand_item<E>& r) {
  return l.when == r.when;
}
template <class E, class TP>
bool operator!=(const strand_item<E>& l, const strand_item<E>& r) {
  return !(l == r);
}
template <class E, class TP>
bool operator<=(const strand_item<E>& l, const strand_item<E>& r) {
  return !(l > r);
}
template <class E, class TP>
bool operator>=(const strand_item<E>& l, const strand_item<E>& r) {
  return !(l < r);
}

template <class E>
class strand_queue_base
    : public std::enable_shared_from_this<strand_queue_base<E>> {
 public:
  std::mutex lock_;
  size_t owned_ = 0;
  size_t entered_ = 0;
  std::queue<strand_item<E>> items_;

  virtual ~strand_queue_base() {}

  strand_item<E>& front() {
    // :(
    return const_cast<strand_item<E>&>(this->items_.front());
  }

  virtual void dispatch() = 0;
};

template <class E, class Exec>
class strand_queue : public strand_queue_base<E> {
 public:
  ~strand_queue() {}
  strand_queue(Exec ex) : ex_(std::move(ex)) {}
  Exec ex_;

  void dispatch() override;

  auto shared_from_that() {
    return std::static_pointer_cast<strand_queue<E, Exec>>(
        this->shared_from_this());
  }

  template <class SubExec>
  void value(SubExec&&) {
    //
    // pull ready items from the queue in order.

    std::unique_lock<std::mutex> guard{this->lock_};

    ++this->entered_;

    // do not allow recursive queueing to block this executor
    auto remaining = this->items_.size();

    auto that = shared_from_that();
    auto subEx = strand_executor<E, Exec>{that};

    while (!this->items_.empty() && --remaining >= 0) {
      auto item{std::move(this->front())};
      this->items_.pop();
      guard.unlock();
      set_value(item.what, any_executor_ref<E>{subEx});
      set_done(item.what);
      guard.lock();
    }
  }
  template <class AE>
  void error(AE e) noexcept {
    //
    // propagate error result to ready items from the queue in order.

    std::unique_lock<std::mutex> guard{this->lock_};

    // do not allow recursive queueing to block this executor
    auto remaining = this->items_.size();

    while (!this->items_.empty() && --remaining >= 0) {
      auto what{std::move(this->front().what)};
      this->items_.pop();
      guard.unlock();
      set_error(what, folly::as_const(e));
      guard.lock();
    }

    // reset for next worker
    this->entered_ = 0;

    // exit worker when empty
    if (this->items_.empty()) {
      --this->owned_;
      return;
    }

    //
    // reschedule for new items in the queue

    auto that = shared_from_that();

    guard.unlock();
    submit(schedule(ex_), strand_queue_receiver<E, Exec>{that});
  }
  void done() {
    std::unique_lock<std::mutex> guard{this->lock_};

    if (this->entered_ == 0) {
      //
      // propagate empty result to ready items from the queue in order.

      // do not allow recursive queueing to block this executor
      auto remaining = this->items_.size();

      while (!this->items_.empty() && --remaining >= 0) {
        auto what{std::move(this->front().what)};
        this->items_.pop();
        guard.unlock();
        set_done(what);
        guard.lock();
      }
    }

    // reset for next worker
    this->entered_ = 0;

    // exit worker when empty
    if (this->items_.empty()) {
      --this->owned_;
      return;
    }

    //
    // reschedule for new items in the queue

    auto that = shared_from_that();

    guard.unlock();
    submit(schedule(ex_), strand_queue_receiver<E, Exec>{that});
  }
};

template <class E, class Exec>
struct strand_queue_receiver : std::shared_ptr<strand_queue<E, Exec>> {
  ~strand_queue_receiver() {}
  explicit strand_queue_receiver(std::shared_ptr<strand_queue<E, Exec>> that)
      : std::shared_ptr<strand_queue<E, Exec>>(that) {}
  using receiver_category = receiver_tag;
};

template <class E, class Exec>
void strand_queue<E, Exec>::dispatch() {
  submit(schedule(ex_), strand_queue_receiver<E, Exec>{shared_from_that()});
}

//
// strand is used to build a fifo single_executor from a concurrent
// single_executor.
//

template <class E, class Exec>
class strand_executor;

template <class E, class Exec>
class strand_task
: public single_sender_tag::with_values<any_executor_ref<E>>
    ::template with_error<E> {
  std::shared_ptr<strand_queue<E, Exec>> queue_;

 public:
  using properties = property_set<is_never_blocking<>>;

  strand_task(std::shared_ptr<strand_queue<E, Exec>> queue)
      : queue_(std::move(queue)) {}

  PUSHMI_TEMPLATE(class Out)
  (requires ReceiveValue<Out&, any_executor_ref<E>>&& ReceiveError<Out, E>) //
  void submit(Out out) {
    // queue for later
    std::unique_lock<std::mutex> guard{queue_->lock_};
    queue_->items_.push(any_receiver<E, any_executor_ref<E>>{std::move(out)});
    if (queue_->owned_ == 0) {
      ++queue_->owned_;
      // noone is minding the shop, send a worker
      guard.unlock();
      ::folly::pushmi::submit(
          ::folly::pushmi::schedule(queue_->ex_),
          strand_queue_receiver<E, Exec>{queue_});
    }
  }
};

template <class E, class Exec>
class strand_executor {
  std::shared_ptr<strand_queue<E, Exec>> queue_;

 public:
  using properties = property_set<is_fifo_sequence<>>;

  strand_executor(std::shared_ptr<strand_queue<E, Exec>> queue)
      : queue_(std::move(queue)) {}

  strand_task<E, Exec> schedule() {
    return {queue_};
  }
};

//
// the strand executor factory produces a new fifo ordered queue each time that
// it is called.
//

template <class E, class Exec>
class same_strand_factory_fn {
  Exec ex_;

 public:
  explicit same_strand_factory_fn(Exec ex) : ex_(std::move(ex)) {}
  auto make_strand() const {
    auto queue = std::make_shared<strand_queue<E, Exec>>(ex_);
    return strand_executor<E, Exec>{queue};
  }
};

PUSHMI_TEMPLATE(class E = std::exception_ptr, class Provider)
(requires ExecutorProvider<Provider>&&
         is_concurrent_sequence_v<executor_t<Provider>>) //
auto strands(Provider ep) {
  return same_strand_factory_fn<E, executor_t<Provider>>{get_executor(ep)};
}
PUSHMI_TEMPLATE(class E = std::exception_ptr, class Exec)
(requires Executor<Exec>&& is_concurrent_sequence_v<Exec>) //
auto strands(Exec ex) {
  return same_strand_factory_fn<E, Exec>{std::move(ex)};
}

} // namespace pushmi
} // namespace folly
