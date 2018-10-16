#include "pushmi.h"

using namespace pushmi::aliases;

using namespace std::literals;

#if __cpp_deduction_guides >= 201703
#define MAKE(x) x MAKE_
#define MAKE_(...) {__VA_ARGS__}
#else
#define MAKE(x) make_ ## x
#endif


void receiver_0_test() {
  auto out0 = pushmi::MAKE(receiver)();
  auto out1 = pushmi::MAKE(receiver)(pushmi::ignoreVF{});
  auto out2 = pushmi::MAKE(receiver)(pushmi::ignoreVF{}, pushmi::abortEF{});
  auto out3 = pushmi::MAKE(receiver)(pushmi::ignoreVF{}, pushmi::abortEF{}, pushmi::ignoreDF{});
  auto out4 = pushmi::MAKE(receiver)([](auto e) noexcept{ e.get(); });
  auto out5 = pushmi::MAKE(receiver)(
      pushmi::on_value(
        []() { }
      ));
  auto out6 = pushmi::MAKE(receiver)(
      pushmi::on_error(
        [](std::exception_ptr e) noexcept {},
        [](auto e) noexcept { e.get(); }
      ));
  auto out7 = pushmi::MAKE(receiver)(
      pushmi::on_done([]() {  }));

  using Out0 = decltype(out0);

  auto proxy0 = pushmi::MAKE(receiver)(out0);
  auto proxy1 = pushmi::MAKE(receiver)(out0, pushmi::passDVF{});
  auto proxy2 = pushmi::MAKE(receiver)(out0, pushmi::passDVF{}, pushmi::on_error(pushmi::passDEF{}));
  auto proxy3 = pushmi::MAKE(receiver)(
      out0, pushmi::passDVF{}, pushmi::passDEF{}, pushmi::passDDF{});
  auto proxy4 = pushmi::MAKE(receiver)(out0, [](Out0&){}, pushmi::on_error([](Out0& d, auto e)noexcept {
    d.error(e);
  }));
  auto proxy5 = pushmi::MAKE(receiver)(
      out0,
      pushmi::on_value(
        [](Out0&) { }
      ));
  auto proxy6 = pushmi::MAKE(receiver)(
      out0,
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept{},
        [](Out0&, auto e) noexcept{ e.get(); }
      ));
  auto proxy7 = pushmi::MAKE(receiver)(
      out0,
      pushmi::on_done([](Out0&) { }));

  std::promise<void> p0;
  auto promise0 = pushmi::MAKE(receiver)(std::move(p0));
  promise0.done();

  std::promise<void> p1;

  auto any0 = pushmi::any_receiver<>(std::move(p1));
  auto any1 = pushmi::any_receiver<>(std::move(promise0));
  auto any2 = pushmi::any_receiver<>(out0);
  auto any3 = pushmi::any_receiver<>(proxy0);
}

