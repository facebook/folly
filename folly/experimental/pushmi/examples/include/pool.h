#pragma once

// Copyright (c) 2018-present, Facebook, Inc.
//
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

#include <experimental/thread_pool>

#include <pushmi/executor.h>
#include <pushmi/trampoline.h>
#include <pushmi/time_single_sender.h>

#if __cpp_deduction_guides >= 201703
#define MAKE(x) x MAKE_
#define MAKE_(...) {__VA_ARGS__}
#else
#define MAKE(x) make_ ## x
#endif

namespace pushmi {

using std::experimental::static_thread_pool;
namespace execution = std::experimental::execution;

template<class Executor>
struct pool_time_executor {
  using properties = property_set<is_time<>, is_executor<>, is_single<>>;

  using e_t = Executor;
  e_t e;
  explicit pool_time_executor(e_t e) : e(std::move(e)) {}
  auto now() {
    return std::chrono::system_clock::now();
  }
  auto executor() { return *this; }
  PUSHMI_TEMPLATE(class TP, class Out)
    (requires Regular<TP> && Receiver<Out>)
  void submit(TP at, Out out) const {
    e.execute([e = *this, at = std::move(at), out = std::move(out)]() mutable {
      std::this_thread::sleep_until(at);
      ::pushmi::set_value(out, e);
    });
  }
};

class pool {
  static_thread_pool p;
public:

  inline explicit pool(std::size_t threads) : p(threads) {}

  inline auto executor() {
    auto exec = execution::require(p.executor(), execution::never_blocking, execution::oneway);
    return pool_time_executor<decltype(exec)>{exec};
  }

  inline void stop() {p.stop();}
  inline void wait() {p.wait();}
};

} // namespace pushmi
