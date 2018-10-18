#include <vector>
#include <algorithm>
#include <cassert>
#include <iostream>

#include <folly/experimental/pushmi/o/just.h>
#include <folly/experimental/pushmi/o/tap.h>
#include <folly/experimental/pushmi/o/filter.h>
#include <folly/experimental/pushmi/o/transform.h>
#include <folly/experimental/pushmi/o/empty.h>

using namespace pushmi::aliases;

const bool setting_exists = false;

auto get_setting() {
  return mi::make_single_sender(
    [](auto out){
      if(setting_exists) {
        op::just(42) | op::submit(out);
      } else {
        op::empty<int>() | op::submit(out);
      }
    }
  );
}

auto println = [](auto v){std::cout << v << std::endl;};

// concat not yet implemented
template<class T, class E = std::exception_ptr>
auto concat =
  [](auto in){
    return mi::make_single_sender(
      [in](auto out) mutable {
        ::pushmi::submit(in, mi::make_receiver(out,
        [](auto out, auto v){
          ::pushmi::submit(v, mi::any_receiver<E, T>(out));
        }));
      });
  };

int main()
{
  get_setting() |
    op::transform([](int i){ return std::to_string(i); }) |
    op::submit(println);

  op::just(42) |
    op::filter([](int i){ return i < 42; }) |
    op::transform([](int i){ return std::to_string(i); }) |
    op::submit(println);

    op::just(42) |
      op::transform([](int i) {
        if (i < 42) {
          return mi::any_single_sender<std::exception_ptr, std::string>{op::empty<std::string>()};
        }
        return mi::any_single_sender<std::exception_ptr, std::string>{op::just(std::to_string(i))};
      }) |
      concat<std::string> |
      op::submit(println);

  std::cout << "OK" << std::endl;
}
