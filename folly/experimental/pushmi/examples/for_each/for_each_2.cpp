#include <vector>
#include <algorithm>
#include <cassert>
#include <iostream>

#include <pool.h>
#include <for_each.h>

using namespace pushmi::aliases;

template<class Executor, class Allocator = std::allocator<char>>
auto naive_executor_bulk_target(Executor e, Allocator a = Allocator{}) {
  return [e, a](
      auto init,
      auto selector,
      auto input,
      auto&& func,
      auto sb,
      auto se,
      auto out) {
        using RS = decltype(selector);
        using F = std::conditional_t<
          std::is_lvalue_reference<decltype(func)>::value,
          decltype(func),
          typename std::remove_reference<decltype(func)>::type>;
        using Out = decltype(out);
        try {
          typename std::allocator_traits<Allocator>::template rebind_alloc<char> allocState(a);
          auto shared_state = std::allocate_shared<
            std::tuple<
              std::exception_ptr, // first exception
              Out, // destination
              RS, // selector
              F, // func
              std::atomic<decltype(init(input))>, // accumulation
              std::atomic<std::size_t>, // pending
              std::atomic<std::size_t> // exception count (protects assignment to first exception)
            >>(allocState, std::exception_ptr{}, std::move(out), std::move(selector), std::move(func), init(std::move(input)), 1, 0);
          e | op::submit([e, sb, se, shared_state](auto ){
            auto stepDone = [](auto shared_state){
              // pending
              if (--std::get<5>(*shared_state) == 0) {
                // first exception
                if (std::get<0>(*shared_state)) {
                  mi::set_error(std::get<1>(*shared_state), std::get<0>(*shared_state));
                  return;
                }
                try {
                  // selector(accumulation)
                  auto result = std::get<2>(*shared_state)(std::move(std::get<4>(*shared_state).load()));
                  mi::set_value(std::get<1>(*shared_state), std::move(result));
                } catch(...) {
                  mi::set_error(std::get<1>(*shared_state), std::current_exception());
                }
              }
            };
            for (decltype(sb) idx{sb}; idx != se; ++idx, ++std::get<5>(*shared_state)){
                e | op::submit([shared_state, idx, stepDone](auto ex){
                  try {
                    // this indicates to me that bulk is not the right abstraction
                    auto old = std::get<4>(*shared_state).load();
                    auto step = old;
                    do {
                      step = old;
                      // func(accumulation, idx)
                      std::get<3>(*shared_state)(step, idx);
                    } while(!std::get<4>(*shared_state).compare_exchange_strong(old, step));
                  } catch(...) {
                    // exception count
                    if (std::get<6>(*shared_state)++ == 0) {
                      // store first exception
                      std::get<0>(*shared_state) = std::current_exception();
                    } // else eat the exception
                  }
                  stepDone(shared_state);
                });
            }
            stepDone(shared_state);
          });
        } catch(...) {
          e | op::submit([out = std::move(out), ep = std::current_exception()]() mutable {
            mi::set_error(out, ep);
          });
        }
    };
}

int main()
{
  mi::pool p{std::max(1u,std::thread::hardware_concurrency())};

  std::vector<int> vec(10);

  mi::for_each(naive_executor_bulk_target(p.executor()), vec.begin(), vec.end(), [](int& x){
    x = 42;
  });

  assert(std::count(vec.begin(), vec.end(), 42) == static_cast<int>(vec.size()));

  std::cout << "OK" << std::endl;

  p.wait();
}
