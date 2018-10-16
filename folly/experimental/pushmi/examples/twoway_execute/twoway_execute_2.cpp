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

#include <pool.h>

#include <pushmi/deferred.h>
#include <pushmi/single_deferred.h>
#include <pushmi/o/transform.h>

using namespace pushmi::aliases;

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

template<class Executor, class Function>
    std::experimental::standard_future<std::result_of_t<std::decay_t<Function>()>, std::decay_t<Executor>>
    twoway_execute(Executor&& e, Function&& f) {
      using T = std::result_of_t<std::decay_t<Function>()>;
      auto pc = make_promise_contract<T>(e);
      auto p = std::get<0>(pc);
      auto r = std::get<1>(pc);
      e.execute([p, f]() mutable {p.set_value(f());});
      return r;
    }
}

namespace p1055 {

template<class Executor, class Function>
auto twoway_execute(Executor&& e, Function&& f) {
  return e | op::transform([f](auto){return f();});
}

}

int main()
{
  mi::pool p{std::max(1u,std::thread::hardware_concurrency())};

  std::experimental::static_thread_pool sp{std::max(1u,std::thread::hardware_concurrency())};

  p1054::twoway_execute(sp.executor(), [](){return 42;}).get();

  p1055::twoway_execute(p.executor(), [](){return 42;}) | op::get<int>;

  sp.wait();
  p.wait();

  std::cout << "OK" << std::endl;
}
