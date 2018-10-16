#include <vector>
#include <algorithm>
#include <cassert>
#include <iostream>

#include <pushmi/o/just.h>
#include <pushmi/o/error.h>
#include <pushmi/o/empty.h>
#include <pushmi/o/transform.h>
#include <pushmi/o/switch_on_error.h>
#include <no_fail.h>

using namespace pushmi::aliases;

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
  auto stop_abort = mi::on_error([](auto) noexcept {});
  // support all error value types

  op::error(std::exception_ptr{}) |
    op::submit(stop_abort);

  op::error(std::errc::argument_list_too_long) |
    op::submit(stop_abort);

// transform an error

  op::error(std::errc::argument_list_too_long) |
    op::switch_on_error([](auto e) noexcept { return op::error(std::exception_ptr{}); }) |
    op::submit(stop_abort);

// use default value if an error occurs

  op::just(42) |
    op::switch_on_error([](auto e) noexcept { return op::just(0); }) |
    op::submit();

// suppress if an error occurs

  op::error(std::errc::argument_list_too_long) |
    op::switch_on_error([](auto e) noexcept { return op::empty(); }) |
    op::submit();

// abort if an error occurs

  op::just(42) |
    op::no_fail() |
    op::submit();

// transform value to error_

  op::just(42) |
    op::transform([](auto v) {
      using r_t = mi::any_single_sender<std::exception_ptr, int>;
      if (v < 40) {
        return r_t{op::error<int>(std::exception_ptr{})};
      } else {
        return r_t{op::just(v)};
      }
    }) |
    concat<int> |
    op::submit();

// retry on error

  // http.get(ex) |
  //   op::timeout(ex, 1s) |
  //   op::switch_on_error([](auto e) noexcept { return op::timer(ex, 1s); }) |
  //   op::repeat() |
  //   op::timeout(ex, 10s) |
  //   op::submit();

  std::cout << "OK" << std::endl;
}
