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

#include <exception>
#include <memory>
#include <mutex>
#include <vector>

#include <folly/experimental/pushmi/receiver/concepts.h>
#include <folly/experimental/pushmi/traits.h>
#include <folly/experimental/pushmi/executor/trampoline.h>
#include <folly/experimental/pushmi/detail/opt.h>
#include <folly/experimental/pushmi/o/extension_operators.h>

namespace folly {
namespace pushmi {

template <class... TN>
struct subject;

template <class PS, class... TN>
struct subject<PS, TN...> : single_sender_tag::with_values<TN...> {
  struct subject_shared : single_sender_tag::with_values<TN...> {
    using receiver_t = any_receiver<std::exception_ptr, TN...>;
    bool done_ = false;
    ::folly::pushmi::detail::opt<std::tuple<std::decay_t<TN>...>> t_;
    std::exception_ptr ep_;
    std::vector<receiver_t> receivers_;
    std::mutex lock_;

    PUSHMI_TEMPLATE(class Out)
    (requires ReceiveError<Out, std::exception_ptr> && ReceiveValue<Out, TN...>)
    void submit(Out out) {
      std::unique_lock<std::mutex> guard(lock_);
      if (ep_) {
        set_error(out, ep_);
        return;
      }
      if (!!t_ && done_) {
        auto args = *t_;
        ::folly::pushmi::apply(
          [&out](std::decay_t<TN>&&... ts) {
            set_value(out, (std::decay_t<TN>&&) ts...);
          },
          std::move(args)
        );
        return;
      }
      if (done_) {
        set_done(out);
        return;
      }
      receivers_.push_back(receiver_t{std::move(out)});
    }

    PUSHMI_TEMPLATE(class... VN)
    (requires sizeof...(VN) == sizeof...(TN)) //
    static constexpr bool AllConvertibleTo() noexcept {
      return And<ConvertibleTo<VN, std::decay_t<TN>>...>;
    }

    PUSHMI_TEMPLATE(class... VN)
    (requires subject_shared::AllConvertibleTo<VN...>())
    void value(VN&&... vn) {
      std::unique_lock<std::mutex> guard(lock_);
      t_ = std::tuple<std::decay_t<TN>...>{(VN&&) vn...};
      for (auto& out : receivers_) {
        ::folly::pushmi::apply(
          [&out](const std::decay_t<TN>&... ts) { set_value(out, ts...); },
          *t_
        );
      }
    }

    void error(std::exception_ptr e) noexcept {
      std::unique_lock<std::mutex> guard(lock_);
      ep_ = e;
      for (auto& out : receivers_) {
        set_error(out, e);
      }
      receivers_.clear();
    }

    void done() {
      std::unique_lock<std::mutex> guard(lock_);
      done_ = true;
      for (auto& out : receivers_) {
        set_done(out);
      }
      receivers_.clear();
    }
  };

  struct subject_receiver {
    using receiver_category = receiver_tag;

    std::shared_ptr<subject_shared> s;

    PUSHMI_TEMPLATE(class... VN)
    (requires And<SemiMovable<VN>...>)
    void value(VN&&... vn) {
      s->value((VN &&) vn...);
    }
    PUSHMI_TEMPLATE(class E)
    (requires SemiMovable<E>)
    void error(E e) noexcept {
      s->error(std::move(e));
    }
    void done() {
      s->done();
    }
  };

  std::shared_ptr<subject_shared> s = std::make_shared<subject_shared>();

  PUSHMI_TEMPLATE(class Out)
  (requires Receiver<Out>)
  void submit(Out out) {
    s->submit(std::move(out));
  }

  auto receiver() {
    return detail::receiver_from_fn<subject>{}(subject_receiver{s});
  }
};

} // namespace pushmi
} // namespace folly
