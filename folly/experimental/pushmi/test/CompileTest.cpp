/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/experimental/pushmi/sender/flow_single_sender.h>
#include <folly/experimental/pushmi/o/empty.h>
#include <folly/experimental/pushmi/o/extension_operators.h>
#include <folly/experimental/pushmi/o/from.h>
#include <folly/experimental/pushmi/o/just.h>
#include <folly/experimental/pushmi/o/on.h>
#include <folly/experimental/pushmi/o/submit.h>
#include <folly/experimental/pushmi/o/tap.h>
#include <folly/experimental/pushmi/o/transform.h>

using namespace folly::pushmi::aliases;

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace std::literals;

#if __cpp_deduction_guides >= 201703 && PUSHMI_NOT_ON_WINDOWS
#define MAKE(x) x MAKE_
#define MAKE_(...) \
  { __VA_ARGS__ }
#else
#define MAKE(x) make_##x
#endif

template <class...> struct tuple;
template <class...> struct variant;

template <class S>
using values_of_t = mi::sender_values_t<S, tuple, variant>;
template <class S>
using error_of_t = mi::sender_error_t<S, variant>;

using s0 = mi::single_sender_tag::no_values;
static_assert(std::is_same<values_of_t<s0>, variant<>>::value, "");
static_assert(std::is_same<error_of_t<s0>, variant<std::exception_ptr>>::value, "");

using s1 = mi::single_sender_tag::no_values::no_error;
static_assert(std::is_same<values_of_t<s1>, variant<>>::value, "");
static_assert(std::is_same<error_of_t<s1>, variant<>>::value, "");

using s2 = mi::single_sender_tag::with_values<int, int*>;
static_assert(std::is_same<values_of_t<s2>, variant<tuple<int, int*>>>::value, "");
static_assert(std::is_same<error_of_t<s2>, variant<std::exception_ptr>>::value, "");

using s3 = mi::single_sender_tag::with_values<int, int*>::no_error;
static_assert(std::is_same<values_of_t<s3>, variant<tuple<int, int*>>>::value, "");
static_assert(std::is_same<error_of_t<s3>, variant<>>::value, "");

using s4 = mi::single_sender_tag::with_values<int>::with_error<short>;
static_assert(std::is_same<values_of_t<s4>, variant<tuple<int>>>::value, "");
static_assert(std::is_same<error_of_t<s4>, variant<short>>::value, "");

using s5 = mi::single_sender_tag::with_values<int>::or_<char*>;
static_assert(std::is_same<values_of_t<s5>, variant<tuple<int>, tuple<char*>>>::value, "");
static_assert(std::is_same<error_of_t<s5>, variant<std::exception_ptr>>::value, "");

using s6 = mi::single_sender_tag::with_values<int>::or_<char*>::no_error;
static_assert(std::is_same<values_of_t<s6>, variant<tuple<int>, tuple<char*>>>::value, "");
static_assert(std::is_same<error_of_t<s6>, variant<>>::value, "");

using s7 = mi::single_sender_tag::with_values<int>::or_<char*>::with_error<short>;
static_assert(std::is_same<values_of_t<s7>, variant<tuple<int>, tuple<char*>>>::value, "");
static_assert(std::is_same<error_of_t<s7>, variant<short>>::value, "");

using s8 = mi::single_sender_tag::with_values<int>::or_<char*>::with_error<short>::or_<float>;
static_assert(std::is_same<values_of_t<s8>, variant<tuple<int>, tuple<char*>>>::value, "");
static_assert(std::is_same<error_of_t<s8>, variant<short, float>>::value, "");

using s9 = mi::single_sender_tag::with_values<int>::with_error<short>::or_<float>;
static_assert(std::is_same<values_of_t<s9>, variant<tuple<int>>>::value, "");
static_assert(std::is_same<error_of_t<s9>, variant<short, float>>::value, "");

using namespace testing;

