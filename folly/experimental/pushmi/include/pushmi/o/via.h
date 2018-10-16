#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../piping.h"
#include "../executor.h"
#include "extension_operators.h"

namespace pushmi {

namespace detail {

template<class Executor, class Out>
struct via_fn_data : public Out {
  Executor exec;

  via_fn_data(Out out, Executor exec) :
    Out(std::move(out)), exec(std::move(exec)) {}
};

template<class Out, class Executor>
auto make_via_fn_data(Out out, Executor ex) -> via_fn_data<Executor, Out> {
  return {std::move(out), std::move(ex)};
}

struct via_fn {
private:
  template <class Out>
  struct on_value_impl {
    template <class V>
    struct impl {
      V v_;
      Out out_;
      void operator()(any) {
        ::pushmi::set_value(out_, std::move(v_));
      }
    };
    template <class Data, class V>
    void operator()(Data& data, V&& v) const {
      ::pushmi::submit(
        data.exec,
        ::pushmi::now(data.exec),
        ::pushmi::make_single(
          impl<std::decay_t<V>>{(V&&) v, std::move(static_cast<Out&>(data))}
        )
      );
    }
  };
  template <class Out>
  struct on_error_impl {
    template <class E>
    struct impl {
      E e_;
      Out out_;
      void operator()(any) {
        ::pushmi::set_error(out_, std::move(e_));
      }
    };
    template <class Data, class E>
    void operator()(Data& data, E e) const noexcept {
      ::pushmi::submit(
        data.exec,
        ::pushmi::now(data.exec),
        ::pushmi::make_single(
          impl<E>{std::move(e), std::move(static_cast<Out&>(data))}
        )
      );
    }
  };
  template <class Out>
  struct on_done_impl {
    struct impl {
      Out out_;
      void operator()(any) {
        ::pushmi::set_done(out_);
      }
    };
    template <class Data>
    void operator()(Data& data) const {
      ::pushmi::submit(
        data.exec,
        ::pushmi::now(data.exec),
        ::pushmi::make_single(
          impl{std::move(static_cast<Out&>(data))}
        )
      );
    }
  };
  template <class In, class ExecutorFactory>
  struct executor_impl {
    ExecutorFactory ef_;
    template <class Data>
    auto operator()(Data& data) const {
      return ef_();
    }
  };
  template <class In, class ExecutorFactory>
  struct out_impl {
    ExecutorFactory ef_;
    PUSHMI_TEMPLATE(class Out)
      (requires Receiver<Out>)
    auto operator()(Out out) const {
      auto exec = ef_();
      return ::pushmi::detail::receiver_from_fn<In>()(
        make_via_fn_data(std::move(out), std::move(exec)),
        ::pushmi::on_value(on_value_impl<Out>{}),
        ::pushmi::on_error(on_error_impl<Out>{}),
        ::pushmi::on_done(on_done_impl<Out>{})
      );
    }
  };
  template <class ExecutorFactory>
  struct in_impl {
    ExecutorFactory ef_;
    PUSHMI_TEMPLATE (class In)
      (requires Sender<In>)
    auto operator()(In in) const {
      return ::pushmi::detail::sender_from(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(
          out_impl<In, ExecutorFactory>{ef_}
        ),
        ::pushmi::on_executor(executor_impl<In, ExecutorFactory>{ef_})
      );
    }
  };
public:
  PUSHMI_TEMPLATE(class ExecutorFactory)
    (requires Invocable<ExecutorFactory&>)
  auto operator()(ExecutorFactory ef) const {
    return in_impl<ExecutorFactory>{std::move(ef)};
  }
};

} // namespace detail

namespace operators {
PUSHMI_INLINE_VAR constexpr detail::via_fn via{};
} // namespace operators

} // namespace pushmi
