#pragma once

// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <vector>

#include <pushmi/time_single_deferred.h>

namespace pushmi {

template<class... TN>
struct subject;

template<class T, class PS>
struct subject<T, PS> {

  using properties = property_set_insert_t<property_set<is_sender<>, is_single<>>, PS>;

  struct subject_shared {
    bool done_ = false;
    pushmi::detail::opt<T> t_;
    std::exception_ptr ep_;
    std::vector<any_single<T>> receivers_;
    std::mutex lock_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    void submit(Out out) {
      std::unique_lock<std::mutex> guard(lock_);
      if (ep_) {::pushmi::set_error(out, ep_); return;}
      if (!!t_) {::pushmi::set_value(out, (const T&)t_); return;}
      if (done_) {::pushmi::set_done(out); return;}
      receivers_.push_back(any_single<T>{out});
    }
    PUSHMI_TEMPLATE(class V)
      (requires SemiMovable<V>)
    void value(V&& v) {
      std::unique_lock<std::mutex> guard(lock_);
      t_ = detail::as_const(v);
      for (auto& out : receivers_) {::pushmi::set_value(out, (V&&) v);}
      receivers_.clear();
    }
    PUSHMI_TEMPLATE(class E)
      (requires SemiMovable<E>)
    void error(E e) noexcept {
      std::unique_lock<std::mutex> guard(lock_);
      ep_ = e;
      for (auto& out : receivers_) {::pushmi::set_error(out, std::move(e));}
      receivers_.clear();
    }
    void done() {
      std::unique_lock<std::mutex> guard(lock_);
      done_ = true;
      for (auto& out : receivers_) {::pushmi::set_done(out);}
      receivers_.clear();
    }
  };

  // need a template overload of none/deferred and the rest that stores a 'ptr' with its own lifetime management
  struct subject_receiver {

    using properties = property_set_insert_t<property_set<is_receiver<>, is_single<>>, PS>;

    std::shared_ptr<subject_shared> s;

    PUSHMI_TEMPLATE(class V)
      (requires SemiMovable<V>)
    void value(V&& v) {
      s->value((V&&) v);
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
    return detail::out_from_fn<subject>{}(subject_receiver{s});
  }
};

} // namespace pushmi
