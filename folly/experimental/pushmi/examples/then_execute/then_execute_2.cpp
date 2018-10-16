#include <vector>
#include <algorithm>
#include <cassert>
#include <iostream>

#include <memory>
#include <utility>
#include <atomic>
#include <thread>
#include <functional>
#include <futures.h>
#include <futures_static_thread_pool.h>

#include <pool.h>

#include <request_via.h>
#include <share.h>

#include <pushmi/deferred.h>
#include <pushmi/single_deferred.h>
#include <pushmi/o/just.h>
#include <pushmi/o/transform.h>

using namespace pushmi::aliases;

struct inline_executor
{
public:
  friend bool operator==(
    const inline_executor&, const inline_executor&) noexcept { return true; }
  friend bool operator!=(
    const inline_executor&, const inline_executor&) noexcept { return false; }
  template<class Function>
  void execute(Function f) const noexcept { f(); }
  constexpr bool query(std::experimental::execution::oneway_t) { return true; }
  constexpr bool query(std::experimental::execution::twoway_t) { return false; }
  constexpr bool query(std::experimental::execution::single_t) { return true; }
};

namespace p1054 {
// A promise refers to a promise and is associated with a future, 
// either through type-erasure or through construction of an 
// underlying promise with an overload of make_promise_contract().

// make_promise_contract() cannot be written to produce a lazy future.
// the promise has to exist prior to .then() getting a continuation.
// there must be a shared allocation to connect the promise and future.
template <class T, class Executor>
std::pair<std::experimental::standard_promise<T>, std::experimental::standard_future<T, std::decay_t<Executor>>>
make_promise_contract(const Executor& e) {
    std::experimental::standard_promise<T> promise;
    auto ex = e;
    return {promise, promise.get_future(std::move(ex))};
}

template<class Executor, class Function, class Future>
    std::experimental::standard_future<
      std::result_of_t<Function(std::decay_t<typename std::decay_t<Future>::value_type>&&)>, std::decay_t<Executor>>
    then_execute(Executor&& e, Function&& f, Future&& pred) {
      using V = std::decay_t<typename std::decay_t<Future>::value_type>;
      using T = std::result_of_t<Function(V&&)>;
      auto pc = make_promise_contract<T>(e);
      auto p = std::get<0>(pc);
      auto r = std::get<1>(pc);
      ((Future&&)pred).then([e, p, f](V v) mutable {
        e.execute([p, f, v]() mutable {
           p.set_value(f(v));
        });
        return 0;
      });
      return r;
    }

}

namespace p1055 {

template<class Executor, class Function, class Future>
auto then_execute(Executor&& e, Function&& f, Future&& pred) {
    return pred | op::via([e](){return e;}) | op::transform([f](auto v){return f(v);});
}


}

int main()
{
  mi::pool p{std::max(1u,std::thread::hardware_concurrency())};

  std::experimental::futures_static_thread_pool sp{std::max(1u,std::thread::hardware_concurrency())};

  auto pc = p1054::make_promise_contract<int>(inline_executor{});
  auto& pr = std::get<0>(pc);
  auto& r = std::get<1>(pc);
  auto f = p1054::then_execute(sp.executor(), [](int v){return v*2;}, std::move(r));
  pr.set_value(42);
  f.get();

  p1055::then_execute(p.executor(), [](int v){return v*2;}, op::just(21)) | op::get<int>;

  sp.wait();
  p.wait();

  std::cout << "OK" << std::endl;
}

