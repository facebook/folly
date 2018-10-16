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

struct on_fn {
  PUSHMI_TEMPLATE(class ExecutorFactory)
    (requires Invocable<ExecutorFactory&>)
  auto operator()(ExecutorFactory ef) const {
    return constrain(lazy::Sender<_1>, [ef = std::move(ef)](auto in) {
      using In = decltype(in);
      return ::pushmi::detail::deferred_from<In, single<>>(
        std::move(in),
        ::pushmi::detail::submit_transform_out<In>(
          constrain(lazy::SenderTo<In, _2>, [ef](In& in, auto out) {
            auto exec = ef();
            ::pushmi::submit(exec, ::pushmi::now(exec),
              ::pushmi::make_single([in = in, out = std::move(out)](auto) mutable {
                ::pushmi::submit(in, std::move(out));
              })
            );
          }),
          constrain(lazy::TimeSenderTo<In, _3>, [ef](In& in, auto at, auto out) {
            auto exec = ef();
            ::pushmi::submit(exec, at,
              ::pushmi::on_value([in = in, at, out = std::move(out)](auto) mutable {
                ::pushmi::submit(in, at, std::move(out));
              })
            );
          })
        )
      );
    });
  }
};

} // namespace detail

namespace operators {

PUSHMI_INLINE_VAR constexpr detail::on_fn on{};

} // namespace operators

#if 0
namespace detail {

template <class ExecutorFactory>
class fsdon {
  using executor_factory_type = std::decay_t<ExecutorFactory>;

  executor_factory_type factory_;

  template <class In>
  class start_on {
    using in_type = std::decay_t<In>;

    executor_factory_type factory_;
    in_type in_;

    template <class Out, class Executor>
    class out_on {
      using out_type = std::decay_t<Out>;
      using exec_type = std::decay_t<Executor>;

      template <class Producer>
      struct producer_proxy {
        RefWrapper<Producer> up_;
        std::shared_ptr<std::atomic_bool> stopped_;
        exec_type exec_;

        producer_proxy(
            RefWrapper<Producer> p,
            std::shared_ptr<std::atomic_bool> stopped,
            exec_type exec)
            : up_(std::move(p)),
              stopped_(std::move(stopped)),
              exec_(std::move(exec)) {}

        template <class V>
        void value(V v) {
          auto up = wrap_ref(up_.get());
          exec_ |
              execute([up = std::move(up),
                       v = std::move(v),
                       stopped = std::move(stopped_)](auto) mutable {
                if (*stopped) {
                  return;
                }
                up.get().value(std::move(v));
              });
        }

        template <class E>
        void error(E e) {
          auto up = wrap_ref(up_.get());
          exec_ |
              execute([up = std::move(up),
                       e = std::move(e),
                       stopped = std::move(stopped_)](auto) mutable {
                if (*stopped) {
                  return;
                }
                up.get().error(std::move(e));
              });
        }
      };

      bool done_;
      std::shared_ptr<std::atomic_bool> stopped_;
      out_type out_;
      exec_type exec_;
      AnyNone<> upProxy_;

     public:
      out_on(out_type out, exec_type exec)
          : done_(false),
            stopped_(std::make_shared<std::atomic_bool>(false)),
            out_(std::move(out)),
            exec_(std::move(exec)),
            upProxy_() {}

      template <class T>
      void value(T t) {
        if (done_) {
          return;
        }
        done_ = true;
        out_.value(std::move(t));
      }

      template <class E>
      void error(E e) {
        if (done_) {
          return;
        }
        done_ = true;
        out_.error(std::move(e));
      }

      template <class Producer>
      void starting(RefWrapper<Producer> up) {
        upProxy_ =
            producer_proxy<Producer>{std::move(up), stopped_, std::move(exec_)};
        out_.starting(wrap_ref(upProxy_));
      }
    };

   public:
    start_on(executor_factory_type&& ef, in_type&& in)
        : factory_(std::move(ef)), in_(std::move(in)) {}

    template <class Out>
    auto then(Out out) {
      auto exec = factory_();
      auto myout = out_on<Out, decltype(exec)>{std::move(out), exec};
      exec | execute([in = in_, myout = std::move(myout)](auto) mutable {
        in.then(std::move(myout));
      });
    }
  };

 public:
  explicit fsdon(executor_factory_type&& ef) : factory_(std::move(ef)) {}

  template <class In>
  auto operator()(In in) {
    return start_on<In>{std::move(factory_), std::move(in)};
  }
};

} // namespace detail

namespace fsd {
template <class ExecutorFactory>
auto on(ExecutorFactory factory) {
  return detail::fsdon<ExecutorFactory>{std::move(factory)};
}
} // namespace fsd
#endif

} // namespace pushmi