void receiver_1_test(){
  auto out0 = pushmi::MAKE(receiver)();
  auto out1 = pushmi::MAKE(receiver)(pushmi::ignoreVF{});
  auto out2 = pushmi::MAKE(receiver)(pushmi::ignoreVF{}, pushmi::abortEF{});
  auto out3 =
      pushmi::MAKE(receiver)(pushmi::ignoreVF{}, pushmi::abortEF{}, pushmi::ignoreDF{});
  auto out4 = pushmi::MAKE(receiver)([](auto v) { v.get(); });
  auto out5 = pushmi::MAKE(receiver)(
      pushmi::on_value([](auto v) { v.get(); }, [](int v) {}),
      pushmi::on_error(
        [](std::exception_ptr e) noexcept {},
        [](auto e)noexcept { e.get(); }
      ));
  auto out6 = pushmi::MAKE(receiver)(
      pushmi::on_error(
        [](std::exception_ptr e) noexcept {},
        [](auto e) noexcept { e.get(); }
      ));
  auto out7 = pushmi::MAKE(receiver)(
      pushmi::on_done([]() {  }));

  using Out0 = decltype(out0);

  auto proxy0 = pushmi::MAKE(receiver)(out0);
  auto proxy1 = pushmi::MAKE(receiver)(out0, pushmi::passDVF{});
  auto proxy2 = pushmi::MAKE(receiver)(out0, pushmi::passDVF{}, pushmi::on_error(pushmi::passDEF{}));
  auto proxy3 = pushmi::MAKE(receiver)(
      out0, pushmi::passDVF{}, pushmi::passDEF{}, pushmi::passDDF{});
  auto proxy4 = pushmi::MAKE(receiver)(out0, [](auto d, auto v) {
    pushmi::set_value(d, v.get());
  });
  auto proxy5 = pushmi::MAKE(receiver)(
      out0,
      pushmi::on_value([](Out0&, auto v) { v.get(); }, [](Out0&, int v) {}),
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy6 = pushmi::MAKE(receiver)(
      out0,
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy7 = pushmi::MAKE(receiver)(
      out0,
      pushmi::on_done([](Out0&) { }));

  std::promise<int> p0;
  auto promise0 = pushmi::MAKE(receiver)(std::move(p0));
  promise0.value(0);

  std::promise<int> p1;

  auto any0 = pushmi::any_receiver<std::exception_ptr, int>(std::move(p1));
  auto any1 = pushmi::any_receiver<std::exception_ptr, int>(std::move(promise0));
  auto any2 = pushmi::any_receiver<std::exception_ptr, int>(out0);
  auto any3 = pushmi::any_receiver<std::exception_ptr, int>(proxy0);
}

void receiver_n_test() {
  auto out0 = pushmi::MAKE(receiver)();
  auto out1 = pushmi::MAKE(receiver)(pushmi::ignoreNF{});
  auto out2 = pushmi::MAKE(receiver)(pushmi::ignoreNF{}, pushmi::abortEF{});
  auto out3 =
      pushmi::MAKE(receiver)(pushmi::ignoreNF{}, pushmi::abortEF{}, pushmi::ignoreDF{});
  auto out4 = pushmi::MAKE(receiver)([](auto v) { v.get(); });
  auto out5 = pushmi::MAKE(receiver)(
      pushmi::on_value([](auto v) { v.get(); }, [](int v) {}),
      pushmi::on_error(
        [](std::exception_ptr e) noexcept {},
        [](auto e)noexcept { e.get(); }
      ));
  auto out6 = pushmi::MAKE(receiver)(
      pushmi::on_error(
        [](std::exception_ptr e) noexcept {},
        [](auto e) noexcept { e.get(); }
      ));
  auto out7 = pushmi::MAKE(receiver)(
      pushmi::on_done([]() {  }));

  using Out0 = decltype(out0);

  auto proxy0 = pushmi::MAKE(receiver)(out0);
  auto proxy1 = pushmi::MAKE(receiver)(out0, pushmi::passDVF{});
  auto proxy2 = pushmi::MAKE(receiver)(out0, pushmi::passDVF{}, pushmi::on_error(pushmi::passDEF{}));
  auto proxy3 = pushmi::MAKE(receiver)(
      out0, pushmi::passDVF{}, pushmi::passDEF{}, pushmi::passDDF{});
  auto proxy4 = pushmi::MAKE(receiver)(out0, [](auto d, auto v) {
    pushmi::set_value(d, v.get());
  });
  auto proxy5 = pushmi::MAKE(receiver)(
      out0,
      pushmi::on_value([](Out0&, auto v) { v.get(); }, [](Out0&, int v) {}),
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy6 = pushmi::MAKE(receiver)(
      out0,
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy7 = pushmi::MAKE(receiver)(
      out0,
      pushmi::on_done([](Out0&) { }));

  auto any0 = pushmi::any_receiver<std::exception_ptr, int>(out0);
  auto any1 = pushmi::any_receiver<std::exception_ptr, int>(proxy0);
}

void single_sender_test(){
  auto in0 = pushmi::MAKE(single_sender)();
  auto in1 = pushmi::MAKE(single_sender)(pushmi::ignoreSF{});
  auto in2 = pushmi::MAKE(single_sender)(pushmi::ignoreSF{}, pushmi::trampolineEXF{});
  auto in3 = pushmi::MAKE(single_sender)([&](auto out){
    in0.submit(pushmi::MAKE(receiver)(std::move(out),
      pushmi::on_value([](auto d, int v){ pushmi::set_value(d, v); })
    ));
  }, [](){ return pushmi::trampoline(); });

  std::promise<int> p0;
  auto promise0 = pushmi::MAKE(receiver)(std::move(p0));
  in0 | ep::submit(std::move(promise0));

  auto out0 = pushmi::MAKE(receiver)();
  auto out1 = pushmi::MAKE(receiver)(out0, pushmi::on_value([](auto d, int v){
    pushmi::set_value(d, v);
  }));
  in3.submit(out1);

  auto any0 = pushmi::any_single_sender<std::exception_ptr, int>(in0);

  static_assert(pushmi::Executor<pushmi::executor_t<decltype(in0)>>, "sender has invalid executor");
}

void many_sender_test(){
  auto in0 = pushmi::MAKE(many_sender)();
  auto in1 = pushmi::MAKE(many_sender)(pushmi::ignoreSF{});
  auto in2 = pushmi::MAKE(many_sender)(pushmi::ignoreSF{}, pushmi::trampolineEXF{});
  auto in3 = pushmi::MAKE(many_sender)([&](auto out){
    in0.submit(pushmi::MAKE(receiver)(std::move(out),
      pushmi::on_value([](auto d, int v){ pushmi::set_value(d, v); })
    ));
  }, [](){ return pushmi::trampoline(); });

  auto out0 = pushmi::MAKE(receiver)();
  auto out1 = pushmi::MAKE(receiver)(out0, pushmi::on_value([](auto d, int v){
    pushmi::set_value(d, v);
  }));
  in3.submit(out1);

  auto any0 = pushmi::any_many_sender<std::exception_ptr, int>(in0);

  static_assert(pushmi::Executor<pushmi::executor_t<decltype(in0)>>, "sender has invalid executor");
}

void constrained_single_sender_test(){
  auto in0 = pushmi::MAKE(constrained_single_sender)();
  auto in1 = pushmi::MAKE(constrained_single_sender)(pushmi::ignoreSF{});
  auto in2 = pushmi::MAKE(constrained_single_sender)(pushmi::ignoreSF{}, pushmi::inlineConstrainedEXF{}, pushmi::priorityZeroF{});
  auto in3 = pushmi::MAKE(constrained_single_sender)([&](auto c, auto out){
    in0.submit(c, pushmi::MAKE(receiver)(std::move(out),
      pushmi::on_value([](auto d, int v){ pushmi::set_value(d, v); })
    ));
  }, [](){ return pushmi::inline_constrained_executor(); }, [](){ return 0; });
  auto in4 = pushmi::MAKE(constrained_single_sender)(pushmi::ignoreSF{}, pushmi::inlineConstrainedEXF{});

  std::promise<int> p0;
  auto promise0 = pushmi::MAKE(receiver)(std::move(p0));
  in0.submit(in0.top(), std::move(promise0));

  auto out0 = pushmi::MAKE(receiver)();
  auto out1 = pushmi::MAKE(receiver)(out0, pushmi::on_value([](auto d, int v){
    pushmi::set_value(d, v);
  }));
  in3.submit(in0.top(), out1);

  auto any0 = pushmi::any_constrained_single_sender<std::exception_ptr, std::ptrdiff_t, int>(in0);

  static_assert(pushmi::Executor<pushmi::executor_t<decltype(in0)>>, "sender has invalid executor");

  in3 | op::submit();
  in3 | op::blocking_submit();
}

void time_single_sender_test(){
  auto in0 = pushmi::MAKE(time_single_sender)();
  auto in1 = pushmi::MAKE(time_single_sender)(pushmi::ignoreSF{});
  auto in2 = pushmi::MAKE(time_single_sender)(pushmi::ignoreSF{}, pushmi::inlineTimeEXF{}, pushmi::systemNowF{});
  auto in3 = pushmi::MAKE(time_single_sender)([&](auto tp, auto out){
    in0.submit(tp, pushmi::MAKE(receiver)(std::move(out),
      pushmi::on_value([](auto d, int v){ pushmi::set_value(d, v); })
    ));
  }, [](){ return pushmi::inline_time_executor(); }, [](){ return std::chrono::system_clock::now(); });
  auto in4 = pushmi::MAKE(time_single_sender)(pushmi::ignoreSF{}, pushmi::inlineTimeEXF{});

  std::promise<int> p0;
  auto promise0 = pushmi::MAKE(receiver)(std::move(p0));
  in0.submit(in0.top(), std::move(promise0));

  auto out0 = pushmi::MAKE(receiver)();
  auto out1 = pushmi::MAKE(receiver)(out0, pushmi::on_value([](auto d, int v){
    pushmi::set_value(d, v);
  }));
  in3.submit(in0.top(), out1);

  auto any0 = pushmi::any_time_single_sender<std::exception_ptr, std::chrono::system_clock::time_point, int>(in0);

  static_assert(pushmi::Executor<pushmi::executor_t<decltype(in0)>>, "sender has invalid executor");

  in3 | op::submit();
  in3 | op::blocking_submit();
  in3 | op::submit_at(in3.top() + 1s);
  in3 | op::submit_after(1s);
}

void flow_receiver_1_test() {
  auto out0 = pushmi::MAKE(flow_receiver)();
  auto out1 = pushmi::MAKE(flow_receiver)(pushmi::ignoreVF{});
  auto out2 = pushmi::MAKE(flow_receiver)(pushmi::ignoreVF{}, pushmi::abortEF{});
  auto out3 =
      pushmi::MAKE(flow_receiver)(
        pushmi::ignoreVF{},
        pushmi::abortEF{},
        pushmi::ignoreDF{});
  auto out4 = pushmi::MAKE(flow_receiver)([](auto v) { v.get(); });
  auto out5 = pushmi::MAKE(flow_receiver)(
      pushmi::on_value([](auto v) { v.get(); }, [](int v) {}),
      pushmi::on_error(
        [](std::exception_ptr e) noexcept{},
        [](auto e) noexcept { e.get(); }
      ));
  auto out6 = pushmi::MAKE(flow_receiver)(
      pushmi::on_error(
        [](std::exception_ptr e) noexcept {},
        [](auto e) noexcept{ e.get(); }
      ));
  auto out7 = pushmi::MAKE(flow_receiver)(
      pushmi::on_done([]() {  }));

  auto out8 =
      pushmi::MAKE(flow_receiver)(
        pushmi::ignoreVF{},
        pushmi::abortEF{},
        pushmi::ignoreDF{},
        pushmi::ignoreStrtF{});

  using Out0 = decltype(out0);

  auto proxy0 = pushmi::MAKE(flow_receiver)(out0);
  auto proxy1 = pushmi::MAKE(flow_receiver)(out0, pushmi::passDVF{});
  auto proxy2 = pushmi::MAKE(flow_receiver)(out0, pushmi::passDVF{}, pushmi::passDEF{});
  auto proxy3 = pushmi::MAKE(flow_receiver)(
      out0, pushmi::passDVF{}, pushmi::passDEF{}, pushmi::passDDF{});
  auto proxy4 = pushmi::MAKE(flow_receiver)(out0, [](auto d, auto v) {
    pushmi::set_value(d, v.get());
  });
  auto proxy5 = pushmi::MAKE(flow_receiver)(
      out0,
      pushmi::on_value([](Out0&, auto v) { v.get(); }, [](Out0&, int v) {}),
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy6 = pushmi::MAKE(flow_receiver)(
      out0,
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy7 = pushmi::MAKE(flow_receiver)(
      out0,
      pushmi::on_done([](Out0&) { }));

  auto proxy8 = pushmi::MAKE(flow_receiver)(out0,
    pushmi::passDVF{},
    pushmi::passDEF{},
    pushmi::passDDF{});

  auto any2 = pushmi::any_flow_receiver<std::exception_ptr, std::ptrdiff_t, std::exception_ptr, int>(out0);
  auto any3 = pushmi::any_flow_receiver<std::exception_ptr, std::ptrdiff_t, std::exception_ptr, int>(proxy0);
}

void flow_receiver_n_test() {
  auto out0 = pushmi::MAKE(flow_receiver)();
  auto out1 = pushmi::MAKE(flow_receiver)(pushmi::ignoreVF{});
  auto out2 = pushmi::MAKE(flow_receiver)(pushmi::ignoreVF{}, pushmi::abortEF{});
  auto out3 =
      pushmi::MAKE(flow_receiver)(
        pushmi::ignoreVF{},
        pushmi::abortEF{},
        pushmi::ignoreDF{});
  auto out4 = pushmi::MAKE(flow_receiver)([](auto v) { v.get(); });
  auto out5 = pushmi::MAKE(flow_receiver)(
      pushmi::on_value([](auto v) { v.get(); }, [](int v) {}),
      pushmi::on_error(
        [](std::exception_ptr e) noexcept{},
        [](auto e) noexcept { e.get(); }
      ));
  auto out6 = pushmi::MAKE(flow_receiver)(
      pushmi::on_error(
        [](std::exception_ptr e) noexcept {},
        [](auto e) noexcept{ e.get(); }
      ));
  auto out7 = pushmi::MAKE(flow_receiver)(
      pushmi::on_done([]() {  }));

  auto out8 =
      pushmi::MAKE(flow_receiver)(
        pushmi::ignoreVF{},
        pushmi::abortEF{},
        pushmi::ignoreDF{},
        pushmi::ignoreStrtF{});

  using Out0 = decltype(out0);

  auto proxy0 = pushmi::MAKE(flow_receiver)(out0);
  auto proxy1 = pushmi::MAKE(flow_receiver)(out0, pushmi::passDVF{});
  auto proxy2 = pushmi::MAKE(flow_receiver)(out0, pushmi::passDVF{}, pushmi::passDEF{});
  auto proxy3 = pushmi::MAKE(flow_receiver)(
      out0, pushmi::passDVF{}, pushmi::passDEF{}, pushmi::passDDF{});
  auto proxy4 = pushmi::MAKE(flow_receiver)(out0, [](auto d, auto v) {
    pushmi::set_value(d, v.get());
  });
  auto proxy5 = pushmi::MAKE(flow_receiver)(
      out0,
      pushmi::on_value([](Out0&, auto v) { v.get(); }, [](Out0&, int v) {}),
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy6 = pushmi::MAKE(flow_receiver)(
      out0,
      pushmi::on_error(
        [](Out0&, std::exception_ptr e) noexcept {},
        [](Out0&, auto e) noexcept { e.get(); }
      ));
  auto proxy7 = pushmi::MAKE(flow_receiver)(
      out0,
      pushmi::on_done([](Out0&) { }));

  auto proxy8 = pushmi::MAKE(flow_receiver)(out0,
    pushmi::passDVF{},
    pushmi::passDEF{},
    pushmi::passDDF{});

  auto any2 = pushmi::any_flow_receiver<std::exception_ptr, std::ptrdiff_t, std::exception_ptr, int>(out0);
  auto any3 = pushmi::any_flow_receiver<std::exception_ptr, std::ptrdiff_t, std::exception_ptr, int>(proxy0);
}

void flow_single_sender_test(){
  auto in0 = pushmi::MAKE(flow_single_sender)();
  auto in1 = pushmi::MAKE(flow_single_sender)(pushmi::ignoreSF{});
  auto in2 = pushmi::MAKE(flow_single_sender)(pushmi::ignoreSF{}, pushmi::trampolineEXF{});
  auto in3 = pushmi::MAKE(flow_single_sender)([&](auto out){
    in0.submit(pushmi::MAKE(flow_receiver)(std::move(out),
      pushmi::on_value([](auto d, int v){ pushmi::set_value(d, v); })
    ));
  }, [](){ return pushmi::trampoline(); });

  auto out0 = pushmi::MAKE(flow_receiver)();
  auto out1 = pushmi::MAKE(flow_receiver)(out0, pushmi::on_value([](auto d, int v){
    pushmi::set_value(d, v);
  }));
  in3.submit(out1);

  auto any0 = pushmi::any_flow_single_sender<std::exception_ptr, std::exception_ptr, int>(in0);

  static_assert(pushmi::Executor<pushmi::executor_t<decltype(in0)>>, "sender has invalid executor");
}

void flow_many_sender_test(){
  auto in0 = pushmi::MAKE(flow_many_sender)();
  auto in1 = pushmi::MAKE(flow_many_sender)(pushmi::ignoreSF{});
  auto in2 = pushmi::MAKE(flow_many_sender)(pushmi::ignoreSF{}, pushmi::trampolineEXF{});
  auto in3 = pushmi::MAKE(flow_many_sender)([&](auto out){
    in0.submit(pushmi::MAKE(flow_receiver)(std::move(out),
      pushmi::on_value([](auto d, int v){ pushmi::set_value(d, v); })
    ));
  }, [](){ return pushmi::trampoline(); });

  auto out0 = pushmi::MAKE(flow_receiver)();
  auto out1 = pushmi::MAKE(flow_receiver)(out0, pushmi::on_value([](auto d, int v){
    pushmi::set_value(d, v);
  }));
  in3.submit(out1);

  auto any0 = pushmi::any_flow_many_sender<std::exception_ptr, std::ptrdiff_t, std::exception_ptr, int>(in0);

  static_assert(pushmi::Executor<pushmi::executor_t<decltype(in0)>>, "sender has invalid executor");
}