void receiver_0_test() {
  auto out0 = mi::MAKE(receiver)();
  static_assert(mi::Receiver<decltype(out0)>, "out0 not a receiver");
  auto out1 = mi::MAKE(receiver)(mi::ignoreVF{});
  static_assert(mi::Receiver<decltype(out1)>, "out1 not a receiver");
  auto out2 = mi::MAKE(receiver)(mi::ignoreVF{}, mi::abortEF{});
  static_assert(mi::Receiver<decltype(out2)>, "out2 not a receiver");
  auto out3 = mi::MAKE(receiver)(mi::ignoreVF{}, mi::abortEF{}, mi::ignoreDF{});
  static_assert(mi::Receiver<decltype(out3)>, "out3 not a receiver");
  auto out4 = mi::MAKE(receiver)([](auto e) noexcept { e.get(); });
  static_assert(mi::Receiver<decltype(out4)>, "out4 not a receiver");
  auto out5 = mi::MAKE(receiver)(mi::on_value([]() {}));
  static_assert(mi::Receiver<decltype(out5)>, "out5 not a receiver");
  auto out6 = mi::MAKE(receiver)(mi::on_error(
      [](std::exception_ptr) noexcept {}, [](auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(out6)>, "out6 not a receiver");
  auto out7 = mi::MAKE(receiver)(mi::on_done([]() {}));
  static_assert(mi::Receiver<decltype(out7)>, "out7 not a receiver");

  using Out0 = decltype(out0);

  auto proxy0 = mi::MAKE(receiver)(out0);
  static_assert(mi::Receiver<decltype(proxy0)>, "proxy0 not a receiver");
  auto proxy1 = mi::MAKE(receiver)(out0, mi::passDVF{});
  static_assert(mi::Receiver<decltype(proxy1)>, "proxy1 not a receiver");
  auto proxy2 =
      mi::MAKE(receiver)(out0, mi::passDVF{}, mi::on_error(mi::passDEF{}));
  static_assert(mi::Receiver<decltype(proxy2)>, "proxy2 not a receiver");
  auto proxy3 =
      mi::MAKE(receiver)(out0, mi::passDVF{}, mi::passDEF{}, mi::passDDF{});
  static_assert(mi::Receiver<decltype(proxy3)>, "proxy3 not a receiver");
  auto proxy4 =
      mi::MAKE(receiver)(out0, [](Out0&) {}, mi::on_error([
                         ](Out0 & d, auto e) noexcept { d.error(e); }));
  static_assert(mi::Receiver<decltype(proxy4)>, "proxy4 not a receiver");
  auto proxy5 = mi::MAKE(receiver)(out0, mi::on_value([](Out0&) {}));
  static_assert(mi::Receiver<decltype(proxy5)>, "proxy5 not a receiver");
  auto proxy6 = mi::MAKE(receiver)(
      out0, mi::on_error([](Out0&, std::exception_ptr) noexcept {}, [
      ](Out0&, auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(proxy6)>, "proxy6 not a receiver");
  auto proxy7 = mi::MAKE(receiver)(out0, mi::on_done([](Out0&) {}));
  static_assert(mi::Receiver<decltype(proxy7)>, "proxy7 not a receiver");

  std::promise<void> p0;
  auto promise0 = mi::MAKE(receiver)(std::move(p0));
  promise0.done();

  std::promise<void> p1;

  auto any0 = mi::any_receiver<>(std::move(p1));
  auto any1 = mi::any_receiver<>(std::move(promise0));
  auto any2 = mi::any_receiver<>(out0);
  auto any3 = mi::any_receiver<>(proxy0);
}

void receiver_1_test() {
  auto out0 = mi::MAKE(receiver)();
  static_assert(mi::Receiver<decltype(out0)>, "out0 not a receiver");
  auto out1 = mi::MAKE(receiver)(mi::ignoreVF{});
  static_assert(mi::Receiver<decltype(out1)>, "out1 not a receiver");
  auto out2 = mi::MAKE(receiver)(mi::ignoreVF{}, mi::abortEF{});
  static_assert(mi::Receiver<decltype(out2)>, "out2 not a receiver");
  auto out3 = mi::MAKE(receiver)(mi::ignoreVF{}, mi::abortEF{}, mi::ignoreDF{});
  static_assert(mi::Receiver<decltype(out3)>, "out3 not a receiver");
  auto out4 = mi::MAKE(receiver)([](auto v) { v.get(); });
  static_assert(mi::Receiver<decltype(out4)>, "out4 not a receiver");
  auto out5 = mi::MAKE(receiver)(
      mi::on_value([](auto v) { v.get(); }, [](int) {}),
      mi::on_error([](std::exception_ptr) noexcept {}, [](auto e) noexcept {
        e.get();
      }));
  static_assert(mi::Receiver<decltype(out5)>, "out5 not a receiver");
  auto out6 = mi::MAKE(receiver)(mi::on_error(
      [](std::exception_ptr) noexcept {}, [](auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(out6)>, "out6 not a receiver");
  auto out7 = mi::MAKE(receiver)(mi::on_done([]() {}));
  static_assert(mi::Receiver<decltype(out7)>, "out7 not a receiver");

  using Out0 = decltype(out0);

  auto proxy0 = mi::MAKE(receiver)(out0);
  static_assert(mi::Receiver<decltype(proxy0)>, "proxy0 not a receiver");
  auto proxy1 = mi::MAKE(receiver)(out0, mi::passDVF{});
  static_assert(mi::Receiver<decltype(proxy1)>, "proxy1 not a receiver");
  auto proxy2 =
      mi::MAKE(receiver)(out0, mi::passDVF{}, mi::on_error(mi::passDEF{}));
  static_assert(mi::Receiver<decltype(proxy2)>, "proxy2 not a receiver");
  auto proxy3 =
      mi::MAKE(receiver)(out0, mi::passDVF{}, mi::passDEF{}, mi::passDDF{});
  static_assert(mi::Receiver<decltype(proxy3)>, "proxy3 not a receiver");
  auto proxy4 = mi::MAKE(receiver)(
      out0, [](auto d, auto v) { mi::set_value(d, v.get()); });
  static_assert(mi::Receiver<decltype(proxy4)>, "proxy4 not a receiver");
  auto proxy5 = mi::MAKE(receiver)(
      out0,
      mi::on_value([](Out0&, auto v) { v.get(); }, [](Out0&, int) {}),
      mi::on_error([](Out0&, std::exception_ptr) noexcept {}, [
      ](Out0&, auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(proxy5)>, "proxy5 not a receiver");
  auto proxy6 = mi::MAKE(receiver)(
      out0, mi::on_error([](Out0&, std::exception_ptr) noexcept {}, [
      ](Out0&, auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(proxy6)>, "proxy6 not a receiver");
  auto proxy7 = mi::MAKE(receiver)(out0, mi::on_done([](Out0&) {}));
  static_assert(mi::Receiver<decltype(proxy7)>, "proxy7 not a receiver");

  std::promise<int> p0;
  auto promise0 = mi::MAKE(receiver)(std::move(p0));
  promise0.value(0);

  std::promise<int> p1;

  auto any0 = mi::any_receiver<std::exception_ptr, int>(std::move(p1));
  auto any1 = mi::any_receiver<std::exception_ptr, int>(std::move(promise0));
  auto any2 = mi::any_receiver<std::exception_ptr, int>(out0);
  auto any3 = mi::any_receiver<std::exception_ptr, int>(proxy0);
}

void receiver_n_test() {
  auto out0 = mi::MAKE(receiver)();
  static_assert(mi::Receiver<decltype(out0)>, "out0 not a receiver");
  auto out1 = mi::MAKE(receiver)(mi::ignoreNF{});
  static_assert(mi::Receiver<decltype(out1)>, "out1 not a receiver");
  auto out2 = mi::MAKE(receiver)(mi::ignoreNF{}, mi::abortEF{});
  static_assert(mi::Receiver<decltype(out2)>, "out2 not a receiver");
  auto out3 = mi::MAKE(receiver)(mi::ignoreNF{}, mi::abortEF{}, mi::ignoreDF{});
  static_assert(mi::Receiver<decltype(out3)>, "out3 not a receiver");
  auto out4 = mi::MAKE(receiver)([](auto v) { v.get(); });
  static_assert(mi::Receiver<decltype(out4)>, "out4 not a receiver");
  auto out5 = mi::MAKE(receiver)(
      mi::on_value([](auto v) { v.get(); }, [](int) {}),
      mi::on_error([](std::exception_ptr) noexcept {}, [](auto e) noexcept {
        e.get();
      }));
  static_assert(mi::Receiver<decltype(out5)>, "out5 not a receiver");
  auto out6 = mi::MAKE(receiver)(mi::on_error(
      [](std::exception_ptr) noexcept {}, [](auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(out6)>, "out6 not a receiver");
  auto out7 = mi::MAKE(receiver)(mi::on_done([]() {}));
  static_assert(mi::Receiver<decltype(out7)>, "out7 not a receiver");

  using Out0 = decltype(out0);

  auto proxy0 = mi::MAKE(receiver)(out0);
  static_assert(mi::Receiver<decltype(proxy0)>, "proxy0 not a receiver");
  auto proxy1 = mi::MAKE(receiver)(out0, mi::passDVF{});
  static_assert(mi::Receiver<decltype(proxy1)>, "proxy1 not a receiver");
  auto proxy2 =
      mi::MAKE(receiver)(out0, mi::passDVF{}, mi::on_error(mi::passDEF{}));
  static_assert(mi::Receiver<decltype(proxy2)>, "proxy2 not a receiver");
  auto proxy3 =
      mi::MAKE(receiver)(out0, mi::passDVF{}, mi::passDEF{}, mi::passDDF{});
  static_assert(mi::Receiver<decltype(proxy3)>, "proxy3 not a receiver");
  auto proxy4 = mi::MAKE(receiver)(
      out0, [](auto d, auto v) { mi::set_value(d, v.get()); });
  static_assert(mi::Receiver<decltype(proxy4)>, "proxy4 not a receiver");
  auto proxy5 = mi::MAKE(receiver)(
      out0,
      mi::on_value([](Out0&, auto v) { v.get(); }, [](Out0&, int) {}),
      mi::on_error([](Out0&, std::exception_ptr) noexcept {}, [
      ](Out0&, auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(proxy5)>, "proxy5 not a receiver");
  auto proxy6 = mi::MAKE(receiver)(
      out0, mi::on_error([](Out0&, std::exception_ptr) noexcept {}, [
      ](Out0&, auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(proxy6)>, "proxy6 not a receiver");
  auto proxy7 = mi::MAKE(receiver)(out0, mi::on_done([](Out0&) {}));
  static_assert(mi::Receiver<decltype(proxy7)>, "proxy7 not a receiver");

  auto any0 = mi::any_receiver<std::exception_ptr, int>(out0);
  auto any1 = mi::any_receiver<std::exception_ptr, int>(proxy0);
}

void single_sender_test() {
  auto in0 = mi::MAKE(single_sender)();
  static_assert(mi::SingleSender<decltype(in0)>, "in0 not a sender");
  auto in1 = mi::MAKE(single_sender)(mi::ignoreSF{});
  static_assert(mi::SingleSender<decltype(in1)>, "in1 not a sender");
  auto in2 = mi::MAKE(single_sender)(
      [&](auto out) {
        in0.submit(mi::MAKE(receiver)(
            std::move(out),
            mi::on_value([](auto d, int v) { mi::set_value(d, v); })));
      });
  static_assert(mi::SingleSender<decltype(in2)>, "in2 not a sender");

  std::promise<int> p0;
  auto promise0 = mi::MAKE(receiver)(std::move(p0));
  in0 | ep::submit(std::move(promise0));

  auto out0 = mi::MAKE(receiver)();
  auto out1 = mi::MAKE(receiver)(
      out0, mi::on_value([](auto d, int v) { mi::set_value(d, v); }));
  in2.submit(out1);

  auto any0 = mi::any_single_sender<std::exception_ptr, int>(in0);
}

void many_sender_test() {
  auto in0 = mi::MAKE(sender)();
  static_assert(mi::Sender<decltype(in0)>, "in0 not a sender");
  auto in1 = mi::MAKE(sender)(mi::ignoreSF{});
  static_assert(mi::Sender<decltype(in1)>, "in1 not a sender");
  auto in2 = mi::MAKE(sender)(
      [&](auto out) {
        in0.submit(mi::MAKE(receiver)(
            std::move(out),
            mi::on_value([](auto d, int v) { mi::set_value(d, v); })));
      });
  static_assert(mi::Sender<decltype(in2)>, "in2 not a sender");

  auto out0 = mi::MAKE(receiver)();
  auto out1 = mi::MAKE(receiver)(
      out0, mi::on_value([](auto d, int v) { mi::set_value(d, v); }));
  in2.submit(out1);

  auto any0 = mi::any_sender<std::exception_ptr, int>(in0);
}

void flow_receiver_1_test() {
  auto out0 = mi::MAKE(flow_receiver)();
  static_assert(mi::Receiver<decltype(out0)>, "out0 not a receiver");
  auto out1 = mi::MAKE(flow_receiver)(mi::ignoreVF{});
  static_assert(mi::Receiver<decltype(out1)>, "out1 not a receiver");
  auto out2 = mi::MAKE(flow_receiver)(mi::ignoreVF{}, mi::abortEF{});
  static_assert(mi::Receiver<decltype(out2)>, "out2 not a receiver");
  auto out3 =
      mi::MAKE(flow_receiver)(mi::ignoreVF{}, mi::abortEF{}, mi::ignoreDF{});
  static_assert(mi::Receiver<decltype(out3)>, "out3 not a receiver");
  auto out4 = mi::MAKE(flow_receiver)([](auto v) { v.get(); });
  static_assert(mi::Receiver<decltype(out4)>, "out4 not a receiver");
  auto out5 = mi::MAKE(flow_receiver)(
      mi::on_value([](auto v) { v.get(); }, [](int) {}),
      mi::on_error([](std::exception_ptr) noexcept {}, [](auto e) noexcept {
        e.get();
      }));
  static_assert(mi::Receiver<decltype(out5)>, "out5 not a receiver");
  auto out6 = mi::MAKE(flow_receiver)(mi::on_error(
      [](std::exception_ptr) noexcept {}, [](auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(out6)>, "out6 not a receiver");
  auto out7 = mi::MAKE(flow_receiver)(mi::on_done([]() {}));
  static_assert(mi::Receiver<decltype(out7)>, "out7 not a receiver");

  auto out8 = mi::MAKE(flow_receiver)(
      mi::ignoreVF{}, mi::abortEF{}, mi::ignoreDF{}, mi::ignoreStrtF{});
  static_assert(mi::Receiver<decltype(out8)>, "out8 not a receiver");

  using Out0 = decltype(out0);

  auto proxy0 = mi::MAKE(flow_receiver)(out0);
  static_assert(mi::Receiver<decltype(proxy0)>, "proxy0 not a receiver");
  auto proxy1 = mi::MAKE(flow_receiver)(out0, mi::passDVF{});
  static_assert(mi::Receiver<decltype(proxy1)>, "proxy1 not a receiver");
  auto proxy2 = mi::MAKE(flow_receiver)(out0, mi::passDVF{}, mi::passDEF{});
  static_assert(mi::Receiver<decltype(proxy2)>, "proxy2 not a receiver");
  auto proxy3 = mi::MAKE(flow_receiver)(
      out0, mi::passDVF{}, mi::passDEF{}, mi::passDDF{});
  static_assert(mi::Receiver<decltype(proxy3)>, "proxy3 not a receiver");
  auto proxy4 = mi::MAKE(flow_receiver)(
      out0, [](auto d, auto v) { mi::set_value(d, v.get()); });
  static_assert(mi::Receiver<decltype(proxy4)>, "proxy4 not a receiver");
  auto proxy5 = mi::MAKE(flow_receiver)(
      out0,
      mi::on_value([](Out0&, auto v) { v.get(); }, [](Out0&, int) {}),
      mi::on_error([](Out0&, std::exception_ptr) noexcept {}, [
      ](Out0&, auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(proxy5)>, "proxy5 not a receiver");
  auto proxy6 = mi::MAKE(flow_receiver)(
      out0, mi::on_error([](Out0&, std::exception_ptr) noexcept {}, [
      ](Out0&, auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(proxy6)>, "proxy6 not a receiver");
  auto proxy7 = mi::MAKE(flow_receiver)(out0, mi::on_done([](Out0&) {}));
  static_assert(mi::Receiver<decltype(proxy7)>, "proxy7 not a receiver");

  auto proxy8 = mi::MAKE(flow_receiver)(
      out0, mi::passDVF{}, mi::passDEF{}, mi::passDDF{});
  static_assert(mi::Receiver<decltype(proxy8)>, "proxy8 not a receiver");

  auto any2 = mi::any_flow_receiver<
      std::exception_ptr,
      std::ptrdiff_t,
      std::exception_ptr,
      int>(out0);
  auto any3 = mi::any_flow_receiver<
      std::exception_ptr,
      std::ptrdiff_t,
      std::exception_ptr,
      int>(proxy0);
}

void flow_receiver_n_test() {
  auto out0 = mi::MAKE(flow_receiver)();
  static_assert(mi::Receiver<decltype(out0)>, "out0 not a receiver");
  auto out1 = mi::MAKE(flow_receiver)(mi::ignoreVF{});
  static_assert(mi::Receiver<decltype(out1)>, "out1 not a receiver");
  auto out2 = mi::MAKE(flow_receiver)(mi::ignoreVF{}, mi::abortEF{});
  static_assert(mi::Receiver<decltype(out2)>, "out2 not a receiver");
  auto out3 =
      mi::MAKE(flow_receiver)(mi::ignoreVF{}, mi::abortEF{}, mi::ignoreDF{});
  static_assert(mi::Receiver<decltype(out3)>, "out3 not a receiver");
  auto out4 = mi::MAKE(flow_receiver)([](auto v) { v.get(); });
  static_assert(mi::Receiver<decltype(out4)>, "out4 not a receiver");
  auto out5 = mi::MAKE(flow_receiver)(
      mi::on_value([](auto v) { v.get(); }, [](int) {}),
      mi::on_error([](std::exception_ptr) noexcept {}, [](auto e) noexcept {
        e.get();
      }));
  static_assert(mi::Receiver<decltype(out5)>, "out5 not a receiver");
  auto out6 = mi::MAKE(flow_receiver)(mi::on_error(
      [](std::exception_ptr) noexcept {}, [](auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(out6)>, "out6 not a receiver");
  auto out7 = mi::MAKE(flow_receiver)(mi::on_done([]() {}));
  static_assert(mi::Receiver<decltype(out7)>, "out7 not a receiver");

  auto out8 = mi::MAKE(flow_receiver)(
      mi::ignoreVF{}, mi::abortEF{}, mi::ignoreDF{}, mi::ignoreStrtF{});
  static_assert(mi::Receiver<decltype(out8)>, "out8 not a receiver");

  using Out0 = decltype(out0);

  auto proxy0 = mi::MAKE(flow_receiver)(out0);
  static_assert(mi::Receiver<decltype(proxy0)>, "proxy0 not a receiver");
  auto proxy1 = mi::MAKE(flow_receiver)(out0, mi::passDVF{});
  static_assert(mi::Receiver<decltype(proxy1)>, "proxy1 not a receiver");
  auto proxy2 = mi::MAKE(flow_receiver)(out0, mi::passDVF{}, mi::passDEF{});
  static_assert(mi::Receiver<decltype(proxy2)>, "proxy2 not a receiver");
  auto proxy3 = mi::MAKE(flow_receiver)(
      out0, mi::passDVF{}, mi::passDEF{}, mi::passDDF{});
  static_assert(mi::Receiver<decltype(proxy3)>, "proxy3 not a receiver");
  auto proxy4 = mi::MAKE(flow_receiver)(
      out0, [](auto d, auto v) { mi::set_value(d, v.get()); });
  static_assert(mi::Receiver<decltype(proxy4)>, "proxy4 not a receiver");
  auto proxy5 = mi::MAKE(flow_receiver)(
      out0,
      mi::on_value([](Out0&, auto v) { v.get(); }, [](Out0&, int) {}),
      mi::on_error([](Out0&, std::exception_ptr) noexcept {}, [
      ](Out0&, auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(proxy5)>, "proxy5 not a receiver");
  auto proxy6 = mi::MAKE(flow_receiver)(
      out0, mi::on_error([](Out0&, std::exception_ptr) noexcept {}, [
      ](Out0&, auto e) noexcept { e.get(); }));
  static_assert(mi::Receiver<decltype(proxy6)>, "proxy6 not a receiver");
  auto proxy7 = mi::MAKE(flow_receiver)(out0, mi::on_done([](Out0&) {}));
  static_assert(mi::Receiver<decltype(proxy7)>, "proxy7 not a receiver");

  auto proxy8 = mi::MAKE(flow_receiver)(
      out0, mi::passDVF{}, mi::passDEF{}, mi::passDDF{});
  static_assert(mi::Receiver<decltype(proxy8)>, "proxy8 not a receiver");

  auto any2 = mi::any_flow_receiver<
      std::exception_ptr,
      std::ptrdiff_t,
      std::exception_ptr,
      int>(out0);
  auto any3 = mi::any_flow_receiver<
      std::exception_ptr,
      std::ptrdiff_t,
      std::exception_ptr,
      int>(proxy0);
}

void flow_single_sender_test() {
  auto in0 = mi::MAKE(flow_single_sender)();
  static_assert(mi::SingleSender<decltype(in0)>, "in0 not a sender");
  static_assert(mi::FlowSender<decltype(in0)>, "in0 not flow");
  auto in1 = mi::MAKE(flow_single_sender)(mi::ignoreSF{});
  static_assert(mi::SingleSender<decltype(in1)>, "in1 not a sender");
  static_assert(mi::FlowSender<decltype(in1)>, "in1 not flow");
  auto in2 = mi::MAKE(flow_single_sender)(
      [&](auto out) {
        in0.submit(mi::MAKE(flow_receiver)(
            std::move(out),
            mi::on_value([](auto d, int v) { mi::set_value(d, v); })));
      });
  static_assert(mi::SingleSender<decltype(in2)>, "in2 not a sender");
  static_assert(mi::FlowSender<decltype(in2)>, "in2 not flow");

  auto out0 = mi::MAKE(flow_receiver)();
  auto out1 = mi::MAKE(flow_receiver)(
      out0, mi::on_value([](auto d, int v) { mi::set_value(d, v); }));
  in2.submit(out1);

  auto any0 =
      mi::any_flow_single_sender<std::exception_ptr, std::exception_ptr, int>(
          in0);
}

void flow_many_sender_test() {
  auto in0 = mi::MAKE(flow_sender)();
  static_assert(mi::Sender<decltype(in0)>, "in0 not a sender");
  static_assert(mi::FlowSender<decltype(in0)>, "in0 not flow");
  auto in1 = mi::MAKE(flow_sender)(mi::ignoreSF{});
  static_assert(mi::Sender<decltype(in1)>, "in1 not a sender");
  static_assert(mi::FlowSender<decltype(in1)>, "in1 not flow");
  auto in2 = mi::MAKE(flow_sender)(
      [&](auto out) {
        in0.submit(mi::MAKE(flow_receiver)(
            std::move(out),
            mi::on_value([](auto d, int v) { mi::set_value(d, v); })));
      });
  static_assert(mi::Sender<decltype(in2)>, "in2 not a sender");
  static_assert(mi::FlowSender<decltype(in2)>, "in2 not flow");

  auto out0 = mi::MAKE(flow_receiver)();
  auto out1 = mi::MAKE(flow_receiver)(
      out0, mi::on_value([](auto d, int v) { mi::set_value(d, v); }));
  in2.submit(out1);

  auto any0 = mi::any_flow_sender<
      std::exception_ptr,
      std::ptrdiff_t,
      std::exception_ptr,
      int>(in0);
}

TEST(CompileTest, Test) {}
