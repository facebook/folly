#pragma once
// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include "../piping.h"
#include "../executor.h"
#include "extension_operators.h"

namespace pushmi {

namespace operators {

namespace detail {

class via_fn {
  template <class ExecutorFactory>
  auto operator()(ExecutorFactory ef) const;
};

template<class Executor, class Out>
struct via_fn_data : public Out {
  Executor exec;

  via_fn_data(Out out, Executor exec) :
    Out(std::move(out)), exec(std::move(exec)) {}
};

template<class Executor, class Out>
via_fn_data(Out, Executor) -> via_fn_data<Executor, Out>;

template <class ExecutorFactory>
auto via_fn::operator()(ExecutorFactory ef) const {
  return [ef = std::move(ef)]<class In>(In in) {
    return ::pushmi::detail::deferred_from<In, archetype_single>(
      std::move(in),
      ::pushmi::detail::submit_transform_out<In>(
        [ef]<class Out>(Out out) {
          auto exec = ef();
          return ::pushmi::detail::out_from<In>(
            via_fn_data{std::move(out), std::move(exec)},
            // copy 'f' to allow multiple calls to submit
            ::pushmi::on_value{[]<class V>(auto& data, V&& v){
              ::pushmi::submit(
                  data.exec,
                  ::pushmi::now(data.exec),
                  ::pushmi::single([v = (V&&)v, out = std::move(static_cast<Out&>(data))](auto) mutable {
                    ::pushmi::set_value(out, std::move(v));
                  }));
            }},
            ::pushmi::on_error{[](auto& data, auto e) noexcept {
              ::pushmi::submit(
                  data.exec,
                  ::pushmi::now(data.exec),
                  ::pushmi::single([e = std::move(e), out = std::move(static_cast<Out&>(data))](auto) mutable {
                    ::pushmi::set_error(out, std::move(e));
                  }));
            }},
            ::pushmi::on_done{[](auto& data){
              ::pushmi::submit(
                  data.exec,
                  ::pushmi::now(data.exec),
                  ::pushmi::single([out = std::move(static_cast<Out&>(data))](auto) mutable {
                    ::pushmi::set_done(out);
                  }));
            }}
          );
        }
      )
    );
  };
}

} // namespace detail

inline constexpr detail::via_fn via{};

} // namespace operators

#if 0

namespace detail {

template <class ExecutorFactory>
class fsdvia {
  using executor_factory_type = std::decay_t<ExecutorFactory>;

  executor_factory_type factory_;

  template <class In>
  class start_via {
    using in_type = std::decay_t<In>;

    executor_factory_type factory_;
    in_type in_;

    template <class Out, class Executor>
    class out_via {
      using out_type = std::decay_t<Out>;
      using executor_type = std::decay_t<Executor>;

      struct shared_type {
        shared_type(out_type&& out) : out_(std::move(out)), stopped_(false) {}
        out_type out_;
        std::atomic_bool stopped_;
      };

      template <class Producer>
      struct producer_proxy {
        RefWrapper<Producer> up_;
        std::shared_ptr<shared_type> shared_;

        producer_proxy(RefWrapper<Producer> p, std::shared_ptr<shared_type> s)
            : up_(std::move(p)), shared_(std::move(s)) {}

        template <class V>
        void value(V v) {
          if (!!shared_->stopped_.exchange(true)) {
            return;
          }
          up_.get().value(std::move(v));
        }

        template <class E>
        void error(E e) {
          if (!!shared_->stopped_.exchange(true)) {
            return;
          }
          up_.get().error(std::move(e));
        }
      };

      bool done_;
      std::shared_ptr<shared_type> shared_;
      executor_type exec_;
      std::shared_ptr<AnyNone<>> upProxy_;

     public:
      explicit out_via(out_type&& out, executor_type&& exec)
          : done_(false),
            shared_(std::make_shared<shared_type>(std::move(out))),
            exec_(std::move(exec)),
            upProxy_() {}

      template <class T>
      void value(T t) {
        if (done_ || shared_->stopped_) {
          done_ = true;
          return;
        }
        if (!upProxy_) {
          std::abort();
        }
        done_ = true;
        exec_ | execute([t = std::move(t), shared = shared_](auto) mutable {
          shared->out_.value(std::move(t));
        });
      }

      template <class E>
      void error(E e) {
        if (done_ || shared_->stopped_) {
          done_ = true;
          return;
        }
        if (!upProxy_) {
          std::abort();
        }
        done_ = true;
        exec_ | execute([e = std::move(e), shared = shared_](auto) mutable {
          shared->out_.error(std::move(e));
        });
      }

      void stopping() {
        if (done_) {
          return;
        }
        if (!upProxy_) {
          std::abort();
        }
        done_ = true;
        if (!shared_->stopped_.exchange(true)) {
          exec_ |
              // must keep out and upProxy alive until out is notified that it
              // is unsafe
              execute([shared = shared_](auto) mutable {
                shared->out_.stopping();
              });
        }
      }

      template <class Producer>
      void starting(RefWrapper<Producer> up) {
        if (!!upProxy_) {
          std::abort();
        }
        upProxy_ = std::make_shared<AnyNone<>>(AnyNone<>{
            producer_proxy<Producer>{std::move(up), shared_}});
        // must keep out and upProxy alive until out is notified that it is
        // starting
        exec_ | execute([shared = shared_, upProxy = upProxy_](auto) mutable {
          shared->out_.starting(wrap_ref(*upProxy));
        });
      }
    };

   public:
    start_via(executor_factory_type&& ef, in_type&& in)
        : factory_(ef), in_(in) {}

    template <class Out>
    auto then(Out out) {
      auto exec = factory_();
      in_.then(out_via<Out, decltype(exec)>{std::move(out), std::move(exec)});
    }
  };

 public:
  explicit fsdvia(executor_factory_type&& ef) : factory_(std::move(ef)) {}

  template <class In>
  auto operator()(In in) {
    return start_via<In>{std::move(factory_), std::move(in)};
  }
};

} // namespace detail

namespace fsd {

template <class ExecutorFactory>
auto via(ExecutorFactory factory) {
  return detail::fsdvia<ExecutorFactory>{std::move(factory)};
}

} // namespace fsd
#endif

} // namespace pushmi
